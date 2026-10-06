/*
 * iprop.h — the stable C interface to the infinite-props runtime.
 *
 * The C API is the shared foundation every native host (SFML, Godot,
 * tools) links against. It loads a validated `.iprop` binary, generates
 * one (seed, variant, archetype, size, uniforms) tuple and hands back the
 * RGBA8 pixels plus the named structured outputs. Everything the host
 * receives is owned by an opaque handle and valid until that handle is
 * freed; the host never allocates on the library's behalf.
 *
 * ABI and versioning rules: see backends/capi/README.md.
 *
 * The API is C99-compatible and header-only to consume. Thread safety:
 * handles are not thread-safe; there is no global state, so independent
 * handles may be used from different threads concurrently.
 */

#ifndef IPROP_H
#define IPROP_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(IPROP_CAPI_STATIC)
#if defined(IPROP_CAPI_BUILD)
#define IPROP_API __declspec(dllexport)
#else
#define IPROP_API __declspec(dllimport)
#endif
#else
#define IPROP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Header API feature version, independent of the product/file versions.
 * API 0.2 is frozen on ABI line 0.4: its declarations, enum values,
 * layouts and ownership cannot change in an additive API release.
 * Later APIs add symbols and separate sized descriptors; they do not
 * append fields to these structs. See docs/c-api-abi.md for the policy.
 * These macros describe this header, not a loaded library's capabilities. */
#define IPROP_API_VERSION_MAJOR 0
#define IPROP_API_VERSION_MINOR 3

/* Opaque handles. */
typedef struct iprop_program iprop_program; /* a loaded, validated .iprop */
typedef struct iprop_result iprop_result;   /* one generation's outputs */

typedef enum {
    IPROP_OK = 0,        /* success */
    IPROP_E_OOM = 1,     /* allocation failure */
    IPROP_E_FORMAT = 2,  /* the bytes are not a valid .iprop */
    IPROP_E_TRAP = 3,    /* a runtime trap (fuel, bad operand, ...) */
    IPROP_E_ARGUMENT = 4 /* a bad argument (null handle, null bytes, ...) */
} iprop_status;

/* The input types a uniform override may carry (the v0.1 format has no
 * i32 inputs). */
typedef enum {
    IPROP_UNIFORM_COLOR = 0,
    IPROP_UNIFORM_F32 = 1,
    IPROP_UNIFORM_U32 = 2,
} iprop_uniform_type;

/* One uniform override (spec §5.4). The name is the declared input name;
 * the value lives in the field matching `type`. */
typedef struct {
    const char* name; /* UTF-8, NUL-terminated */
    iprop_uniform_type type;
    float f;            /* the F32 value */
    uint32_t u;         /* the U32 value */
    uint8_t r, g, b, a; /* the colour value (RGBA8) */
} iprop_uniform;

typedef struct {
    uint32_t seed;
    uint32_t variant;
    uint32_t archetype;
    uint32_t size_x; /* 0 = the asset's meta size */
    uint32_t size_y; /* 0 = the asset's meta size */
    const iprop_uniform* uniforms;
    uint32_t uniform_count;
} iprop_generate_options;

/* The metadata of the loaded program (spec §4). */
typedef struct {
    uint32_t size_x;
    uint32_t size_y;
    float origin_x; /* asset units, y grows downward */
    float origin_y;
    uint32_t variants;
    uint32_t archetypes;
    uint64_t fuel;
} iprop_meta;

/* Structured output kinds (spec §5.4). */
typedef enum {
    IPROP_OUTPUT_POINT = 0,
    IPROP_OUTPUT_RECT = 1,
    IPROP_OUTPUT_CIRCLE = 2,
} iprop_output_kind;

/* One named structured output slot. `values` holds the packed floats:
 * two per point (x, y), four per rect (x, y, w, h), three per circle
 * (x, y, r); `count` values per slot (1 for singular slots, N for list
 * slots, in program order). `name` and `values` are borrowed from the
 * result and die with it. */
typedef struct {
    iprop_output_kind kind;
    int is_list;
    const char* name;
    uint32_t count;
    const float* values;
    uint32_t owner; /* 0 = root, N+1 = part N in the code part table */
} iprop_output;

