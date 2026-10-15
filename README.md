# Infinite Props

A portable binary format and open-source toolchain for **seed-based procedural
2D assets**. Write a recipe in `.ips`, compile it into a self-contained `.iprop`
file, then generate reproducible RGBA images and named geometry from a seed
and declared inputs.

Product **0.7.0** and source language **0.7** are under development. The compiler
and runtime use an **unversioned** file format; C API **0.4** uses ABI / SONAME **0.5**.
See the [language handbook](docs/language/index.md) and
[migration from 0.6](docs/language/migration-v0.7.md).

## Quick start

Build from the repository root with CMake 3.24+, a C++20/C99 toolchain and
zlib 1.2.9+. Python 3.10+ enables generated documentation. Tests fetch Catch2
by default; optional native backends build when their dependencies are available.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
build/tools/ipc examples/language/first-asset.ips -o build/first-asset.iprop
build/tools/ipval build/first-asset.iprop
build/tools/ipgen build/first-asset.iprop --seed 7 -o build/first-asset.png
```

Installed SDKs put the tools in `bin/` and documentation/examples under
`share/iprop/`. Windows tool names add `.exe`. See [building](docs/building.md)
for dependencies, optional integrations, tests and installation.

## A small recipe

```ips
asset stone {
  size 32x32
  input color tint = #8a9299
  output rect collider
  draw {
    w = 18 to 26
    h = 14 to 22
    x = (32.0 - float(w)) * 0.5
    y = 28.0 - float(h)
    oval(16, y + float(h) * 0.5, float(w) * 0.5, float(h) * 0.5, tint)
    collider = {x, y, float(w), float(h)}
  }
}
```

The same file, seed and input values produce the same pixels and ordered output
bits on every conforming backend. Assets declare their own style selections
as inputs. [Samplers](docs/language/samplers.md) capture ranges without drawing;
`.pick()` and `.pick(n)` sample them. [Steppers](docs/language/steppers.md)
produce deterministic batches without consuming RNG.

## Capabilities

- Raster primitives, curves, vectors, distance fields, procedural noise and palettes.
- Typed constants, immutable lists, bounded distributions and explicit sampling.
- Linked nested parts with isolated RNG, exact D4 transforms and named outputs.
- Captured layers, RGBA source-over, coverage masks, clipping, morphology and blur.
- Validated bytecode, fuel budgets and bounded generation resources.
- Compiler, assembler, validator, disassembler, renderer and seed-sheet tools.

## Documentation and integrations

- [Language handbook](docs/language/index.md), [recipes](examples/recipes/README.md)
  and [compact LLM reference generation](docs/llm-reference.md).
- [Format and VM specification](spec/iprop-v0.7.md).
- [C API](backends/capi/README.md), [ABI policy](docs/c-api-abi.md),
  [HTML5/WASM](backends/html5/README.md), [SFML](backends/sfml/README.md)
  and [Godot](backends/godot/README.md).
- [VS Code editor](editor/README.md) and [website build](website/README.md).
- [Export contracts](docs/export-contracts.md), [CI and publication](docs/ci.md)
  and [changelog](CHANGELOG.md).

## License

Code is [Apache-2.0](LICENSE). Open examples and gallery assets are CC-BY-4.0,
except the compiled props in `examples/showcase/` and images rendered from them,
which are all rights reserved. Their license is `examples/showcase/LICENSE.md`
in the source repository. Showcase files are excluded from the native SDK.
