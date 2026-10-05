# Compiler tools, diagnostics and emoji

## Compiler output

`ipc file.ips -o file.iprop` validates and compiles the entry and imports.
Production output is optimized, debug-stripped and losslessly compressed
when compression saves space. `--debug` retains source maps/symbols;
`--strip` restores the default (the last flag wins). `--no-compress` writes
an expanded file; `--no-opt` disables compiler optimization for inspection.
`ipval` validates a binary, `ipdis` disassembles it and `ipgen` generates
pixels/outputs. The `.ipa` assembly language is the VM-level tool format.

`ipc entry.ips --format canonical.ips` also writes canonical parseable source.
The output uses two-space indentation, explicit expression grouping, word
keywords, normalized literals and named builtin arguments in signature order.
It must differ from the input and binary output. Comments are absent from
the AST and therefore from formatted output. Keep the formatted file beside
its imports when recompiling it. Formatting is idempotent and preserves
executable bytes after stripping source-line debug data.

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
