# Material maps

A prop can publish **material maps** beside its image, for engines that
light it: how rough each pixel is, how metallic, how much ambient light
reaches it and what glows. They come from the same seed and inputs as the
image and are as reproducible, like the [normal map](relief.md). An asset that
declares none publishes none. On a [model](models.md), a skin carries the maps
of its asset onto the surfaces it covers.

<!-- example: ../../examples/language/material-maps.ips -->

## At a glance

`output roughness name`, `output metallic name`, `output ao name` and
`output emission name` each declare one map; assign each a colour layer.
Roughness, metallic and AO read the red of what the layer paints, over 255,
0 and 255 where it paints nothing; emission keeps the colour, over black. An
asset declares at most one map of each kind. Read a part's map as
`alias.outputs.name`; only the root asset's maps reach the host.

## The four maps

| Output | Holds | Where the layer paints nothing |
| --- | --- | --- |
| `roughness` | 0 for a mirror, 255 for a matt surface | 255 |
| `metallic` | 0 for paint, wood or stone, 255 for bare metal | 0 |
| `ao` | 0 where no ambient light reaches, 255 where all of it does | 255 |
| `emission` | the colour of the glow | black |

`output roughness name` declares a map; assign it a layer captured as
colours. A later assignment replaces the earlier one, and an asset that never
assigns a map publishes no image of that kind. The kinds are contextual
words: `roughness`, `metallic`, `ao` and `emission` stay ordinary names
everywhere else, and a map is never a list.

The host receives an image of the canvas size for each map. The layer is
painted over the background of its kind, so where it paints nothing the map
holds the value that changes nothing, and a partly covered pixel lies between
the two. Roughness, metallic and AO keep one value: the red of that result,
in red, green and blue alike. Emission keeps the colour. Alpha is 255
everywhere.

Paint a map with the same shapes as the colours, in a layer of its own:

```ips
layer paint(alpha = surface.source_over) {
  rect(0, 0, 48, 32, #d0d0d0)   // matt paint on the frame
  rect(4, 4, 40, 24, #404040)   // smooth metal inside it
}
finish = paint
```

A grey stands for its value, so `#404040` is a roughness of 64 of 255. Draw
in a `source_over` layer: a translucent shape then leans toward its value,
which is how a worn edge or a film of grime fades out.

<!-- llm: skip -->
Roughness, metallic and AO are linear data: a host uploads them without an
sRGB transfer, as it does a normal map. Emission is a colour like the image's
own. A result lists its images by kind: the normal map first, then roughness,
metallic, AO and emission, whatever the order of the declarations. See the
[material map contract](../../spec/material-maps-v1.2.md) for the exact
encoding and costs, and [compiler tools](tooling.md) for writing the maps to
files.

<!-- example: ../../examples/language/errors/material-map-twice.ips -->
<!-- example: ../../examples/language/traps/material-map-direction.ips -->

## Parts

A part declares its own maps. Its parent reads `alias.outputs.name` after
the call, as it reads any layer output, and assigns it or paints it into a
layer of its own. The part's map arrives in the parent's coordinates, placed,
turned and mirrored with the part. Only the root asset's maps reach the
host.

<!-- llm: skip -->
Whether a layer holds directions is a property of its pixels, not of its
type, so assigning a direction layer to a material map traps while running,
naming the kind; a normal map is `output normal`.

## On a model

A skin made from an asset with maps brings them along: an engine shades the
surface with them. The numbers of a skin then multiply its maps, as in glTF.
A new skin whose texture has a roughness, metallic or emission map starts
that number at 1, so the map shows as painted; `skin.roughness(oak, 0.5)`
halves it. Without a map, a number stands alone as before, and emission
multiplies the texture's own colour. AO has no number. See
[skins](models.md#skins).

<!-- llm: skip -->
A model's texture limit counts the pixels of colour images; each texture
carries its maps beside them, at its own size.
