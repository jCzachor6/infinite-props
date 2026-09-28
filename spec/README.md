# Infinite Props — Specification

This directory contains the formal specification of the `.iprop` binary format
and its virtual machine. The requirements document at the repository root
(`README.md`) describes the project goals; the files here define the contract
that every compiler and interpreter MUST implement.

## Documents

| Document | Status | Scope |
| -------- | ------ | ----- |
| [iprop-v0.1.md](iprop-v0.1.md) | Draft v0.1 | File structure, meta/const/code/debug sections, VM semantics, full instruction set, seeded primitives, canvas operations, parts and composition, determinism rules, validation, conformance |

The language specification (`.ips`) is not part of v0.1; it is scheduled for
Phase 2 and will live in `spec/` as a separate document.

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
