# Compiler optimizations in v0.4

Optimizations act on typed compiler IR. The VM instruction definitions and
frozen mathematical kernels retain their existing semantics. `ipc --no-opt`
(or `compileProgram(program, parts, false)`) retains the reference lowering
for comparisons and debugging compiler behavior.

## Dead code and palettes

Control-flow liveness accounts for both conditional branches, structured
branch targets and loop back edges. Dead local stores become discarded
values; only proven non-trapping, effect-free producer trees are removed.
RNG draws, helper and part calls, output/canvas writes and possibly trapping
operations retain their original evaluation order. Unreferenced local slots,
names and literal pool entries are compacted with their indices remapped.
Constant palette indexing emits only the selected element; unused palettes
and discarded palette values have no retained literal data.

Optimization candidates must fit within both the complete debug artifact
and its stripped executable relative to the reference lowering. Pool and
local metadata costs are included. Compiling the same source is deterministic.
Source maps retain the source line of every remaining instruction; deleted
locals disappear from debug metadata. Compiler optimization may reduce fuel
consumption. It preserves successful pixel/output bits and the effects of
executed instructions; exact instruction counts are not a source contract.

## Constant chains and peepholes

Straight-line regions propagate known local values; control-flow boundaries
clear those facts. Contiguous constant operand trees fold without crossing
calls or side effects. Colors and vectors use the runtime's existing math
kernels, including alpha, clamping, NaN canonicalization and signed zero.
Vectors are reconstructed using two f32 constants and `vec2.make`; the VM
and constant-section layouts need no extension.

Nonzero integer division/remainder fold only when they cannot trap. Division
by zero, signed division overflow and non-finite HSV hue shifts keep their
runtime operations. Historical non-finite scalar arithmetic is conservative.
Adjacent `local.set n` / `local.get n` becomes `local.tee n`; liveness then
removes unnecessary stores. Each pass is accepted only if neither complete
artifact mode grows relative to the previous best plan.

## Common subexpressions

Pure contiguous expression trees can reuse an equivalent value already
stored in a local. Value numbering retains operand order and type; it never
reassociates floating-point expressions. Every local write changes its
version, so overwritten storage cannot satisfy an old expression. Control,
call and part-read boundaries clear availability. RNG, stream state,
outputs, canvas operations and possibly trapping calculations are excluded.

This pass uses existing storage and adds no speculative evaluations or
scratch locals. Expressions over 512 key bytes and caches over 4096 entries
fall back conservatively to normal lowering.
