# Changelog

## v1.0.0 — 2026-11-10

Product/source and editor, website, Unity/Python packages: **1.0.0**.
C API **0.6**, ABI/SONAME **0.5**; binary format remains unversioned.
See [0.9 to 1.0 migration](docs/language/migration-v1.0.md) and the
[frozen contract set](spec/iprop-v1.0.md). Release/tag publication is separate.

### Language 1.0: one spelling and one rule (breaking for sources)

The VM, the binary format and the fuel tariff are unchanged: existing `.iprop`
files run as before, and a migrated source compiles to the bytecode it produced
before. Nothing is aliased; an old spelling is a compile error that names its
replacement. See [0.9 to 1.0 migration](docs/language/migration-v1.0.md).

- **Renamed builtins.** `noise` is `noise.value`, `fault` is `noise.fault`,
  `warp` is `noise.warp`, `fold` and `polar` are `vec2.fold` and `vec2.polar`,
  `fill` is `dist.fill`, `color.set.a` is `color.set_a`, and `stream_push` and
  `stream_pop` are `stream.push` and `stream.pop`. Every builtin is now a bare
  word or one namespace and one member.
- **Renamed named arguments.** `noise.mask(mode)`, `vec2.rot(angle)`,
  `vec2.fold(sectors)`, `dist.fill(distance, width)`, `surface.paint(source)`,
  and the handle of `part.*`, `regions.*`, `diagram.*` and `parts.*` accessors
  is `part`, `regions`, `diagram` and `parts`.
- **Removed duplicates.** `i32trunc(x)` was `int(x)`; `pick(n)` was `index(n)`;
  the variadic `poly` takes its color last, like every other drawing call.
- **Names.** A helper cannot take a builtin's name: before, it was silently
  never called, except for six constructor names where the helper won. Inputs,
  outputs, palettes, constants, helpers and import aliases share one namespace.
  An import alias cannot be a builtin namespace. `seed` and `draw` cannot be
  declared; `at`, `rot`, `mirror_x` and `mirror_y` are reserved for inputs only.
  A parameter cannot take the name of a palette or an output.
- **Operators.** Bitwise operators bind more tightly than comparisons, so
  `flags & 1 == 0` tests the masked value. `a < b < c` and `a == b == c` are
  rejected instead of comparing a 0/1 result.
- **Literals.** `010` was octal 8 and `4294967296` wrapped to 0; both are now
  errors, as are a float literal outside `f32` and a word attached to a number.
- **Declarations.** `size` and `origin` are declared once. A spare `;` is
  accepted between declarations, as it was between statements. A `for` counter
  can take any name a local can.
- **Earlier and clearer errors.** A value-returning helper that can reach its
  end, more than 1024 locals, a call on a value (`v.len()`), a read of an
  output, and a `to` range or distribution constructor whose constant operands
  always trap are reported where they are written. A binary operator's
  diagnostic location no longer depends on the host compiler.
- **No crash on deep nesting.** The compiler overflowed its stack on source
  nested a few thousand levels deep (a few hundred in a sanitizer build), and
  on a chain of a few thousand helpers calling one another. Expressions and
  blocks now nest at most 256 levels, with a located error beyond that; a
  chain such as `a + b + c` counts one level per operator. The limit of 1024
  functions is reported before any helper body is checked.
- The editor grammar highlights every builtin the compiler reports; a test
  keeps the two in step.

### A larger default fuel budget

- The compiler and assembler default fuel budget rises from 5,000,000 to
  25,000,000. Recompiled assets record the new budget in `meta.fuel`; existing
  binaries keep the budget they were built with. Instruction costs, images,
  outputs and RNG are unchanged. The host's independent work guard keeps its
  100,000,000-unit default. A program made mostly of discounted kernels can
  now reach that guard before its fuel: full-canvas four-octave noise masks
  do at about 16,000,000 fuel. A host that runs such programs passes a larger
  work limit.

### Performance

- Faster generation with unchanged images, outputs, fuel and work totals;
  existing `.iprop` files need no recompilation. The interpreter keeps its
  per-instruction accounting and its stack, control and scalar instructions
  inline and reuses call frames; polygons, Bézier strokes, blurs, ovals and
  layer/mask painting do the same pixel tests with less work per pixel.
  On an Apple M-series Mac the showcase weapons and portraits generate
  2.0-2.5x faster, the 256x256 butterfly parts of 0.9.0 3.6-5.4x (hindwing
  58 ms to 11 ms) and thin strokes about 5x. In WebAssembly (Node 26) those
  three butterfly parts take 23 ms instead of 94 ms; `iprop.wasm` is 7% larger
  (5% gzipped).
- The optimizing compiler emits four fewer instructions around every `for`
  iteration and two fewer around every `while` and `!`: a comparison tested
  against zero becomes the opposite comparison (never for ordered float
  comparisons, whose negation is true for NaN), and a `for` body without
  `continue` loses its unused label. Recompiled loop-heavy programs spend
  7-20% less fuel and run 3-12% faster with the same images and outputs.
- The row-wise blur above computed a pointer outside the row for a surface
  narrower than its kernel. It was never read, and images, fuel and work are
  the same, but it was undefined behaviour that UBSan reports; it is gone.

### Platforms

