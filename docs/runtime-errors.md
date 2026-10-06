# Structured error foundations

`iprop_error_ex` is an additive API 0.3 descriptor. It records a fixed-width
status, stable cause category, stage and optional location. Initialize it with
`iprop_error_ex_init(&error, sizeof(error))`: success, reason/stage `NONE`, and
no location, message buffer or message length. Like other sized initializers, it writes only complete known
fields; partial fields, padding and unknown tail bytes remain untouched.
It owns no memory and requires no free function.

The [checked load and metadata queries](c-api-checked.md) consume this descriptor
through the call-local error sink and advertise only those implemented
capabilities. The existing six legacy C entry points keep their enums, layouts,
status policies and located text. Structured generation is specified for the
parameters stage, with no exported symbol or capability yet.

## Causes and stages

Codes are assigned at the originating checks, rather than recovered by parsing
human text. They describe cause categories: two different invalid metadata
messages can share `INVALID_METADATA`, while division and fuel exhaustion have
distinct codes. Published numbers are never reassigned. An unknown future
code must be handled using its status/stage, without assuming its text.

| Code | Stable category |
| --- | --- |
| 0 | No error |
| 1000–1010 | Invalid format fallback, truncated input, bad magic, unsupported version, header, compression, layout, constants, metadata, code, debug |
| 2000–2002 | Invalid instruction/stack/control validation, signature, part cycle |
| 3000–3014 | Runtime fallback, fuel, stack underflow/type/limit, call depth, index, control, divide by zero, integer overflow, numeric range, output, missing part output, part render, stream underflow |
| 4000–4004 | Invalid argument fallback, variant, archetype, canvas size, uniform type |
| 5000–5001 | Out of memory, unexpected internal failure |

The corresponding `IPROP_REASON_*` constants in `iprop.h` name each category.
Stages are `NONE=0`, `LOAD=1` (container/decode/payload parsing), `VALIDATE=2`
(static bytecode/signature/part-graph checks), `ARGUMENTS=3` (host invocation
checks before execution), and `EXECUTE=4` (VM instruction failure).
Status is a separate API policy. A legacy VM-construction `Trap`, such as an
invalid variant, retains `IPROP_E_TRAP` even though its typed stage is
`ARGUMENTS`. The metadata foundation does not reclassify old calls.

## New status policy and exception boundary

`backends/capi/ErrorBoundary.hpp` defines the shared policy for structured
operations. Checked load/program/library queries use it through the storage
adapter; structured generation remains a future task. Classification uses the
exception type and its
typed originating stage; it never infers a status from message text or a
numeric cause range. A recognized typed failure retains its cause and location.

| Failure | New status | Reason and stage |
| --- | --- | --- |
| Expected host argument failure | `IPROP_EX_E_ARGUMENT` | Argument cause, `ARGUMENTS` |
| `FormatError` or its `Trap` subtype at `ARGUMENTS` | `IPROP_EX_E_ARGUMENT` | Original typed cause and stage |
| `FormatError` or its `Trap` subtype at `LOAD`/`VALIDATE` | `IPROP_EX_E_FORMAT` | Original typed cause and stage |
| Actual `Trap` at `EXECUTE` | `IPROP_EX_E_TRAP` | Original typed cause and stage |
| `std::bad_alloc` | `IPROP_EX_E_OOM` | `OUT_OF_MEMORY`, active phase |
| Other standard or unknown exception | `IPROP_EX_E_INTERNAL` | `INTERNAL`, active phase |
| Base `FormatError` at `EXECUTE`, or typed error with absent/unknown stage | `IPROP_EX_E_INTERNAL` | `INTERNAL`, active phase |

VM construction still raises `Trap` for variant, archetype, canvas-size and
uniform-type checks to preserve the legacy C++/C behavior. Their typed
`ARGUMENTS` stage means the new boundary returns `E_ARGUMENT`; a real division,
fuel or other instruction failure is an `EXECUTE`-stage `Trap`. The class name
alone cannot distinguish these cases. A base format exception tagged as
execution is an internal contract failure, rather than proof of a VM trap.

`CallOutcome::argument` returns a borrowed message and an optional specific
argument cause without constructing or throwing an allocating exception.
`ErrorContext` tracks the active phase for untyped OOM/internal exceptions;
the caller updates it before load, validation, argument preparation or VM
execution. A typed error's own originating stage takes precedence. Internal
exceptions use borrowed `what()` text or a static unknown-exception fallback;
they claim no location. OOM uses the static `out of memory` message.

`errorBoundary` binds the optional error output before invoking the operation.
A null descriptor permits the operation. An invalid descriptor size or
NULL/nonzero message span returns `E_ARGUMENT` without invoking it; a valid
metadata prefix still receives that error. The boundary clears error state on
success and catches every exception type from the operation. Its binding,
classification and reporting paths allocate no memory. It does not repeat
loading, validation or execution, and owns no handle or operation result.

