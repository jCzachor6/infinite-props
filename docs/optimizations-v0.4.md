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
