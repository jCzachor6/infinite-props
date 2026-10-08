# Part geometry and signatures

API 0.3 provides the loaded executable's stored part table through checked
queries. `IPROP_CAP_PART_REFLECTION` advertises these implemented operations.
They do not execute the VM, reconstruct source import paths or grant host
overrides of part inputs. Root parameters and
[checked generation](c-api-checked.md#checked-generation-generate_ex) are
documented separately.

| Symbol | Output |
| --- | --- |
| `iprop_program_part_count` | Fixed-width stored part count, including zero |
| `iprop_program_part_get` | One caller-sized `iprop_part_info_ex` record |
| `iprop_part_info_ex_init` | Bounded initialization using caller capacity |
| `iprop_program_part_input_count` | Input count of the selected part signature |
| `iprop_program_part_input_get` | One `iprop_input_info_ex` declaration |
| `iprop_program_part_input_find` | Local signature index for an exact UTF-8 name |

## Table, geometry and output ranges

Part indices begin at zero in stored table order and belong to this loaded
program. Resolve them again for another load or compilation. Owner zero is the
root; part index N has owner N+1. Every stored part occupies its owner even if
its input or output signature is empty.

Each record returns the part's stored dimensions, exact binary32 origin bits,
input count, output count and global `output_begin`. The output range is
`[output_begin, output_begin + output_count)`, in the existing root-then-parts
slot order. Read those slots with `iprop_program_output_get`, or find a name
with `iprop_program_output_find(program, part.owner, name, ...)`. These are the
same [output declarations](c-api-output-declarations.md), not a second table.
An empty part has count zero and its begin still identifies the next slot;
successive empty/nonempty parts may share that begin. Output-owner gaps are
retained. Geometry or a declaration does not predict generated values.

## Names and input signatures

A format 0.5 file with a parts section, which the compiler writes for every
file with parts, names each part instance by its alias path from the root
asset, such as `hilt` or `hilt.grip`. Such a part returns `name_present == 1`
and an immutable program-owned UTF-8 `name` view, valid until
`iprop_program_free`. Hosts can join it with a local output name to present
`hilt.grip.top`; output lookup still takes the owner and the local name.

Files without that section, including every 0.1–0.4 file, store no part name.
They return `name_present == 0`, `name.data == NULL` and
`name.byte_count == 0`, including debug containers that have a named
associated function. A function name is not a part name; no import alias or
path is inferred from it. Presence is explicit: an absent part name differs
from a stored name.

Part input access reuses the [input declaration record, types and exact default
writer](c-api-reflection.md). `input_index` and find results are local to the
selected part signature. For example, part 0 input index zero and root input
index zero may have different names, types and defaults. A part index is not
a root parameter ID, and reflection introduces no host part-input override.
Source `i32` still reports binary U32; no source-only signed tag is invented.

Input names are exact byte-counted UTF-8, including embedded U+0000. Matching
uses neither sentinel reads nor normalization or case folding. NULL/zero and
non-null empty views are valid strings and match only an actual empty input
name. NULL/nonzero and malformed UTF-8 fail. Returned immutable input names
belong to the program until `iprop_program_free`, survive further queries and
generation, and remain valid after destroying the caller's original load bytes.

## Sized descriptors and errors

The part descriptor has only `struct_size` and optional `flags` as inputs.
Minimum capacity is four readable bytes; absent flags default to zero. No
flags are supported, so a nonzero complete flag field fails before publishing
any output. The initializer writes complete known fields only. Get preserves
size, flags, padding, partial fields and unknown tail bytes. The name pointer
and byte count are bounded separately; inspect a view only when both fields
fit. Four-byte prefixes succeed without returning absent fields. Part-input
get uses exactly the same sized rules as root-input get.

Null required pointers, malformed descriptors, unsupported flags, an
out-of-range part/input index or unknown input name return `E_ARGUMENT`, reason
`INVALID_ARGUMENT`, stage `ARGUMENTS`, without fabricated locations. All main
outputs are preserved on failure. Optional errors use the common
[checked message and storage-alias policy](c-api-checked.md): malformed error
storage and overlaps stop queries before publication; successful queries clear
available error metadata and text. Querying, validating names and reporting
these errors allocate no new memory.

```c
uint32_t count, index;
iprop_part_info_ex part;
if (iprop_program_part_count(program, &count, NULL) == IPROP_EX_OK) {
    for (index = 0; index < count; ++index) {
        iprop_part_info_ex_init(&part, sizeof(part));
        if (iprop_program_part_get(program, index, &part, NULL) == IPROP_EX_OK) {
            /* part.owner, dimensions/origin, input_count, output_begin/count */
        }
    }
}
```

These queries work with either legacy-loaded or checked-loaded program handles,
with debug present or stripped, compressed or plain containers, and older
accepted formats. The six legacy entry points and their layouts remain unchanged.
