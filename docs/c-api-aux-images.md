# Auxiliary images, normal and material maps (C API 0.8, ABI 0.5)

A generation can publish images beside the main one. The first kind is the
**normal map** of a prop whose root asset declares `output normal`; see
[relief and normal maps](language/relief.md) for authoring and the
[normal map contract](../spec/normal-maps-v1.1.md) for the exact bytes.
`IPROP_CAP_AUX_IMAGES` (bit 21) advertises the queries and
`IPROP_CAP_DERIVED_NORMAL` (bit 22) the derived fallback, both since API 0.7.
API 0.8 adds the four **material maps**, roughness, metallic, AO and emission
(`IPROP_CAP_MATERIAL_MAPS`, bit 23); see [material maps](language/material-maps.md)
and their [contract](../spec/material-maps-v1.2.md). Both versions use ABI
line 0.5: existing structures, enums and layouts are unchanged.

```c
uint32_t index;
iprop_aux_image_view_ex normal;
iprop_aux_image_view_ex_init(&normal, sizeof(normal));
if (iprop_result_aux_image_find(result, IPROP_AUX_IMAGE_NORMAL, &index, NULL) == IPROP_EX_OK &&
    iprop_result_aux_image_get(result, index, &normal, NULL) == IPROP_EX_OK) {
    /* normal.pixels + y * normal.row_stride + x * 4 addresses one direction. */
}
```

Every result answers these queries, including results of `iprop_generate`,
`iprop_generate_ex` and `iprop_generate_controlled`. A result without auxiliary
images reports a count of zero, and no generation flag is needed to receive an
authored image.

## What a program declares

`iprop_program_aux_image_count` and `iprop_program_aux_image_get` describe the
images a loaded program can publish: the image outputs of its root asset, at
most one of each kind, in kind order, with their kinds and exact names. Image
outputs of linked parts are internal and are not listed. A declaration is a
promise of at most one image: a generation that never assigns the output
publishes none, so use the result queries to index a result.

## What a result carries

`iprop_result_aux_image_count`, `iprop_result_aux_image_get` and
`iprop_result_aux_image_find` enumerate the published images.
`find` returns the index of the first image of a kind and fails with
`IPROP_EX_E_ARGUMENT` when the result has none, leaving its output unchanged.

| Field | Meaning |
| --- | --- |
| `image_index` | Position in the result's list |
| `kind` | `IPROP_AUX_IMAGE_NORMAL` (1), `_ROUGHNESS` (2), `_METALLIC` (3), `_AO` (4) or `_EMISSION` (5); a result lists them in this order |
| `source` | `IPROP_AUX_IMAGE_AUTHORED` (1): painted by the program. `IPROP_AUX_IMAGE_DERIVED` (2): made by the runtime on request |
| `name` | The output's name, or `normal` for a derived image; byte-counted UTF-8 without a terminator |
| `width`, `height` | Those of the main image, including size overrides |
| `byte_count`, `row_stride` | Width × height × 4, and tight width × 4 rows |
| `pixel_format` | `IPROP_PIXEL_FORMAT_RGBA8` |
| `alpha_mode` | `IPROP_IMAGE_ALPHA_OPAQUE` (2): alpha is 255 in every pixel and carries no coverage |
| `origin` | `IPROP_IMAGE_ORIGIN_TOP_LEFT` |
| `pixels` | Borrowed bytes owned by the result |

Pixels and names stay valid until `iprop_result_free`. Freeing the program,
generating again or freeing another result does not invalidate them. The
queries run no VM and allocate nothing.

## Reading a normal image

R, G and B store a direction as `x` rightward, `y` **upward** and `z` toward
the viewer. A byte `b` decodes as `b / 127.5 - 1`; `(128, 128, 255)` is flat.
Pixels the prop does not cover are flat, so the image needs no mask of its own:
use the main image's alpha for coverage.

