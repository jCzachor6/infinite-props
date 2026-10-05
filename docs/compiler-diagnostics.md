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
first-error JSON scaffold is schema 0; byte ranges, source identity, optional
fix-its and the final protocol/version declarations follow in B.3/B.4.
An I/O failure for the entry has no invented source location. An import load
failure can point at its importing statement because that snapshot exists.
Unexpected loader exceptions and allocation failures are internal errors,
not translated into a misleading missing-import diagnostic.