The optional diagnostic file in `ErrorContext` must stay alive until the
boundary returns, including stack unwinding. Its owner belongs outside the
callable; a local destroyed while throwing cannot supply a diagnostic view to
the catch handler. [Checked load/query publication](c-api-checked.md) follows the
same lifetime rule; future generation will reuse it. Future limits/cancellation
causes remain a 0.6 task.

The six legacy entry points continue using their own frozen classification:
load maps typed/other failures to `E_FORMAT`, generation maps `Trap` to
`E_TRAP`, base `FormatError` to `E_FORMAT`, and other failures to `E_TRAP`.
Their `bad_alloc` and explicit bad-argument checks remain `E_OOM` and
`E_ARGUMENT`. They do not invoke the new exception boundary.

## Locations

`location_flags` is authoritative. Offset/function/PC zero can be a valid
location; cleared presence bits explicitly mean unavailable. Initialization
sets all absent values to zero. A short prefix writer clears presence bits
whose complete value fields do not fit. Always check both field capacity and
the applicable bit before consuming a value.

`byte_offset` is an absolute byte offset. `offset_space` identifies either
`EXPANDED_FILE` or `CONTAINER`; it is `NONE` when absent. VM, static validation
and parsed section errors identify the fully expanded file. Header and
compression-envelope errors identify the physical input container. Corrupt
compressed bytes cannot be labeled as an expanded-file offset. Plain and
compressed versions of the same malformed expanded section report the same
expanded location. An offset can identify the end of a truncated input.

`function_index` is the zero-based code table index, not a name or a source
identifier. `pc` is the zero-based instruction index in that function. A
structural error at the end of a function may identify the one-past-end PC;
signature errors identify the function without claiming a PC. Execution
snapshots the instruction before dispatch, including call/part instructions
that advance their frame before an error. Existing C++ `FormatError::offset`
keeps the legacy value used in old located messages; structured `location`
can therefore be more precise on such calls.

`debug_line` is one-based, present only when the binary has a usable matching
debug map and a nonzero instruction line. Stripped, absent, truncated or
count-mismatched maps produce no line. Lookup is bounded, does not allocate or
throw, and does not change validation or execution. The binary has no reliable
filename or column data; neither is synthesized.

The runtime `FormatError`/`Trap` carry typed reason/stage/location fields.
`backends/capi/Error.hpp` copies these through the common sized writer and can
enrich a location with an available debug line. It accepts status from its
calling boundary so legacy and future strict policies share the same VM and
validator. It does not allocate or inspect message wording.

## Caller-owned messages

After initializing `iprop_error_ex`, the caller may configure `message_buffer`
and `message_capacity`. Capacity counts writable bytes, including space for
NUL. The buffer is borrowed for the synchronous call only; its storage must
not overlap the descriptor. Reporting preserves both input fields and
`struct_size`. Absent fields default to NULL/zero. NULL with nonzero capacity
is an argument error, without dereferencing that pointer; a valid metadata
prefix can still report this error. NULL/zero and non-null/zero are valid
length-only configurations, with no buffer access. A NULL error descriptor
means no optional error output.

`message_required_bytes` reports the entire message length **excluding NUL**,
independently of the actual capacity. Allocate that length plus one for the
complete message. `message_truncated` is a fixed-width boolean: one exactly
when some message bytes were omitted, including nonempty length-only output.
The field is zero for empty text, including success with no buffer.

Positive capacity always writes NUL. Short buffers contain a prefix with
complete UTF-8 characters when the original message is valid UTF-8. Required
length still counts the full original bytes. Capacity one yields an empty
NUL-terminated string and truncation for a nonempty message. No bytes beyond
the declared capacity are written. Descriptor prefixes can omit the newer
buffer or length fields; writers neither read nor write incomplete fields,
padding or an unknown tail.
Copying also supports overlap between borrowed message text and the message
buffer; overlap between the buffer and the descriptor remains invalid.

Every completed new call initializes error output, including success: status
OK, reason/stage NONE, locations absent, required length/truncation zero, and
empty text when a writable buffer exists. New argument and OOM adapters use
static meaningful fallback messages (`invalid argument`, `out of memory`);
call sites can supply a more specific borrowed argument message. Runtime
exceptions supply their existing borrowed `what()` text with the typed
metadata. The new boundary above supplies their common status policy.

`backends/capi/ErrorSink.hpp` validates/snapshots the optional output once, then
renders through bounded helpers. Its bind/report/success/argument/OOM paths
do not allocate or throw. Error state belongs to this call and caller storage;
there is no global last_error or borrowed global error message.

Legacy calls retain their historical behavior: byte-prefix truncation, old
statuses, and buffers left untouched for success, argument errors and OOM.
Their catch handlers now copy a borrowed string view instead of allocating a
temporary `std::string`. This fixes allocation failures escaping while
reporting a format error or uniform-type trap, without changing the original
message-buffer policy. Controlled tests inject a single `bad_alloc` at every
C++ allocation observed in fixture-backed load/generate calls, including
uniform overrides and part-output marshalling. Malformed-load and type-trap
cases also cover reporting; no exception crosses the C ABI in those paths.
