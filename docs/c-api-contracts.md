# C API handle and descriptor contracts

These rules govern new API 0.3 functions as they are introduced. Their presence
in this document does not advertise an unimplemented capability. The frozen
[API 0.2 policy](c-api-abi.md) continues to govern legacy calls.

## Strings and public types

`iprop_utf8_view` is a fixed leaf record: `const char *data` and `uint32_t
byte_count`. Counts are UTF-8 bytes, excluding any optional trailing NUL.
No sentinel read or `strlen` is permitted. `NULL, 0` and a non-null empty view
are valid empty strings; `NULL` with a nonzero count is an argument error.
Embedded U+0000 is retained and participates in exact name matching. Invalid,
overlong, surrogate and truncated UTF-8 is rejected before a lookup or mutation.
An empty valid string need not name an existing declaration.

Input strings are borrowed only for the synchronous call. A successful setter
stores the identified value, without retaining the caller's string pointer.
Output strings are immutable borrowed views owned by the handle in the query.
Their storage must not be written to or freed. Optional source information has
an explicit validity/presence field; an empty string alone does not mean that
an unavailable source alias was recovered from the executable.

New statuses, reason/type/kind tags, booleans, IDs, counts and flags use explicit
`uint32_t` or `uint64_t` fields. Signed values use `int32_t`; floating values
use IEEE-754 binary32 `float`. Pointer width/alignment follow the native ABI.
No C++ object, allocator, exception, ABI-sized enum or `size_t` is added to new
descriptor layouts. Existing API 0.2 layouts are unchanged. Leaf records such
as the UTF-8 view have frozen fields; extensible enclosing descriptors use
`struct_size`. Collections use single-item accessors, without an inferred
array stride.

## Ownership and invalidation

| Object or borrowed view | Owner and invalidation |
| --- | --- |
| Loaded program and its metadata/reflection views | The caller frees the program; program query views expire at `program_free` |
| Parameter set | Owns mutable values and retains the necessary loaded program data; remains usable after the originating program handle is freed |
| Parameter value view | Borrowed until a mutating set/reset operation or freeing its parameter set; cloning creates independent values |
| Generation result, pixels, output names and values | Result owns all required storage independently of program/parameter handles; views expire at `result_free` |
| Error descriptor and message buffer | Caller owns both; no borrowed global error string, and no release call is required |
| Input descriptors, byte buffers and strings | Caller owns readable storage for the duration of the call; load copies the bytes it needs |

Another generation does not invalidate an existing result. Freeing a parameter
set or program does not invalidate a result. Root input IDs and global output
slots are scoped to a particular loaded program; they are not stable across
recompilation. Parameter sets from independently loaded programs must not be
silently accepted as belonging to each other, even for identical bytes.
Part-input reflection does not imply host overrides of those inputs.

Free functions accept `NULL`. Other handles are required unless their function
explicitly documents a null default; required output pointers are non-null.
Functions do not accept stale handles or invalid pointers. No shared-handle
concurrency guarantee is introduced in 0.5; use separate handle groups on
different threads. Calls are synchronous and retain no caller callbacks.

## Sized descriptors and failures

The caller supplies at least four readable bytes for `struct_size`, which is
the actual allocation capacity. A library cannot detect a falsely claimed
capacity or a dangling pointer. Validate the size before any further field
access; read/write complete fields only, through the common bounded helpers.
Absent optional input fields use documented defaults. Unknown trailing bytes
are ignored and never written; requested unknown flags are rejected. A missing
required prefix or null required output is an argument error.

Output queries preserve `struct_size`. Initializers take capacity explicitly,
write known complete fields only and leave padding, partial fields and the
unknown tail untouched. `capacity < 4` or a null descriptor fails without
writing. All `_ex` query descriptors document which fields are required and
which may be omitted by an older caller.

Validate the complete request before publication or mutation. A failed query
leaves its output view untouched; failed load/clone/generate leaves the caller's
output handle untouched and transfers no partial allocation. A successful call
assigns a new handle but does not free a previous value stored in that variable.
A failed parameter update/reset leaves all values unchanged. Error output is
the intentional exception: its validated prefix reports the failure, with an
explicitly bounded caller-owned message buffer. No exception crosses C, even
when allocating or reporting an error fails; basic error reporting allocates
no memory. Specific status, reason and message-buffer rules accompany the X
implementation tasks; the [message-buffer foundation](runtime-errors.md)
already defines caller capacity, NUL, required length and success clearing.

Current library/options initializers and query already use these bounded
descriptor rules. `Utf8View.hpp` provides the common no-allocation validator;
reflection and parameter entry points use it when implemented.

The sized `iprop_error_ex` foundation and initializer follow the same rules.
Its typed categories, stages and optional coordinate spaces are specified in
[runtime-errors.md](runtime-errors.md). Presence bits distinguish unavailable
locations from valid zero offsets/indices. The
[checked load/query operations](c-api-checked.md) consume this foundation;
structured generation is specified for P.5. Its call-local ErrorSink
snapshots caller-owned message storage, performs no allocation while reporting,
and preserves the descriptor's buffer/capacity inputs.

`ErrorBoundary.hpp` adds the shared new-operation exception policy: originating
argument checks become `E_ARGUMENT`, load/validation failures `E_FORMAT`, and
an actual execution `Trap` `E_TRAP`; allocation failure is `E_OOM`, while
unexpected exceptions are `E_INTERNAL`. Its bounded error output is optional,
and malformed output storage stops the operation before publication. Legacy
entry points keep their existing classification. See
[the policy and stage/type distinction](runtime-errors.md#new-status-policy-and-exception-boundary).

Checked load and metadata queries reject error descriptor/message spans that
overlap their output pointer/query storage, or a writable message span that
overlaps its own error descriptor. They return `E_ARGUMENT` without invoking
or reporting the operation and preserve both outputs. This exception to error
publication prevents the error writer itself from corrupting protected output.
All other valid error prefixes follow the bounded message/status contract.
