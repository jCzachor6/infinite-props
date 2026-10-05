# Structured error foundations

`iprop_error_ex` is an additive API 0.3 descriptor. It records a fixed-width
status, stable cause category, stage and optional location. Initialize it with
`iprop_error_ex_init(&error, sizeof(error))`: success, reason/stage `NONE`, and
no location. Like other sized initializers, it writes only complete known
fields; partial fields, padding and unknown tail bytes remain untouched.
It owns no memory and requires no free function.

This foundation does not add a structured load/generation/query entry point
or an error capability. The existing six C entry points keep their enums,
layouts, status policies and located text. Later entry points consume this
descriptor and define message-buffer and exception-boundary behavior.

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
