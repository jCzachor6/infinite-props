# Contributing to Infinite Props

Thanks for your interest in contributing. This project is a portable binary
format and toolchain for seed-based procedural 2D assets. The core values are
**determinism**, **safety** and **portability**: a `.iprop` file must produce
the exact same pixels on every platform.

## Ground rules

1. **The specification is the contract.** Anything that changes the semantics
   of the `.iprop` format or the VM (opcodes, algorithms, section layouts,
   determinism rules) is a spec change first and a code change second. Update
   `spec/` and get agreement before touching behaviour.
2. **Determinism is sacred.** No environment-dependent behaviour: no
   time/threading in generation, no FMA contraction, integer control flow
   only. When in doubt, read `spec/iprop-v0.1.md` §13.
3. **Golden tests are the law.** Any change that alters a golden image or a
   golden structured output is a breaking change unless the spec changed with
   it.
4. **Work in phases.** Development follows the numbered phases in
   [PHASES.md](PHASES.md). Pick a phase, implement it, satisfy its acceptance
   criteria, then move on.

## Workflow

- Branch from `main`, keep commits small and focused on one phase.
- Every commit must build with `-Werror` and pass `ctest`.
- New VM or format behaviour requires tests, preferably pinned vectors or
  golden images.
- Match the formatting of the surrounding code; no formatter config is
  enforced yet.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Style

- C++20, 4-space indentation, `snake_case` for functions/variables,
  `PascalCase` for types, `kUPPER` for constants.
- Keep files under ~500 lines; one concern per file, mirroring the structure
  of the spec sections.
- Comments explain *why*, not *what*; reference spec sections where an
  algorithm is pinned (e.g. `// spec §11.1 alpha rule`).

## Reporting issues

Include: the `.ips` source or `.iprop` file, the exact inputs (seed, variant,
archetype, size, uniforms), the observed and expected outputs, and the
platform. Determinism bugs are the highest priority.

## Code of conduct

Be kind, be constructive, stay on topic. Contributions are licensed under
Apache-2.0 (code) and CC-BY-4.0 (examples and gallery assets).
