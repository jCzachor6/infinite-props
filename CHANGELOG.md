# Changelog

## v0.7.1 — 2026-10-15

Product/editor/website version: 0.7.1. Source language 0.7, the frozen
[0.7 specification](spec/iprop-v0.7.md), C API 0.4 and ABI line 0.5 are
unchanged.

- The compiler and assembler default fuel budget rises from 2,000,000 to
  5,000,000. Recompiled assets record the new budget in `meta.fuel`; existing
  binaries keep the budget they were built with. Instruction costs, images,
  outputs and RNG are unchanged.

## v0.7.0 — 2026-10-15

Product/editor/website version: 0.7.0. Source language: 0.7. The binary format
is unversioned and specified by the frozen [0.7 specification](spec/iprop-v0.7.md).
C API: 0.4, on ABI line / SONAME 0.5. Migration:
[from 0.6](docs/language/migration-v0.7.md).

- Exact integer bounds, stable vector normalization and typed asset constants.
- Immutable lists with distinct/sort, captured samplers, sampling palettes,
  weighted/density distributions and deterministic steppers.
- Captured layers, explicit source-over, masks, clipping, morphology, internal
  resource exports and Gaussian blur.
- Unversioned binary format: the former header version bytes are reserved
  zeros, so earlier binaries need recompiling; implicit variant/archetype
  controls removed. C API 0.4 uses ABI 0.5; language-description schema 4
  describes explicit asset inputs; documentation manifest schema 3 drops
  `format_version`.
- Reserved source keywords require renaming before recompilation. See
  [migration from 0.6](docs/language/migration-v0.7.md).
- Compiler: statically known invalid values are reported by the checker in
  source order with `IPS-CONST-003`; pure traps in unreached code stay runtime
  traps; nesting past 32 surface scopes and unstorable slots are located
  errors. Constant folding runs the VM's own operations.
- Runtime: opcode-indexed dispatch, one checked truncated draw for samplers and
  stepper quantiles, and layer captures that no longer inherit clips, so a
  painted layer is clipped exactly once.

## v0.6.0 — 2026-10-08

- Color channel reads, inclusive random ranges, analytic distributions and
  shared-coefficient color/vector gradients; binary format 0.5.
- Nested part instances with names/parents and caller/root-coordinate outputs.
- Extended language exports and Windows tool stack reservation.

## v0.5.0 — 2026-10-08

- Signed defaults, input i32, mixed palettes, declaration semicolons and
  consistent positional/named argument binding and evaluation.
- Full in-memory checking, structured diagnostics and comment-preserving
  atomic source formatting; lexer emoji aliases removed.
- C API 0.3 reflection, checked operations, retained parameters, typed outputs,
  image views and statistics on ABI 0.4.
- Compiler-backed LLM reference, export schemas and shared SDK/web documentation.

## v0.4.0 — 2026-10-05

- Simplified draw syntax, named builtins, numeric/color constructors and
  seed-isolated selection helpers.
- Compiler optimizations, bounded lossless zlib containers and production
  debug stripping, with explicit inspection flags.
- Language handbook, canonical formatter, 24 recipes and release packaging.

## v0.3.0 — 2026-10-02

- Vectors, distance fields, Bézier strokes, procedural noise, color/curve
  utilities, palette ramps and D4 part transforms.
- Typed control-flow validation, numeric boundary fixes and native/WASM parity.

## v0.2.0 — prepared for release, 2026-09-30

- Typed helpers, palettes, loops and pinned trigonometry/math.
- C API, SFML and Godot integrations; relocatable native SDKs and conformance.

## v0.1.0 — 2026-09-29

- Initial binary format, validated VM, seeded primitives, raster operations,
  structured outputs and linked parts.
- Source compiler, inspection/render tools, HTML5/WASM, editor and website.
