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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IPROP_H */
