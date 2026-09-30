# Changelog

## v0.2.0 — prepared for release, 2026-09-30

- Added typed function parameters and return values, constant palettes,
  `while`/`for` loops, `break`/`continue`, and pinned trig/math builtins.
- Format 0.2 gates the new math opcodes. Readers also accept frozen format
  0.1 files; new writers emit format 0.2. Product, format and C API versions
  are tracked separately.
- Added the C99 shared/static API, SFML host and Godot 4.5.2 GDExtension.
  API 0.2 includes output owners; adding that field changes struct layout,
  so clients must rebuild. Godot exposes root outputs, outputs by part
  owner and the complete ordered slot list.
- Fixed function-local loop checks, typed temporary allocation, dynamic
  numeric/bool palette selection and helper lookup in imported parts,
  including repeated imports of the same source.
- Fixed `pow` with infinite exponents, `atan2` for steep directions,
  canonical NaNs and signed zero in `min`/`max`. Added independent edge
  vectors and varied numeric samples; native/WASM compares raw output bits.
- Fixed SFML texture rectangles, screenshot rendering, viewport fitting
  and controlled errors for invalid CLI inputs. Godot reloads are
  transactional; sprites refresh on asset changes and clear stale textures.
- Fixed PIC for shared-library linking, Windows DLL exports and static
  library naming, GCC header/warning issues, required backend configuration,
  and Godot manifests/export presets for platform, architecture and flavor.
- Added desktop CI, required native backend checks, sanitizers and expanded
  conformance. Updated language documentation and editor highlighting.
- Added relocatable CMake installs/exports, minor-version shared-library
  names, native packages with enabled backends and release checks from a
  moved prefix, including external shared/static C API consumers.

- Corrected the portrait example's inverse CDF to sample ages 18–40,
  rather than repeatedly overwriting the result with age 39. Regenerated
  the recipe binary and checked all portrait golden images and browser
  hash pins. Age is currently sampled but unused by the renderer, so the
  images and hash pins remain unchanged. VM 0.1 semantics are unchanged.

This entry describes the local release candidate. Merge, tagging and
publication are separate actions.

## v0.1.0 — 2026-09-29

The first public release: a portable binary format and toolchain for
seed-based procedural 2D assets.

**The format and the VM** (frozen semantics, guarded by the golden corpus):

- The `.iprop` binary: header, section table, `meta`/`const`/`code` sections
  and the optional `debug` section (source maps and symbol names, spec §7).
- A validated bytecode VM: the full instruction set (spec §9), the pinned
  seeded primitives, the canvas raster operations, structured outputs and
  parts — every algorithm specified bit-exactly (§11–§12) and executed
  under a fuel budget.
- The validator (`ipval`), the disassembler (`ipdis`), the renderer
  (`ipgen`), the sheet tool (`ipsheet`) and the golden corpus that freezes
  the semantics.

**The language and the compiler**:

- `.ips`: assets, inputs/outputs, statements, expressions, ~20 builtins,
  parts and composition; a compiler (`ipc`) emitting deterministic,
  validated binaries with the debug section.
- Example ports: the QuantSpace character portrait (composed from the hair
  and eyes parts) and the interior potted plant (pot + flower parts, a
  sunflower species, six pot shapes).

**Backends, editor and website**:

- The HTML5/WASM backend: the reference interpreter in the browser, with a
  conformance runner pinned to the desktop output.
- The VS Code extension: syntax highlighting, compiler diagnostics, the live
  preview with seed controls, the atlas, the bytecode inspector and
  one-click exports.
- The website: the playground with shareable links and the documentation.

**Deferred (documented in `spec/README.md`)**:

- The native hosts (SFML, OpenGL, Vulkan — Part D of the plan).
- The public gallery (a separate project).
