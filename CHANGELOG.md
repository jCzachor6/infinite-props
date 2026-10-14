# Changelog

## v0.7.0 — development

- Exact integer bounds, stable vector normalization and typed asset constants.
- Immutable lists with distinct/sort, captured samplers, sampling palettes,
  weighted/density distributions and deterministic steppers.
- Captured layers, explicit source-over, masks, clipping, morphology, internal
  resource exports and Gaussian blur.
- Format 0.7 only; implicit variant/archetype controls removed. C API 0.4 uses
  ABI 0.5; language-description schema 4 describes explicit asset inputs.
- Reserved source keywords require renaming before recompilation. See
  [migration from 0.6](docs/language/migration-v0.7.md).

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
