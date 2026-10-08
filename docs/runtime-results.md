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

## Storage owned by the C result

The C adapter consumes the completed C++ result through the private
`packResult` operation. It moves the canvas and each output name into the
result handle, reserves one packed float buffer, and writes point, rectangle
and circle components individually. The C output table keeps its existing
owners, order, kinds, list flags, counts and 2/4/3 float layout. Its pointers
are assigned after both storage tables reach their final size.

Typed C++ vectors and their temporary slot table are released before the
generation call returns. The C handle retains the canvas, one copy of each
name, packed floats and the C slot table. It owns these independently of the
program and of later generations. Empty slots remain declared; a final empty
slot can point one past the packed buffer and must never be dereferenced.
When the whole buffer is empty, its value pointers remain NULL.

Allocation failure leaves an unpublished candidate owned by the checked
adapter, so its partial storage is destroyed and no result is handed to the
caller. The standalone C packing probe observes moved long-name and pixel
pointers, deallocation of all typed payloads, exact raw float bits, owner gaps,
empty slots and three destination-table allocations. It observes zero
allocations for a result with no outputs. C99 conformance also keeps the first
result alive while generating and freeing a second result and the program.

The [packing measurement](../benchmarks/capi/README.md#packing-comparison-05-m3)
records actual allocation traffic before and after this adapter change.
Packing still briefly requires both typed and packed values; peak memory
was not measured and no claim of a completely copy-free result is made.
