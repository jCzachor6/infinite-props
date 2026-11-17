# Infinite Props specification

[Format and VM 1.1](iprop-v1.1.md) is the frozen normative contract set for
product/source 1.1.0: the retained 0.8 baseline, the explicit 0.9 extensions
and the 1.1 [normal maps](normal-maps-v1.1.md) and
[named seeds](named-seeds-v1.1.md), with precedence defined in one place.
The binary format is unversioned; C API 0.7 and ABI/SONAME 0.5 are independent.

The [language handbook](../docs/language/index.md) defines authoring.
See [1.0 to 1.1 migration](../docs/language/migration-v1.1.md),
[0.9 to 1.0 migration](../docs/language/migration-v1.0.md),
[shared fuel tariff](fuel-v0.9.md) and [execution limits](execution-limits-v0.9.md).
The frozen [1.0](iprop-v1.0.md), [0.9](iprop-v0.9.md), [0.8](iprop-v0.8.md),
[materials](materials-0.8.md) and [0.7](iprop-v0.7.md) contracts remain for
historical comparison.

[Models](models-v1.2.md) specifies the `.iprop3d` format and the extended VM
of 3D models. It is a **preview** outside the frozen set: it may change until
it is frozen, and it changes nothing in a `.iprop` file or in the contracts
above. See [models](../docs/language/models.md) for authoring.

An implementation must reject reserved fields, flags and instructions it cannot
honor. Exact kernels and wire encodings must match the normative set; conformance
tests resolve implementation disagreements. Release publication separately
requires a dated changelog, matching tag and all release gates described in
[CI and publication](../docs/ci.md).
