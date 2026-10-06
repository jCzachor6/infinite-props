# Retained root parameters

API 0.3 advertises `IPROP_CAP_PARAMETERS` for the operations below. This bit
does not enable typed convenience setters, bulk overrides or `generate_ex`.
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

## Failures, publication and aliases

Required handles and main outputs are non-null; error output is optional and
uses the [checked bounded-message policy](c-api-checked.md). Unknown ID/name,
malformed size/view, unsupported flags or unknown type return `E_ARGUMENT`,
reason `INVALID_ARGUMENT`. A valid type mismatching the declaration returns
`E_ARGUMENT`, reason `UNIFORM_TYPE`. All these failures use `ARGUMENTS` stage
with no invented coordinates. Actual allocation failure during create/clone
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
