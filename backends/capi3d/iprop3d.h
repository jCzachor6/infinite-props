/*
 * iprop3d.h — the C interface to Infinite Props models (preview).
 *
 * A model is a compiled `.iprop3d` file (spec/models-v1.2.md): one seed and
 * its inputs give triangles and the textures they wear. This header adds
 * models to the C API of iprop.h and uses that header's status codes,
 * structured errors, sized descriptors, input descriptors, parameter sets,
 * execution limits and image tags unchanged.
 *
 * PREVIEW. API 0.2 follows the models contract, which is a preview until it
 * is frozen; 0.2 adds the 3D outputs of a result. Until then a release may change any declaration here, and no
 * ABI is promised: the library is static only (`iprop3dc`) and a client is
 * built together with it. iprop.h and `ipropc` are unaffected.
 *
 * Geometry is right-handed with +Y up and +Z toward the viewer; triangles
 * are counterclockwise seen from outside. Positions are in model units;
 * units_per_meter converts them. UV (0, 0) is the upper left corner of the
 * texture image.
 *
 * Ownership and threading follow iprop.h: everything the host receives is
 * owned by an opaque handle and valid until that handle is freed, handles
 * are not thread-safe, and there is no global state.
 */

#ifndef IPROP3D_H
#define IPROP3D_H

#include "iprop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The version of this header, independent of IPROP_API_VERSION_*. */
#define IPROP3D_API_VERSION_MAJOR 0
#define IPROP3D_API_VERSION_MINOR 2

/* Opaque handles. */
typedef struct iprop3d_model iprop3d_model;   /* a loaded, validated .iprop3d */
typedef struct iprop3d_result iprop3d_result; /* one generation */

/* Every descriptor below follows the sized policy of iprop.h: struct_size is
 * the caller's actual capacity, at least four bytes; a `flags` field is an
 * input that must be zero (absent means zero); only complete fields within
 * the capacity are written; size, flags, padding, partial fields and unknown
 * tail are preserved; a failed call leaves its output unchanged. An
 * initializer takes the capacity, writes it to struct_size and zeroes the
 * complete known fields. Optional error output follows the checked policy:
 * it is initialized on every completed call and may not overlap an output
 * or a handle. Required handles and outputs are non-null; free accepts NULL. */

/* The loaded library: this API's version and the limits of one generation
 * (spec models v1.2). Only struct_size is input. */
typedef struct {
    uint32_t struct_size;
    uint32_t api_major, api_minor;
    uint32_t max_vertices, max_triangles;
    uint32_t max_textures, max_texture_side;
    uint64_t max_texels; /* over all textures of one generation */
} iprop3d_library_info;

