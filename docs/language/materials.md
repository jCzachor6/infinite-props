# Materials

A material is a named look declared once and used everywhere: a base color,
author-chosen shades, an edge color and width, and a taper. Declare materials
only in a **material file** — a file with no asset — and import that file with
the ordinary import syntax. One file may define one material or a library of
several, and it may declare its own `palette` and `const` values for the
fields to use. Those belong to the material file: the importing asset does not
see them and may declare its own palettes and consts with the same names.

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

`base` (a color) is the only required field. Every other field is named by
the author and may be a `color`, `i32`, `f32` or `vec2` constant or range,
with an optional `with` distribution. A field may read the fields written
before it by name, so shades can follow the rolled `base` (`shadow` above is
always a darker version of this instance's bark).

Materials are named by the import alias: with `import forest from ...`, the
definition above is `forest.Bark`. Create an instance by assigning it in a
function. Creation rolls every range once, each field on its own, in the
order the fields are written; reading a field never rolls. Named arguments
replace fields for one instance; an override keeps the field's type, and the
fields after it read the override.

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

Each definition is its own type: a helper declares `m: forest.Bark` and the compiler
checks every field read against that definition. Instances are immutable,
cannot be host inputs, and are passed to helpers by value (each field travels
as its own scalar). Copying an instance (`n = m`) copies its rolled fields
without new draws. Returning a material from a helper, `list<Material>` and
`sampler`/`stepper` of materials are not supported; pass a numeric selector
to a part and create the material inside it when a part needs one.

A local keeps its first type. You can replace a whole instance with another
instance of the same material, but cannot change it into a scalar or a
different material type. The replacement is evaluated completely before
updating the local: `m = forest.Bark(taper = m.taper * 0.9)` reads the old
`m.taper`. A default expression that reads an earlier field still sees the
new instance's field. Helper parameters remain read-only.

Check a material library directly with `ipc materials/forest.ips --check`.
Format it with `ipc materials/forest.ips --format-only --format formatted.ips`;
both commands support material files without creating an `.iprop`. Ordinary
compilation uses an asset that imports the library. The editor uses these
same commands for diagnostics and Format Document.

A material never draws. Fields such as `edge`, `edge_width` and `taper` above
are ordinary fields the author chose; the helper drawing a shape decides per
side and per end whether to use them — for example, a branch helper omits the edge line at the joint where
the branch meets the trunk, so the branch reads as grown, not glued on.

<!-- example: ../../examples/language/materials/forest.ips -->

<!-- example: ../../examples/language/material-tree.ips -->
