# Material maps — 1.2 contract (draft)

**Status:** Draft v1.2 extension of the 2D format, to be frozen with product
1.2.0. It builds on [normal maps](normal-maps-v1.1.md) (the flags field of a
resource-export record and auxiliary images) and on
[resource exports](iprop-v0.8.md#32-internal-resource-exports). No opcode,
section or instruction is added; the extension uses four bits of a flags
field whose other values a 1.1 runtime already rejects.

A program that uses none of this extension keeps its bytes, image, outputs,
RNG, fuel and work. A program that uses any of it is rejected by a 1.1
runtime during loading (`unknown resource export flags`).

## Kinds

| Kind | Image code | Flag bit | Background | Published |
| --- | --- | --- | --- | --- |
| normal | 1 | 0 | `(128, 128, 255, 255)` | directions ([normal maps](normal-maps-v1.1.md)) |
| roughness | 2 | 1 | `(255, 255, 255, 255)` | red of the composite in R, G and B |
| metallic | 3 | 2 | `(0, 0, 0, 255)` | red of the composite in R, G and B |
| ao | 4 | 3 | `(255, 255, 255, 255)` | red of the composite in R, G and B |
| emission | 5 | 4 | `(0, 0, 0, 255)` | the composite's R, G and B |

The background of a map is the value that changes nothing: fully rough, not
metal, unoccluded, dark. Roughness, metallic and AO are linear values from 0
to 255 (255 is fully rough, metal, unoccluded): a host samples them without a
transfer function. Emission is a colour encoded like the main image's colours.

## Export records

The flags field (offset 10 of a resource-export record) holds at most one
**image bit**: bit 0 (normal) or bits 1–4 (the maps above); every other bit
must be zero. A record with two image bits fails loading with
`InvalidMetadata` (`a resource export publishes one image at most`), and so
does an unknown bit (`unknown resource export flags`). A record with a map
bit must have type descriptor `layer` (`a material map output must be a layer
export`), and an owner has at most one record of each kind (`an asset
declares more than one material map of a kind`).

`out.layer` and `part.get.layer` address a map slot as any layer slot.
Writing a direction layer to it traps with `InvalidOutput`, after the existing
handle checks, with the message `KIND output needs a colour layer, not a
direction layer`, where `KIND` is the kind's name. Mapping, ownership, the
part-output register and the one-fuel cost are unchanged.

## Publication

When execution completes successfully, the runtime publishes the root's
image slots **by kind**: normal, roughness, metallic, ao, emission, whatever
the order of the records. A slot that was never written publishes nothing.
A derived normal image ([normal maps](normal-maps-v1.1.md#derived-normal-map))
is published only when no authored normal image was, and is inserted first,
so a normal image is always at index 0.

A map image has the root canvas's dimensions and starts as its kind's
background in every pixel. The slot's layer is painted onto it once with the
source-over kernel and no clips, exactly as a normal image is painted. For
roughness, metallic and AO every pixel is then replaced by
`(R, R, R, 255)` of the composite. Alpha is 255 in every published pixel.

Publication is result packing: it costs no fuel. Work is one step per
initialized pixel and the paint's own steps, as for a normal image, plus one
step per pixel for the grey pass of roughness, metallic and AO. A work,
deadline or cancellation failure during it fails the generation, and no
image is published.

Slots owned by parts are never published; a parent reads them and paints or
assigns them.

## Source language

Source 1.2 adds, with no new reserved word:

- `output roughness NAME`, `output metallic NAME`, `output ao NAME` and
  `output emission NAME`: singular declarations whose kinds are contextual,
  as `normal` is. An asset declares at most one output of each kind
  (`an asset declares at most one KIND map`); none is a list. Each is
  assigned a `layer`.

A part's map is read as `alias.outputs.NAME`, as any layer output. Whether a
layer holds directions is not part of its static type: the check on map
outputs is a runtime trap.

## Host results

A generation result lists its auxiliary images in kind order. A material map
has kind 2–5, source authored, the name of its record and tight top-left
RGBA8 pixels with alpha 255. C API 0.8 (`IPROP_CAP_MATERIAL_MAPS`, bit 23)
names the kinds `IPROP_AUX_IMAGE_ROUGHNESS`, `_METALLIC`, `_AO` and
`_EMISSION`; layouts and functions are those of API 0.7. A host that does not
know a kind skips it. The C interface is described in
[auxiliary images](../docs/c-api-aux-images.md).

On a model, a texture carries the maps of its program, and a skin's numbers
multiply them: see [models](models-v1.2.md#material-maps-on-skins).
