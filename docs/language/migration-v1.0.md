# Migrating from 0.9 to 1.0

Language 1.0 gives every construct one spelling and one rule. It adds nothing:
it renames the builtins that did not follow the others, removes three
duplicate spellings and turns several silent surprises into compile errors.
Nothing is aliased. An old spelling does not compile, and its diagnostic names
what to write instead.

Only source changes. The VM, the binary format and the fuel tariff are the
same, existing `.iprop` files run as before, and a migrated source compiles to
the bytecode it produced under 0.9. Its metadata records a larger default fuel
budget, described below.

<!-- example: ../../examples/language/errors/renamed-builtin.ips -->

This example is intentionally rejected: `noise` is now `noise.value`.

## Renamed builtins

A builtin is a bare word or one namespace and one member. These did not fit:

| 0.9 | 1.0 |
| --- | --- |
| `noise(x, y, seed)` | `noise.value(x, y, seed)` |
| `fault(x, y, seed)` | `noise.fault(x, y, seed)` |
| `warp(x, y, amount, seed)` | `noise.warp(x, y, amount, seed)` |
| `fold(v, n)` | `vec2.fold(v, sectors)` |
| `polar(v)` | `vec2.polar(v)` |
| `fill(d, w)` | `dist.fill(distance, width)` |
| `color.set.a(color, alpha)` | `color.set_a(color, alpha)` |
| `stream_push(seed)` | `stream.push(seed)` |
| `stream_pop()` | `stream.pop()` |

Positional calls only change their name. The `p: vec2` overloads moved with
their functions.

## Renamed arguments

These matter only where an argument is passed by name:

| Call | 0.9 | 1.0 |
| --- | --- | --- |
| `noise.mask` | `kind` | `mode` |
| `vec2.rot` | `a` | `angle` |
| `surface.paint` | `image` | `source` |
| `part.id`, `part.place`, `part.fill` and the other `part.*` accessors | `p` | `part` |
| `regions.count`, `regions.id`, `regions.mask` and the other accessors | `rs` | `regions` |
| `regions.gap`, `regions.bevel` | `source` | `regions` |
| `diagram.count`, `diagram.site`, `diagram.neighbors` | `d` | `diagram` |
| `parts.count`, `parts.get` | `source` | `parts` |

The constructors `regions(source)`, `contour(source, ...)`,
`part.create(source)` and `parts.from_regions(source)` keep `source`.

## Removed spellings

| 0.9 | 1.0 |
| --- | --- |
| `i32trunc(x)` | `int(x)` |
| `pick(n)` | `index(n)` |
| `poly(color, x0, y0, x1, y1, x2, y2)` | `poly(x0, y0, x1, y1, x2, y2, color)` |

`i32trunc` took an `f32`: an integer argument was widened first. If you passed
an integer and rely on that, write `int(float(x))`. `pick` now always takes two
or more choices. Every drawing call ends with its color, as `poly(points,
color)` already did; a call with named arguments needs no change.

## Names

- A helper cannot take the name of a bare builtin. Before, such a helper was
  silently never called, except for `gaussian`, `triangular`, `beta`,
  `voronoi`, `contour` and `regions`, where the helper won. Rename the helper.
- An input, output, palette, constant, helper and import alias of one asset
  all need different names.
- An import alias cannot be a builtin namespace: `noise`, `color`, `vec2`,
  `dist`, `cell`, `stream`, `surface`, `list`, `diagram`, `regions`, `part`,
  `parts`, `voronoi` or `contour`. An import called `part` must be renamed.
- `seed` and `draw` cannot be declared by anything, including an output, a
  helper or a parameter. `at`, `rot`, `mirror_x` and `mirror_y` are reserved
  for inputs only; a constant or palette may now use them.
- A parameter cannot take the name of a palette or an output.
- A `for` counter can use any name a local can, such as `at`.

## Operators

Bitwise operators now bind more tightly than comparisons, so
`flags & 1 == 0` means `(flags & 1) == 0`. It used to mean
`flags & (1 == 0)`. A source that parenthesized the mask is unaffected.

`a < b < c` and `a == b == c` are rejected. They compared the 0/1 result of
the first comparison with `c`. Write `a < b && b < c`, or parenthesize the
comparison whose result you compare.

## Literals and declarations

- A decimal literal cannot start with a zero. `010` was read as octal 8.
- An integer literal must fit 32 bits. `4294967296` used to wrap to 0. In an
  `f32` position write a float literal: `4294967296.0`.
- A float literal outside the `f32` range and a word attached to a number,
  as in `3px`, are lexical errors.
- `size` and `origin` are declared once. A second declaration used to replace
  the first.
- A spare `;` is accepted between declarations, as it was between statements.

## Fuel budget

The compiler writes a default fuel budget of 25,000,000 instead of 5,000,000.
Prices are unchanged, so a program spends what it spent before; a binary
compiled earlier keeps the budget it was built with until it is recompiled.

## Reported earlier

- A value-returning helper that can reach its end is reported at the helper,
  instead of failing validation of the compiled program.
- `3 to 1` and `gaussian(0.0, -1.0)` are compile errors. A range or a
  distribution constructor whose constant operands always trap used to compile
  and trap on every run. Operands only known when the program runs still trap.
- A function with more than 1024 parameters and locals, and an asset with
  more than 1024 functions, are located errors.
- An expression nested or chained deeper than 256 levels, or blocks nested
  deeper than 256, are a located error. Deeper source used to overflow the
  compiler's stack and crash it.