IPROP_API iprop_status_ex iprop3d_library_info_init(iprop3d_library_info* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_library_query(iprop3d_library_info* out, iprop_error_ex* error);

/* ---- Models ---------------------------------------------------------------- */

/* Loads and validates once, the model program and every embedded skin
 * program. byte_count must fit the platform's size_t; bytes must be
 * non-null. The model owns its loaded copy. Success publishes exactly one
 * new handle and does not free an old value; failure leaves *out_model
 * untouched. A 2D `.iprop` file fails with E_FORMAT and BAD_MAGIC, as an
 * `.iprop3d` file does in iprop_program_load_ex. */
IPROP_API iprop_status_ex iprop3d_model_load(const uint8_t* bytes, uint64_t byte_count,
                                             iprop3d_model** out_model, iprop_error_ex* error);
IPROP_API void iprop3d_model_free(iprop3d_model* model);

/* size_* is the nominal size the source declares, in model units: width,
 * height and depth. A generated model need not fill it and may exceed it;
 * the bounds of a result are exact. fuel is the budget of the model program;
 * each skin program runs under its own. container_flags describe the loaded
 * physical container, as in iprop_program_info_ex. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    float size_x, size_y, size_z;
    float units_per_meter;
    uint64_t fuel;
    uint32_t input_count;
    uint32_t skin_program_count;
    uint32_t container_flags;
} iprop3d_model_info;

IPROP_API iprop_status_ex iprop3d_model_info_init(iprop3d_model_info* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_model_query(const iprop3d_model* model, iprop3d_model_info* out,
                                              iprop_error_ex* error);

/* One embedded 2D program the model can generate as a texture, in the order
 * of the skins section. name is its import alias, exact byte-counted UTF-8
 * owned by the model until iprop3d_model_free; width and height are the
 * size it declares, in pixels. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t program_index;
    uint32_t width, height;
    iprop_utf8_view name;
} iprop3d_skin_program_info;

IPROP_API iprop_status_ex iprop3d_skin_program_info_init(iprop3d_skin_program_info* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_model_skin_program(const iprop3d_model* model, uint32_t program_index,
                                                     iprop3d_skin_program_info* out, iprop_error_ex* error);

/* The inputs of the model, with the descriptors and the rules of
 * iprop_program_input_count/get/find. Names belong to the model until
 * iprop3d_model_free. */
IPROP_API iprop_status_ex iprop3d_model_input_count(const iprop3d_model* model, uint32_t* out_count,
                                                    iprop_error_ex* error);
IPROP_API iprop_status_ex iprop3d_model_input_get(const iprop3d_model* model, uint32_t input_index,
                                                  iprop_input_info_ex* out, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop3d_model_input_find(const iprop3d_model* model, iprop_utf8_view name,
                                                   uint32_t* out_index, iprop_error_ex* error);

/* A parameter set for this model load, holding every input at its default.
 * It is an ordinary iprop_parameters: read and change it with
 * iprop_parameters_get/set and their typed, named and list forms, clone it
 * with iprop_parameters_clone and release it with iprop_parameters_free. It
 * stays usable after iprop3d_model_free. Success publishes one new handle;
 * failure leaves *out_parameters untouched. */
IPROP_API iprop_status_ex iprop3d_model_parameters_create(const iprop3d_model* model,
                                                          iprop_parameters** out_parameters,
                                                          iprop_error_ex* error);

/* ---- Generation ------------------------------------------------------------ */

/* A four-byte prefix suffices; absent fields are zero. flags must be zero. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t seed;
} iprop3d_generate_options;

IPROP_API iprop_status_ex iprop3d_generate_options_init(iprop3d_generate_options* out, uint32_t capacity);

/* Generates one model. model, options and out_result are required.
 * parameters is optional: NULL selects the defaults, and a set must come
 * from this model load (FOREIGN_PARAMETERS otherwise, even for identical
 * bytes). limits is optional and follows iprop_execution_limits_ex: one work
 * budget, deadline and cancellation cover the model program and every
 * texture it generates.
 * Invalid options, limits and foreign parameters fail as E_ARGUMENT at
 * ARGUMENTS stage before anything runs. A violation while generating is
 * E_TRAP at EXECUTE stage, located in the model program; one inside a
 * texture names its skin program in the message and carries that program's
 * reason, located at the instruction of the model that asked for the
 * texture. Success publishes exactly one new, independent result and does
 * not free a previous pointer in out_result; failure leaves out_result
 * untouched and publishes nothing. The result survives iprop3d_model_free,
 * later parameter changes and later generations. Error storage cannot
 * overlap out_result, options, limits, the model or parameter storage. */
IPROP_API iprop_status_ex iprop3d_generate(const iprop3d_model* model,
                                           const iprop3d_generate_options* options,
                                           const iprop_parameters* parameters,
                                           const iprop_execution_limits_ex* limits,
                                           iprop3d_result** out_result, iprop_error_ex* error);
IPROP_API void iprop3d_result_free(iprop3d_result* result);

/* ---- Results --------------------------------------------------------------- */

/* bounds_* enclose every generated position, x, y, z in model units; both
 * are zero for a model without triangles. size and units_per_meter repeat
 * the model's. Counts cover all surfaces. fuel_* count the model program
 * only; work_* count the model and all its textures. output_count (API 0.2)
 * counts the model's own outputs. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    float bounds_min[3];
    float bounds_max[3];
    float size[3];
    float units_per_meter;
    uint32_t vertex_count, triangle_count;
    uint32_t surface_count, texture_count;
    uint64_t fuel_budget, fuel_used;
    uint64_t work_budget, work_used;
    uint32_t output_count;
} iprop3d_result_info;

IPROP_API iprop_status_ex iprop3d_result_info_init(iprop3d_result_info* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_result_query(const iprop3d_result* result, iprop3d_result_info* out,
                                               iprop_error_ex* error);

typedef uint32_t iprop3d_alpha_mode;
#define IPROP3D_ALPHA_OPAQUE UINT32_C(0) /* alpha is ignored */
#define IPROP3D_ALPHA_CUTOUT UINT32_C(1) /* a texel below alpha_cutoff is not drawn */
#define IPROP3D_ALPHA_BLEND UINT32_C(2)  /* mixed over what is behind it by alpha (glass) */

/* The triangles of one host material, in the order the model emitted them.
 * A surface is every triangle that shares a texture and material numbers;
 * surfaces are listed in order of first use. Arrays are borrowed from the
 * result until iprop3d_result_free and are NULL exactly when their count is
 * zero.
 *   positions  3 floats per vertex, model units
 *   normals    3 floats per vertex, unit length
 *   tangents   4 floats per vertex: the unit direction of growing u, then a
 *              sign w; w * cross(normal, tangent) points up the image
 *   uvs        2 floats per vertex; outside 0..1 where the texture repeats
 *   indices    index_count indices into this surface's vertices, three per
 *              triangle
 * roughness and metallic are 0-1 and emission is 0 or more. Each multiplies
 * its map of the texture's auxiliary images (material maps v1.2) when the
 * texture has one, and a skin then starts it at 1; without a map it stands
 * alone, and emission multiplies the texture's colour, 0 for none. An AO map
 * applies as it is. alpha_cutoff is 0-1 and applies in CUTOUT mode only;
 * an opaque surface reports the value its skin holds, 0.5 unless the model
 * set one. two_sided and crisp are 0 or 1: crisp asks for unfiltered
 * (nearest) sampling without mipmaps. All floats are exact binary32 bits. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t surface_index;
    uint32_t texture_index;
    float roughness, metallic, emission;
    iprop3d_alpha_mode alpha_mode;
    float alpha_cutoff;
    uint32_t two_sided;
    uint32_t crisp;
    uint32_t vertex_count;
    uint32_t index_count;
    const float* positions;
    const float* normals;
    const float* tangents;
    const float* uvs;
    const uint32_t* indices;
} iprop3d_surface_view;

IPROP_API iprop_status_ex iprop3d_surface_view_init(iprop3d_surface_view* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_result_surface(const iprop3d_result* result, uint32_t surface_index,
                                                 iprop3d_surface_view* out, iprop_error_ex* error);

/* One generated texture: the image of a skin program for one seed, size and
 * set of inputs. Surfaces that differ only in material numbers share it.
 * key identifies the pixels across results and models: equal keys mean
 * equal images, so a host may keep one GPU texture per key. name is the
 * skin program's import alias. asset_* is the size the program declares and
 * width/height the size generated. repeats is 1 when a surface samples the
 * texture beyond 0..1 and needs repeat wrapping, else 0 (clamp).
 * The pixel fields follow iprop_image_view_ex, with its alpha mode: the
 * image is what iprop_generate_ex gives for the same program, seed, size
 * and inputs. image_count is the number of auxiliary images: the normal map
 * and the material maps the program declares, in kind order. Pixels and the
 * name are borrowed from the result until iprop3d_result_free. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t texture_index;
    uint32_t repeats;
    uint64_t key;
    iprop_utf8_view name;
    uint32_t asset_width, asset_height;
    uint32_t width, height;
    uint64_t byte_count;
    uint64_t row_stride;
    iprop_pixel_format pixel_format;
    iprop_image_alpha_mode alpha_mode;
    iprop_image_origin origin;
    uint32_t image_count;
    const uint8_t* pixels;
} iprop3d_texture_view;

IPROP_API iprop_status_ex iprop3d_texture_view_init(iprop3d_texture_view* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_result_texture(const iprop3d_result* result, uint32_t texture_index,
                                                 iprop3d_texture_view* out, iprop_error_ex* error);

/* The auxiliary images of a texture, with the descriptor and the rules of
 * iprop_result_aux_image_get/find. A normal image stores tangent-space
 * directions for the tangents of the surfaces above (spec models v1.2); its
 * bytes are linear data. Roughness, metallic and AO maps are linear data
 * too; an emission map is a colour like the texture's (material maps v1.2).
 * Pixels and names are borrowed from the result. */
IPROP_API iprop_status_ex iprop3d_result_texture_image(const iprop3d_result* result, uint32_t texture_index,
                                                       uint32_t image_index, iprop_aux_image_view_ex* out,
                                                       iprop_error_ex* error);
IPROP_API iprop_status_ex iprop3d_result_texture_image_find(const iprop3d_result* result, uint32_t texture_index,
                                                            iprop_aux_image_kind kind, uint32_t* out_index,
                                                            iprop_error_ex* error);

/* ---- Outputs (API 0.2) ------------------------------------------------------ */

typedef uint32_t iprop3d_output_kind;
#define IPROP3D_OUTPUT_POINT3D UINT32_C(0) /* 3 floats: x, y, z */
#define IPROP3D_OUTPUT_BOX3D UINT32_C(1)   /* 15 floats: centre, size, x axis, y axis, z axis */

/* One output the model declares, in declaration order, with the values the
 * generation wrote: none for a singular output never written, one for a
 * written one, any number for a list. Values are in model units like
 * positions and lie where the model's blocks put them. A box is its centre,
 * its size along its own axes and those axes, three unit vectors forming a
 * rotation. values has value_count * floats_per_value floats and is NULL
 * exactly when value_count is zero; it and the name are borrowed from the
 * result until iprop3d_result_free. All floats are exact binary32 bits. */
typedef struct {
    uint32_t struct_size;
    uint32_t flags;
    uint32_t output_index;
    iprop3d_output_kind kind;
    uint32_t list;
    uint32_t value_count;
    uint32_t floats_per_value;
    iprop_utf8_view name;
    const float* values;
} iprop3d_output_view;

IPROP_API iprop_status_ex iprop3d_output_view_init(iprop3d_output_view* out, uint32_t capacity);
IPROP_API iprop_status_ex iprop3d_result_output(const iprop3d_result* result, uint32_t output_index,
                                                iprop3d_output_view* out, iprop_error_ex* error);
IPROP_API iprop_status_ex iprop3d_result_output_find(const iprop3d_result* result, iprop_utf8_view name,
                                                     uint32_t* out_index, iprop_error_ex* error);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IPROP3D_H */
