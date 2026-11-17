# C API 0.8 and ABI line 0.5

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
queries report API 0.8 and ABI 0.5 independently; the file format is
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
ABI 0.5 remain compatible. These API additions leave file/program encodings unchanged.
The runtime uses the [shared fuel tariff](../spec/fuel-v0.9.md).

API 0.7 adds [auxiliary images](c-api-aux-images.md) beside the main image:
`IPROP_CAP_AUX_IMAGES` advertises the program and result queries
(`iprop_program_aux_image_*`, `iprop_result_aux_image_*`) with their separate
sized descriptors, and `IPROP_CAP_DERIVED_NORMAL` advertises
`iprop_generate_derived_normal` with `iprop_derived_normal_ex`. The first image
kind is the normal map; `IPROP_IMAGE_ALPHA_OPAQUE` is a new value of the
existing alpha tag and is reported only for auxiliary images. No existing
structure, enum value or entry point changes, so API 0.4–0.6 clients on ABI 0.5
remain compatible and see the same results. Programs that declare a normal
output use the [normal map extension](../spec/normal-maps-v1.1.md) of the
unversioned file format; a runtime without it rejects them at load.

API 0.8 adds the [material maps](c-api-aux-images.md#reading-a-material-map):
`IPROP_CAP_MATERIAL_MAPS` advertises image kinds 2–5 (roughness, metallic, AO
and emission) in the queries of API 0.7. No structure, entry point or layout
changes; a client built for 0.7 sees new kind values and may skip them.
Programs that declare a material map use the
[material map extension](../spec/material-maps-v1.2.md); a runtime without it
rejects them at load.
