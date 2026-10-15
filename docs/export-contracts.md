# Export contracts

The development compiler exports source language **0.7** with product
**0.7.0**, format **0.7**, C API **0.4** and ABI **0.5**. JSON protocol versions
are independent: language-description schema **4**, diagnostic schema **1**
and documentation-manifest schema **2**.

| Payload | Formal contract | Consumer |
| --- | --- | --- |
| `ipc --describe-language` | [Language description](language-description.schema.json) | Documentation and LLM generators, catalog checks |
| `ipc --check --diagnostics=json` | [Compiler diagnostics](compiler-diagnostics.schema.json) | Editor and executable example checks |
| Generated `docs/language-manifest.json` (schema 3) | [Documentation manifest](documentation-manifest.schema.json) | SDK/archive verification and website rendering |

The schema documents every emitted field. Builtin names, overloads, parameter
binding and effects come from the compiler's shared registries; schemas contain
no builtin signature catalogue. [Descriptive rules](language-description.md)
and [diagnostic semantics](compiler-diagnostics.md) explain their runtime meaning.

A schema version fixes known field types and meanings. Consumers accept unknown
additive fields, including additions inside signatures or diagnostics. They
reject missing required fields, malformed known fields and unsupported schema
versions. Changing a known field's type, meaning or enum domain requires a new
schema version, a documented migration and updated executable consumer cases;
changing the source-language or product version alone does not migrate JSON.

Language-description schema 4 describes explicit asset inputs, without
implicit draw controls. It includes typed constants, parameterized lists,
samplers/steppers, point distributions and surfaces, with signatures and effects
from shared compiler/runtime registries. Older schema versions are rejected.
Diagnostic and documentation-manifest schemas retain their independent versions.

Documentation manifest schema 3 replaces schema 2, which also required
`format_version` before the file format became unversioned. It requires four
independent flat strings: `product_version` (major.minor.patch),
`language_version`, `c_api_version` and `c_abi_soversion` (major.minor).
Current values are `0.7.0`, `0.7`, `0.4` and `0.5`. The file format is
unversioned and has no field.
SDK/Web manifests, preflight and CI/collector reports carry the same fields.
The language export's version pairs and `abi_version` retain their existing
types; `c_abi_soversion` is an optional alias checked against that ABI pair.
Verification compares source constants, compiler exports and every artifact
matrix. Valid checksums cannot override version mismatches.

Current handbooks use the source-language filename `language-v0.7.md`, not
the product version. The emitted, unversioned format is specified by the
frozen [self-contained 0.7 contract](../spec/iprop-v0.7.md), named after
the product line. Product release tags, source language, C API features and the
ABI loader line are never inferred from one another.

Formal schemas check structure. Small protocol readers additionally check
unique names, evaluation-order permutations, source/binary input mappings,
part defaults, effect domains, safe relative manifest paths and ordered ranges.
Bundle readers verify the complete file inventory, content hashes, builtin and
example counts, and optional source/compiler identity. Correct checksums cannot
make a malformed catalog valid. Current bundles carry the three schema files
and the bounded `LLM_REFERENCE.md`, so the SDK and website expose the same
contracts offline.

The editor also checks diagnostic status against process exit, exact source
hashes, UTF-8 boundaries and transactional fix ranges before applying edits.
Offsets are half-open UTF-8 byte ranges; lines and columns are one-based, with
columns counting Unicode codepoints. Protocol integers fit JavaScript's exact
integer range.

Readers accept only these current schemas. A schema 1/2/3 language export or
a schema 1 or 2 documentation manifest is rejected; no reader falls back to an older
shape.

`compiler_export_contracts` checks actual registry exports, every registered
language example, CLI and allocation-failure diagnostics, shared mutation cases
and a malformed rehashed archive. The editor receives the same diagnostic
mutations. CI repeats the check with the pinned JSON Schema implementation from
`scripts/requirements-ci.txt`. Normal documentation builds use only Python's
standard library; they do not install or import the CI schema dependency.
