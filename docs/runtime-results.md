# Receiving VM generation results

`VM::result() const` keeps its copying behavior for existing C++ consumers.
The returned canvas, names and typed output vectors are independently owned;
the accessor remains reusable while the VM has not been consumed.

`VM::takeResult()` transfers these buffers once after a successful `run()`.
The C runtime adapter uses this path. It first allocates the destination output
slot table, then moves the canvas, names and typed vectors without further
allocation. If the table allocation fails, the VM remains intact and transfer
can be retried. It allocates no table when there are no output declarations.

Taking a result before successful execution, after a trap, or a second time
raises `std::logic_error`. Once transfer succeeds, `run()`, `canvas()` and the
copying `result()` accessor also reject use. Fuel counters and `debugStack()`
remain readable. References previously obtained from `canvas()` must not be
used after transfer. The returned result owns its data independently of the
VM and loaded program.

Transfer does not execute instructions, draw, consume fuel or alter RNG.
The standalone allocation probe checks one slot-table allocation for a result
with long names and populated point/rectangle/circle lists, retry after failed
allocation, and zero allocations for a result without outputs. The semantic
tests compare exact RGBA and raw float bits with the copying path, including
empty slots, helper RNG, part placement and transforms. Packing legacy C
output arrays still requires separate storage; this change does not imply
that the complete C result is produced without copies.
