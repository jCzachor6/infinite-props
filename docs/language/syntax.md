# Source syntax and names

Source is UTF-8. Whitespace separates tokens; newlines do not end statements.
`//` comments end at the newline; `/* ... */` comments do not nest and end at
the first closing delimiter. Semicolons are optional between statements,
asset declarations and imports. Extra semicolons are ignored.

Ordinary identifiers match `[A-Za-z_][A-Za-z0-9_]*` and are case-sensitive.
Control/declaration keywords (`asset`, `input`, `output`, `import`, `from`,
`if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`,
`true`, `false`) and `const`, `to`, `with`, `distribution`, `sampler`, `stepper`,
`weighted`, `density`, `layer`, `mask` and `clip` are reserved in all name
positions, without compatibility aliases. `draw`, `at`, scalar/geometry type
names and `list` are soft keywords usable as names; an asset-level function
named `draw` is the entry. Asset and material names require ordinary identifiers.
`size`, `origin` and `palette` introduce asset metadata; declare size and origin
at most once. `const type name = expression` declares an
[asset constant](assets.md#asset-constants).

`material` is contextual only in file-level `material Name { ... }`.
Material types use an import alias, such as `forest.Bark`, in a separate
namespace from bare helper names.

A builtin has a bare name (`rect`, `lerp`) or one namespace and member
(`noise.fbm`). Namespaces are `color`, `vec2`,
`dist`, `noise`, `cell`, `stream`, `surface`, `mask`, `list`, `diagram`,
`regions`, `part` and `parts`. Functions `voronoi` and `contour` also qualify
constants, as in `voronoi.grid`. Helpers cannot use bare builtin names;
import aliases cannot use namespace names, `voronoi` or `contour`.
Asset declarations share one namespace and need distinct names; `seed` and
`draw` cannot be declared. `seed` is also a helper parameter and result type.
Locals and parameters may reuse builtin names: `rect = 2` does not hide
`rect(...)`.

<!-- example: ../../examples/language/errors/builtin-helper.ips -->

<!-- llm: skip -->
This example is intentionally rejected: `sqrt` is a builtin.

<!-- llm: skip -->
An asset with `render` has more builtins: the primitives such as `box` and
the namespaces `vec3` and `skin`. Those names stay free for helpers, locals
and import aliases in an asset with `draw`; see [models](models.md).

## Literals

- Integers: decimal or `0x`/`0X` hexadecimal, stored as 32-bit patterns
  (`0xffffffff` is -1). Maximum: `4294967295` or eight hex digits.
  Leading decimal zeros such as `010` are rejected; there is no octal form.
- Floats: a fraction and/or exponent, such as `2.5`, `1e3`, `4.0e-2`.
  Fractions need digits on both sides of the dot (`0.5`, not `.5`).
  Values use IEEE-754 binary32; out-of-range literals such as `1e400` are rejected.
- Colors: `#RGB` and `#RGBA` repeat each digit; `#abc` is `#aabbcc`.
  Long forms are `#RRGGBB` and `#RRGGBBAA`. Missing alpha is 255.
- `true` and `false` are `i32` values 1 and 0.
- Strings are quoted import paths, with escapes `\n`, `\t`, `\r`, `\"` and
  `\\`. Strings are not runtime values.

Floating operations can produce infinity/NaN, but neither has a literal.
Minus is an operator. Input defaults and literal palette elements allow a
literal, parentheses and one numeric unary minus, without arithmetic, calls
or RNG. Runtime expressions use ordinary operators and wrapping integers.

<!-- example: ../../examples/language/syntax.ips -->

<!-- example: ../../examples/language/errors/leading-zero.ips -->

<!-- llm: skip -->
This example is intentionally rejected: a leading zero does not select octal.

<!-- llm: skip -->
An expression may be nested or chained up to 256 levels, counting each pair
of parentheses, call, index and operator on the way down; a sum of 256 terms
is at the limit. Blocks, including the branches of an `else if` chain, may
nest 256 deep. Deeper source is a compile error: compute part of the
expression in a local, or move inner blocks into a helper.

Calls, helper parameters, palettes, lists and compound literals allow trailing
commas. Syntax is ASCII; Unicode appears only in comments and strings.
