# Grammar

This EBNF records the current parser. Braces in the grammar mean repetition,
square brackets mean optional syntax and quoted strings are literal tokens.
`identifier` is an ordinary identifier; `name` additionally accepts soft
keywords as described in [source syntax](syntax.md). Semantic checks such as
types, literal-only defaults/palettes, required declarations and acyclic calls
apply after parsing. Strings are only valid as import paths after typing.

```ebnf
program       = { import }, asset, EOF ;
import        = "import", name, "from", string ;
asset         = "asset", identifier, "{", { declaration }, "}" ;
declaration   = size | origin | variants | archetypes | input | output
              | palette | draw | helper ;
size          = "size", integer, (integer | xdigits) ;
origin        = "origin", expression, ",", expression ;
variants      = "variants", integer ;
archetypes    = "archetypes", integer ;
input         = "input", input_type, name, "=", literal ;
input_type    = "color" | "f32" | "u32" | "i32" ;
output        = "output", [ "list" ], output_kind, name ;
output_kind   = "point" | "circle" | "rect" ;
palette       = "palette", name, "=", "[", [ expressions ], "]" ;
draw          = "draw", block ;
helper        = name, "(", [ parameters ], ")", [ "->", value_type ], block ;
parameters    = parameter, { ",", parameter }, [ "," ] ;
parameter     = name, ":", value_type ;
value_type    = "i32" | "f32" | "color" | "vec2" ;
block         = "{", { ";" | statement }, "}" ;
statement     = assignment | call_statement | if | while | for
              | "break" | "continue" | return ;
assignment    = name, "=", expression ;
call_statement = name, { postfix_suffix } ;  (* must end as a call AST *)
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
comparison    = shift, { ("<" | "<=" | ">" | ">="), additive } ;
shift         = additive, { ("<<" | ">>"), additive } ;
additive      = multiplicative, { ("+" | "-"), multiplicative } ;
multiplicative = unary, { ("*" | "/" | "%"), unary } ;
unary         = ("-" | "!" | "~"), unary | postfix ;
postfix       = primary, { postfix_suffix } ;
postfix_suffix = ".", name | "(", [ arguments ], ")"
               | "[", expression, "]" ;
primary       = literal | name | compound | "(", expression, ")" ;
compound      = "{", [ expressions ], "}" ;
expressions   = expression, { ",", expression }, [ "," ] ;
arguments     = argument, { ",", argument }, [ "," ] ;
argument      = [ name, "=" ], expression ;
literal       = integer | floating | color | string | "true" | "false" ;
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

The comparison production intentionally reflects the current asymmetric
shift restriction. Parentheses make shifted right operands unambiguous.
