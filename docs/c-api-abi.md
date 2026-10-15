# C API 0.4 and ABI line 0.5

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
queries report API 0.4 and ABI 0.5 independently; the file format is
unversioned, so the library's format fields are reserved zeros.

Conformance checks the new sizes and every field offset, shared/static clients,
descriptor boundaries, ownership, error paths and relocated SDK packages.
Older headers/clients and frozen 0.4 fixtures remain historical records, not
positive compatibility gates for the new library.
