# Writing idiomatic props

A prop is judged by how different, and how good, every seed looks. The rules
below are the house style of the examples and the showcase props. Follow them
when you write a new asset: they keep sources short, every roll visible and
every seed distinct. The complete example at the end applies all of them.

The smallest prop in this style draws its proportions and its color from one
shaped distribution and reports its collider:

<!-- example: ../../examples/language/minimal-prop.ips -->

## Draw values with `to`

Every random number is a range: `w = 20 to 30`, `tilt = -0.4 to 0.4`,
`lean = base - 2 to base + 2`. Do not build draws by hand from `unit()`,
`range()` or arithmetic such as `20 + int(unit() * 11)`: `to` says the same
thing in one readable step, picks `i32` or `f32` from the bounds and
includes both ends. Keep `index(n)` for selecting a palette element.

## Shape every proportion with `with`

Uniform draws make half of all seeds look extreme. Give sizes, lengths and
offsets a shape: `gaussian` for natural variation around a typical value,
`beta` to lean towards one end (rust that is mostly light, wear that is
mostly low) and `triangular` for a clear most-likely value. Declare a shared
description once, as in `distribution typical = gaussian(mean=0.5, sigma=0.18)`,
and reuse it for every proportion of one object.

Distribution parameters are in the units of the drawn value; they are not
rescaled to the range. `14.0 to 26.0 with gaussian(mean=0.5, sigma=0.18)` has
no probability inside its range and traps. Either describe the value in its
own units, as in `lean = -2.0 to 2.0 with gaussian(mean=0.0, sigma=0.8)`, or
draw a normalized roll and map it:
`height = lerp(14.0, 26.0, 0.0 to 1.0 with typical)`.

## Draw colors as gradients

Colors are drawn the same way as numbers. Pick the two ends of the look you
want and draw between them: `metal = #705845 to #c8c8c8`. Add `with` to skew
the result: `body = color.shade(base, -6) to base with beta(alpha=4.0, beta=2.0)`
mostly lands near `base`. One draw moves every channel together, so the
result never leaves the gradient. Do not draw red, green and blue
separately, and do not compute random channels by hand.

Derive the related tones of one material from its drawn color with
`color.shade` (lighter or darker steps), `color.mix` and `color.hsv`. Adjust a
channel with `rgba(c.r + 5, c.g, c.b, c.a)`; `rgb()` and `rgba()` clamp.

## Use palettes for fixed sets

A set of hand-picked colors or levels belongs in an asset-level `palette`:
`palette hues = [#6fd3ff, #c38bff, #7dffb0]`. Choose an element with
`hues[index(3)]` and blend a color or `f32` palette continuously with
`ramp(hues, t)`. Palettes are visible in every helper, so a material ramp is
declared once and used everywhere. Keep literal colors out of the drawing
code when a palette can name them.

## Choose with `pick` and `chance`

Pick one of several values with `pick(2, 3, 4)` or
`pick(#c0a060, #a0a0a8, #b87333)`; all choices share one type. Decide an
optional feature with `chance(p)`, as in `if (chance(0.3)) { ... }`. Both
consume one draw.

## Make every seed a different prop

With only a seed, and every input at its default, every seed must give a
visibly different result. Variety must never depend on inputs:

- An ordinary style input with default 0 rolls from the seed; values from 1 force one
  look. Derive the roll by hashing the seed, as in
  `look = style; if (style == 0) { look = 1 + (hash(seed ^ 0x2f6b9a13) & 1) }`, so a
  rolled style renders exactly like the same style chosen explicitly.
- Input defaults mean "roll it": `0` for `u32` style switches and a
  transparent color for color inputs, as in `input color tint = #00000000`
  read with `if (tint.a == 0) { base = hues[index(4)] }`.
- Roll every part of the look (shape, material, colors and details) from the
  seed; an input only overrides one of those rolls.

## Keep one silhouette across styles

Styles of one seed should read as the same object. Roll the shape inside
its own stream, `stream_push(hash(seed ^ 0x51ed2701))` ... `stream_pop()`,
before any style-dependent choice. Make every roll unconditionally, even
when a style does not use it, so a style never shifts the draws that come
after it.

## Pass inputs to helpers

Helpers see palettes and `seed`, but not inputs. Pass what
they need as typed parameters, as in `paint(base: color, look: i32)`. A local
keeps the type of its first assignment, so write `x = 0.0` when the value
becomes `f32` later.

## Paint runs, not pixels

Every executed instruction spends fuel from the asset's budget (5,000,000 by
default). Filling a 64×64 area costs about 4,200 fuel as one `rect`, 9,600 as
one `rect` per row, 71,000 as a `pixel` loop and 84,000 with a helper call per
pixel. Compute each row's extent once and paint it with `rect`; draw outlines
with `line`, `oval` and `poly`. Reserve per-pixel loops for small details.

## Before and after

Each program below compiles and runs, but breaks one rule. The correction
follows each one.

<!-- example: ../../examples/language/style/unit-arithmetic.ips -->

Write `w = 20 to 30`: one readable draw with both ends included.

<!-- example: ../../examples/language/style/uniform-proportion.ips -->

A uniform height makes every second pillar look extreme. Write
`h = lerp(14.0, 26.0, 0.0 to 1.0 with gaussian(mean=0.5, sigma=0.18))`.

<!-- example: ../../examples/language/style/channel-draws.ips -->

Three independent channel draws produce colors outside the intended family.
Draw the color as one gradient: `c = #2878b4 to #5ac8ff`.

<!-- example: ../../examples/language/style/pixel-loop.ips -->

The per-pixel loop spends about 43,000 fuel. Painting one `rect` per row from
the row's extent, `half = sqrt(r * r - dy * dy)`, draws the same disc for
about 2,200.

<!-- example: ../../examples/language/style/input-variety.ips -->

With only a seed this banner never changes. Make the default `0` mean "roll
it": `input u32 pattern = 0`, then
`kind = pattern; if (pattern == 0) { kind = 1 + (hash(seed ^ 0x6d2b79f5) & 1) }`
and branch on `kind`.

## Pitfalls

These compile errors and surprises catch most first drafts:

- `min`, `max` and `clamp` return `i32` for integer-only arguments. Use an
  explicit `float(...)` or one f32 argument before reassigning an f32 local.
- Colors cannot be compared with `==`. Compare channels, as in
  `tint.a == 0`.
- `for` bounds must be `i32`: write `for i in 0..int(count)`.
- There are no arrays besides palettes. Use a palette for a fixed list and
  compute repeated positions inside a loop.
- `part.outputs.name` reads the last call of that part. Read an output right
  after the call that produced it.
- A translucent color drawn inside a part over its empty canvas disappears.
  Draw glazes and shadows that cover other parts in the asset that places
  them.
- A local keeps the type of its first assignment: `x = 0` stays `i32`.

## A complete example

The crystal below applies every rule: a hashed variant roll, a transparent
color input that means "roll it", a shape in its own stream shaped by one
distribution, a skewed color gradient, palettes, `pick`, `chance`, a helper
that receives what it needs, and rows painted with `rect`.

<!-- example: ../../examples/language/idioms.ips -->
