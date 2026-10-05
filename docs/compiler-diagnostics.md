# Compiler diagnostic protocol

The compiler pipeline produces one shared `ips::Diagnostic` structure. Text
and JSON renderers use the same code, stage, severity, message, file and
location. Text keeps the readable caret snippets and existing wording. A code
is assigned by a typed cause or stage boundary; no message matching determines
its identity. Message wording, variable names and localization may change
without changing a code. Generic stage codes cover multiple causes until a
specific typed cause is introduced; clients must not infer a fix from the code
alone. Existing identifiers are never renumbered or reused.

| Stable code | Cause |
| --- | --- |
| `IPS-LEX-001` | Invalid source UTF-8 |
| `IPS-LEX-002` | Other malformed lexical input |
| `IPS-PARSE-001` | Expected token/expression is absent |
| `IPS-PARSE-002` | Other invalid source syntax |
| `IPS-IMPORT-001` | Import cycle |
| `IPS-IO-001` | Cannot load a source |
| `IPS-TYPE-001` | General typing/name/signature/declaration error |
| `IPS-TYPE-002` | Invalid declaration constant grammar |
| `IPS-TYPE-003` | Signed input default outside the mathematical i32 range |
| `IPS-TYPE-004` | Mathematically negative U32 default |
| `IPS-PLAN-001` | General planning/lowering restriction |
| `IPS-LIMIT-001` | Compiler pool/resource limit |
| `IPS-VALIDATE-001` | Compiled executable rejected by the runtime validator |
| `IPS-CLI-001` | Invalid CLI arguments |
| `IPS-INTERNAL-001` | Unexpected internal failure |

Severities are `error`, `warning`, `note`. Stages are `lex`, `parse`, `import`,
`typing`, `planning`, `validation`, `io`, `cli`, `internal`. The current
JSON protocol is [schema 1](compiler-diagnostics.schema.json). It includes
independent product and source-language versions, status and a diagnostics
array. The develop compiler identifies source language 0.5; its current
product version is separate, and binary format 0.4 is unchanged.
An I/O failure for the entry has no invented source location. An import load
failure can point at its importing statement because that snapshot exists.
Unexpected loader exceptions and allocation failures are internal errors,
not translated into a misleading missing-import diagnostic.

## Positions and source identity

`range.start` and `range.end` contain `byte_offset`, `line`, `column`. Offsets
are zero-based UTF-8 bytes; lines and columns are one-based Unicode codepoints,
including tabs as one column. The end is exclusive. A four-byte emoji is one
column and four bytes, including when it normalizes to an ASCII keyword.
CRLF stays in the owning byte snapshot; LF advances the line. EOF may have a
zero-width range. Diagnostic ranges identify the offending token/anchor,
not necessarily the complete enclosing expression. Legacy locations without
token spans fall back to one codepoint; unreachable locations remain null.
Invalid UTF-8 ranges advance over the offending byte without inventing a
Unicode scalar. Clients must handle byte-based spans even for malformed input.

`source_id` is `sha256:` followed by 64 lowercase hex digits over the exact
snapshot bytes, without newline, Unicode or path normalization. An unavailable
snapshot has a null ID and range. `location` retains the legacy line/column
as a convenience; clients should prefer the full range. Convert codepoints
to an editor's UTF-16 units only at the editor boundary.

`expected_type` and `actual_type` are optional strings; `suggestions` is a
string array. `related_locations` contains file/message/range records.
`fix_its` contains range/replacement/source_id records scoped to the primary
file. Apply a fix only to the matching source identity; never apply it by
line number to a changed file. Empty arrays mean no machine-readable
suggestion or edit was supplied; clients must not parse one from the message.

## Bounded recovery

Compile and `--check` collect up to 20 diagnostics across the import graph.
The in-memory API can request a smaller budget (clamped to 1–20). Lexing
stops at its first invalid token. The parser synchronizes at semicolons,
declaration/statement tokens and matching braces; a newline is never an
implicit statement boundary. EOF ends recovery without repeated missing-token
reports. Complete imports can still be discovered from a file with a syntax
error, but that file exposes no executable AST and no typing or generation
runs on any recovered syntax.

On a complete parsed AST, independent defaults and statements are checked.
A local with a failed declaration has no usable type; dependent expressions
and locals are suppressed until a later valid assignment establishes it.
A failed reassignment does not erase a previously known type. Invalid binding
names stop body checking for that asset, and an invalid control condition
skips its subtree conservatively. Lazy helper checking restores the caller's
locals and loop state, including on failure. Shared imported files are checked
once even if they have multiple aliases; each diagnostic owns its file's
snapshot and range. The cap is a hard budget, not an exhaustive error count.

Planning/lowering restrictions and runtime validation of the assembled file
remain part of `--check`; these later phases currently stop at their first
failure. Any error leaves binary bytes empty and prevents artifact writes.

## Output and exit classes

`--diagnostics=json` publishes one complete document on stdout, including
invalid invocations. No colors, progress lines or compiler logs share that
stream; ordinary human diagnostics go to stderr in text mode. The JSON
renderer builds the document before publishing it, so an allocation failure
while escaping cannot leave a partial JSON prefix. A literal fallback reports
an unexpected failure without allocating diagnostic/source objects. Failure
of the operating system's output stream itself cannot guarantee delivery.

| Exit | Status | Meaning |
| --- | --- | --- |
| 0 | `ok` | No errors; warnings/notes alone do not fail a call |
| 1 | `invalid_source` | Invalid source/import or an expected I/O failure |
| 2 | `cli_error` | Invalid invocation, conflicting options or invalid UTF-8 arguments |
| 3 | `internal_error` | Unexpected compiler failure, including allocation failure |

CLI failures have null source locations. Basic error handling does not recover
by interpreting message strings. UTF-8 CLI arguments are validated before
including them in JSON. Fix-its always carry their owning snapshot identity.
For a negative U32 default in signed range, the compiler suggests `input i32`
and supplies an exact edit of the type token; out-of-range defaults do not
receive that edit, because changing the type cannot repair them. The editor
must verify `source_id` before applying the replacement.
