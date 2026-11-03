# Gaps and bevels — 0.9 implementation contract

Stage 08 builds on [regions](regions-v0.9.md). Product/source version and C ABI
remain unchanged until the release integration stage. New opcode values are
append-only, with zero aux and operand, checked even in unreachable code.

| Opcode | Mnemonic | Stack |
| --- | --- | --- |
| 0xFC | `regions.gap` | regions2d f32 → regions2d |

## Constant gap (08.1)

`regions.gap(source:regions2d,width:f32)->regions2d` uses opcode `FC`,
`regions.gap`. Reject unassigned sources and nonfinite or negative width before
any shortcut. Compute `width * 0.5f` in binary32 and call the **same** offset
kernel as `regions(source,inset,0)`, with no separate geometry engine or tariff.
Width zero (including negative zero), and a positive width whose half rounds
to zero, returns the original immutable store. Empty valid stores are shared.

The offset includes outer and hole edges; fragments keep source IDs,
provenance, neighbors, canonical ordering and the existing limits. Repeated
gaps act on the current geometry and retain the flat original ancestry.
No drawing and no active RNG reads/advances occur. Named arguments, helper
transfer, formatting and assembler/disassembler use existing language rules.

Verification: `tests/gap_bevel_test.cpp` compares the complete point/edge/ring
and component metadata and operation fuel with offset for three sources and
five widths, including disappearance and repeated application. Frontend tests
run with/without optimization, reversed named arguments, helper transfer,
roundtrips and checked dead-result/unassigned failures.
