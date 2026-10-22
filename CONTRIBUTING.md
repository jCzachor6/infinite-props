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
   only. When in doubt, read `spec/iprop-v0.2.md` §13 (0.1 remains frozen).
3. **Golden tests are the law.** Any change that alters a golden image or a
   golden structured output is a breaking change unless the spec changed with
   it.
4. **Work in phases.** Development follows the numbered phases in
   the CHANGELOG and the commit history. Pick a task, implement it, satisfy
   its acceptance
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

## Test layout

The larger Catch2 suites are grouped in `tests/compiler/`, `tests/language/`,
`tests/math/` and `tests/validation/`. Shared builders, fixtures and assertion
helpers live in `tests/support/`; backend conformance suites live beside their
backend. Register each new test source explicitly in the matching
`CMakeLists.txt` so it is compiled and run by CTest.

## Language documentation

The [language handbook](docs/language/index.md) is the shared Markdown source
for native SDKs, WASM packages and the website. Edit its chapters rather than
generated HTML or downloads. Add every chapter to `book.json` and the overview;
describe each builtin exactly once and register real examples in
`examples/language/cases.json`. The compiler supplies signatures and lexer
aliases through `ipc --describe-language`.

With Python 3.10 or newer, a full native build regenerates the versioned book
and CTest checks links, builtin coverage, successful generations and expected
compile errors/traps. `npm --prefix website run build` generates the language
page and search index from the same book; `npm --prefix website run test:docs`
checks rendering and link failures. Packaging needs Python 3.12 or newer and
validates the documentation inside the relocated and extracted archives.

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

Be kind, be constructive, stay on topic.

## Licensing of contributions

A contribution is licensed under the license of the directory it lands in
(see [LICENSE](LICENSE)): MPL-2.0 for the runtime, FSL-1.1-ALv2 for the
toolchain and CC-BY-4.0 for documentation and open examples. Sign off every
commit (`git commit -s`) to certify the
[Developer Certificate of Origin](https://developercertificate.org/), and
grant the maintainer the right to relicense your contribution together with
the project.