- Macs with Intel processors get an SDK of their own. CI builds and tests on
  an Intel macOS runner beside the Apple silicon one, and a release carries
  `macos-x86_64` next to `macos-arm64`, Linux and Windows: four native SDKs,
  fifteen assets. The Apple package and the Unity plugin were already
  universal, and the Godot extension builds for either processor.

### Website

- The butterfly benchmark and the swarm draw a new butterfly. Every wing is
  shaded scale by scale from its own polar coordinates, with veins in relief,
  an anti-aliased outline traced from its silhouette and soft shadows; the
  body is furred and shaded as bodies of revolution. The thirteen species
  looks now come in twelve wing forms (classic, rounded, hooked, toothed,
  tailed, twin-tailed, hairstreak, narrow, birdwing, notched, ribbon-tailed,
  broad), and wear their natural colours a little under half the time. The three
  programs are 40 KB together; a 256x256 butterfly takes about 68 ms in
  WebAssembly on an Apple M-series Mac (113 ms at most over 1000 seeds, a
  wing spending up to 10 million fuel), a 128x128 one about 23 ms. They are
  written in the 1.0 language and need the larger default budget above.
- The README's butterfly collection and the website's social preview are
  drawn again from the same sixteen seeds, so they show the new butterflies.

## v0.9.0 — 2026-11-05

Product/source and editor, website, Unity/Python packages: **0.9.0**.
C API **0.6**, ABI/SONAME **0.5**; binary format remains unversioned.
See [0.8 to 0.9 migration](docs/language/migration-v0.9.md) and the
[frozen contract set](spec/iprop-v0.9.md). Release/tag publication is separate.

- Configurable scalar noise and direct warped noise masks; bounded Voronoi,
  contours with holes, region offsets, gaps, bevels and rasterization.
- Immutable dynamic parts with shared sources, D4 placement, appearance and
  explicit typed exports in C++, C API and all seven maintained bindings.
  Generated results have no new persisted format.
- Shared fuel discounts and exact disk-morphology optimization; unchanged
  image/RNG semantics and independent work, cancellation and deadline controls.
- Catalog-backed editor hints, checked texture/geometry/export workflows,
  handbook/LLM/host documentation and language-description schema **6**.
  Diagnostic schema **1** and documentation-manifest schema **3** are unchanged.
- The WebAssembly module uses native WebAssembly exceptions instead of
  JavaScript-based ones: the website butterfly generates about 3x faster
  (0.8: ~190 ms, 0.9 before the change: ~330 ms, now ~110 ms in Node 22) and
  `iprop.wasm` is ~17% smaller. Browsers need Chrome 95, Firefox 100 or
  Safari 15.2.


## v0.8.0 — 2026-10-28

Product, editor, website, Unity and Python packages: **0.8.0**. Source
language: **0.8.0**. The unversioned binary format, C API **0.4** and
ABI / SONAME **0.5** remain unchanged. Existing 0.7 binaries do not need
recompilation. See the [0.7 to 0.8 migration guide](docs/language/migration-v0.8.md)
and [release preparation guide](docs/release-v0.8.md).

### Language: materials

- New `material Name { ... }` definitions in material files (files with no
  asset), imported with the ordinary `import` syntax and named by the import
  alias (`forest.Bark`). `base` is the only required field; other fields are
  author-named `color`/`i32`/`f32`/`vec2` constants or ranges and may read
  the fields before them. The file's own `palette`/`const` stay private.
- `m = forest.Bark()` creates an instance, rolling each field once in the
  order written; `forest.Bark(base = #2a1a10)` overrides fields by name.
  Each definition is a distinct helper parameter type (`m: forest.Bark`);
  instances are immutable and cannot be host inputs. See the
  [materials handbook chapter](docs/language/materials.md) and the
  [0.8 materials contract](spec/materials-0.8.md).
- Material replacement preserves the old value while evaluating overrides;
  lowering retains nominal types and read-only bindings, keeps generated
  names private, and preserves the library's builtin resolution. Material
  files support standalone checks and source-only formatting in the CLI
  and editor.

### Authoring and documentation

- `ipc --describe-language` exposes the optional `source_rules.materials`
  capability in schema 4, with matching formal schema and reader validation.
- Frozen 0.8 format/VM and material contracts, a 0.7-to-0.8 migration guide,
  updated grammar, and offline release notes in the generated documentation.
- The complete coding-agent reference includes material libraries and their
  importing assets, with an explicit 128 KiB budget and executable checks.
- Optimization acceptance validates source-only material libraries separately
  from assets while retaining exact rejection, image and output comparisons.
- SDK installation retains the verified, LF-normalized documentation on
  Windows instead of overwriting it with CRLF source files.

### Website

- The home-page forest example uses an imported foliage material to keep
  each tree's base, shadow, light and rim colors together.
- The butterfly benchmark precedes the forest, shows mobile results above
  the animation and restores a fresh preview after a run. Butterflies keep
  flying along their final heading for two more seconds before fading.
- Browsers revalidate stable paths outside Vite's hashed `assets/`, including
  WASM/runtime files, showcase manifests and documentation data.
- The built website includes the 0.8 specification and retains the frozen 0.7
  contract for historical migration links. Release notes remain readable offline.

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
