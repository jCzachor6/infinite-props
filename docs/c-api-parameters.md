# Retained root parameters

API 0.3 advertises `IPROP_CAP_PARAMETERS` for the operations below and
`IPROP_CAP_PARAMETER_OVERRIDES` for strict override lists. Neither bit enables
typed convenience setters; `IPROP_CAP_GENERATE_EX` advertises checked generation
from a parameter set.
Legacy `iprop_generate` still accepts its frozen named-uniform array and keeps
its existing type, duplicate and unknown-name policies.

| Symbol | Behavior |
| --- | --- |
| `iprop_parameters_create` | Retains immutable program data and copies root defaults into a new handle |
| `iprop_parameters_clone` | Retains the same program data and copies current values into an independent handle |
| `iprop_parameters_reset` | Restores all stored root defaults without allocation |
| `iprop_parameters_free` | Releases the handle; NULL is accepted |
| `iprop_parameters_get`, `iprop_parameters_get_named` | Copy one value into a caller-sized descriptor |
| `iprop_parameters_set`, `iprop_parameters_set_named` | Validate and replace one value |
| `iprop_parameter_value_ex_init` | Initializes complete descriptor fields using the caller's capacity |
| `iprop_parameters_set_overrides` | Validates a whole override list, then replaces exactly its values |
| `iprop_parameter_override_ex_init` | Initializes one list element using the caller's capacity |


IDs are indices in the stored root-input declaration order, as returned by
[root-input reflection](c-api-reflection.md). Part signature indices are local
to their part and do not identify root parameters or permit host overrides.
Named get/set resolves an exact byte-counted UTF-8 name. There is no case
folding, normalization or sentinel read; embedded NUL is part of the name.
NULL/zero and non-null/zero views mean the empty name and succeed only if that
declaration exists. NULL/nonzero, malformed UTF-8 and unknown names fail.

## Ownership and identity

The program and its parameter handles retain the same immutable loaded data.
A parameter handle remains usable after `iprop_program_free`, including named
lookup and reset. It owns a mutable value vector in root-ID order. Clones share
program identity but own separate vectors; mutation or destruction of one
parameter handle does not affect another. Input descriptors and name bytes are
borrowed only for the synchronous call. Getter descriptors contain copied
values and remain valid after handle mutation or destruction.

A new load of identical bytes creates a different program identity. Future
checked generation will require parameters from the same retained data object,
rather than accepting another program's values based on equal names or bytes.
No public compatibility query is needed by these operations, which never accept
both a program and a parameter handle. Program reflection name views still
expire at `program_free`; retention does not extend that public view contract.

## Sized values and exact types

`iprop_parameter_value_ex` has `uint32_t struct_size`, `uint32_t flags`, fixed
`iprop_input_type type`, `iprop_color_rgba8 color`, binary32 `float f32`, and
`uint32_t u32`, in that order. Type tags are exactly `COLOR`, `F32`, `U32`;
source `i32` declarations use the stored binary U32 tag and representation.
There is no source-only signed tag, coercion, clamp or range inference.

Initialization and get accept any actual capacity of at least four bytes.
Absent flags mean zero; a complete nonzero flags field is unsupported. Queries
write complete output fields only and preserve size, flags, partial fields,
padding and unknown tail bytes. Complete inactive value fields are zero.
Initialization writes capacity and zeroes known complete fields, preserving
partial fields, padding and tail. NULL or capacity below four fails without
initialization writes. A caller must provide four readable bytes for a query's
size field and must not falsely claim more allocated storage.

Set requires a complete type plus its selected complete active field. The
fixed leaf layout requires 16 bytes for Color, 20 for F32, and 24 for U32.
Inactive fields are not read and may be absent or contain arbitrary bytes.
The type must exactly match the root declaration. All operations copy F32
representations, preserving negative zero, NaN payloads and other binary32
bits; stored defaults and U32 values are likewise copied exactly.

## Strict override lists

`iprop_parameters_set_overrides` applies one list of `count` elements located
every `stride` bytes. The stride is explicit: it is never inferred from an
element's `struct_size` or from this header's `sizeof`, so an element may be a
shorter prefix or part of a larger caller record. Count zero is a successful
no-op that reads no element; the list pointer may then be NULL. Otherwise the
list must be non-null, the stride at least four bytes and `count * stride`
addressable.

Each `iprop_parameter_override_ex` has `uint32_t struct_size`, `uint32_t
flags`, `iprop_parameter_target target`, `uint32_t input_id`,
`iprop_utf8_view name`, `iprop_input_type type`, `iprop_color_rgba8 color`,
binary32 `float f32` and `uint32_t u32`, in that order. Its `struct_size` is
its actual capacity, at least four bytes and at most the stride. Absent flags
mean zero; nonzero complete flags are rejected. `target` is exactly
`IPROP_PARAMETER_TARGET_ID` (1) or `IPROP_PARAMETER_TARGET_NAME` (2); zero is
invalid, so an initialized element must choose. The selected `input_id` or
exact byte-counted name must be complete; the other selector is never read.
`type` and the selected active value follow the single-value rules above,
including exact tags, preserved F32 bits and unread inactive fields.

