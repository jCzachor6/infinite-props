# The v0.4 source language

Language additions lower to existing VM operations and retain frozen runtime
semantics. The draw entry is `draw { ... }`, with read-only i32 `v` and `a`.
Helpers retain explicit parameter and return types. Color helpers use the
`color.*` namespace; bare builtin `mix` and `shade` have been removed.

Diagnostics suggest close canonical names, including transposed letters.
Source text must be valid UTF-8. Locations are 1-based Unicode codepoint
columns, with tabs counting as one; the editor maps them to UTF-16 positions.

`ipc entry.ips --format canonical.ips` compiles the original source and also
writes its canonical AST as parseable source. Keep the output beside its
imports when recompiling it. Formatting uses two-space indentation, explicit
expression grouping, canonical word keywords, normalized literal spellings
and named builtin arguments in signature order. Reformatting is idempotent;
roundtrips preserve the executable file after stripping source-line debug
information. Comments are absent from the AST and the formatted output.
The source output must differ from the input and the binary destination.

All builtin and canvas calls accept named arguments. A name binds the
corresponding signature slot; positional arguments bind slots from the start.
Duplicate, missing and unknown names are errors. Arguments evaluate in
signature order regardless of the order of written names. Polygon coordinates
evaluate before its color, retaining its existing lowering. Helpers accept
positional arguments. Part calls retain their existing named uniform rules.

```ips
asset example {
  size 16x16
  draw { rect(color = #fff, h = 10, x = 0, w = 10, y = 0) }
}
```

`#RGB` and `#RGBA` repeat each hex digit (e.g. `#abc` is `#aabbcc`);
`#RRGGBB` and `#RRGGBBAA` remain available. Omitted alpha is 255.
`rgb`/`rgba` clamp integer channels to 0–255. `int` truncates floats toward
zero with saturation and NaN → 0; an i32 remains exact. `float` widens i32,
or preserves f32 including negative zero. `lerp` accepts colors or numbers;
color interpolation retains the first alpha, like `color.mix`.

`pick(n)` is the exact `index(n)` alias, including its unsigned bounds.
`pick(value0, value1, ...)` eagerly evaluates all choices once in signature
order, then consumes one selection draw. Choices share a storable type;
mixed numeric choices widen to f32. `chance(p)` evaluates p then consumes
one draw and returns an i32 boolean for `unit() < p`, including p ≤ 0,
p ≥ 1 and NaN (false). These RNG calls retain their draw effects.

`roll(seed, a, b)` draws from a range using an explicit i32 seed.
`choose(seed, value0, ...)` selects from one or more typed choices, using
the same type rules as `pick`. All arguments evaluate in signature order
before seed isolation. The operation draws once from the explicit seed and
restores the outer stream after argument evaluation. Nested calls retain
each caller's stream; named arguments bind `seed`, `a`/`b` or `value0`,
`value1`, etc. A single choice still performs its isolated draw.

## Parameter names

The following names are the builtin contracts; numeric arguments widen i32
to f32 where required. Overloads with identical names resolve by value types.

| Call | Parameter names in evaluation order |
| --- | --- |
| `stream_push` | `seed` |
| `range` | `a, b` |
| `index` | `n` |
| `pick` | `n` |
| `chance` | `p` |
| `hash` | `x` |
| `noise` | `x, y, seed` |
| `fault` | `x, y, seed` |
| `smooth` | `t` |
| `sqrt` | `x` |
| `sin` | `x` |
| `cos` | `x` |
| `abs` | `x` |
| `floor` | `x` |
| `ceil` | `x` |
| `min` | `x, y` |
| `max` | `x, y` |
| `pow` | `x, y` |
| `exp` | `x` |
| `atan2` | `y, x` |
| `dist.circle` | `p, center, radius` |
| `dist.rect` | `p, center, half_size` |
| `dist.roundrect` | `p, center, half_size, radius` |
| `dist.line` | `p, a, b` |
| `dist.bezier` | `p, a, b, c` |
| `dist.bezier` | `p, a, b, c, d` |
| `fill` | `d, w` |
| `gain` | `t, k` |
| `quintic` | `t` |
| `noise.grad` | `x, y, seed` |
| `noise.fbm` | `x, y, octaves, seed` |
| `noise.cell` | `x, y, seed` |
| `warp` | `x, y, amount, seed` |
| `cell.jitter` | `cx, cy, seed` |
| `noise.ridge` | `x, y, octaves, seed` |
| `color.set.a` | `color, alpha` |
| `color.mul` | `a, b` |
| `color.scale` | `color, scale` |
| `color.hsv` | `color, hue, saturation, value` |
| `dither` | `t, x, y` |
| `fold` | `v, n` |
| `polar` | `v` |
| `vec2.len` | `v` |
| `vec2.dot` | `a, b` |
| `vec2.rot` | `v, a` |
| `smoothstep` | `a, b, t` |
| `remap` | `t, a, b, c, d` |
| `clamp` | `x, min, max` |
| `i32trunc` | `x` |
| `int` | `x` |
| `float` | `x` |
| `rgb` | `r, g, b` |
| `rgba` | `r, g, b, a` |
| `round` | `x` |
| `lerp` | `a, b, t` |
| `color.mix` | `a, b, t` |
| `color.shade` | `color, amount` |
| `pixel` | `x, y, color` |
| `rect` | `x, y, w, h, color` |
| `line` | `ax, ay, bx, by, thickness, color` |
| `curve` | `a, b, c, thickness, color` |
| `curve` | `a, b, c, d, thickness, color` |
| `oval` | `x, y, rx, ry, color` |
| `weather` | `seed, amount` |
| `unit`, `stream_pop` | none |
| `ramp` | `palette, t` |
| `poly` | `x0, y0, x1, y1, ...`, then `color` (positional color is first) |
| `pick` (multiple choices) | `value0, value1, ...` |
| `roll` | `seed, a, b` |
| `choose` | `seed, value0, value1, ...` |

## Emoji aliases

These 18 single Unicode scalars are accepted alongside the canonical words.
The lexer normalizes them to words, so the AST, diagnostics and `--format`
output use the canonical names. Namespace members retain their word spelling.
Comments and strings keep their original text.

| Word | Alias | Scalar |
| --- | --- | --- |
| asset | 📦 | U+1F4E6 |
| draw | ✏ | U+270F |
| input | 📥 | U+1F4E5 |
| output | 📤 | U+1F4E4 |
| palette | 🌈 | U+1F308 |
| import | 🔗 | U+1F517 |
| color | 🎨 | U+1F3A8 |
| vec2 | 📐 | U+1F4D0 |
| variants | 🔀 | U+1F500 |
| archetypes | 🗂 | U+1F5C2 |
| point | 🔘 | U+1F518 |
| rect | 🟦 | U+1F7E6 |
| circle | ⭕ | U+2B55 |
| true | 👍 | U+1F44D |
| false | 👎 | U+1F44E |
| noise | 🌀 | U+1F300 |
| dist | 📏 | U+1F4CF |
| cell | 🦠 | U+1F9A0 |

Variation selectors (including U+FE0F) and ZWJ sequences are rejected in
syntax. In particular, `draw` uses U+270F alone and `archetypes` uses U+1F5C2
alone. For example, `📏.circle(p, c, r)` is valid; `dist.⭕(p, c, r)` is not.
Numeric types, control flow, size/origin, `at`, `list`, and other builtins keep
their word spellings. The compiled executable is identical to the word form.
