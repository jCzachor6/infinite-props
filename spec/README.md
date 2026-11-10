# Infinite Props specification

[Format and VM 1.0](iprop-v1.0.md) is the frozen normative contract set for
product/source 1.0.0: the retained 0.8 baseline plus the explicit 0.9
extensions, with precedence defined in one place. Product 1.0 adds no
instruction or encoding to them. The binary format is unversioned;
C API 0.6 and ABI/SONAME 0.5 are independent.

The [language handbook](../docs/language/index.md) defines authoring.
See [0.9 to 1.0 migration](../docs/language/migration-v1.0.md),
[shared fuel tariff](fuel-v0.9.md) and [execution limits](execution-limits-v0.9.md).
The frozen [0.9](iprop-v0.9.md), [0.8](iprop-v0.8.md),
[materials](materials-0.8.md) and [0.7](iprop-v0.7.md) contracts remain for
historical comparison.

An implementation must reject reserved fields, flags and instructions it cannot
honor. Exact kernels and wire encodings must match the normative set; conformance
tests resolve implementation disagreements. Release publication separately
requires a dated changelog, matching tag and all release gates described in
[CI and publication](../docs/ci.md).
