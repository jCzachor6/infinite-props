# Root-input reflection

API 0.3 provides root-input declarations through checked count/get/find calls.
The loaded library advertises `IPROP_CAP_ROOT_INPUT_REFLECTION` only for these
implemented operations. [Output declarations](c-api-output-declarations.md)
have their own capability. [Part tables and input signatures](c-api-parts.md)
are separately advertised by `IPROP_CAP_PART_REFLECTION`. [Emission/format
metadata](c-api-metadata.md) has its own capability. Implemented
[root parameter handles](c-api-parameters.md) use `IPROP_CAP_PARAMETERS`;
structured generation remains a separate task.

| Symbol | Output |
| --- | --- |
| `iprop_program_input_count` | `uint32_t` root-input count, including zero |
| `iprop_program_input_get` | One caller-sized `iprop_input_info_ex` declaration |
| `iprop_program_input_find` | `uint32_t` index for an exact UTF-8 name |
| `iprop_input_info_ex_init` | Bounded initialization using actual caller capacity |

All required program/output pointers must be non-null. The optional error
descriptor follows the [checked status and message policy](c-api-checked.md).
Invalid arguments return `E_ARGUMENT`, reason `INVALID_ARGUMENT`, stage
`ARGUMENTS`, with no fabricated location. Count/find/get never allocate while
querying or reporting an error. A failure preserves the complete main output;
success resets all available error fields and caller-owned message text.
The checked boundary rejects overlapping error/output storage before reporting
or publication, preserving both outputs as described in that policy.

## Order, names and defaults

Indices range from zero to count minus one, in the binary's stored root-input
declaration order. They belong to that loaded program and are not persistent
identifiers across recompilation. Resolve them again for each loaded program.
There is no part-input override or parameter API in this capability.

Names use `iprop_utf8_view`: a borrowed pointer and explicit `uint32_t` byte
count. The count excludes an optional trailing terminator; no sentinel read
is performed. Embedded U+0000 participates in exact matching. For example,
the four stored bytes `C3 A9 00 78` match only that four-byte view, not the first
two/three bytes or the same view with an extra terminator. Matching does not
normalize Unicode or fold case. `NULL,0` and non-null empty input views are
valid empty strings; they succeed only if such a declaration exists. NULL
with nonzero length or invalid/truncated UTF-8 is an argument error. The caller
provides readable bytes for the whole declared input view.

Returned name storage is immutable and belongs to the program. It remains
valid through subsequent reflection/generation calls until `iprop_program_free`.
It must not be written to or freed. Loading copies caller bytes; destroying
the caller's input buffer does not invalidate reflected names/defaults.

`iprop_input_type` is `uint32_t`, with binary tags `IPROP_INPUT_COLOR` (0),
`IPROP_INPUT_F32` (1), and `IPROP_INPUT_U32` (2). Source `i32` inputs have the
existing U32 binary type: a default such as -7 is returned as `0xFFFFFFF9`.
Reflection does not infer a signed source alias from binary metadata or invent
a fourth binary tag.

The matching default field contains exact RGBA8 channels, IEEE-754 binary32
bits or a U32 bit pattern. The F32 copy performs no arithmetic: negative zero,
subnormals and stored NaN payloads are retained. The other two default fields
are zero. `iprop_color_rgba8` is a frozen four-channel leaf record; no sized
array stride or reinterpretation of runtime objects is exposed.

## Sized declarations

`struct_size` and `flags` are the descriptor's only inputs. Minimum capacity
is four readable bytes. Absent flags default to zero; no flags are supported,
so any nonzero complete flag field fails before writing outputs. Unknown tail
bytes do not request behavior. The initializer writes size/defaults only for
complete known fields and needs no allocation.

Get preserves `struct_size`, flags, all padding, partial fields and unknown
tail bytes. Each scalar and color record is written only when complete. The
fixed name leaf's `data` and `byte_count` are individually bounded; its native
padding is preserved. A prefix may contain the pointer before the byte count;
the caller inspects the name only when both fields fit. Four-byte-only queries
succeed without publishing absent fields. NULL or capacity below four, an
unsupported flag, unknown name or out-of-range index fails without a main
output write. Count/find return standalone fixed-width scalars, so they have
no descriptor flags or initializer.

```c
uint32_t count, index;
iprop_input_info_ex input;
iprop_library_info_ex library;
iprop_library_info_ex_init(&library, sizeof(library));
if (iprop_library_query(&library) == IPROP_EX_OK &&
    (library.capabilities & IPROP_CAP_ROOT_INPUT_REFLECTION) &&
    iprop_program_input_count(program, &count, NULL) == IPROP_EX_OK) {
    for (index = 0; index < count; ++index) {
        iprop_input_info_ex_init(&input, sizeof(input));
        if (iprop_program_input_get(program, index, &input, NULL) == IPROP_EX_OK) {
            /* input.name.data/byte_count, type, matching default field */
        }
    }
}
```

Both legacy and checked loads produce the same program representation, so
reflection is available for either successfully loaded handle. The legacy six
entry points, their layouts, enum policy and generation behavior remain unchanged.
