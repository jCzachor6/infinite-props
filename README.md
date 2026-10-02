# Infinite Props (`infinite-props`)

A portable binary format and open-source toolchain for **seed-based procedural
2D assets**: one small file encodes the complete recipe for an asset, and a
runtime executes it to produce pixels — deterministically, on any platform.

One `.iprop` file is not one image. It is a recipe for **infinitely many
variants**: every seed, variant and archetype index produces a different,
reproducible result.

## 1. Vision

Today, procedural assets are engine-bound source code. In QuantSpace, the
"recipe" for a rock is the C++ code of `PaRock::draw(...)` compiled into the
game, and its gameplay metadata (collider, origin, emission) travels alongside
the pixels in separate structures.

Infinite Props turns that recipe into a **portable, self-contained, binary
artifact**:

```
source (.ips) ── compiler ──▶ asset.iprop ── interpreter/backend ──▶ pixels
```

The `.iprop` file contains binary instructions executed by a small virtual
machine. The same file runs in the browser, in an SFML game, and through
OpenGL or Vulkan hosts, and produces the same pixels everywhere.

## 2. Goals

- Define a versioned binary format (`.iprop`) for procedural 2D sprite assets.
- Provide a human-writable language (`.ips`) that compiles to that format.
- Provide a reference CPU interpreter written in C++20.
- Provide a stable C API as the shared foundation every native host links.
- Provide backends for HTML5 (WASM), SFML, Godot (GDExtension), OpenGL and
  Vulkan.
- Provide a web-based pseudo-IDE for authoring and inspecting assets.
- Provide a website with a playground, documentation, installation guides and a
  gallery.
- Guarantee bit-comparable, cross-platform deterministic output for a given
  (program, seed) pair.
- Guarantee safe execution: a `.iprop` file MUST be executable in a browser
  without risk to the host.

## 3. Non-goals (v1)

- 3D geometry, meshes or SDF output. The format targets 2D raster sprites only.
- Vector path export (may be revisited after v1).
- Runtime loading of parts from separate files ("bundles"). Parts are linked
  at compile time; runtime bundles are a possible post-v1 extension.
- Executing arbitrary programs on the GPU. GPU backends are execution hosts in
  v1 (see §8).
- Integration with a specific game engine. Engine adapters are consumers, not
  part of the core.

## 4. Requirements terminology

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD",
"SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be
interpreted as described in RFC 2119.

## 5. The `.iprop` binary format

The format is a contract, not an implementation detail. All interpreters MUST
implement the semantics defined by the specification; the reference
interpreter is authoritative where tests disagree.

### 5.1 Header

- Magic bytes: `IPRP` (4 bytes) followed by a format version.
- A flags field for optional feature sections.
- Versioning MUST follow semantic rules: existing semantics MUST NOT change
  across versions; new capabilities are added as new sections or opcodes with
  an associated feature flag. Old interpreters MUST reject files they cannot
  fully honour (never silently degrade).

### 5.2 Sections

| Section   | Required | Contents |
| --------- | -------- | -------- |
| `meta`    | yes      | Canvas size, origin/anchor, collider, emission (offset, radius, colour), variant count, archetype count, fuel budget, declared input/output signature |
| `const`   | yes      | Constant pool: integers, floats, colours, strings |
| `code`    | yes      | Instruction stream and function table, including `draw(variant, archetype)` |
| `debug`   | no       | Symbol names and a source map (`.ips` line → instruction offset); strippable without changing behaviour |

### 5.3 Metadata

The file MUST carry the asset's gameplay/rendering metadata inline — the
equivalent of QuantSpace's `PropPrototype`:

- `size` — canvas dimensions (pixels).
- `origin` / `anchor` — the ground/anchor point used for placement.
- `collider` — the footprint used for collision.
- `emission` — emissive offset, radius and colour (e.g. crystals glow).
- `variants`, `archetypes` — counts exposed to the interpreter.
- `fuel` — the maximum instruction budget for one generation (see §6).

