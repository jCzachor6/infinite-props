# Changelog

## Unreleased fixes for 0.2.0

- Corrected the portrait example's inverse CDF to sample ages 18–40,
  rather than repeatedly overwriting the result with age 39. Regenerated
  the recipe binary and checked all portrait golden images and browser
  hash pins. Age is currently sampled but unused by the renderer, so the
  images and hash pins remain unchanged. VM 0.1 semantics are unchanged.

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