/* The generated image plus the structured outputs. `pixels` is RGBA8,
 * row-major, top-left origin, width*height*4 bytes; `outputs` has
 * `output_count` entries in the global slot order (root outputs first,
 * then parts — spec §4.3). Both are borrowed from the result and die
 * with it. */
typedef struct {
    uint32_t width;
    uint32_t height;
    const uint8_t* pixels;
    uint32_t output_count;
    const iprop_output* outputs;
} iprop_image;

/* Loads and validates `bytes`. On failure returns the status and, when
 * `err`/`err_size` are non-null, copies a located message into `err`
 * (always NUL-terminated when err_size > 0). `out_program` is untouched
 * on failure. */
IPROP_API iprop_status iprop_program_load(const uint8_t* bytes, size_t length,
                                iprop_program** out_program,
                                char* err, size_t err_size);

/* Frees a program. NULL is accepted. Any result generated from the
 * program stays valid (the result owns its data). */
IPROP_API void iprop_program_free(iprop_program* program);

/* Copies the program's metadata. */
IPROP_API void iprop_program_meta(const iprop_program* program, iprop_meta* out);

/* Generates one image + the structured outputs for `options`. The
 * program must stay alive for the call; the returned result is
 * independent of it. On failure returns the status and, when
 * `err`/`err_size` are non-null, copies the message. */
IPROP_API iprop_status iprop_generate(const iprop_program* program,
                            const iprop_generate_options* options,
                            iprop_result** out_result,
                            char* err, size_t err_size);

/* Frees a result. NULL is accepted. */
IPROP_API void iprop_result_free(iprop_result* result);

/* Copies the result's image view (width/height/pixels/outputs). */
IPROP_API void iprop_result_image(const iprop_result* result, iprop_image* out);

/* Additive API 0.3. Fixed-width statuses leave the legacy enum unchanged. */
typedef uint32_t iprop_status_ex;
#define IPROP_EX_OK UINT32_C(0)
#define IPROP_EX_E_OOM UINT32_C(1)
#define IPROP_EX_E_FORMAT UINT32_C(2)
#define IPROP_EX_E_TRAP UINT32_C(3)
#define IPROP_EX_E_ARGUMENT UINT32_C(4)
#define IPROP_EX_E_INTERNAL UINT32_C(5)

/* Fixed leaf record for byte-counted UTF-8, never an extensible descriptor.
 * No NUL terminator is required; embedded U+0000 remains part of the view.
 * NULL is valid exactly when byte_count is zero. The caller promises that
 * byte_count bytes are readable. Input views are borrowed for the call only;
 * output views belong to the handle documented by their containing query.
 * Do not free data or append fields to this record. */
typedef struct {
    const char* data;
    uint32_t byte_count;
} iprop_utf8_view;

/* New-handle/view ownership, nullability and transaction rules are specified
 * in docs/c-api-contracts.md. Handles are opaque and free(NULL) is valid.
 * Input buffers/descriptors must remain readable for the call; returned
 * borrowed views expire with their documented owner. Failed calls do not
 * replace output handles or publish partial views (error output excepted).
 * struct_size is actual allocated capacity, including at least four readable
 * bytes. A library cannot verify a caller's allocation or dangling pointer. */

#define IPROP_CAP_LIBRARY_QUERY (UINT64_C(1) << 0)
#define IPROP_CAP_SIZED_DESCRIPTORS (UINT64_C(1) << 1)
#define IPROP_CAP_PROGRAM_LOAD_EX (UINT64_C(1) << 2)
#define IPROP_CAP_PROGRAM_QUERY (UINT64_C(1) << 3)
#define IPROP_CAP_STRUCTURED_ERRORS (UINT64_C(1) << 4)
#define IPROP_CAP_LIBRARY_QUERY_EX (UINT64_C(1) << 5)
#define IPROP_CAP_ROOT_INPUT_REFLECTION (UINT64_C(1) << 6)
#define IPROP_CAP_OUTPUT_DECLARATIONS (UINT64_C(1) << 7)
#define IPROP_CAP_PART_REFLECTION (UINT64_C(1) << 8)

