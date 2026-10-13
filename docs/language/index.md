# The .ips language

This is the complete authoring reference for the current compiler. `.ips`
programs generate deterministic raster images and structured geometry. The
compiler produces a self-contained `.iprop` file; hosts execute it with a
seed, variant, archetype and optional input uniforms.

This checkout implements stages 1–7 of source language **0.7**, in development
with product 0.7.0. Product, source language, binary format, C API and ABI versions are
independent: `ipc --describe-language` reports the versions of the compiler
being used. The compiler emits the oldest required format, from 0.4 to 0.6;
the ABI remains 0.4. The new implemented features are integer bounds overloads
and vector normalization, typed immutable asset constants, and lists with
helper transfer and polygon drawing, and captured samplers with scalar/batch picks; later 0.7
stages are not yet available. Weighted literals now provide discrete masses
with exact integer keys and one bounded draw. Density profiles add continuous
piecewise linear shapes with clipped integration and a stable inverse. Const samplers and sampling palettes now share
the same explicit picks.
Development documentation describes the checkout being built; a release ships
documentation from its exact tag. The generated book identifies its product
version, status and source commit. Historical 0.4 contracts and rejected
examples remain frozen in their baseline and Git tag.

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
13. [Immutable lists](collections.md)
14. [Captured samplers](samplers.md)
15. [Imports and part composition](composition.md)
16. [Writing idiomatic props](idioms.md)
17. [Compiler tools and diagnostics](tooling.md)
18. [Grammar](grammar.md)
19. [Maintaining this documentation](development.md)
20. [Migration and implementation contracts for 0.5](migration-v0.5.md)
21. [Migration and implementation contracts for 0.6](migration-v0.6.md)
22. [Migration and implementation contracts for 0.7](migration-v0.7.md)

The [format and VM specifications](../../spec/README.md) define binary
encoding, exact numerical/raster algorithms, validation and deterministic
execution. The authoring reference links those contracts where relevant.
The [recipe gallery](../../examples/recipes/README.md) contains 24 larger
patterns combining the features described here.
