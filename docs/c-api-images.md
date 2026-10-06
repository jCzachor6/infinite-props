# Borrowed generated image views

`IPROP_CAP_IMAGE_VIEW` (bit 12) advertises `iprop_image_view_ex_init` and
`iprop_result_query_image`. They extend API 0.3 on ABI line 0.4 without
changing `iprop_image`, legacy entry points or renderer behavior.

```c
iprop_image_view_ex image;
iprop_image_view_ex_init(&image, sizeof(image));
if (iprop_result_query_image(result, &image, NULL) == IPROP_EX_OK) {
    /* image.pixels + y * image.row_stride + x * 4 addresses RGBA8. */
}
```

The query requires a result and output descriptor. `struct_size` is the actual
caller allocation capacity, with a four-byte minimum. `flags` is input, absent
flags mean zero, and any nonzero complete flags field is rejected. Only complete
known output fields are written. Size/flags, native padding, partial fields and
future tail bytes are preserved. The initializer zeros complete fields, sets
`struct_size` to the supplied capacity and follows the same padding/tail rules;
NULL or capacity below four fails without a write. With a shorter prefix, inspect
only fields that fit completely, including the pixel pointer before using it.

| Field | Meaning |
| --- | --- |
| `width`, `height` | Actual generated canvas dimensions, including generation size overrides |
| `byte_count` | Owned pixel storage in bytes, equal to width × height × 4 for generated canvases |
| `row_stride` | Tight width × 4 byte rows, with no row padding |
| `pixel_format` | `IPROP_PIXEL_FORMAT_RGBA8`: exact R, G, B, A byte order, one byte per channel |
| `origin` | `IPROP_IMAGE_ORIGIN_TOP_LEFT`: top row first, x grows rightward, y downward |
| `alpha_mode` | `IPROP_IMAGE_ALPHA_LEGACY_DESTINATION`, the existing compositor described below |
| `pixels` | The exact legacy `iprop_result_image` borrowed pixel pointer |

The storage origin is independent of the source asset's geometric origin.
Pixels are immutable result-owned memory, valid until `iprop_result_free`.
Freeing the program, generating another result, or freeing that later result
does not invalidate this view. Copy the bytes before freeing their owning result
if the host needs a longer lifetime; no host buffers or conversion are introduced.

## Frozen alpha semantics

The canvas starts with every RGBA byte zero. Under
`IPROP_IMAGE_ALPHA_LEGACY_DESTINATION`, a source alpha of 255 replaces all four
destination channels. Otherwise each RGB channel is computed as
`(source_channel * source_alpha + destination_channel * (255-source_alpha))/255`
using integer division with truncation, and destination alpha is retained.
Alpha zero leaves all destination channels unchanged.

This is neither standard straight-alpha nor premultiplied-alpha compositing.
For example, painting RGBA `(200,100,50,128)` on transparent zero storage yields
`(100,50,25,0)`; painting it on `(20,40,60,255)` yields `(110,70,54,255)`.
RGB can therefore be nonzero where stored alpha is zero. The query returns those
exact bytes without normalizing or converting them. Hosts must interpret this
explicit mode when selecting their own rendering or export policy. The normative
algorithm remains [pixel blending](../spec/iprop-v0.3.md#111-pixelx-y-colour),
also described by [language drawing](language/drawing.md).

## Checked failure behavior

Query failure leaves its descriptor unchanged. NULL result/output, capacity
below four and unsupported flags return `IPROP_EX_E_ARGUMENT`. Optional
`iprop_error_ex` storage follows the existing
[checked error policy](c-api-checked.md#sized-query-outputs-and-error-storage):
status/reason/stage are `E_ARGUMENT`/`INVALID_ARGUMENT`/`ARGUMENTS`, with no
location coordinates for these checks; success clears available error fields
and text. Malformed error storage prevents publication. Error descriptor or
writable message storage overlapping the image output, or message storage
overlapping its own error descriptor, returns `E_ARGUMENT` while preserving
both outputs. A zero-capacity message span touches no buffer bytes.

Pixel/image queries execute no VM and allocate no new storage. They do not
expose structured output values; those remain available through the unchanged
legacy image output collection until separate typed result accessors exist.
