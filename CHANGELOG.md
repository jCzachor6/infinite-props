# Changelog

## Unreleased

Target: **0.8** (breaking changes allowed; see the migration note at release).

### Language: materials

- New `material Name { ... }` definitions in material files (files with no
  asset), imported with the ordinary `import` syntax. `base` is required;
  `edge` defaults to `base`, `taper` to `1.0` and `edge_width` to `0`. Other
  fields are author-named `color`/`i32`/`f32`/`vec2` constants or ranges.
- `m = Name()` creates an instance, rolling each field once in definition
  order. Each definition is a distinct helper parameter type (`m: Name`);
  instances are immutable and cannot be host inputs. See the
  [materials handbook chapter](docs/language/materials.md) and the
  [0.8 materials contract](spec/materials-0.8.md).

## v0.7.2 — 2026-10-25

Product, editor, website and native binding packages: **0.7.2**. Source language:
**0.7.2**, including the vector overloads below. C API **0.4**, ABI / SONAME
**0.5** and the unversioned format remain unchanged. Language exports and
JSON diagnostics now include the source-version patch component.

### Language: vector overloads

Added **20 `vec2` overloads across 16 existing functions**. The complete list of
new signatures follows; all existing scalar and color overloads remain available.

| Area | Added overload |
| --- | --- |
| Drawing | `pixel(p: vec2, color: color) -> void` |
| Drawing | `rect(position: vec2, size: vec2, color: color) -> void` |
| Drawing | `rect(position: vec2, w: f32, h: f32, color: color) -> void` |
| Drawing | `rect(x: f32, y: f32, size: vec2, color: color) -> void` |
| Drawing | `line(a: vec2, b: vec2, thickness: i32, color: color) -> void` |
| Drawing | `oval(center: vec2, radii: vec2, color: color) -> void` |
| Drawing | `oval(center: vec2, rx: f32, ry: f32, color: color) -> void` |
| Drawing | `oval(x: f32, y: f32, radii: vec2, color: color) -> void` |
| Noise and procedural utilities | `noise(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `fault(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.grad(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.fbm(p: vec2, octaves: i32, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.cell(p: vec2, seed: i32) -> f32` |
| Noise and procedural utilities | `noise.ridge(p: vec2, octaves: i32, seed: i32) -> f32` |
| Noise and procedural utilities | `warp(p: vec2, amount: f32, seed: i32) -> f32` |
| Noise and procedural utilities | `cell.jitter(cell: vec2, seed: i32) -> vec2` |
| Noise and procedural utilities | `dither(t: f32, p: vec2) -> f32` |
| Masks | `mask.offset(source: mask, offset: vec2) -> mask` |
| Math | `lerp(a: vec2, b: vec2, t: f32) -> vec2` |
| Math | `atan2(v: vec2) -> f32` |

Arguments evaluate once in signature order, including named and mixed calls.
`pixel`, `cell.jitter`, `dither` and `mask.offset` convert vector coordinates
exactly as `int()`: truncate toward zero, saturate to i32 and map NaN to zero.
`atan2(v)` is `atan2(v.y, v.x)`; vector `lerp` applies the existing clamped
scalar interpolation independently to x and y. Existing pixel results, C API
and binary format are unchanged.

### Native integrations

- Every backend consumes the shared Native/WASM pixel corpus, comparing all
  RGBA bytes and structured output bits. Unity Editor Gamma/Linear GPU checks
  remain local; standalone C# binding checks run in CI.
- Android `toBitmap()` preserves raw unpremultiplied RGBA. Use the explicit
  `toPremultipliedBitmap()` helper for Android Canvas/View drawing.
- Apple Swift package and multiplatform XCFramework builder, with CGImage,
  SpriteKit and Metal adapters independent of game code.
- Unity UPM package with native C# bindings, `.iprop` import previews,
  Texture2D/Sprite helpers and per-platform plugin builds.
- Android Kotlin/JNI AAR with a Bitmap adapter and host/device conformance.
- Python package with typed inputs, exact output bits, Pillow export and CLI.
- Shared native binding contracts, SDK source-package installation and
  runtime-only CMake builds for mobile and plugin cross-compilation.
- Strict zlib decoding through baseline inflate APIs for Android API 23
  compatibility, preserving decoded-size, checksum and trailing-byte checks.

### Website

- Asset Store links point to [iprop.store](https://iprop.store/) and show that
  the store is available.
- All seven pages ship unique descriptions, canonical URLs, Open Graph and
  X/Twitter cards, and structured data in static HTML. The build generates
  `robots.txt` and the sitemap from its page inventory.
- A shared link-preview image features the generated butterfly collection.

### Documentation

- Redesigned README with centered branding, a 4×4 collector display of the
  website's generated butterflies, and clearer integration and documentation
  navigation. The installed SDK includes its preview assets and license notice.

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
