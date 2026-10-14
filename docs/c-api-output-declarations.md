# Output declarations before generation

API 0.3 exposes the loaded program's output schema through checked count/get/find
calls. `IPROP_CAP_OUTPUT_DECLARATIONS` advertises these implemented operations.
They need no generation result and do not execute the VM. This capability does
not provide parameters or new generated-value accessors. The separately
advertised [part table](c-api-parts.md) identifies each part's contiguous output
range and owner for use with these same declaration accessors.

| Symbol | Output |
| --- | --- |
| `iprop_program_output_count` | Fixed-width total declaration count |
| `iprop_program_output_get` | One caller-sized `iprop_output_info_ex` declaration |
| `iprop_program_output_find` | Global slot index for an exact `(owner, name)` |
| `iprop_output_info_ex_init` | Bounded declaration initialization |

## Slots and owners

All C API output counts, indices and part output ranges count point/rect/circle
geometry only. Format 0.6 mask/layer exports use a separate internal table and
add no host slot, public kind tag or handle accessor. A part containing only
resource exports has a geometry output count of zero.

The global `output_index` begins with all stored root declarations, followed by
all declarations of part 0, part 1, and so on. Declaration order within each
owner is retained. Indices are local to this loaded program; resolve them again
for a different load or compilation. Owner 0 means root; owner N+1 means stored
part N. A part with no outputs still occupies its owner number, so output owners
can have gaps. That number does not imply a recovered import alias or source path.

All declared slots are present before generation. A singular slot that will
remain unassigned and a list that will remain empty are still declarations.
The count reflects this complete schema. It does not count generated elements,
and querying a declaration cannot predict whether a later generation sets it.
Existing legacy generation uses the same root-then-parts slot order.

`kind` uses the fixed-width `iprop_output_kind_ex` tags
`IPROP_OUTPUT_KIND_POINT` (0), `IPROP_OUTPUT_KIND_RECT` (1), and
`IPROP_OUTPUT_KIND_CIRCLE` (2). `is_list` is a `uint32_t` equal to zero for a
singular declaration or one for a list. The frozen legacy enum and output
struct remain unchanged.

Names may repeat under different owners. Find checks only the requested owner;
it never resolves an ambiguous global name by selecting a different owner.
For example, root `anchor` may be a singular point while part-owner 3 `anchor`
is a point list. Both declarations have their own global slot index. An empty
valid owner has no matches; an owner beyond the stored part table is invalid.

## Names, bounds and failures

The [UTF-8 contract](c-api-contracts.md#strings-and-public-types) applies to
names, including exact embedded-NUL matching and invalid UTF-8 rejection.
Matching uses explicit byte counts, without normalization, case folding or
sentinel reads. NULL/zero and non-null empty views are valid names and match
only an actual empty declaration name. NULL/nonzero is invalid.

Returned names are immutable views belonging to the program until
`iprop_program_free`. They survive subsequent reflection/generation and caller
load-buffer destruction. Do not write to or free their storage. Their ownership
is independent of legacy result-name views, which belong to the result.

The descriptor has input `struct_size` and optional `flags`; minimum capacity
is four readable bytes. No flags are supported. A nonzero complete flag field
fails before publishing any output. Absent flags are zero. Get writes only
complete known output fields, preserving size, flags, all padding, partial fields
and unknown tail bytes. The name leaf's pointer and byte count are bounded
individually, preserving its native padding. Inspect the view only when both
fields fit. The initializer uses actual caller capacity and follows the same
bounded rules. Count/find return standalone `uint32_t` scalars.

Null required program/output pointers and invalid owner/index/name/descriptor/flags
produce `E_ARGUMENT`, reason `INVALID_ARGUMENT`, stage `ARGUMENTS`, without
fabricated locations. Unknown names also return this argument status. Failed
queries preserve the entire main output. Optional error descriptors and their
message buffers follow the [checked error and alias policy](c-api-checked.md),
including success clearing and early rejection of overlapping outputs. Querying,
validating names and reporting these errors allocates no new memory.

```c
uint32_t count, index;
iprop_output_info_ex output;
if (iprop_program_output_count(program, &count, NULL) == IPROP_EX_OK) {
    for (index = 0; index < count; ++index) {
        iprop_output_info_ex_init(&output, sizeof(output));
        if (iprop_program_output_get(program, index, &output, NULL) == IPROP_EX_OK) {
            /* owner, name.data/byte_count, kind, is_list; no values yet */
        }
    }
}
```

Declarations are stored executable metadata. They remain available in older
accepted binary formats, with debug stripped or present, and in compressed or
plain containers. Reflection works on both legacy-loaded and checked-loaded
program handles. No metadata is reconstructed from debug/source information.