/* All fields except struct_size are output-only. Set struct_size to the
 * actual caller capacity, at least sizeof(uint32_t). Only complete fields
 * within that capacity are written; unknown tail bytes are untouched.
 * Numbers describe the loaded library, independently of this header.
 * Formats are the inclusive [format_minor_min, format_minor_max] range
 * under format_major. Capability bits advertise implemented behavior only. */
typedef struct {
    uint32_t struct_size;
    uint32_t product_major, product_minor, product_patch;
    uint32_t api_major, api_minor;
    uint32_t abi_major, abi_minor;
    uint32_t format_major, format_minor_min, format_minor_max;
    uint64_t capabilities;
} iprop_library_info_ex;

IPROP_API iprop_status_ex iprop_library_query(iprop_library_info_ex* out);

/* Initializers also take the caller capacity, rather than the library's
 * sizeof. They set struct_size and defaults for complete known fields;
 * unknown tail/padding and incomplete trailing fields remain untouched.
 * NULL or capacity < 4 fails without a write. */
IPROP_API iprop_status_ex iprop_library_info_ex_init(iprop_library_info_ex* out,
                                                   uint32_t capacity);

/* New generation options, separate from the frozen API 0.2 structure.
 * All absent optional fields default to zero: current seed/index defaults,
 * source dimensions (size_x/size_y == 0), no requested flags. No flags are
 * supported yet; any nonzero complete flags field is rejected by consumers.
 * Future requests must negotiate a capability and set a recognized flag;
 * appending a nonzero unknown tail alone never requests behavior. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t seed, variant, archetype;
    uint32_t size_x, size_y;
} iprop_generate_options_ex;

IPROP_API iprop_status_ex iprop_generate_options_ex_init(iprop_generate_options_ex* out,
                                                       uint32_t capacity);

/* Structured error foundations. Codes identify stable cause categories; they
 * never depend on message wording. New operations consume this descriptor as
 * they are implemented; this declaration does not advertise load/generate_ex.
 * Fields are output-only except struct_size and the message buffer/capacity. */
typedef uint32_t iprop_error_reason;
#define IPROP_REASON_NONE UINT32_C(0)
#define IPROP_REASON_INVALID_FORMAT UINT32_C(1000)
#define IPROP_REASON_TRUNCATED_INPUT UINT32_C(1001)
#define IPROP_REASON_BAD_MAGIC UINT32_C(1002)
#define IPROP_REASON_UNSUPPORTED_VERSION UINT32_C(1003)
#define IPROP_REASON_INVALID_HEADER UINT32_C(1004)
#define IPROP_REASON_COMPRESSION UINT32_C(1005)
#define IPROP_REASON_INVALID_LAYOUT UINT32_C(1006)
#define IPROP_REASON_INVALID_CONSTANTS UINT32_C(1007)
#define IPROP_REASON_INVALID_METADATA UINT32_C(1008)
#define IPROP_REASON_INVALID_CODE UINT32_C(1009)
#define IPROP_REASON_INVALID_DEBUG UINT32_C(1010)
#define IPROP_REASON_INVALID_INSTRUCTION UINT32_C(2000)
#define IPROP_REASON_INVALID_SIGNATURE UINT32_C(2001)
#define IPROP_REASON_PART_CYCLE UINT32_C(2002)
#define IPROP_REASON_RUNTIME_VIOLATION UINT32_C(3000)
#define IPROP_REASON_FUEL_EXHAUSTED UINT32_C(3001)
#define IPROP_REASON_STACK_UNDERFLOW UINT32_C(3002)
#define IPROP_REASON_STACK_TYPE UINT32_C(3003)
#define IPROP_REASON_STACK_LIMIT UINT32_C(3004)
#define IPROP_REASON_CALL_DEPTH UINT32_C(3005)
#define IPROP_REASON_INDEX_RANGE UINT32_C(3006)
#define IPROP_REASON_INVALID_CONTROL UINT32_C(3007)
#define IPROP_REASON_DIVIDE_BY_ZERO UINT32_C(3008)
#define IPROP_REASON_INTEGER_OVERFLOW UINT32_C(3009)
#define IPROP_REASON_NUMERIC_RANGE UINT32_C(3010)
#define IPROP_REASON_INVALID_OUTPUT UINT32_C(3011)
#define IPROP_REASON_PART_OUTPUT_MISSING UINT32_C(3012)
#define IPROP_REASON_INVALID_PART UINT32_C(3013)
#define IPROP_REASON_STREAM_UNDERFLOW UINT32_C(3014)
#define IPROP_REASON_INVALID_ARGUMENT UINT32_C(4000)
#define IPROP_REASON_VARIANT_RANGE UINT32_C(4001)
#define IPROP_REASON_ARCHETYPE_RANGE UINT32_C(4002)
#define IPROP_REASON_CANVAS_SIZE UINT32_C(4003)
#define IPROP_REASON_UNIFORM_TYPE UINT32_C(4004)
#define IPROP_REASON_OUT_OF_MEMORY UINT32_C(5000)
#define IPROP_REASON_INTERNAL UINT32_C(5001)