- The bytes are data, not colour. Upload them as a linear texture, without an
  sRGB transfer, premultiplication or colour management.
- Normalize after decoding. Blended edges and filtered samples are shorter than
  unit length.
- Green grows upward (the OpenGL convention). A renderer that expects green
  downward negates `y`, which for stored bytes is `255 - G`.
- The map is in the image's own axes. A sprite drawn mirrored or rotated needs
  the same transform applied to the decoded vector.

## Reading a material map

| Kind | Holds | Where the prop painted nothing |
| --- | --- | --- |
| `IPROP_AUX_IMAGE_ROUGHNESS` | one value in R, G and B: 0 a mirror, 255 matt | 255 |
| `IPROP_AUX_IMAGE_METALLIC` | one value in R, G and B: 0 not metal, 255 metal | 0 |
| `IPROP_AUX_IMAGE_AO` | one value in R, G and B: 0 no ambient light, 255 all of it | 255 |
| `IPROP_AUX_IMAGE_EMISSION` | the colour of the glow | black |

Roughness, metallic and AO are linear data: upload them like a normal map,
without an sRGB transfer, and read any one channel. Emission is a colour
encoded like the main image's, so upload it as sRGB. Alpha is 255 in every
pixel. A renderer that packs maps, such as glTF's occlusion (R), roughness
(G) and metallic (B), copies the red channel of each into its slot. A host
that does not know a kind skips it.

## Derived normal image

A program that declares no normal output publishes none. A host that still
wants one asks for it at generation:

```c
iprop_derived_normal_ex derived;
iprop_derived_normal_ex_init(&derived, sizeof(derived));
status = iprop_generate_derived_normal(program, &options, parameters, NULL,
                                       &derived, &result, &error);
```

`iprop_generate_derived_normal` is `iprop_generate_controlled` with one more
optional descriptor; with a NULL request the two behave identically. With a
request, a successful generation that published no authored normal image gains
one derived from the finished image: a pillow of the alpha plane blended with
alpha-weighted brightness. An authored image wins and the request is then
ignored, so one code path serves both kinds of program; `source` tells them
apart.

| Field | Meaning | Zero selects |
| --- | --- | --- |
| `depth` | Height of full coverage in image pixels; finite and not negative | longer side / 16 |
| `rounding` | Gaussian sigma of the pillow in image pixels, 0–64 | longer side / 32, at most 64 |
| `detail` | Share of brightness in the height, 0–1 | 0.5 |

An initialized request is the recommended one. `flags` must be zero and a
shorter prefix leaves the absent fields at their defaults. The derived image is
as deterministic as the main image. It spends no fuel and leaves the main
image, outputs and fuel statistics as they would be without the request; its
work is charged to the generation's work budget, and the deadline and
cancellation of the call apply. It is an approximation from colours: lighting
painted into the image turns into relief. Prefer authored relief where the
asset provides it.

## Checked failure behavior

Queries follow the [image view rules](c-api-images.md#checked-failure-behavior):
`struct_size` is the caller's capacity with a four-byte minimum, `flags` is
input and must be zero, only complete known fields are written, and padding,
partial fields and unknown tail bytes are preserved. A failed query leaves its
output unchanged. NULL handles or outputs, a capacity below four, unsupported
flags, an index outside the list and a kind with no image return
`IPROP_EX_E_ARGUMENT` with reason `INVALID_ARGUMENT` at the `ARGUMENTS` stage.

`iprop_generate_derived_normal` validates the request before VM setup: an
unreadable or short descriptor, nonzero flags or a value outside its range is
`IPROP_EX_E_ARGUMENT` at the `ARGUMENTS` stage, and `*out_result` is untouched.
An image whose derivation would need more than 64 MiB of scratch fails the
generation with `IPROP_EX_E_OOM`; exhausting the work budget, the deadline or a
cancellation during derivation fails it like the same event during execution.
No partial result is published.
