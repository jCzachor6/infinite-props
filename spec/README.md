# Infinite Props — Specification

This directory contains the formal specification of the `.iprop` binary format
and its virtual machine. The requirements document at the repository root
(`README.md`) describes the project goals; the files here define the contract
that every compiler and interpreter MUST implement.

## Documents

| Document | Status | Scope |
| -------- | ------ | ----- |
| [iprop-v0.1.md](iprop-v0.1.md) | **v0.1 — frozen** | File structure, meta/const/code/debug sections, VM semantics, full instruction set, seeded primitives, canvas operations, parts and composition, determinism rules, validation, conformance |
| [iprop-v0.2.md](iprop-v0.2.md) | **v0.2 — frozen** | Version-gated trigonometry and math; all frozen 0.1 layouts and algorithms remain supported |
| [iprop-v0.3.md](iprop-v0.3.md) | **v0.3 — frozen** | Vectors, SDF queries and Bézier strokes, procedural noise, colour and curve utilities, palette ramps and D4 part transforms; all frozen 0.1/0.2 layouts and algorithms remain supported |
| [iprop-v0.4.md](iprop-v0.4.md) | **v0.4 — release candidate** | Lossless bounded zlib/DEFLATE containers; frozen 0.1–0.3 files, VM behavior and output bits remain supported |

The `.ips` language compiles to this contract. Its complete authoring reference
is the [language handbook](../docs/language/index.md), checked against the
grammar/type rules in `language/` and lowering in `compiler/`. The website
and release documentation are generated from that handbook. The VM's value
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
| v0.3.0 | 2026-10-02 | **Released.** Format minor 3 adds vectors, distance fields, curves, procedural noise, colour utilities and D4 part transforms; 0.1/0.2 reader compatibility is preserved. |
| v0.4.0 | pending | **Release candidate.** Format minor 4 adds lossless compression; compiler/source additions retain existing VM semantics. |