A `.iprop` file MUST be fully self-contained: no external resources, code or
configuration are needed to execute it. Imported parts (see §7.3) are linked
into the file at compile time, so self-containment holds for composed assets
too.

### 5.4 Input/output model

The format declares an explicit, typed I/O signature in the `meta` section.
Generation is a pure function of its inputs: identical inputs MUST produce
identical outputs — both the image and every structured output.

**Inputs**

| Input | Type | Notes |
| ----- | ---- | ----- |
| `seed` | u32 | always required; the root of every RNG stream |
| `variant` | u32 | validated against `meta.variants` |
| `archetype` | u32 | validated against `meta.archetypes` |
| `size` | u32×u32 | requested output resolution (resolution independence) |
| uniforms | declared | typed parameters (`color`, `f32`, `u32`) with default values; the host supplies values such as biome palettes |

There are deliberately **no time or animation inputs**: a `.iprop` file is a
pure function of a seed. Animation remains a host concern (for example,
advancing a derived seed per frame).

**Outputs**

- `image` — the RGBA canvas; implicit and always present.
- Structured outputs — named slots declared in `meta` and written by
  `draw(...)`:
  - `point` (x, y) — named anchors and attachment points: entrances,
    chimneys, sockets, or the eyes and mouth of a generated portrait.
  - `rect` (x, y, w, h) — collision blocks, such as a computed rock or
    crater collider.
  - `circle` (x, y, r) — round collision shapes.
  - A slot may be singular or a list (`rect colliders[]`).
- The **name is the contract**: output semantics are defined per asset kind
  (`collider`, `bowl`, `eyes`, `hatAnchor`, ...), not by the format.

**Host API** (identical on every backend):

```
result = generate(file, { seed, variant, archetype, size, uniforms })
        → { image, outputs: { collider, anchors, ... } }
```

Structured outputs are part of the determinism contract and of the
conformance suite (see §9).

In composed assets, a parent MAY read the structured outputs of its parts and
MAY assign them to its own declared outputs, so a child's computed anchor can
surface in the final `outputs` map (see §7.3).

### 5.5 Deterministic layout

- Endianness and instruction encoding MUST be pinned (little-endian,
  fixed-width instructions in v1).
- The constant pool and instruction stream MUST be byte-for-byte
  deterministic: compiling the same source twice MUST produce the identical
  file.

## 6. The virtual machine

