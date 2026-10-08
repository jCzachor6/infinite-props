# Recipe gallery

24 small standalone `.ips` patterns for the v0.4 language. Each draws a
32×32 asset, uses only the existing runtime instructions, and can be copied
or combined with the other recipes. The variant badge demonstrates `v`/`a`;
the uniform emblem exposes color, scale and density inputs. The attachment
recipe writes structured point outputs alongside its pixels.

The stored sources use the compiler's canonical style. The 0.5 formatter's
`--format-only` output is idempotent and preserves their exact stripped bytecode;
the recipes also compile with the frozen 0.4 compiler. Comments remain in the
canonical source. Check the gallery with:

```sh
python3 scripts/check_recipe_format.py build/tools/ipc
```

```sh
build/tools/ipc examples/recipes/09-rounded-mask.ips --strip -o /tmp/rounded.iprop
build/tools/ipgen /tmp/rounded.iprop --seed 7 -o /tmp/rounded.png
```

Alpha examples start from an opaque background because the frozen pixel
compositor preserves the destination alpha for translucent source colors.
For imported part composition, see `../portrait.ips` and `../plant.ips`.

| Source | Pattern |
| --- | --- |
| [01-basic-rect.ips](01-basic-rect.ips) | Named canvas parameters |
| [02-palette-bands.ips](02-palette-bands.ips) | Typed palette indexing |
| [03-variant-badge.ips](03-variant-badge.ips) | Implicit variant and archetype indices |
| [04-seeded-speckles.ips](04-seeded-speckles.ips) | One stream for repeated placement |
| [05-chance-stars.ips](05-chance-stars.ips) | Probability gates that retain RNG draws |
| [06-direct-seed-cells.ips](06-direct-seed-cells.ips) | Independent values from an explicit seed |
| [07-color-gradient.ips](07-color-gradient.ips) | One lerp for colors and float values |
| [08-alpha-glaze.ips](08-alpha-glaze.ips) | RGBA channels over an opaque background |
| [09-rounded-mask.ips](09-rounded-mask.ips) | Distance coverage with rounded rectangle geometry |
| [10-sdf-ring.ips](10-sdf-ring.ips) | Distance field composition |
| [11-bezier-stroke.ips](11-bezier-stroke.ips) | A quadratic vector stroke |
| [12-distance-line.ips](12-distance-line.ips) | Analytic coverage around a segment |
| [13-noise-marble.ips](13-noise-marble.ips) | Fractal noise with domain warping |
| [14-cellular-stones.ips](14-cellular-stones.ips) | Cellular distances mapped to colors |
| [15-rotated-spokes.ips](15-rotated-spokes.ips) | Vec2 rotation and components |
| [16-radial-fold.ips](16-radial-fold.ips) | Kaleidoscope folding in a distance field |
| [17-sine-height.ips](17-sine-height.ips) | Float math with explicit conversion to pixels |
| [18-smoothstep-border.ips](18-smoothstep-border.ips) | Two-sided coverage and color scaling |
| [19-looped-checkers.ips](19-looped-checkers.ips) | Nested integer loops and parity |
| [20-output-anchors.ips](20-output-anchors.ips) | Drawing and structured attachment points |
| [21-while-growth.ips](21-while-growth.ips) | Stateful iteration with typed locals |
| [22-helper-leaves.ips](22-helper-leaves.ips) | A reusable typed drawing helper |
| [23-nested-choices.ips](23-nested-choices.ips) | Typed vector choices and nested seed-direct draws |
| [24-uniform-emblem.ips](24-uniform-emblem.ips) | Uniform colors scales and integer density |