The library validates every element, in list order, before changing any value.
For one element it checks the descriptor shape, then resolves the target, then
rejects an input already resolved by an earlier element of the same list, then
compares the type with the declaration. The first failure decides the result:

| Condition | Reason |
| --- | --- |
| Null handle/list, short stride, size outside 4..stride, flags, invalid target or type tag, incomplete selector/type/active value, NULL/nonzero or malformed name | `INVALID_ARGUMENT` |
| Well-formed ID or name absent from the root inputs, including an absent empty name | `UNKNOWN_PARAMETER` |
| A second element resolving to the same input, by ID, name or both | `DUPLICATE_PARAMETER` |
| Type differing from the declaration | `UNIFORM_TYPE` |

Every failure is `E_ARGUMENT` at `ARGUMENTS` stage and leaves all values
unchanged, including values of earlier valid elements. Element failures prefix
the message with `parameter override N:`; clients decide by reason, not text.
Success replaces exactly the listed values and leaves the others unchanged.
A duplicate is a property of one list. Separate calls, like repeated typed or
generic setters, simply update a value again with the latest request.

The list and the names it references are borrowed for the call. List storage
must not overlap the parameter handle or its values, because values are
decoded again after validation; names are read only before the first
replacement. The optional error descriptor and message must not overlap the
list or any referenced name. These aliases fail before error binding, without
error, list, name or value writes. Validation records at most one position per
root input on the stack: the call allocates nothing. Unlike legacy
`iprop_generate`, which ignores unknown uniforms, lets the first duplicate win
and traps on a mismatched type, this list is strict.

## Transactions, snapshots and threads

Every value mutation is one transaction: `iprop_parameters_set`,
`iprop_parameters_set_named`, `iprop_parameters_set_overrides`,
`iprop_parameters_reset` and any convenience setter built on them. The library
validates the complete request and the optional error storage first; the
replacement that follows cannot fail, throw or allocate. A failed call
therefore leaves every value bit unchanged, including list entries validated
before the failing one. There is no partial override and no rollback step.
Malformed error descriptors and rejected aliases stop the call before the
operation runs. Create and clone publish a new handle only on success.

`iprop_parameters_clone` is the snapshot of a mutable set. It copies all current
values at the call and shares only the immutable program data and identity.
Mutating, resetting or freeing either handle never changes the other, and the
snapshot outlives `program_free` and its source. To prepare several
configurations, clone a base set and apply a list or setters to each clone.

Generation only reads parameters. The request check of new generation entries
takes `const iprop_parameters *` and leaves the handle and its values unchanged,
whether it accepts or rejects the request. `iprop_generate_ex` takes the same
const handle and passes its values to the VM by root-input ID. A result owns
its data, so later parameter changes never alter it.

A parameter set is a single-user builder, not a synchronized object. Never call
functions on one set from two threads at the same time, and never mutate it
while another thread reads, clones or generates with it. In 0.5, a program and
the sets and clones created from it share loaded data and form one handle
group: hosts serialize calls within a group and use separately loaded programs
for concurrent workers. Sharing one immutable program across workers is left
to the 0.6 concurrency audit.

## Failures, publication and aliases

Required handles and main outputs are non-null; error output is optional and
uses the [checked bounded-message policy](c-api-checked.md). Malformed
size/view, unsupported flags or unknown type return `E_ARGUMENT`, reason
`INVALID_ARGUMENT`. A well-formed ID or name that is not a root input of the
handle's program returns reason `UNKNOWN_PARAMETER`. A valid type mismatching
the declaration returns reason `UNIFORM_TYPE`. All these failures use
`ARGUMENTS` stage with no invented coordinates. Actual allocation failure during create/clone
returns `E_OOM`; unexpected standard or unknown exceptions return `E_INTERNAL`,
also at `ARGUMENTS`. Basic reporting, lookup, get, set and reset allocate no
memory. Successful calls clear available error metadata and message text.

Failed create/clone preserves the caller's existing output pointer, releases
all unpublished candidate storage, and transfers no handle. Success publishes
exactly one new handle without freeing a prior pointer stored in that variable.
Failed get preserves the complete output descriptor. Set validates the entire
request before its nonthrowing value replacement; failure preserves all values.
Reset restores existing storage and does not allocate. A malformed error
descriptor/message span prevents publication or mutation.

Error descriptor/message storage must not overlap main output storage, caller
value/name inputs, the parameter object or its value buffer. Create also
protects the source program object. Main outputs must not overlap protected
handle/value storage; named getter output must not overlap the input name.
Such aliases are rejected before error binding and leave error/input/output
storage and parameter values untouched. Named setters may share readable
name/value bytes because both are consumed before mutation. The common overlap
check uses fixed protected spans and does not allocate. Invalid pointers,
stale handles and writes to immutable reflection views remain outside the
caller contract. No shared-handle concurrency guarantee is added.
