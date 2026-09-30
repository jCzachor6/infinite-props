# Infinite Props — Specification

This directory contains the formal specification of the `.iprop` binary format
and its virtual machine. The requirements document at the repository root
(`README.md`) describes the project goals; the files here define the contract
that every compiler and interpreter MUST implement.

## Documents

| Document | Status | Scope |
| -------- | ------ | ----- |
| [iprop-v0.1.md](iprop-v0.1.md) | **v0.1 — frozen** | File structure, meta/const/code/debug sections, VM semantics, full instruction set, seeded primitives, canvas operations, parts and composition, determinism rules, validation, conformance |
| [iprop-v0.2.md](iprop-v0.2.md) | **v0.2 release candidate** | Version-gated trigonometry and math; all frozen 0.1 layouts and algorithms remain supported |
| [iprop-v0.3.md](iprop-v0.3.md) | **v0.3 in development** | The `vec2` value type, vector ops, SDF queries and quadratic/cubic Bézier strokes; all frozen 0.1/0.2 layouts and algorithms remain supported |

The `.ips` language compiles 1:1 to this contract; its reference lives in the
toolchain (the grammar and the type rules in `language/`, the lowering in
`compiler/`) and is summarised in the website documentation. The VM's value
types, the instruction encoding and every algorithm in §11–§12 of each released version are frozen by
the golden corpus — a change that alters a golden image is a breaking change
and needs a spec update first.

## Versioning

- The specification evolves under semantic versioning.
- Semantic changes to existing opcodes, section layouts or algorithms are
  FORBIDDEN once a version ships; they require a new major version.
- New capabilities are added as new sections, new opcodes, or new `meta`/part
  fields, gated behind feature flags or a minor-version bump.
- Implementations MUST reject files whose version or flags they cannot fully
  honour (never silently degrade).
- The reference interpreter is authoritative where implementations disagree;
  disputes are settled by the conformance suite.

## Change log

| Version | Date | Notes |
| ------- | ---- | ----- |
| v0.1 | 2026-09-28 | Initial draft: format skeleton, VM, instruction set, determinism, validation, conformance |
| v0.1.0 | 2026-09-29 | **Released.** Semantics frozen by the golden corpus (Phases 1–16); the debug section and the source maps (§7); the language and the compiler (Phases 17–23); the HTML5 backend; the editor and the website. Deferred and documented: the native hosts (SFML/OpenGL/Vulkan, Part D) and the public gallery (a separate project). |

| v0.2.0 | 2026-09-30 | Format minor 2 gates new trig and math opcodes; the reader preserves format 0.1 support. |
