# Grammar

This EBNF records the current parser. Braces in the grammar mean repetition,
square brackets mean optional syntax and quoted strings are literal tokens.
`identifier` is an ordinary identifier; `name` additionally accepts soft
keywords as described in [source syntax](syntax.md). Semantic checks such as
types, restricted declaration constants, required declarations and acyclic calls
apply after parsing. Strings are only valid as import paths after typing.

```ebnf
program       = { import }, asset, EOF ;
import        = "import", name, "from", string, [ ";" ] ;
asset         = "asset", identifier, "{", { declaration }, "}" ;
declaration   = size | origin | variants | archetypes | input | output
              | palette | asset_constant | draw | helper ;
size          = "size", integer, (integer | xdigits), [ ";" ] ;
origin        = "origin", expression, ",", expression, [ ";" ] ;
variants      = "variants", integer, [ ";" ] ;
archetypes    = "archetypes", integer, [ ";" ] ;
input         = "input", input_type, name, "=", declaration_constant, [ ";" ] ;
input_type    = "color" | "f32" | "u32" | "i32" ;
output        = "output", [ "list" ], output_kind, name, [ ";" ] ;
output_kind   = "point" | "circle" | "rect" ;
palette       = "palette", name, "=", "[", [ expressions ], "]", [ ";" ] ;
asset_constant = "const", value_type, name, "=", expression, [ ";" ] ;  (* contextual "const" *)
draw          = "draw", block ;
helper        = name, "(", [ parameters ], ")", [ "->", value_type ], block ;
parameters    = parameter, { ",", parameter }, [ "," ] ;
parameter     = name, ":", value_type ;
scalar_type   = "i32" | "f32" | "color" | "vec2" | "distribution" ;
sampler_type  = "sampler", "<", ("i32" | "f32" | "color" | "vec2"), ">" ;
list_type     = "list", "<", (scalar_type | sampler_type), ">" ;
value_type    = scalar_type | list_type | sampler_type ;
block         = "{", { ";" | statement }, "}" ;
statement     = assignment | call_statement | if | while | for
              | "break" | "continue" | return ;
assignment    = [ "distribution" | list_type | sampler_type ], name, "=", expression ;
call_statement = name, { postfix_suffix } ;  (* must end as a call, capture or pick *)
if            = "if", "(", expression, ")", block,
                [ "else", (if | block) ] ;
while         = "while", "(", expression, ")", block ;
for           = "for", identifier, "in", expression, "..", expression, block ;
return        = "return", [ expression ] ;
expression    = logical_or ;
logical_or    = logical_and, { "||", logical_and } ;
logical_and   = bit_or, { "&&", bit_or } ;
bit_or        = bit_xor, { "|", bit_xor } ;
bit_xor       = bit_and, { "^", bit_and } ;
bit_and       = equality, { "&", equality } ;
equality      = comparison, { ("==" | "!="), comparison } ;
comparison    = random_range, { ("<" | "<=" | ">" | ">="), random_range } ;
random_range  = shift, [ "to", shift, [ "with", shift ] ] ;  (* contextual "to"/"with"; no chaining *)
shift         = additive, { ("<<" | ">>"), additive } ;
additive      = multiplicative, { ("+" | "-"), multiplicative } ;
multiplicative = unary, { ("*" | "/" | "%"), unary } ;
unary         = ("-" | "!" | "~"), unary | postfix ;
postfix       = primary, { postfix_suffix } ;
postfix_suffix = ".", name | "(", [ arguments ], ")"
               | "[", expression, "]" ;
primary       = literal | name | compound | list_literal | point_distribution | "(", expression, ")" ;
point_distribution = ("weighted" | "density"), "{", [ distribution_points ], "}" ;  (* contextual before brace *)
distribution_points = expression, ":", expression, { ",", expression, ":", expression }, [ "," ] ;
sampler_capture = "sampler", "(", expression, ")" ;  (* contextual; the outer random_range is captured *)
sampler_pick  = postfix, ".pick", "(", [ [ "n", "=" ], expression ], ")" ;
list_literal  = "[", [ expressions ], "]" ;
compound      = "{", [ expressions ], "}" ;
expressions   = expression, { ",", expression }, [ "," ] ;
arguments     = argument, { ",", argument }, [ "," ] ;
argument      = [ name, "=" ], expression ;
literal       = integer | floating | color | string | "true" | "false" ;
declaration_constant = literal | "(", declaration_constant, ")"
                     | "-", constant_atom ;
constant_atom = literal | "(", constant_atom, ")" ;
identifier    = (letter | "_"), { letter | digit | "_" } ;
xdigits       = ("x" | "X"), digit, { digit } ;
integer       = digit, { digit } | "0", ("x" | "X"), hex, { hex } ;
floating      = digits, ".", digits, [ exponent ] | digits, exponent ;
exponent      = ("e" | "E"), [ "+" | "-" ], digits ;
color         = "#", (3 hex | 4 hex | 6 hex | 8 hex) ;
```

In `return`, the optional expression is omitted when the next token is `;`
or `}`; newline alone does not terminate it. Numeric token conversion has
the [literal restrictions](syntax.md#literals) described above. The parser
allows empty palette/compound syntax, but semantic checks reject empty
palettes and compounds whose arity is not 2, 3 or 4. Metadata keywords are
recognized contextually; their names cannot introduce helpers in that position.
Declaration typing rejects strings as values and permits unary minus only
on numeric literals. Palette elements use the same restricted constant
grammar after parsing; a general expression AST does not make it a valid
palette element.

Asset `const` declarations use the general expression grammar with the
[compile-time restrictions](assets.md#asset-constants). Their graph permits
forward references and rejects cycles. `const(...)` still introduces a helper.

Shifts bind more tightly than comparisons on both sides: `value < 1 << bits`
means `value < (1 << bits)`. Comparison chains associate leftward, so
`a < b < c` means `(a < b) < c`, not two comparisons joined by `&&`.

## Declaration effects

Asset const and sampling-palette expressions must be pure compile-time values.
An outer palette range captures a sampler; scalar entries then lift to constant
descriptions. Runtime list ranges stay eager, and sampler picks cannot execute
in declaration initializers. See [palettes](palettes.md#sampling-palettes).

Point distribution literals accept general numeric runtime expressions. Static
operands are checked during compilation; const declarations require pure
operands. Each key/value pair snapshots its original numeric type in written
order. See [weighted descriptions](random.md#weighted-point-distributions) and
[density profiles](random.md#density-point-distributions).

At the top level of a for upper bound, the following brace starts the loop
body, so numeric bounds named `weighted` or `density` keep their meaning.
Inside nested arguments, brackets or parentheses, point literals remain
ordinary expressions.
