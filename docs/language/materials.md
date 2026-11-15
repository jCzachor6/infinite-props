# Materials

A material names a reusable look: base color, shades, edges or taper.
Declare it in a **material file** with no asset and import it normally.
A file may define several materials and private `palette`/`const` values.
Importing assets cannot see those values and may reuse their names.

```ips
material Bark {
  base = #5a4230 to #7a5c40 with gaussian(mean = 0.5, sigma = 0.18)
  shadow = color.mix(base, #000000, 0.45)
  highlight = color.mix(base, #ffffff, 0.3)
  edge = #1f140c
  taper = 0.5 to 0.8
  edge_width = 1
}
```

Only `base` (a color) is required. Author-named fields hold `color`, `i32`,
`f32` or `vec2` constants/ranges, optionally shaped with `with`. They can
read earlier fields, so `shadow` above follows this instance's rolled `base`.

With `import forest from ...`, the type is `forest.Bark`. Create instances
in functions. Each range rolls once in field order; reads never roll.
Named arguments override fields with matching types. Later fields read
the overridden value.

```ips
import forest from "materials/forest.ips";

draw {
  bark = forest.Bark()
  burnt = forest.Bark(base = #2a1a10)
  branch(32.0, 52.0, 12.0, 3.0, bark)
}

branch(x: f32, y: f32, dx: f32, w: f32, m: forest.Bark) {
  line(x, y, x + dx, y - 11.0, int(w * m.taper), m.shadow)
  pixel(int(x + dx), int(y - 11.0), m.highlight)
}
```

Each definition is a distinct type: `m: forest.Bark` restricts reads to its
fields. Immutable instances pass to helpers as scalar fields; `n = m` copies
rolled values without draws. Materials cannot be host inputs, helper results,
`list<Material>` or `sampler`/`stepper` elements. For parts, pass a numeric
selector and create the material inside.

A local can be replaced only by the same material type. Replacement evaluates
before assignment: `m = forest.Bark(taper = m.taper * 0.9)` reads the old
`m.taper`, while defaults read earlier fields of the new instance.
Helper parameters stay read-only.

Check libraries with `ipc materials/forest.ips --check`; format with
`ipc materials/forest.ips --format-only --format formatted.ips`. Neither
creates an `.iprop`; compile an importing asset instead. The editor uses
these commands for diagnostics and Format Document.

Materials never draw. The drawing helper decides where to use author-defined
fields such as `edge`, `edge_width` and `taper`. For example, omit the edge
at a branch/trunk joint to make the branch look grown rather than glued on.

<!-- example: ../../examples/language/materials/forest.ips -->

<!-- example: ../../examples/language/material-tree.ips -->
