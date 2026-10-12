# Compiler tools and diagnostics

## Compiler output

`ipc file.ips -o file.iprop` validates and compiles the entry and imports.
Production output is optimized, debug-stripped and losslessly compressed
when compression saves space. `--debug` retains source maps/symbols;
`--strip` restores the default (the last flag wins). `--no-compress` writes
an expanded file; `--no-opt` disables compiler optimization for inspection.
`ipval` validates a binary, `ipdis` disassembles it and `ipgen` generates
pixels/outputs. The `.ipa` assembly language is the VM-level tool format.
For exact binary32 transport, assembly accepts `bits:0xXXXXXXXX` with eight
hexadecimal digits wherever it accepts f32 constants/defaults/origins. `ipdis`
uses this spelling for subnormals and nonfinite values, preserving NaN payloads
and signs. This assembly spelling is not an `.ips` literal; ordinary source
arithmetic and constant evaluation can produce those values.

`ipc --describe-language` exports the implemented source rules, restrictions,
shared call signatures and independent product/language/format/C API/ABI
versions. See the [language-description contract](../language-description.md)
for field meanings and executable acceptance cases.

The documentation build also generates `LLM_REFERENCE.md`, the complete
language for coding agents: the idioms chapter first, then the handbook's
language chapters, every builtin and checked registered examples. See
[reference generation](../llm-reference.md) for its budget, selection and
validated rejection/repair pairs.

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
the readable renderer. Bounded recovery collects at most 20 diagnostics;
dependent cascades are suppressed and a damaged AST never reaches generation.

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
comments keeps its written argument order. When the output is in another
directory, import paths are adjusted to refer to the same files, with aliases
and comments preserved. Formatting is idempotent and preserves
executable bytes after stripping source-line debug data.

`ipc entry.ips --format canonical.ips --format-only` runs the same full
in-memory compile/validation pipeline and writes only canonical source. It
creates no `.iprop`; `--format` without `--format-only` still compiles normally.
Source output must differ from the entry, all imports and any requested binary,
including filesystem aliases. `--format-only` requires `--format` and rejects
`--output` and `--check`. A compile or output error preserves the previous
source destination: the completed source is published by an atomic rename
on its destination filesystem. Temporary files belong to an exclusive
directory and are removed on failure. This is atomic replacement, without a
power-loss durability guarantee. A final destination symlink is replaced by
the formatted file; its earlier referent is left intact. Symlinks in the
destination directory are resolved before deriving relative imports.
JSON diagnostics keep the same schema and
exit statuses in this mode.

Optional semicolons and redundant grouping normalize away when they carry no
comments. A bare `return;` retains its required delimiter, and punctuation
carrying comments remains to preserve attachment. Float text roundtrips to the
same binary32 value, including signed zero; integer inputs preserve their
signed declaration or unsigned bit-pattern spelling.

Legacy assignments remain in their original branch or loop. Formatting does
not insert `let`, move an initializer to the function entry or rewrite helper
declarations with `fn`. In particular, a local first assigned in a skipped
branch keeps its existing typed zero at runtime; moving an RNG expression
outside that branch would change the stream. Existing identifiers and helper
names `let`, `fn` and `vec2` keep their bindings. Migration advice belongs in
a separate lint or diagnostic action rather than a formatting edit.

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

The compiler currently emits a default fuel budget of 2,000,000. The host
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

<!-- llm: skip -->
`--describe-language` separates each signature's intrinsic checks from its
argument expressions and from `effect_rules.resource_traps`. Fuel exhaustion
and the runtime call-depth limit can affect otherwise valid programs.
Operand-stack bounds are checked by executable validation; allocation failure
is a host/resource failure rather than a signature's numeric precondition.

<!-- example: ../../examples/language/traps/divide-by-zero.ips -->
