# Export contracts

The current compiler exports source language **0.5** while the product remains
**0.4.0**. JSON protocol versions are independent of product, source language,
binary format, C API and runtime ABI versions. The language description uses
schema **2**, the diagnostic protocol schema **1** and generated documentation
manifests schema **2**:

| Payload | Formal contract | Consumer |
| --- | --- | --- |
| `ipc --describe-language` | [Language description](language-description.schema.json) | Documentation and LLM generators, catalog checks |
| `ipc --check --diagnostics=json` | [Compiler diagnostics](compiler-diagnostics.schema.json) | Editor and executable example checks |
| Generated `docs/language-manifest.json` (schema 2) | [Documentation manifest](documentation-manifest.schema.json) | SDK/archive verification and website rendering |

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

Language description schema 2 removes the `emoji_aliases` list that schema 1
required, because source language 0.5 no longer has lexer emoji aliases. All
other fields keep their schema 1 types and meanings. The `historical-0.4`
reader still accepts frozen schema 1 exports with that list.

Documentation manifest schema 2 replaces schema 1 for current bundles. It
requires five independent flat strings: `product_version` (major.minor.patch),
`language_version`, `format_version`, `c_api_version` and `c_abi_soversion`
(major.minor). Current values are `0.4.0`, `0.5`, `0.4`, `0.3` and `0.4`.
SDK/Web manifests, preflight and CI/collector reports carry the same fields.
The language export's version pairs and `abi_version` retain their existing
types; `c_abi_soversion` is an optional alias checked against that ABI pair.
Verification compares source constants, compiler exports and every artifact
matrix. Valid checksums cannot override version mismatches.

Current handbooks use the source-language filename `language-v0.5.md`, even
while the product remains `0.4.0`. The format specification remains
`spec/iprop-v0.4.md`. Product release tags, source language, emitted format,
C API features and the ABI loader line are never inferred from one another.

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
integer range. The editor's explicit unsupported-option response permits the
frozen compiler's text protocol; malformed current JSON does not trigger that
compatibility path.

Only the frozen v0.4.0 exports in `tests/fixtures` and explicitly authored
historical test fixtures may select the `historical-0.4` catalog reader. That profile is selected by the
caller, never by absent fields, product version or a failed current validation.
The language schema exposes its frozen shape in `$defs.historical04`; the
default root remains the complete current contract. Publication, SDK, website
and ordinary documentation commands always use the current profile.
The explicit historical documentation reader accepts actual legacy schema 1
without inventing missing version fields; it keeps the frozen `language-v0.4.md`
filename. Current readers reject legacy manifests and require schema 2.

`compiler_export_contracts` checks actual registry exports, every registered
language example, CLI and allocation-failure diagnostics, shared mutation cases
and a malformed rehashed archive. The editor receives the same diagnostic
mutations. CI repeats the check with the pinned JSON Schema implementation from
`scripts/requirements-ci.txt`. Normal documentation builds use only Python's
standard library; they do not install or import the CI schema dependency.