The format's execution model is a **full imperative stack machine**
(Turing-complete, inspired by WebAssembly's design), with hard guarantees that
make it safe and deterministic.

### 6.1 Structure

- Value stack, locals, and a call stack of functions.
- **Structured control flow only**: `block`, `loop`, `if/else`, `br`, `br_if`.
  Arbitrary jumps MUST NOT exist; this keeps validation and analysis simple.
- A fuel counter: every instruction decrements it; exhausting the budget MUST
  abort generation with a defined error. Interpreters MUST enforce the budget
  declared in `meta.fuel`. This guarantees termination for every file.

### 6.2 Types

- `i32`, `u32`, `f32` (IEEE-754 binary32), `color` (8-bit RGBA), and string
  references into the constant pool.
- Control-flow conditions operate on integers ONLY. Floats are used
  exclusively for geometry and colour computation; no branch may depend on a
  float value. This rule is the foundation of cross-platform determinism.

### 6.3 Memory model (v1)

- The only mutable state is the canvas, the declared output slots, plus the
  stack and locals.
- There MUST NOT be arbitrary linear-memory access in v1. Every canvas write
  is bounds-checked; out-of-bounds writes are no-ops or defined errors.

### 6.4 Canvas operations

The VM exposes pixel painting operations equivalent to QuantSpace's `PaCanvas`
primitives, with semantics pinned in the specification:

- `pixel`, `rect`, `poly`, `line`, `oval` — raster primitives.
- The **alpha compositing rule** MUST be specified exactly (including the
  deliberate preservation of the destination alpha when compositing
  translucent colours, as in `PaCanvas::pixel`).
- `weather` — seeded per-pixel wear, preserving alpha, silhouette and bright
  emissive pixels.

### 6.5 Seeded primitives

The RNG and noise are **specification-pinned instructions**, not library
calls, because bit-exact parity across platforms is a core requirement:

- `hash` — splitmix32-style integer hash (as in `PaGenerationMath::hash`).
- `unit`, `range`, `index` — draws from a Weyl-sequence stream
  (`state += 0x9e3779b9`, hashed, scaled to `[0,1)`).
- `noise` — smoothstep-interpolated value noise over a pinned lattice hash.
- `fault` — triangular-interpolated noise for sharp geological features.
- `color.mix`, `color.shade`, `smooth` — colour/value helpers.
- `lerp(a, b, t)` interpolates either two colors or two numbers; integer
  numbers widen to f32. Color interpolation preserves the first alpha.
- `int(x)` truncates f32 toward zero with saturation (NaN becomes zero),
  and preserves i32 values. `float(x)` widens i32 or preserves f32 values.
- `rgb(r, g, b)` / `rgba(r, g, b, a)` construct colors from integer channels
  clamped to 0–255; `rgb` uses alpha 255. Arguments run once, left to right.
- Stream isolation MUST be expressible: programs may derive independent
  streams from `seed ^ constant ^ (kind · golden-ratio)` without moving a
  shared stream (the QuantSpace invariant).

### 6.6 Determinism requirements

- Integer arithmetic is exact everywhere.
- All float operations MUST be IEEE-754 binary32 with rounding pinned; the
  specification MUST list which functions are used (no libm calls beyond the
  listed set, no environment-dependent behaviour such as rounding modes or
  fast-math).
- Generation MUST NOT depend on time, threads, hardware or execution order.
- The reference interpreter is the arbiter; a conformance suite (see §9)
  compares outputs across backends.

## 7. The language (`.ips`)

A small imperative, human-writable language that mirrors the structure of
QuantSpace's draw code, and compiles 1:1 to VM semantics.

### 7.1 Requirements

- Text format with the `.ips` extension.
- An `asset` block declares the metadata from §5.3.
- Functions, locals, arithmetic, conditionals and bounded loops (`while`
  with `break`/`continue`, and `for i in a..b` sugar — both lowered to the
  VM's `br`).
- Built-in functions for every VM primitive in §6.4–6.5 (no floating-point
  conditions), including the pinned `sin`/`cos`, the math package
  (`abs`, `floor`, `ceil`, `min`, `max`, `pow`, `exp`, `atan2`) and the
  signed-distance fields (`dist.circle`/`dist.rect`/`dist.roundrect`/
  `dist.line`/`dist.bezier` plus `fill`). `dist.bezier(p, a, b, c)` is
  quadratic; an additional control point selects the cubic form.
  `curve(a, b, c [, d], thickness, colour)` draws either form with round
  caps; thickness is an `f32` diameter in target pixels. The noise v2 adds
  `noise.grad`/`noise.fbm`/`noise.cell`/`noise.ridge`, `warp` and
  `cell.jitter`. The curves add `gain(t, k)` and `quintic(t)`, and
  `ramp(palette, t)` interpolates a palette continuously (a compiler
  desugar over the colour/f32 lerps). The colours add `color.set.a`,
  `color.mul`, `color.scale` and `color.hsv` (the hue in turns;
  nonfinite hue shifts trap, [§12.12](spec/iprop-v0.3.md#1212-the-colours-v03)).
  The dither/fold/polar add the ordered Bayer threshold, the kaleidoscope
  symmetry and the cartesian-polar conversion. The curve toolkit adds
  `smoothstep(a, b, t)`, `remap(t, a, b, c, d)` and `clamp(t, a, b)`.
  Parts accept the D4 transforms (`mirror_x`, `mirror_y`, `rot` on the
  `part.draw` call — the exact pixel permutations, the outputs
  propagate, including writes from helper functions). Transform arguments
  are constant integers: mirrors are `0`/`1` (or `false`/`true`), and `rot`
  is `0`, `90`, `180` or `270`; rotation is applied before the mirrors.
  Each transform argument may occur once. These three names are reserved
  for part calls and cannot be declared as input uniforms. The `.ipa`
  syntax is `part.draw name [kind]`, with the optional D4 kind in `0`–`7`;
  disassembly preserves it.
  `gain` defines the `k = 1` limit;
  `ramp` takes positional arguments and evaluates `t` once even for a
  one-element palette ([§12.11](spec/iprop-v0.3.md#1211-the-curves-and-the-ramps-v03)).
  Pure constant ramps fold during planning; dynamic palettes use O(log N)
  selection with O(N) emitted code. Compiler expansion can reduce fuel,
  while each VM instruction keeps its specified cost.
  Sampling outside the noise primitives' defined coordinate domains traps;
  the bounds, octave checks and half-open jitter placement are specified in
  [§12.10](spec/iprop-v0.3.md#1210-the-noise-v2-v03).
- Asset-level constant arrays (`palette name = [ e, e, … ]`) with integer
  indexing; a bare palette name must be indexed (the default is the last
  element).
- Deterministic, simple grammar; unambiguous parse errors; semantic checks
  (type, fuel, canvas) reported with source locations.
- A `draw { ... }` entry block with read-only `v`/`a` builtins; the compiler
  synthesizes the runtime `(i32, i32)` parameters. Variant/archetype indices arrive as
  validated integers.
- Function parameters declare their types (`ageFromUnit(u: f32)` — `i32`,
  `f32`, `color` or `vec2`; pre-1.0 the type is mandatory) and functions that
  return a value declare an explicit return type (`-> i32`).
- `vec2.len(v)`, `vec2.dot(a,b)` and `vec2.rot(v,angle)` expose vector math.
  Positive angles in `vec2.rot`, `polar` and part rotations are counterclockwise
  in the downward-y asset plane.
- `vec2` values use `{x, y}` literals and `.x`/`.y` components, including
  points read from parts. Vectors support `+`, `-`, `*` and `/` with another
  vector, `*` and `/` with a scalar on the right, and scalar `*` vector.
  Operands are evaluated left to right, including RNG calls.
- `input`/`output` declarations define the asset's I/O signature: uniforms,
  and the named points, rects and circles written by `draw` (§5.4).
- `import` declarations for composition: parts linked at compile time,
  acyclic, with part-output access and propagation (§7.3).

Calls retain the compiler's established evaluation order: ordinary builtin
and user-function arguments run left to right; `poly` evaluates vertex
coordinates before its colour. Part calls evaluate uniform overrides in
input declaration order, then seed/variant/archetype and placement; named
transform arguments are compile-time constants. An argument is evaluated
once, including when its result is discarded by a singleton `ramp`.

### 7.2 Sketch

```
asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect  collider

  draw {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    stone_mass(-w * 0.10, 0, w * 0.87, h, palette, seed)
    if (index(3) == 0) {
      stone_mass(-w * 0.67, 2, w * 0.39, h * 0.43,
                 color.shade(palette, -4), hash(seed ^ 9467))
    }
  }
}

// hair.ips — a part authored in its own file
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  output point top

  draw {
    ...
    top = {32, 4}
  }
}

// portrait.ips — imports parts; call order is paint order
import hair from "hair.ips"
import eyes from "eyes.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  input  color hairColor = #33291f
  output point pupilCenter

  draw {
    face(v, a)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    eyes.draw(seed ^ 0x517cc1b7, v, a, at = {32, 24})
    pupilCenter = eyes.outputs.pupil   // a child output surfaces in the parent
  }
}
```

This sketch is illustrative; the final grammar lives in `spec/`.

### 7.3 Imports and composition

An asset MAY be composed of smaller assets ("parts"), each authored in its own
`.ips` file. This mirrors how a QuantSpace portrait calls dedicated hair and
eye generators instead of drawing everything inline.

- **Compile-time linking.** `import name from "file.ips"` brings a part into
  the program. The compiler merges all imported parts into a single `.iprop`;
  no imports exist at runtime, preserving self-containment (§5.3).
- **Acyclic.** Imports form a DAG; the compiler MUST reject cycles.
- **Calling a part.** The parent calls `part.draw(partSeed, variant,
  archetype, at = {x, y}, uniforms...)`. The part renders into its own
  sub-canvas, which is then blitted onto the parent canvas at the given point
  using the pinned alpha rule. **Call order is paint order**, so layer
  stacking is fully deterministic.
- **Derived seeds.** Each part receives its own stream (e.g.
  `seed ^ constant-of-part`); changing one part's stream MUST NOT move the
  draws of other parts — the QuantSpace stream-isolation invariant.
- **Reading part outputs.** After the call, the parent MAY read the part's
  structured outputs as `part.outputs.name`; names and types are checked at
  compile time.
- **Propagation.** The parent MAY assign a part's output to one of its own
  declared outputs, so a child's computed anchor surfaces in the final
  `outputs` map of the composed asset.
- **Determinism.** Part outputs are a pure function of the seed passed by the
  parent; the conformance suite compares the merged program as a whole —
  pixels and every structured output (§9).

## 8. Backends (interpreters)

All backends execute the same validated bytecode. The reference CPU
interpreter is authoritative for correctness.

| Backend | Technology | Role in v1 |
| ------- | ---------- | ---------- |
| reference | C++20 | Authoritative CPU interpreter; conformance arbiter |
| capi | C99 + C++20 | The stable C API (`ipropc`); the foundation for every native host |
| html5 | Emscripten/WASM | Browser execution; powers playground and IDE preview |
| sfml | C++20 + SFML 3.1 | Native desktop host; `sf::Image` output |
| godot | GDExtension over the C API | Engine integration: `IpropAsset`/`IpropSprite2D` |
| opengl | C++20 + OpenGL | Execution host: CPU raster → texture upload |
| vulkan | C++20 + Vulkan | Execution host: CPU raster → texture upload |

- GPU backends in v1 are **hosts**: arbitrary imperative bytecode is not
  lowered to shaders. Generation runs on the CPU interpreter; the GPU presents
  the resulting atlas.
- OPTIONAL future work: detect a restricted "pure per-pixel" subset and lower
  it to GLSL/SPIR-V for hot paths. The reference interpreter remains
  authoritative.
- Backends SHOULD support rendering at a requested size where the program
  permits (resolution independence).

## 9. Conformance suite

- Golden PNGs for a corpus of `(program, seed)` pairs, generated by the
  reference interpreter.
- Every backend MUST reproduce each golden image within a pinned tolerance.
- Structured outputs (computed colliders, anchor points) are compared exactly
  as part of the same corpus.
- The suite runs in CI on every change; a compiler or interpreter change that
  alters golden output is a breaking change.
- The corpus MUST include ports of QuantSpace procedural assets (e.g. `PaRock`
  archetypes, `PaCrystal`) as proof of parity with the reference pipeline.

## 10. Compiler toolchain

- Pipeline: `parse → validate → lower → emit`.
- Emits `.iprop` with the OPTIONAL `debug` section (source map, symbols).
- Ships an assembler/disassembler and a standalone validator.
- Compiler output MUST be byte-identical for identical input.

## 11. Pseudo-IDE

A web-based authoring workspace, consuming the WASM build of the compiler and
interpreter:

- `.ips` code editor with syntax highlighting and diagnostics.
- Live preview canvas.
- Seed / variant / archetype controls.
- Atlas browser (variant grid).
- Bytecode inspector and disassembly view (debug section).
- One-click export of the compiled `.iprop`.

## 12. Website

- Playground running the WASM build in the browser.
- Documentation: format specification, language reference, and installation
  guides per backend (HTML5, SFML, OpenGL, Vulkan).
- Gallery of example assets with their `.ips` sources and seeds.
- Contribution and license information.

## 13. Licensing and governance

- Code: **Apache-2.0**.
- Examples and gallery assets: **CC-BY-4.0**.
- The format specification evolves under semantic versioning; semantic changes
  to existing opcodes are forbidden, additions are feature-flagged.
- Repository ships `CONTRIBUTING.md` and CI (build, conformance, formatting)
  from the first commit.

## 14. Repository layout

```
infinite-props/
  spec/        # The .iprop format and VM specification (versioned)
  language/    # .ips grammar, parser, semantic rules
  compiler/    # lang → IR → bytecode; disassembler, validator
  runtime/     # Reference CPU interpreter (C++20)
  backends/    # html5 (Emscripten), capi (the stable C API), sfml, godot, opengl, vulkan
  editor/      # Web pseudo-IDE
  website/     # Playground, docs, gallery
  examples/    # .ips sources + compiled .iprop files
  tests/       # Golden images and conformance suite
```

## 15. Roadmap

| Phase | Deliverable |
| ----- | ----------- |
| 0 | This requirements document + format spec v0.1 (done — see `spec/`) |
| 1 | Reference CPU interpreter + 5 example programs (QuantSpace ports) + golden tests |
| 2 | `.ips` language (including imports) + compiler + disassembler/validator |
| 3 | HTML5 (WASM) and SFML backends |
| 4 | OpenGL and Vulkan hosts (shader lowering as an extension) |
| 5 | Web pseudo-IDE |
| 6 | Website, CI conformance across backends, first release |

The detailed, clickable plan — 34 numbered phases with acceptance criteria —
was tracked through the project's history (phases 1–16: the format and the
VM; 17–23: the language and the compiler; 24: the HTML5 backend; 28–31: the
editor; 32–34: the website and the v0.1.0 release).

**Current state.** v0.1.0 is released (the format, the VM, the language and
the HTML5 backend). v0.2 rounded out the language — typed
function signatures, the pinned trig and math packages, constant arrays
(palettes) and loop control — and lands the native backends on the C API:
the SFML window and the Godot GDExtension, each with its own conformance.
v0.3.0 adds vectors, distance fields, curves, procedural noise, colour
utilities and exact part transforms. Product 0.3.0 is released on `main`
with tag `v0.3.0`; the CLI, C API, SFML and Godot have regression and
conformance checks. The product version, format
version (0.3, with 0.1/0.2 reader compatibility) and C API version (0.2)
are separate contracts. API 0.2 consumers must rebuild with the new header.
Source and native packages are available as local release artifacts.
Remote publication and GitHub CI require a configured remote; editor/website
publication, Bevy and Unity glue remain separate ecosystem work.

The next example pass has two showcases:

- **Butterfly (512×512):** one wing part placed four times using D4 transforms;
  seeded SDF silhouettes, Bezier veins and antennae, `fold` symmetry,
  `hsv`/`ramp` palettes, `cell.jitter` eyespots, `fbm` mottling and dither shading.
- **Bob Ross landscape:** ramp skies, ridge/fbm mountains with atmospheric
  perspective, jittered trees, folded water reflections, mist and seed-selected
  biomes with their own palettes.

Deferred work includes general affine part transforms with pinned resampling,
masks and multiple rendering passes, full colour arithmetic, `switch`, and
Bevy/Unity integrations. Whole-canvas generators such as diamond-square and
cellular automata depend on the future buffer/mask work. Game-specific mods
can be revisited after the showcases. L-systems and a dedicated noise-graph
DSL remain outside the language scope; user functions, `warp` and curve
builtins cover functional noise composition.

Prepare a local release package from a clean, committed checkout:

```sh
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
scripts/release.sh 0.3.0 build-release
```

Use `IPROP_SFML=ON` and `IPROP_GODOT=ON` with their dependency paths when
including those integrations; required mode fails on missing dependencies.
Godot packages require `GODOTCPP_TARGET=template_release`. The script
validates CTest, a moved installation, all six tools and external shared/static
C API consumers before writing `dist/iprop-0.3.0.tar.gz` (committed source),
`dist/iprop-0.3.0-<os>-<arch>.tar.gz` (native install) and SHA-256 checksums.
The native archive records its source commit and included backends in
`release.json`. Pass `--check` to validate an in-progress build without
creating archives. Packaging does not tag, merge, push or publish.
