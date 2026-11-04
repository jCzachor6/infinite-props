# Export contracts

The development compiler exports source language **0.9.0** with product
**0.9.0**, an unversioned format, C API **0.6** and ABI **0.5**. JSON protocol versions
are independent: language-description schema **6**, diagnostic schema **1**
and documentation-manifest schema **3**.

| Payload | Formal contract | Consumer |
| --- | --- | --- |
| `ipc --describe-language` | [Language description](language-description.schema.json) | Documentation and LLM generators, catalog checks |
| `ipc --check --diagnostics=json` | [Compiler diagnostics](compiler-diagnostics.schema.json) | Editor and executable example checks |
| Generated `docs/language-manifest.json` (schema 3) | [Documentation manifest](documentation-manifest.schema.json) | SDK/archive verification and website rendering |

The schema documents every emitted field. Builtin names, overloads, parameter
binding and effects come from the compiler's shared registries; schemas contain
no builtin signature catalogue. [Descriptive rules](language-description.md)
and [diagnostic semantics](compiler-diagnostics.md) explain their runtime meaning.

Schema 6 retains the geometry/dynamic-part vocabulary and capability objects
introduced during 0.9 development in schema 5. It replaces the closed surface
and mask fuel formulas with the final discounted tariff (including capture
begin/end), and adds execution-wide work/cancellation/deadline trap names.
Update version gates and known enums/strings together; earlier schemas are
rejected. No program header or C API/ABI migration follows from this JSON change.

A schema version fixes known field types and meanings. Consumers accept unknown
additive fields, including additions inside signatures or diagnostics. They
reject missing required fields, malformed known fields and unsupported schema
versions. Changing a known field's type, meaning or enum domain requires a new
schema version, a documented migration and updated executable consumer cases;
changing the source-language or product version alone does not migrate JSON.

The source-rule capabilities describe materials, geometry handles, direct noise
masks, Voronoi, contours, regions, gaps/bevels, dynamic parts and typed exports.
Known optional fields are validated when present. Signatures/effects remain
registry-generated; [descriptive rules](language-description.md) define each
capability and [0.9 migration](language/migration-v0.9.md) explains the boundaries.

Documentation manifest schema 3 replaces schema 2, which also required
`format_version` before the file format became unversioned. It requires four
independent flat strings: `product_version` and `language_version`
(major.minor.patch), plus `c_api_version` and `c_abi_soversion` (major.minor).
Earlier schema-3 bundles may use major.minor for `language_version`.
Current values are `0.9.0`, `0.9.0`, `0.6` and `0.5`. The file format is
unversioned and has no field.
SDK/Web manifests, preflight and CI/collector reports carry the same fields.
