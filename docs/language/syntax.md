# Source syntax and names

Source must be valid UTF-8. Whitespace separates tokens; newlines do not end
statements. `//` comments extend to the next newline and `/* ... */` comments
end at the first closing delimiter (they do not nest). Semicolons are optional
separators at every level: between statements, between the declarations of an
asset and between imports. A spare semicolon is ignored. Forms without
semicolons remain valid; newline is whitespace and never implicitly terminates
a declaration or expression.

Ordinary identifiers match `[A-Za-z_][A-Za-z0-9_]*` and are case-sensitive.
Control/declaration keywords (`asset`, `input`, `output`, `import`, `from`,
`if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`,
`true`, `false`) and `const`, `to`, `with`, `distribution`, `sampler`, `stepper`,
`weighted`, `density`, `layer`, `mask` and `clip` are reserved. `draw`, `at`, scalar type names and geometry kind
names and `list` are soft keywords accepted in name positions; `draw` still selects
the entry block when used as an asset-level function name. Reserved words cannot
name assets, inputs, outputs, locals, helpers, parameters, aliases or members.
Newly introduced keywords have no contextual compatibility aliases. Asset and
material names require an ordinary identifier. Asset-level `size`,
`origin` and `palette` introduce metadata; `size` and `origin` are declared
at most once.
`const type name = expression` introduces an [asset constant](assets.md#asset-constants)
only in declaration position. The reserved word cannot be used as a name elsewhere.

`material` is contextual at file scope in `material Name { ... }`. It remains
an ordinary identifier elsewhere. Material type names use their import alias,
such as `forest.Bark`, and occupy a separate namespace from unqualified helpers.

A builtin is either a bare word, such as `rect` or `lerp`, or one namespace
and one member, such as `noise.fbm`. The namespaces are `color`, `vec2`,
`dist`, `noise`, `cell`, `stream`, `surface`, `mask`, `list`, `diagram`,
`regions`, `part` and `parts`. `voronoi` and `contour` are functions whose
names also qualify their constants, as in `voronoi.grid`.
A helper cannot take the name of a bare builtin, and an import alias cannot
take the name of a namespace or of `voronoi` or `contour`: both are compile
errors. Everything an asset
declares shares one namespace, so an input, output, palette, constant, helper
and import alias all need different names. `seed` and `draw` cannot be
declared. A local or a parameter may still reuse a builtin's name, because a
call and a value are never confused: `rect = 2` does not hide `rect(...)`.

<!-- example: ../../examples/language/errors/builtin-helper.ips -->

<!-- llm: skip -->
This example is intentionally rejected: `sqrt` is a builtin.

## Literals

- Integers: decimal digits or `0x`/`0X` plus hexadecimal digits. They become
  32-bit patterns, so `0xffffffff` is the expression value -1. A literal must
  fit 32 bits: at most `4294967295` or eight hexadecimal digits. A decimal
  literal has no leading zero; `010` is rejected, not read as octal.
- Floats: digits with a fractional part and/or exponent, such as `2.5`,
  `1e3`, `4.0e-2`. The fractional part requires digits on both sides of the
  dot; write `0.5`, not `.5`. Evaluation uses IEEE-754 binary32, and a literal
  outside its range, such as `1e400`, is rejected.
- Colors: `#RGB` and `#RGBA` repeat each digit; `#abc` is `#aabbcc`.
  Long forms are `#RRGGBB` and `#RRGGBBAA`. Missing alpha is 255.
- `true` and `false` are `i32` values 1 and 0.
- Strings are quoted import paths, with escapes `\n`, `\t`, `\r`, `\"` and
  `\\`. Strings are not runtime values.

There are no source literals for infinity or NaN; floating operations can
produce them. Numeric minus is an operator. Input defaults and palette
elements admit a small constant grammar: a literal, parentheses and one
numeric unary minus. They do not admit arithmetic, calls or RNG. Ordinary
runtime expressions retain their usual operator grammar and wrapping
integer semantics.

<!-- example: ../../examples/language/syntax.ips -->

<!-- example: ../../examples/language/errors/leading-zero.ips -->

<!-- llm: skip -->
This example is intentionally rejected: a leading zero does not select octal.

Trailing commas are accepted in call arguments, helper parameters, palette
elements, list literals and compound literals. Syntax is ASCII: Unicode text may appear only
in comments and string literals.
