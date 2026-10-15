# Infinite Props specification

[Format and VM 0.7](iprop-v0.7.md) is the self-contained **development** contract
for this checkout. It covers the file/container layout, all instructions, exact
algorithms, validation and resource limits. The file format itself is
unversioned. Older specifications and releases remain available in Git history.

The [language handbook](../docs/language/index.md) defines `.ips` authoring.
The [0.6 → 0.7 migration](../docs/language/migration-v0.7.md) explains source
and host changes. Product, language, format, C API and ABI versions are independent.

An implementation must reject versions, flags and instructions it cannot honor.
The reference runtime and conformance suite resolve implementation disagreements;
exact kernels and wire encodings must match this document. Release publication
requires a dated changelog and a frozen matching specification, as described in
[CI and publication](../docs/ci.md). The current 0.7 contract remains in development.
