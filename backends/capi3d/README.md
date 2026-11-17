# The C API of models (`backends/capi3d/`)

`iprop3d.h` adds **models** to the [C API](../capi/README.md): a compiled
`.iprop3d` file gives, for one seed and its inputs, the triangles of a 3D
prop and the textures they wear. The format and every number a model
produces are specified in [Infinite Props models](../../spec/models-v1.2.md);
[models in C](../../docs/c-api-models.md) states the contract of this
interface and [models](../../docs/language/models.md) how one is written.

**Preview.** API 0.1 follows the models contract, which is a preview until it
is frozen. Until then a release may change any declaration in `iprop3d.h`
and no ABI is promised: the library is static only (`iprop3dc`), is not
installed with the SDK, and a client is built together with it. `iprop.h`
and `ipropc` are not affected by anything here.

## Building

The library is the CMake target `iprop::capi3d`. It links the static 2D C API
and the model runtime and carries their include directories:

```cmake
target_link_libraries(my_host PRIVATE iprop::capi3d)
```

A client includes `iprop3d.h`, which includes `iprop.h`. The model API uses
that header's status codes, structured errors (`iprop_error_ex`), sized
descriptors, input descriptors, parameter sets, execution limits and image
tags unchanged, so a host that already draws 2D props reuses its code for
all of them.

## One generation

```c
iprop3d_model* model = NULL;
iprop3d_result* result = NULL;
iprop3d_generate_options options;
iprop3d_result_info info;
uint32_t s;

if (iprop3d_model_load(bytes, byte_count, &model, NULL) != IPROP_EX_OK) return 1;
iprop3d_generate_options_init(&options, sizeof(options));
options.seed = 7;
if (iprop3d_generate(model, &options, NULL, NULL, &result, NULL) != IPROP_EX_OK) return 1;

iprop3d_result_info_init(&info, sizeof(info));
iprop3d_result_query(result, &info, NULL);
for (s = 0; s < info.surface_count; ++s) {
    iprop3d_surface_view surface;
    iprop3d_texture_view texture;
    iprop3d_surface_view_init(&surface, sizeof(surface));
    iprop3d_texture_view_init(&texture, sizeof(texture));
    iprop3d_result_surface(result, s, &surface, NULL);
    iprop3d_result_texture(result, surface.texture_index, &texture, NULL);
    /* Upload surface.positions/normals/tangents/uvs/indices and
     * texture.pixels; one host material per surface. */
}
iprop3d_result_free(result);
iprop3d_model_free(model);
```

Inputs of a model are read with `iprop3d_model_input_count/get/find` and
changed through an ordinary parameter set:
`iprop3d_model_parameters_create` returns an `iprop_parameters` that every
`iprop_parameters_*` call accepts, and `iprop3d_generate` takes it. The
fourth argument of `iprop3d_generate` is an optional
`iprop_execution_limits_ex`: one work budget, deadline and cancellation
cover the model and every texture it generates.

## What a result holds

- **Surfaces.** A surface is the triangles of one host material: every
  triangle that shares a texture and the material numbers (roughness,
  metallic, emission, alpha mode and cutoff, two-sided, crisp). Vertices
  are not shared between surfaces.
- **Textures.** A texture is the image a skin program generated for one
  seed, size and set of inputs; surfaces that differ only in material numbers
  share it. Its `key` identifies the pixels across results and models: equal
  keys mean equal images, so a host may keep one GPU texture per key.
- **Auxiliary images** of a texture, read with
  `iprop3d_result_texture_image`: the normal map of a skin that declares
  `output normal`.

Conventions a renderer needs:

- Right-handed coordinates, `+Y` up, `+Z` toward the viewer; triangles are
  counterclockwise seen from outside. Positions are in model units and
  `units_per_meter` converts them.
- UV `(0, 0)` is the upper left corner of the image. A texture with
  `repeats == 1` is sampled beyond `0..1` and needs repeat wrapping.
- A tangent is the unit direction of growing `u` with a sign `w`;
  `w * cross(normal, tangent)` points up the image. Normal maps store `x`
  along the tangent, `y` up the image and `z` out of the surface, and their
  bytes are linear data.
- The colour image has the alpha contract of a 2D generation
  (`IPROP_IMAGE_ALPHA_LEGACY_DESTINATION`). A surface in
  `IPROP3D_ALPHA_OPAQUE` mode ignores alpha; in `IPROP3D_ALPHA_CUTOUT` a texel
  below `alpha_cutoff` is not drawn.
- `crisp == 1` asks for nearest sampling without mipmaps (pixel art);
  otherwise linear filtering with mipmaps is expected.

## Errors

Every call reports through an optional `iprop_error_ex`, as in the 2D API. A
violation while generating is `IPROP_EX_E_TRAP` at the execute stage, located
in the model program (with a source line when the file was compiled with
`--debug`). A violation inside a texture carries the skin program's reason,
names it in the message (`skin 'wood': …`) and is located at the instruction
of the model that asked for the texture. A failed call publishes nothing and
leaves the caller's outputs unchanged.

`iprop3d_model_load` rejects a 2D `.iprop` file and `iprop_program_load_ex`
rejects an `.iprop3d` file, both with `IPROP_REASON_BAD_MAGIC`.

## Tests

`ctest -R capi3d` runs four executables from `test/`: `models.c` (the whole
flow from C99, with a texture compared against the 2D API's image of the same
skin program), `descriptors.c` (every descriptor at every capacity),
`errors.c` (loading damaged files, limits, traps and their locations) and
`conformance.cpp` (every number and array equal to the C++ model runtime's,
bit for bit).