typedef uint32_t iprop_error_stage;
#define IPROP_STAGE_NONE UINT32_C(0)
#define IPROP_STAGE_LOAD UINT32_C(1)
#define IPROP_STAGE_VALIDATE UINT32_C(2)
#define IPROP_STAGE_ARGUMENTS UINT32_C(3)
#define IPROP_STAGE_EXECUTE UINT32_C(4)

#define IPROP_LOCATION_BYTE_OFFSET (UINT32_C(1) << 0)
#define IPROP_LOCATION_FUNCTION (UINT32_C(1) << 1)
#define IPROP_LOCATION_PC (UINT32_C(1) << 2)
#define IPROP_LOCATION_DEBUG_LINE (UINT32_C(1) << 3)
#define IPROP_OFFSET_NONE UINT32_C(0)
#define IPROP_OFFSET_EXPANDED_FILE UINT32_C(1)
#define IPROP_OFFSET_CONTAINER UINT32_C(2)

/* location_flags is authoritative: a zero numeric value may be a real file
 * offset, function or PC. Absent fields are zero with their flag cleared.
 * byte_offset is absolute in offset_space: parsed payload/VM errors use the
 * expanded file, container/header/decompression errors identify the physical
 * input. PC is a zero-based instruction index within function_index (a code
 * table index). debug_line is one-based and present only with a usable map.
 * No filename or column is claimed by this binary's debug data.
 * Prefix writers clear presence bits whose complete values do not fit. */
typedef struct {
    uint32_t struct_size;
    iprop_status_ex status;
    iprop_error_reason reason;
    iprop_error_stage stage;
    uint64_t byte_offset;
    uint32_t location_flags;
    uint32_t offset_space;
    uint32_t function_index;
    uint32_t pc;
    uint32_t debug_line;
    /* Optional caller-owned writable buffer, borrowed for this call only.
     * NULL is valid exactly when message_capacity is zero. A non-null buffer
     * with zero capacity is also valid; neither case reads/writes the buffer.
     * Buffer storage must not overlap this descriptor. Absent fields default
     * to NULL/zero. Reporting preserves these input fields and struct_size. */
    char* message_buffer;
    uint32_t message_capacity;
    /* Required text bytes, excluding the terminating NUL; allocate +1 for
     * the full message. Capacity > 0 always produces NUL. Truncated is 1 iff
     * some message bytes were omitted, including length-only queries.
     * Success clears text (when writable), required length and truncation.
     * New text truncation keeps complete UTF-8 characters when text is UTF-8. */
    uint64_t message_required_bytes;
    uint32_t message_truncated;
} iprop_error_ex;

/* Initializes success/none and absent locations using bounded field writes.
 * A NULL descriptor or capacity < 4 fails without a write. Unknown tail,
 * padding and incomplete fields are untouched, like the other initializers. */
IPROP_API iprop_status_ex iprop_error_ex_init(iprop_error_ex* out, uint32_t capacity);

/* Checked API 0.3 operations share the structured status/message policy.
 * Optional error output is initialized on every completed call, including
 * success. Its descriptor/message storage must not overlap out_program or a
 * query output, or its own message buffer overlap its descriptor. Overlap is
 * rejected as E_ARGUMENT without modifying either output. NULL error is valid.
 * Malformed error storage stops the operation before any handle/view write. */

