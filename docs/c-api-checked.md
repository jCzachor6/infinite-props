# Checked load and metadata queries

These API 0.3 operations are implemented on the frozen ABI line 0.4:

| Symbol | Behavior |
| --- | --- |
| `iprop_program_load_ex` | Loads and validates once; returns structured status/error |
| `iprop_program_query` | Copies size, origin, variant/archetype counts, fuel and advertised extended metadata |
| `iprop_program_info_ex_init` | Initializes the sized program metadata output with the caller's capacity |
| `iprop_library_query_ex` | Existing library-version/capability query with optional structured error output |
| `iprop_result_query_stats` | Copies completed VM fuel, pixel bytes and output element counts |
| `iprop_result_stats_ex_init` | Initializes a sized result statistics descriptor |

`iprop_library_query` advertises `IPROP_CAP_PROGRAM_LOAD_EX`,
`IPROP_CAP_PROGRAM_QUERY`, `IPROP_CAP_STRUCTURED_ERRORS` and
`IPROP_CAP_LIBRARY_QUERY_EX`, alongside its existing library-query/sized bits.
The structured-error bit describes these implemented operations. It does not
promise generation, parameters, result views or statistics. The independently
negotiated `IPROP_CAP_ROOT_INPUT_REFLECTION` now advertises the implemented
[root-input count/get/find calls](c-api-reflection.md).
`IPROP_CAP_OUTPUT_DECLARATIONS` separately advertises the implemented
[output schema queries](c-api-output-declarations.md).
`IPROP_CAP_PART_REFLECTION` advertises the implemented
[part table and input signatures](c-api-parts.md).
`IPROP_CAP_METADATA_EXTENDED` advertises appended
[emission and physical container fields](c-api-metadata.md) in the existing
program descriptor, without another query function.
`IPROP_CAP_RESULT_STATISTICS` separately advertises
[completed result statistics](c-api-statistics.md).
`IPROP_CAP_PARAMETERS` separately advertises the implemented
[root parameter handles and generic value operations](c-api-parameters.md).
`IPROP_CAP_GENERATE_EX` advertises
[checked generation](#checked-generation-generate_ex) from those parameters.

## Load and publication

`iprop_program_load_ex(bytes, byte_count, &program, &error)` accepts a fixed-width
`uint64_t` byte count. A count not representable by the platform's `size_t` is
an argument error before reading the bytes. `bytes` and `out_program` are
required; NULL bytes are an argument error even for count zero. Non-null empty
input reaches the decoder and returns a truncated-input format error. The
caller supplies readable storage for the declared span; the library cannot
verify a falsely declared span or invalid pointer.

On success the call publishes exactly one new handle, owning its loaded byte
copy. It does not free a previous handle stored by the caller. On any error it
leaves that pointer untouched and destroys its unpublished candidate. The
candidate owner lives outside the guarded callable, so its diagnostic file
remains available when reporting a validation exception after stack unwinding.

```c
iprop_program *program = NULL;
iprop_error_ex error;
char message[256];
iprop_error_ex_init(&error, sizeof(error));
error.message_buffer = message;
error.message_capacity = sizeof(message);
if (iprop_program_load_ex(bytes, byte_count, &program, &error) == IPROP_EX_OK) {
    iprop_program_info_ex info;
    iprop_program_info_ex_init(&info, sizeof(info));
    if (iprop_program_query(program, &info, &error) == IPROP_EX_OK) {
        /* info.size_x, size_y, origin_x, origin_y, variants, archetypes, fuel */
    }
    iprop_program_free(program);
}
```

The [structured status policy](runtime-errors.md#new-status-policy-and-exception-boundary)
distinguishes argument, load/validate format, actual VM trap, OOM and internal
failures. Load has no VM execution phase. OOM identifies the active load or
validation phase; unexpected exceptions use `INTERNAL`. Reporting allocates
no new memory and retains no global last error.

## Sized query outputs and error storage

`iprop_program_info_ex` is separate from frozen `iprop_meta`. It requires four
readable bytes for `struct_size` and accepts every valid shorter prefix or
larger future allocation. Queries preserve that size and write only complete
known fields. Partial fields, padding and unknown tail bytes are untouched.
The initializer follows the same bounds and sets complete fields to zero.
The four-byte prefix succeeds without returning absent metadata values. NULL
or a declared size below four is an argument error with no query-output write.

Fields through `fuel` copy the legacy metadata subset and retain their original
offsets and prefix size. Emission and physical format fields are appended to
this same descriptor/accessor under `IPROP_CAP_METADATA_EXTENDED`; older caller
allocations still receive their complete known prefix. There are no result
views in this query. `iprop_library_query_ex` uses the existing library descriptor
and the same bounded writer as `iprop_library_query`.

An error descriptor is optional. Valid prefixes receive available error fields;
success clears all available error metadata and text. A malformed error size
or NULL message pointer with nonzero capacity stops the operation before
publication, even when its main request would otherwise succeed. See the
[buffer contract](runtime-errors.md#caller-owned-messages) for required length,
NUL, truncation and length-only reporting.

Error descriptor storage and its writable message span must not overlap the
operation's output pointer/query allocation; its writable message span must
also not overlap the error descriptor itself. The checked adapter rejects
these overlaps before invoking or reporting the operation. It returns
`E_ARGUMENT` and leaves both outputs untouched, because reporting into aliased
storage would itself violate the output transaction. A zero-capacity message
span accesses no buffer bytes. The caller still promises actual allocations
matching every declared capacity. Input bytes may overlap writable message
storage when valid for both uses: loading consumes/copies them before success
or error text is emitted.

## Shared runtime and legacy behavior

`Handles.hpp` keeps opaque definitions private. `Operations.hpp`/`operations.cpp`
provide one load/validate core, one VM-generation core and the existing result
marshaller. `CheckedBoundary.hpp` validates output storage and invokes the
shared structured boundary. `LegacyBoundary.hpp` preserves the old status and
byte-prefix message policy. Neither adapter repeats the decoder, validator or
VM. The six legacy declarations/layouts remain unchanged; their nullability,
unknown/duplicate uniform policy, setup traps, and untouched success/argument/
OOM text retain their previous behavior.

## Checked generation (`generate_ex`)

`IPROP_CAP_GENERATE_EX` advertises:

```c
iprop_status_ex iprop_generate_ex(const iprop_program *program,
                                 const iprop_generate_options_ex *options,
                                 const iprop_parameters *parameters,
                                 iprop_result **out_result,
                                 iprop_error_ex *error);
```

Program, options and result output are required. Options require a four-byte
sized prefix; absent seed/variant/archetype/dimensions default to zero, with
zero dimensions selecting source size. Parameters are optional: NULL uses the
stored defaults, and a supplied set must come from the same program load.

One private validator, `checkGenerationRequest`, checks every new generation
request and is meant for the future generation context as well. In this order
and before any VM setup or allocation it rejects a NULL,
shorter-than-four-byte or nonzero-flags options descriptor
(`INVALID_ARGUMENT`); parameters from another program load
(`FOREIGN_PARAMETERS`, even for identical bytes); then `VARIANT_RANGE`,
`ARCHETYPE_RANGE` and `CANVAS_SIZE`. The ranges come from the same
`checkGenerationArguments` the VM constructor uses, so each verdict equals the
VM's own setup check. All are `E_ARGUMENT` at `ARGUMENTS` stage. Parameter
IDs, names, types and duplicates were already enforced when the values were
set, by single setters or
[strict override lists](c-api-parameters.md#strict-override-lists).

Names are resolved to root-input IDs once, when values are set. Generation
reads the const parameter set and passes its values to the VM by ID, in
declaration order, through the VM's resolved-value constructor; no layer maps
names again. Without parameters the VM uses the stored defaults. Legacy
`iprop_generate` keeps its own named-uniform policy in the VM's name-based
constructor: unknown names are ignored, the first duplicate wins, and a
mistyped declared uniform, like an out-of-range variant, archetype or canvas,
returns `E_TRAP`.

An execution failure is `E_TRAP` at `EXECUTE` stage with its function/PC
location, plus a debug line when available. Allocation failure is `E_OOM`; an
unexpected exception is `E_INTERNAL`. Success publishes exactly one new result
and does not free a previous pointer stored in `out_result`; any failure leaves
`out_result` untouched and transfers no partial result. Error storage must not
overlap `out_result`, the options, the program or parameter storage, and
`out_result` must not overlap the options; such aliases fail without writes.
The call retains no options, parameter or error storage. A result owns its data
and remains valid after `program_free`, later parameter changes or release, and
later generations. With equal seed, variant, archetype, size and values, the
result is bit-identical to `iprop_generate`, including outputs and fuel.
