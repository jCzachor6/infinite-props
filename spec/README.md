# Infinite Props specification

[Format and VM 0.8](iprop-v0.8.md) is the self-contained, frozen contract for
product 0.8.0. It covers the complete file/container layout, instructions,
numeric/RNG/raster algorithms, validation and resource limits. The format is
unversioned; its encodings and VM behavior are unchanged from 0.7.

[Materials 0.8](materials-0.8.md) is the frozen source-language contract for
the new material definitions, instances, helper parameters and authoring tools.
Materials expand to existing scalar instructions, so they add no VM type or
opcode. Product and source language are 0.8.0; C API 0.4 and ABI 0.5 remain
independent and unchanged. Existing 0.7 binaries stay compatible.

The [language handbook](../docs/language/index.md) defines `.ips` authoring.
See [migration from 0.7 to 0.8](../docs/language/migration-v0.8.md) for the new
capabilities and [migration from 0.6 to 0.7](../docs/language/migration-v0.7.md)
for the earlier binary and source changes. The frozen [0.7 specification](iprop-v0.7.md)
is retained unchanged; older specifications and releases remain in Git history.

An implementation must reject reserved fields, flags and instructions it cannot honor.
The reference runtime and conformance suite resolve implementation disagreements;
exact kernels and wire encodings must match this document. Release publication
requires a dated changelog and a frozen matching specification, as described in
[CI and publication](../docs/ci.md).
