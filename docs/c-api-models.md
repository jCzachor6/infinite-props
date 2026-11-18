# Models in C (API 0.2, a preview)

[`iprop3d.h`](../backends/capi3d/iprop3d.h) generates 3D models from compiled
`.iprop3d` files: for one seed and its inputs, the triangles of a prop and
the textures they wear. See [models](language/models.md) for authoring and
the [model contract](../spec/models-v1.2.md) for every number a model
produces. The [library README](../backends/capi3d/README.md) shows a whole
generation and how to build against the library.

This is a **preview**. API 0.2 is independent of the 2D C API version and
may change in any release until the model contract is frozen; 0.2 adds the
[outputs](#outputs) of a result to 0.1. No ABI is
promised: the library is static (`iprop3dc`, CMake target `iprop::capi3d`),
it is not part of the installed SDK, and a client is built together with it.
[`iprop.h`](../backends/capi/iprop.h), the 2D C API 0.8 and ABI 0.5 do not depend on it.

The header reuses the 2D interface instead of repeating it: status codes,
structured errors, sized descriptors, input descriptors, parameter sets,
execution limits and image tags are those of `iprop.h`, with their
[ownership and error contracts](c-api-contracts.md).

## Handles

| Handle | Made by | Freed by | Holds |
| --- | --- | --- | --- |
| `iprop3d_model` | `iprop3d_model_load` | `iprop3d_model_free` | a loaded and validated model and its embedded skin programs |
| `iprop_parameters` | `iprop3d_model_parameters_create` | `iprop_parameters_free` | one value for each input of that model load |
| `iprop3d_result` | `iprop3d_generate` | `iprop3d_result_free` | one generation: surfaces, textures and counters |

Loading validates the model program and every embedded skin program once; a
generation never validates again. A result is independent of everything
else: it stays valid after its model is freed, after its parameter set
changes and after later generations. A parameter set stays usable after its
model is freed, but only generations of that model load accept it; a set of
another load fails with `IPROP_REASON_FOREIGN_PARAMETERS`, even for identical
bytes. Free functions accept `NULL`.

Handles are not thread-safe and there is no global state. A model and the
parameter sets created from it share loaded data and form one handle group,
as a program and its sets do in the 2D API: serialize the calls within a
group and load the model once more for each concurrent worker. A result owns
its data and may be read on another thread.

## What a model declares

`iprop3d_model_query` fills `iprop3d_model_info`: the nominal size and
`units_per_meter` the source declares, the fuel budget of a generation
(shared by the model program and the textures it generates),
the input count and the number of embedded skin programs.
`iprop3d_model_skin_program` names each of them with its declared pixel
size. The nominal size is a declaration: a generated model may be smaller or
larger, and the bounds of a result are exact.

`iprop3d_model_input_count`, `_get` and `_find` describe the inputs with the
descriptor and the rules of [program reflection](c-api-reflection.md).
`iprop3d_model_parameters_create` returns a set holding every default; it is
an ordinary `iprop_parameters`, read and changed by every call of
[parameter sets](c-api-parameters.md).

## One generation

`iprop3d_generate(model, options, parameters, limits, &result, error)` takes
the seed in `iprop3d_generate_options`, an optional parameter set (`NULL`
selects the defaults) and optional `iprop_execution_limits_ex`. One work
budget, one deadline and one cancellation token cover the model program and
every texture it generates; see [execution limits](execution-limits.md).

Success publishes exactly one new result and does not free a pointer the
caller left in the output. Failure publishes nothing and leaves the output
untouched.

## What a result holds

`iprop3d_result_query` fills `iprop3d_result_info`: the bounds of all
generated positions, the size and scale of the model, the vertex, triangle,
surface, texture and output counts, and the fuel and work counters. Both
count the model program and the textures it generates.

A **surface** (`iprop3d_result_surface`) is the triangles of one host
material: every triangle that shares a texture and the material numbers.
Surfaces come in order of first use, and vertices are never shared between
two of them.

| Field | Layout |
| --- | --- |
| `positions` | 3 floats a vertex, in model units |
| `normals` | 3 floats a vertex, unit length |
| `tangents` | 4 floats a vertex: the direction of growing `u`, then a sign `w` |
| `uvs` | 2 floats a vertex; beyond 0–1 where the texture repeats |
| `indices` | `index_count` 32-bit indices into this surface's vertices, 3 a triangle |

`roughness` and `metallic` are 0–1 and `emission` is 0 or more. Each
multiplies its [material map](c-api-aux-images.md#reading-a-material-map)
when the texture has one, as a glTF factor does, and a skin then starts at 1;
without a map the number stands alone and `emission` multiplies the
texture's colour. An AO map applies as it is. `alpha_mode` is
`IPROP3D_ALPHA_OPAQUE`, where the alpha of the texture is ignored,
`IPROP3D_ALPHA_CUTOUT`, where a texel below `alpha_cutoff` is not drawn, or
`IPROP3D_ALPHA_BLEND`, where the texel is mixed over what is behind it by its
alpha (glass): draw such surfaces after the others, without writing depth.
`two_sided` and `crisp` are 0 or 1; `crisp` asks for nearest sampling
without mipmaps.

A **texture** (`iprop3d_result_texture`) is the image one skin program
generated for one seed, size and set of inputs. Its pixel fields follow
[image views](c-api-images.md), and the image is the one `iprop_generate_ex`
gives for the same program, seed, size and inputs. `key` identifies the
pixels across results and models: equal keys mean equal images, so a host
may keep one GPU texture for each key. `repeats` is 1 when a surface samples
the texture beyond 0–1 and needs repeat wrapping. `asset_width` and
`asset_height` are the size the program declares, `width` and `height` the
size generated.

`iprop3d_result_texture_image` and `_find` return the images a texture
publishes beside its picture, with the descriptor and the rules of
[auxiliary images](c-api-aux-images.md): the normal map and the material
maps of a skin that declares them, in kind order.

Arrays, pixels and names are borrowed from the result until
`iprop3d_result_free`. An array pointer is `NULL` exactly when its count is
zero. Every float holds the exact binary32 bits the runtime computed.

## Outputs

An **output** (`iprop3d_result_output`, API 0.2) is one the model declares
with [`output point3d` or `output box3d`](language/models.md#outputs), in
declaration order; `iprop3d_result_output_find` finds one by name. Its
`values` hold `value_count` values of `floats_per_value` floats each, in
model units like the positions:

| `kind` | Floats | Value |
| --- | --- | --- |
| `IPROP3D_OUTPUT_POINT3D` | 3 | `x`, `y`, `z` |
| `IPROP3D_OUTPUT_BOX3D` | 15 | the centre, the size along the box's own axes, then its x, y and z axes |

A box's axes are unit vectors that form a rotation, so a host builds a
collider from the centre, the rotation and the size. `list` is 1 for a list
output, which holds every value written; a singular output holds the last
one written, or none (`value_count` 0, `values` NULL). A part's outputs do
not appear: only the model's own reach the host. Divide by
`units_per_meter` for metres, as for the positions.

## What a renderer needs to know

- Coordinates are right-handed with `+Y` up and `+Z` toward the viewer.
  Triangles are counterclockwise seen from outside. Positions are in model
  units; divide by `units_per_meter` for metres.
- UV `(0, 0)` is the upper left corner of the image.
- A tangent `t` with its sign `w` gives the bitangent
  `w * cross(normal, t)`, which points up the image. A normal map stores `x`
  along the tangent, `y` up the image and `z` out of the surface, as linear
  data: upload it without an sRGB conversion.
- A host with clockwise front faces swaps two indices of every triangle; a
  host that works in metres scales the positions. Nothing else needs
  converting.

## Failures

Every call reports through an optional `iprop_error_ex`. An invalid argument
fails with `IPROP_EX_E_ARGUMENT` before anything runs. A damaged file fails
loading with `IPROP_EX_E_FORMAT`; a 2D `.iprop` file given to
`iprop3d_model_load` and a model given to `iprop_program_load_ex` both
report `IPROP_REASON_BAD_MAGIC`.

A violation while generating is `IPROP_EX_E_TRAP` at the execute stage,
located in the model program, with a source line when the file was compiled
with `--debug`. A violation inside a texture carries the skin program's
reason, names it in the message (`skin 'wood': ...`) and is located at the
instruction of the model that asked for the texture. See
[runtime errors](runtime-errors.md).

## Descriptors

Every structure starts with `struct_size`, the caller's capacity, and is
prepared by its `_init` function. A call writes only the complete fields
that fit, preserves the rest and leaves its output unchanged when it fails,
as [checked calls](c-api-checked.md) do. A `flags` field is an input that
must be zero.

| Structure | Size on a 64-bit platform |
| --- | --- |
| `iprop3d_library_info` | 40 |
| `iprop3d_model_info` | 48 |
| `iprop3d_skin_program_info` | 40 |
| `iprop3d_generate_options` | 12 |
| `iprop3d_result_info` | 96 |
| `iprop3d_surface_view` | 96 |
| `iprop3d_texture_view` | 96 |

`iprop3d_library_query` reports the API version and the limits of one
generation.