/* Loads once and validates once. byte_count is fixed-width and must fit the
 * platform's size_t. bytes must be non-null, even when byte_count is zero;
 * non-null empty input is a format error. The caller provides readable bytes
 * for the call; the program owns its loaded copy. out_program is required.
 * Success publishes exactly one new handle and does not free an old value;
 * failure leaves the caller's existing pointer untouched. */
IPROP_API iprop_status_ex iprop_program_load_ex(const uint8_t* bytes, uint64_t byte_count,
                                              iprop_program** out_program, iprop_error_ex* error);

/* Program metadata query, separate from frozen iprop_meta. Only struct_size
 * is input; minimum capacity is four bytes. Success writes complete known
 * fields, preserving size, partial fields, padding and unknown tail. Failure
 * leaves the output untouched. This same descriptor will gain additive fields
 * for emission/format information when that capability is implemented. */
typedef struct {
    uint32_t struct_size;
    uint32_t size_x, size_y;
    float origin_x, origin_y;
    uint32_t variants, archetypes;
    uint64_t fuel;
} iprop_program_info_ex;

IPROP_API iprop_status_ex iprop_program_info_ex_init(iprop_program_info_ex* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop_program_query(const iprop_program* program,
                                            iprop_program_info_ex* out, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop_library_query_ex(iprop_library_info_ex* out, iprop_error_ex* error);

/* Binary input types, independent of the legacy ABI-sized enum and any
 * source-language aliases. Source i32 declarations are stored/reported U32;
 * their default is the unchanged uint32_t bit pattern. No signed binary tag
 * is introduced. These fixed-width values match the binary metadata tags. */
typedef uint32_t iprop_input_type;
#define IPROP_INPUT_COLOR UINT32_C(0)
#define IPROP_INPUT_F32 UINT32_C(1)
#define IPROP_INPUT_U32 UINT32_C(2)

/* Frozen leaf RGBA8 record. All four channels are exact default bytes. */
typedef struct {
    uint8_t r, g, b, a;
} iprop_color_rgba8;

/* Input declaration, in the selected root/part collection's stored order.
 * struct_size and flags are input;
 * minimum capacity is four bytes and absent flags mean zero. No flags are
 * supported; a nonzero complete flags field is E_ARGUMENT. Other complete
 * fields are outputs. Queries preserve size/flags, padding, partial fields
 * and unknown tail. name.data and name.byte_count are bounded independently;
 * inspect both only when both fields fit. The UTF-8 view includes embedded
 * NUL bytes and belongs to the program until program_free. input_index is
 * local to the selected collection of this load. Part-input indices identify
 * part signature entries; root parameters use root-input indices. Resolve
 * indices again for each load/compilation. The matching
 * default field is exact (including f32 bits); inactive defaults are zero. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t input_index;
    iprop_input_type type;
    iprop_utf8_view name;
    iprop_color_rgba8 color_default;
    float f32_default;
    uint32_t u32_default;
} iprop_input_info_ex;

IPROP_API iprop_status_ex iprop_input_info_ex_init(iprop_input_info_ex* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop_program_input_count(const iprop_program* program,
                                                   uint32_t* out_count, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop_program_input_get(const iprop_program* program, uint32_t input_index,
                                                 iprop_input_info_ex* out, iprop_error_ex* error);
/* Exact byte-counted UTF-8 match, without normalization or sentinel reads.
 * NULL/zero names are valid empty strings; NULL/nonzero or malformed UTF-8,
 * an unknown name, out-of-range index or null required output is E_ARGUMENT.
 * Count/find/get leave their output unchanged on failure and accept NULL error.
 * Error/output storage follows the same checked overlap policy above. */
IPROP_API iprop_status_ex iprop_program_input_find(const iprop_program* program, iprop_utf8_view name,
                                                  uint32_t* out_index, iprop_error_ex* error);

/* Fixed-width declaration tags, separate from the frozen legacy enum. */
typedef uint32_t iprop_output_kind_ex;
#define IPROP_OUTPUT_KIND_POINT UINT32_C(0)
#define IPROP_OUTPUT_KIND_RECT UINT32_C(1)
#define IPROP_OUTPUT_KIND_CIRCLE UINT32_C(2)

/* Output schema available before generation, including unassigned singular
 * slots and empty lists. Global output_index follows stored root declarations,
 * then each stored part's declarations; it is local to this loaded program.
 * owner 0 is root; owner N+1 is part N, including parts with no outputs.
 * is_list is exactly 0 (singular) or 1 (list). Names may repeat under different
 * owners. name is an exact UTF-8 view borrowed until program_free.
 * struct_size/flags have the same bounded input/output policy as input info:
 * minimum four bytes, absent flags zero, any nonzero complete flags rejected;
 * size/flags, padding, partial fields and unknown tails are preserved. The
 * name pointer/byte_count are bounded separately; inspect both only when both
 * fit. This is a declaration, not a generated-value/result view. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t output_index;
    uint32_t owner;
    iprop_output_kind_ex kind;
    uint32_t is_list;
    iprop_utf8_view name;
} iprop_output_info_ex;

IPROP_API iprop_status_ex iprop_output_info_ex_init(iprop_output_info_ex* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop_program_output_count(const iprop_program* program,
                                                    uint32_t* out_count, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop_program_output_get(const iprop_program* program, uint32_t output_index,
                                                  iprop_output_info_ex* out, iprop_error_ex* error);
/* Lookup is exact by (owner, UTF-8 name); it never chooses a different owner
 * with the same name. Invalid owner/index/name/flags/null arguments return
 * E_ARGUMENT. Outputs are preserved on failure; optional errors and aliases
 * follow the same checked policy as input reflection. */
IPROP_API iprop_status_ex iprop_program_output_find(const iprop_program* program, uint32_t owner,
                                                   iprop_utf8_view name, uint32_t* out_index,
                                                   iprop_error_ex* error);

/* Stored part table. part_index N maps to owner N+1, including empty parts.
 * input_count describes the part's own input signature; output_begin/count
 * select its contiguous global output range for program_output_get, and
 * program_output_find uses this owner. An empty part may have the same
 * output_begin as the following part while keeping its own owner.
 * Current binary formats do not store a part name: name_present=0, name=NULL/0.
 * A stored function name is not a part/import name. Future stored empty names
 * may be present with byte_count=0; name_present distinguishes availability.
 * Fields except size/flags are outputs, with the existing sized/flag/padding
 * policy. Name pointer/count are bounded separately. Names, if available,
 * would be immutable program-owned UTF-8 views until program_free. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t part_index;
    uint32_t owner;
    uint32_t size_x, size_y;
    float origin_x, origin_y;
    uint32_t input_count;
    uint32_t output_begin;
    uint32_t output_count;
    uint32_t name_present;
    iprop_utf8_view name;
} iprop_part_info_ex;

IPROP_API iprop_status_ex iprop_part_info_ex_init(iprop_part_info_ex* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop_program_part_count(const iprop_program* program,
                                                 uint32_t* out_count, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop_program_part_get(const iprop_program* program, uint32_t part_index,
                                               iprop_part_info_ex* out, iprop_error_ex* error);
/* Signature reflection only: no host part-input override is introduced.
 * Input indices and find results are local to the selected part signature,
 * and cannot identify a root parameter. All types/defaults/name lengths use
 * the same input declaration record/writer as root inputs. Invalid arguments,
 * optional bounded errors and output preservation follow the checked policy. */
IPROP_API iprop_status_ex iprop_program_part_input_count(const iprop_program* program, uint32_t part_index,
                                                       uint32_t* out_count, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop_program_part_input_get(const iprop_program* program, uint32_t part_index,
                                                     uint32_t input_index, iprop_input_info_ex* out,
                                                     iprop_error_ex* error);
IPROP_API iprop_status_ex iprop_program_part_input_find(const iprop_program* program, uint32_t part_index,
                                                      iprop_utf8_view name, uint32_t* out_index,
                                                      iprop_error_ex* error);

/* generate_ex is planned for the validated-parameters stage; it has no symbol
 * or capability yet. Its options use iprop_generate_options_ex, with absent
 * scalar fields defaulting to zero and unknown flags rejected. It will accept
 * a compatible parameter handle (NULL means defaults), preserve old output
 * handles on failure, and use this same checked error/operation boundary. */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IPROP_H */
