# Source syntax and names

Source must be valid UTF-8. Whitespace separates tokens; newlines do not end
statements. `//` comments extend to the next newline and `/* ... */` comments
end at the first closing delimiter (they do not nest). Semicolons are optional
separators inside function blocks. One optional semicolon may follow each
import, `size`, `origin`, `variants`, `archetypes`, `input`, `output` or
`palette` or `const` declaration. Forms without semicolons remain valid; newline is
whitespace and never implicitly terminates a declaration or expression.

Ordinary identifiers match `[A-Za-z_][A-Za-z0-9_]*` and are case-sensitive.
Control/declaration keywords (`asset`, `input`, `output`, `import`, `from`,
`if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`,
`true`, `false`) and `const`, `to`, `with`, `distribution`, `sampler`,
`weighted`, `density`, `layer`, `mask` and `clip` are reserved. `draw`, `at`, scalar type names and geometry kind
names and `list` are soft keywords accepted in name positions; `draw` still selects
the entry block when used as an asset-level function name. Reserved words cannot
name assets, inputs, outputs, locals, helpers, parameters, aliases or members.
Newly introduced keywords have no contextual compatibility aliases. Asset names and
`for` counter names require an ordinary identifier. Asset-level `size`,
`origin`, `variants`, `archetypes` and `palette` introduce metadata.
`const type name = expression` introduces an [asset constant](assets.md#asset-constants)
only in declaration position. The reserved word cannot be used as a name elsewhere.

Use descriptive names distinct from builtin functions and the namespaces
`color`, `vec2`, `dist`, `noise`, `cell`, `surface`, `mask` and `list.len`. Builtin call resolution takes
precedence over user helpers with the same name.

## Literals

- Integers: decimal digits or `0x`/`0X` plus hexadecimal digits. They become
  32-bit patterns, so `0xffffffff` is the expression value -1. Use ordinary
  decimal without leading zeroes; literal parsing also recognizes leading-zero
  octal forms. Values outside the literal parser's range are rejected.
- Floats: digits with a fractional part and/or exponent, such as `2.5`,
  `1e3`, `4.0e-2`. The fractional part requires digits on both sides of the
  dot; write `0.5`, not `.5`. Evaluation uses IEEE-754 binary32.
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

Trailing commas are accepted in call arguments, helper parameters, palette
elements, list literals and compound literals. Syntax is ASCII: Unicode text may appear only
in comments and string literals.
