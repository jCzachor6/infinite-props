# Structured error foundations

`iprop_error_ex` is an additive API 0.3 descriptor. It records a fixed-width
status, stable cause category, stage and optional location. Initialize it with
`iprop_error_ex_init(&error, sizeof(error))`: success, reason/stage `NONE`, and
no location, message buffer or message length. Like other sized initializers, it writes only complete known
fields; partial fields, padding and unknown tail bytes remain untouched.
It owns no memory and requires no free function.

This foundation does not add a structured load/generation/query entry point
or an error capability. The existing six C entry points keep their enums,
layouts, status policies and located text. Later entry points consume this
descriptor through the call-local error sink. The message-buffer foundation
below is implemented without advertising a structured load/query/generation
consumer.

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
| 5000–5001 | Out of memory, unexpected internal failure (reserved for boundary adapters) |

The corresponding `IPROP_REASON_*` constants in `iprop.h` name each category.
Stages are `NONE=0`, `LOAD=1` (container/decode/payload parsing), `VALIDATE=2`
(static bytecode/signature/part-graph checks), `ARGUMENTS=3` (host invocation
checks before execution), and `EXECUTE=4` (VM instruction failure).
Status is a separate API policy. A legacy VM-construction `Trap`, such as an
invalid variant, retains `IPROP_E_TRAP` even though its typed stage is
`ARGUMENTS`. The metadata foundation does not reclassify old calls.

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
metadata. This foundation does not decide future entry points' status policy.

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
