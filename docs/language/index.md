# The .ips language

This is the complete authoring reference for the current compiler. `.ips`
programs generate deterministic raster images and structured geometry. The
compiler produces a self-contained `.iprop` file; hosts execute it with a
seed and optional declared input uniforms.

This handbook describes source language **0.7.2** and product **0.7.2**.
Product, source, C API and ABI versions are independent, and the binary format
is unversioned: `ipc --describe-language` reports the compiler's contracts.
C API 0.4 uses ABI 0.5.
The generated book identifies its product, development/release status and source
commit. Earlier documentation is available in Git history.

Read the chapters in order to learn the language, or use the function index
in the generated book to look up a builtin. Each chapter links actual `.ips`
files; their contents are included in the generated book and tested by CI.

1. [Your first asset](getting-started.md)
2. [Assets, inputs and outputs](assets.md)
3. [Source syntax and names](syntax.md)
4. [Types, expressions and variables](types.md)
5. [Control flow](control-flow.md)
6. [User functions and argument evaluation](functions.md)
7. [Randomness and seeds](random.md)
8. [Numbers and interpolation](math.md)
9. [Drawing and colors](drawing.md)
10. [Vectors and distance fields](geometry.md)
11. [Noise and procedural utilities](noise.md)
12. [Palettes](palettes.md)
13. [Materials](materials.md)
14. [Immutable lists](collections.md)
15. [Captured samplers](samplers.md)
16. [Deterministic steppers](steppers.md)
17. [Imports and part composition](composition.md)
18. [Captured layers, masks and clipping](surfaces.md)
19. [Writing idiomatic props](idioms.md)
20. [Compiler tools and diagnostics](tooling.md)
21. [Grammar](grammar.md)
22. [Maintaining this documentation](development.md)
23. [Migrating from 0.6 to 0.7](migration-v0.7.md)

The [format and VM specifications](../../spec/README.md) define binary
encoding, exact numerical/raster algorithms, validation and deterministic
execution. The authoring reference links those contracts where relevant.
The [recipe gallery](../../examples/recipes/README.md) contains 24 larger
patterns combining the features described here.
