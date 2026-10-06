# Compiler tools, diagnostics and emoji

## Compiler output

`ipc file.ips -o file.iprop` validates and compiles the entry and imports.
Production output is optimized, debug-stripped and losslessly compressed
when compression saves space. `--debug` retains source maps/symbols;
`--strip` restores the default (the last flag wins). `--no-compress` writes
an expanded file; `--no-opt` disables compiler optimization for inspection.
`ipval` validates a binary, `ipdis` disassembles it and `ipgen` generates
pixels/outputs. The `.ipa` assembly language is the VM-level tool format.

`ipc --describe-language` exports the implemented source rules, restrictions,
shared call signatures and independent product/language/format/C API/ABI
versions. See the [language-description contract](../language-description.md)
for field meanings and executable acceptance cases.

`ipc entry.ips --check --diagnostics=json` runs the same import, parsing,
typing, planning, lowering, assembly, optimization and final-file validation
pipeline entirely in memory. It writes no executable or formatted source;
combining `--check` with `--output` or `--format` is an invocation error.
`--no-opt`, `--debug` and compression options select the same artifact policy
as ordinary compilation, while still writing no files. Each imported source
is loaded once, and error snippets use that exact owning snapshot.

JSON diagnostics use `schema_version: 1` with independently recorded product
and source-language versions, status and diagnostics. Each diagnostic provides
typed stable code/stage/severity, message, file, an end-exclusive range and
SHA-256 identity of its exact source snapshot. Offsets are zero-based UTF-8
bytes; line/column positions are one-based Unicode codepoints. CLI/I/O locations
may be null. Optional expected/actual types, suggestions, related locations and
fix-its have explicit fields. See the [protocol](../compiler-diagnostics.md)
and [schema](../compiler-diagnostics.schema.json). `--diagnostics=text` selects
the readable renderer. Multi-error recovery follows in the B.5 task.

`ipc entry.ips --format canonical.ips` also writes canonical parseable source.
The output uses two-space indentation, explicit expression grouping, word
keywords and normalized literals. Calls use the compiler's shared binding and
evaluation plan: ordinary builtins and helpers follow parameter order, `poly`
puts vertex coordinates before its colour, and a known part puts uniforms in
declaration order before seed/v/a/at and transforms. Unknown imports retain
written order until their declarations are available. Helpers keep their
existing `helper(parameters) -> type` syntax.
It must differ from the input and binary output. Comments keep their exact
contents, including delimiters and internal whitespace. A call containing
comments keeps its written argument order. Keep the formatted file beside
its imports when recompiling it. Formatting is idempotent and preserves
executable bytes after stripping source-line debug data.

Optional semicolons and redundant grouping normalize away when they carry no
comments. A bare `return;` retains its required delimiter, and punctuation
carrying comments remains to preserve attachment. Float text roundtrips to the
same binary32 value, including signed zero; integer inputs preserve their
signed declaration or unsigned bit-pattern spelling.

Comments are owning trivia on the source tokens, and AST nodes refer to
half-open token ranges. A comment beginning on the same line as the previous
token belongs after that token; other comments belong before the next token.
Thus comments before declarations follow their declaration when its position
is normalized, and comments after statements remain after the statement.
Within expressions and between named arguments, comments stay on the same
side of the adjacent token. Optional parentheses and separators carrying trivia stay in the output, so
multiline comments keep their token owner when the result is parsed again.
Comments in an empty block belong before its closing brace, and a commented
empty else block stays present. Repeated scalar metadata retains each earlier
declaration's source context inside the asset; the last declaration still
sets the effective value. Comments after the file belong to the end token.
None of this trivia affects execution or the byte/codepoint locations of
syntax tokens.

The C++ compilation API retains debug information so callers can select an
artifact policy. The editor requests it for inspection; binary exports use
production defaults. [Optimization behavior](../optimizations-v0.4.md) describes
the passes and their preservation contract.

## Diagnostics

Errors identify the file and one-based line/Unicode codepoint column; tabs
count as one column. The VS Code extension maps these locations to UTF-16
positions. Close misspellings suggest canonical names, including transpositions.
Parsing/type errors fail compilation without producing a valid executable.
Runtime traps are distinct: the program can compile but a given invocation
can exceed a resource limit or encounter invalid data.

<!-- example: ../../examples/language/errors/immutable-input.ips -->

<!-- example: ../../examples/language/errors/compound-local.ips -->

## Execution limits

The compiler currently emits a default fuel budget of 1,000,000. The host
can supply its invocation budget; each executed instruction/drawing operation
retains its normative cost. Optimizations may reduce total fuel without
changing image/output bits. A loop need not be statically bounded, but
execution traps when fuel runs out.

Canvas dimensions are limited to 16384 per side in the reference runtime.
Hosts may impose smaller limits on dimensions or total pixel counts.
Binary pools/indices, function nesting, stream stacks
and structured outputs also have bounds; see
[validation and resource limits](../../spec/iprop-v0.3.md#14-validation).
Compressed v0.4 files bound both encoded and decoded size to 64 MiB.
Host-specific limits and invalid variant/archetype indices can reject an
invocation before draw starts.

Integer division/remainder by zero traps; float division follows IEEE-754.
Noise coordinate checks, nonfinite HSV hue, invalid geometry, unset part
output reads and unbalanced stream pops can also trap. Traps must be handled
by the host; a partial image is not a successful generation result.

<!-- example: ../../examples/language/traps/divide-by-zero.ips -->

## Emoji aliases

The lexer accepts 18 exact single Unicode scalars alongside their canonical
word spellings. The alias table is generated from the lexer's actual registry.

<!-- aliases -->

Aliases normalize before parsing, so diagnostics, the AST and formatter use
word names. Strings/comments retain their original text. Variation selectors
(including U+FE0F) and ZWJ sequences are rejected in syntax. `draw` uses
U+270F alone and `archetypes` U+1F5C2 alone. Namespace members retain words:
`📏.circle(...)` is valid, while `dist.⭕(...)` is rejected. Other syntax keeps
its word spelling. Both forms compile to the same executable.

<!-- example: ../../examples/language/emoji.ips -->
