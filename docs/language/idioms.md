# Writing idiomatic props

A good prop looks varied and convincing across seeds. Follow these rules,
used by the examples and showcase, to keep sources short and rolls clear.
The complete example applies them all.

This small prop draws proportions and color from one shaped distribution
and reports its collider:

<!-- example: ../../examples/language/minimal-prop.ips -->

## Draw values with `to`

Write random numbers as ranges: `w = 20 to 30`, `tilt = -0.4 to 0.4`,
`lean = base - 2 to base + 2`. Prefer `to` over `unit()`, `range()` or
handwritten draws such as `20 + int(unit() * 11)`. The bounds select `i32`
or `f32`, and both ends are included. Use `index(n)` for palette selection.

## Shape every proportion with `with`

Shape sizes, lengths and offsets: `gaussian` varies around a typical value,
`beta` favors one end (mostly light rust or low wear), and `triangular`
favors a peak. Share a description across an object's proportions:
`distribution typical = gaussian(mean=0.5, sigma=0.18)`.

Distribution parameters use the drawn value's units, without rescaling.
`14.0 to 26.0 with gaussian(mean=0.5, sigma=0.18)` has no probability in its
range and traps. Use matching units, as in
`lean = -2.0 to 2.0 with gaussian(mean=0.0, sigma=0.8)`, or map a normalized roll:
`height = lerp(14.0, 26.0, 0.0 to 1.0 with typical)`.

## Draw colors as gradients

Draw between two colors: `metal = #705845 to #c8c8c8`. Shape the result with
`with`: `body = color.shade(base, -6) to base with beta(alpha=4.0, beta=2.0)`
favors `base`. One draw moves all channels together along the gradient.
Avoid separate random channels.

Derive material tones with `color.shade`, `color.mix` and `color.hsv`.
Adjust channels with `rgba(c.r + 5, c.g, c.b, c.a)`; `rgb()` and `rgba()` clamp.

## Use palettes for fixed sets

Put fixed colors or levels in an asset-level `palette`:
`palette hues = [#6fd3ff, #c38bff, #7dffb0]`. Choose an element with
`hues[index(3)]`; blend a color or `f32` palette with `ramp(hues, t)`.
Helpers share palettes, so declare each ramp once. Use named palettes in
drawing code instead of repeated literal colors.

## Choose with `pick` and `chance`

Pick one of several values with `pick(2, 3, 4)` or
`pick(#c0a060, #a0a0a8, #b87333)`; all choices share one type. Decide an
optional feature with `chance(p)`, as in `if (chance(0.3)) { ... }`. Both
consume one draw.

## Make every seed a different prop

With all inputs at their defaults, seeds alone must produce visible variety:

- Style 0 rolls from the seed; values from 1 force a look. Choose from a
  [named seed](random.md#named-seeds), not the stream:
  `look = style; if (style == 0) { look = choose(seed.initial().fork("look"), 1, 2) }`,
  so rolled and explicitly chosen styles render alike.
- Input defaults mean "roll it": `0` for `u32` style switches and a
  transparent color for color inputs, as in `input color tint = #00000000`
  read with `if (tint.a == 0) { base = hues[index(4)] }`.
- Roll shape, material, colors and details from the seed; inputs override rolls.

## Keep one silhouette across styles

Styles of one seed should share a silhouette. Roll shape in its own stream,
`stream.push(seed.initial().fork("shape"))` ... `stream.pop()`, before style choices.
Make even unused rolls so styles do not shift later draws.

## Pass inputs to helpers

Helpers see palettes and `seed`, but not inputs. Pass inputs as typed
parameters: `paint(base: color, look: i32)`. Initialize a later `f32` with
`x = 0.0`; a local keeps its first type.

## Paint runs, not pixels

Instructions spend fuel (default budget: 25,000,000). Filling 64×64 costs
about 4,200 as one `rect`, 9,600 as one `rect` per row, 71,000 as a `pixel`
loop, or 84,000 with a helper per pixel. Compute each row's extent once and
paint it with `rect`. Use `line`, `oval` and `poly` for outlines; pixel loops
for small details.

## Make it look good

Compare twenty or more seeds side by side, at 3× zoom and at real size in place.

- Use 4 or 5 material tones, `stepper(dark to light).pick(5)`, with cool-blue
  shadows and warm-yellow highlights. Paint on asphalt takes a fifth of its grey.
- Break silhouettes with edge lobes and varied angles and sizes; circles read as bubbles.
- Texture with 1–2 px marks (leaves, chips); per-pixel noise reads as static.
- Subtract worn patches with a mask, then add grit to what remains.
- Let parts draw units (clumps, wheels) and the root place them.
- Give looks ranges, `[#2a6a44 to #7ab23c]`; use `weighted` for rare looks
  (1 seed in 16 or 100).
- Keep pixels crisp: one world scale, integer zoom, 90° turns.
- Measure heavy seeds with `ipgen`; many props multiply fuel use.
- Return colliders, anchors, masses and mount points alongside pixels.

## Before and after

<!-- llm: skip -->
Each program below compiles and runs, but breaks one rule. The correction
follows each one.

<!-- example: ../../examples/language/style/unit-arithmetic.ips -->

<!-- llm: skip -->
Write `w = 20 to 30`: one readable draw with both ends included.

<!-- example: ../../examples/language/style/uniform-proportion.ips -->

<!-- llm: skip -->
A uniform height makes every second pillar look extreme. Write
`h = lerp(14.0, 26.0, 0.0 to 1.0 with gaussian(mean=0.5, sigma=0.18))`.

<!-- example: ../../examples/language/style/channel-draws.ips -->

<!-- llm: skip -->
Three independent channel draws produce colors outside the intended family.
Draw the color as one gradient: `c = #2878b4 to #5ac8ff`.

<!-- example: ../../examples/language/style/pixel-loop.ips -->

<!-- llm: skip -->
The per-pixel loop spends about 43,000 fuel. Painting one `rect` per row from
the row's extent, `half = sqrt(r * r - dy * dy)`, draws the same disc for
about 2,200.

<!-- example: ../../examples/language/style/input-variety.ips -->

<!-- llm: skip -->
With only a seed this banner never changes. Make the default `0` mean "roll
it": `input u32 pattern = 0`, then
`kind = pattern; if (pattern == 0) { kind = choose(seed.initial().fork("pattern"), 1, 2) }`
and branch on `kind`.

## Pitfalls

Common mistakes:

- Integer-only `min`, `max` and `clamp` return `i32`. Use `float(...)` or
  one f32 argument before assigning to an f32 local.
- Compare color channels (`tint.a == 0`), not colors with `==`.
- `for` bounds must be `i32`: write `for i in 0..int(count)`.
- Lists cannot grow. Use a palette or complete list literal; compute repeated
  positions in a loop.
- Helpers cannot use builtin names. Name them after what they draw.
- `part.outputs.name` reads that part's last call. Read outputs right after it.
- A translucent color over a part's empty canvas disappears. Paint glazes
  and shadows over other parts in their parent asset.
- A local keeps the type of its first assignment: `x = 0` stays `i32`.

## A complete example

The crystal combines a hashed variant roll, transparent input for seed-drawn
color, a shape distribution in its own stream, a shaped gradient, palettes,
`pick`, `chance`, typed helper arguments and rows painted with `rect`.

<!-- example: ../../examples/language/idioms.ips -->
