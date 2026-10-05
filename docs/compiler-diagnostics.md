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
The later recovery/editor tasks add more causes and consumers without changing
the position contract. Distinct CLI/source/internal exit classes are finalized
in B.4.
