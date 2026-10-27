# Materials

A material is a named look declared once and used everywhere: a base color,
author-chosen shades, an edge color and width, and a taper. Declare materials
only in a **material file** — a file with no asset — and import that file with
the ordinary import syntax. One file may define one material or a library of
several, and it may declare its own `palette` and `const` values for the
fields to use.

```ips
material Bark {
  base = #5a4230 to #7a5c40 with gaussian(mean = 0.5, sigma = 0.18)
  shadow = #2a1a10 to #3a2618
  highlight = #9a7a54
  edge = #1f140c
  taper = 0.5 to 0.8
  edge_width = 1
}
```

`base` (color) is required. `edge` defaults to `base`, `taper` (f32) defaults
to `1.0` (no taper) and `edge_width` (i32 pixels) defaults to `0`. Every other
field is named by the author and may be a `color`, `i32`/`f32` or `vec2`
constant or range, with an optional `with` distribution. Shades are named
values written as colors or ranges; they are not derived from `base`.

Create an instance by calling the definition name in `draw`. Creation rolls
every range once, each field on its own, in definition order; reading a field
never rolls.

```ips
import forest from "materials/forest.ips";

draw {
  bark = Bark()
  branch(32.0, 52.0, 12.0, 3.0, bark)
}

branch(x: f32, y: f32, dx: f32, w: f32, m: Bark) {
  line(x, y, x + dx, y - 11.0, int(w * m.taper), m.shadow)
  pixel(int(x + dx), int(y - 11.0), m.highlight)
}
```

Each definition is its own type: a helper declares `m: Bark` and the compiler
checks every field read against that definition. Instances are immutable,
cannot be host inputs, and are passed to helpers by value (each field travels
as its own scalar). Copying an instance (`n = m`) copies its rolled fields
without new draws. Returning a material from a helper, `list<Material>` and
`sampler`/`stepper` of materials are not supported; pass a numeric selector
to a part and create the material inside it when a part needs one.

A material never draws. It proposes an edge (`edge`, `edge_width`) and a
taper; the helper drawing a shape decides per side and per end whether to use
them — for example, a branch helper omits the edge line at the joint where
the branch meets the trunk, so the branch reads as grown, not glued on.

<!-- example: ../../examples/language/materials/forest.ips -->

<!-- example: ../../examples/language/material-tree.ips -->
