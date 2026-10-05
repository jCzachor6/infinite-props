# The .ips language

This is the complete authoring reference for the current compiler. `.ips`
programs generate deterministic raster images and structured geometry. The
compiler produces a self-contained `.iprop` file; hosts execute it with a
seed, variant, archetype and optional input uniforms.

The language version follows the product version. Development documentation
describes the checkout being built; a release ships the documentation from
its exact tag. The generated book identifies its version, status and source
commit. Historical contracts are available in the corresponding Git tags.

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
13. [Imports and part composition](composition.md)
14. [Compiler tools, diagnostics and emoji](tooling.md)
15. [Grammar](grammar.md)
16. [Maintaining this documentation](development.md)

The [format and VM specifications](../../spec/README.md) define binary
encoding, exact numerical/raster algorithms, validation and deterministic
execution. The authoring reference links those contracts where relevant.
The [recipe gallery](../../examples/recipes/README.md) contains 24 larger
patterns combining the features described here.
