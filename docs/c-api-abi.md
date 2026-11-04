# C API 0.6 and ABI line 0.5

Product 0.7.0 intentionally removes variant/archetype controls and uses C ABI
line 0.5. The earliest compatible product is 0.7.0. Clients must rebuild with
the current header; the prior ABI 0.4 layout and replacement guarantee do not
apply. No adapter library or compatibility aliases are shipped.

Generation options contain seed, dimensions and ordinary input overrides.
Metadata contains dimensions, origin, fuel and existing emission/reflection
data, without index counts. Removed fields leave no compatibility padding.
Unrelated status/tag values, output ownership and transactional error behavior
retain their existing contracts. Sized descriptors retain prefix/tail canaries
and complete-field reads/writes under their new layouts.

The shared library uses SOVERSION/install identity 0.5. Windows uses the DLL
name `ipropc-0.5.dll`; imported CMake targets resolve the current artifact.
Package version selection rejects product requests before 0.7.0. Runtime
queries report API 0.6 and ABI 0.5 independently; the file format is
unversioned, so the library's format fields are reserved zeros.

Conformance checks the new sizes and every field offset, shared/static clients,
descriptor boundaries, ownership, error paths and relocated SDK packages.
Headers/clients from ABI line 0.4 and its frozen fixtures remain historical
records, not positive compatibility gates for ABI line 0.5. API 0.4 headers
already on ABI 0.5 remain compatible; typed part generation requires an explicit
API 0.5 capability/flag, so older checked callers receive an unsupported error.

API 0.5 adds [typed dynamic-part results](c-api-part-outputs.md), separate sized
descriptors and resource queries. API 0.4 layouts/symbols and the legacy enums
remain frozen. Capability bits advertise parts and resources; unsupported
legacy result representations fail explicitly. This additive feature release
does not change the ABI/install identity or the product release version.

API 0.6 adds independent work limits, monotonic deadlines and cancellation
through `IPROP_CAP_EXECUTION_LIMITS`, `iprop_generate_controlled` and separate
`iprop_execution_limits_ex` / `iprop_work_stats_ex` descriptors. The existing
generation options and fuel statistics keep their layouts. Legacy entries
also use a finite default work budget; requesting a different work limit or
deadline requires the new controlled entry. Existing API 0.4/0.5 clients on
ABI 0.5 remain compatible. No file/program version or fuel tariff changes.
