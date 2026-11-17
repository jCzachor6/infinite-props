/* The sized descriptors of the model API at every capacity: an initializer
 * and a query write only complete fields and preserve everything else, and
 * a failed query leaves its output unchanged. */
#include "iprop3d.h"
#include "check.h"

typedef struct { size_t offset, size; } field;
#define FIELD(type, name) {offsetof(type, name), sizeof(((type*)0)->name)}
#define NAME(type) {offsetof(type, name) + offsetof(iprop_utf8_view, data), sizeof(const char*)}, \
                   {offsetof(type, name) + offsetof(iprop_utf8_view, byte_count), sizeof(uint32_t)}
#define COUNT(table) (sizeof(table) / sizeof((table)[0]))

static const field library_fields[] = {
    FIELD(iprop3d_library_info, api_major), FIELD(iprop3d_library_info, api_minor),
    FIELD(iprop3d_library_info, max_vertices), FIELD(iprop3d_library_info, max_triangles),
    FIELD(iprop3d_library_info, max_textures), FIELD(iprop3d_library_info, max_texture_side),
    FIELD(iprop3d_library_info, max_texels)};
static const field model_fields[] = {
    FIELD(iprop3d_model_info, size_x), FIELD(iprop3d_model_info, size_y), FIELD(iprop3d_model_info, size_z),
    FIELD(iprop3d_model_info, units_per_meter), FIELD(iprop3d_model_info, fuel),
    FIELD(iprop3d_model_info, input_count), FIELD(iprop3d_model_info, skin_program_count),
    FIELD(iprop3d_model_info, container_flags)};
static const field skin_fields[] = {
    FIELD(iprop3d_skin_program_info, program_index), FIELD(iprop3d_skin_program_info, width),
    FIELD(iprop3d_skin_program_info, height), NAME(iprop3d_skin_program_info)};
static const field options_fields[] = {FIELD(iprop3d_generate_options, seed)};
static const field result_fields[] = {
    FIELD(iprop3d_result_info, bounds_min), FIELD(iprop3d_result_info, bounds_max), FIELD(iprop3d_result_info, size),
    FIELD(iprop3d_result_info, units_per_meter), FIELD(iprop3d_result_info, vertex_count),
    FIELD(iprop3d_result_info, triangle_count), FIELD(iprop3d_result_info, surface_count),
    FIELD(iprop3d_result_info, texture_count), FIELD(iprop3d_result_info, fuel_budget),
    FIELD(iprop3d_result_info, fuel_used), FIELD(iprop3d_result_info, work_budget),
    FIELD(iprop3d_result_info, work_used), FIELD(iprop3d_result_info, output_count)};
static const field output_fields[] = {
    FIELD(iprop3d_output_view, output_index), FIELD(iprop3d_output_view, kind), FIELD(iprop3d_output_view, list),
    FIELD(iprop3d_output_view, value_count), FIELD(iprop3d_output_view, floats_per_value),
    NAME(iprop3d_output_view), FIELD(iprop3d_output_view, values)};
static const field surface_fields[] = {
    FIELD(iprop3d_surface_view, surface_index), FIELD(iprop3d_surface_view, texture_index),
    FIELD(iprop3d_surface_view, roughness), FIELD(iprop3d_surface_view, metallic),
    FIELD(iprop3d_surface_view, emission), FIELD(iprop3d_surface_view, alpha_mode),
    FIELD(iprop3d_surface_view, alpha_cutoff), FIELD(iprop3d_surface_view, two_sided),
    FIELD(iprop3d_surface_view, crisp), FIELD(iprop3d_surface_view, vertex_count),
    FIELD(iprop3d_surface_view, index_count), FIELD(iprop3d_surface_view, positions),
    FIELD(iprop3d_surface_view, normals), FIELD(iprop3d_surface_view, tangents),
    FIELD(iprop3d_surface_view, uvs), FIELD(iprop3d_surface_view, indices)};
static const field texture_fields[] = {
    FIELD(iprop3d_texture_view, texture_index), FIELD(iprop3d_texture_view, repeats),
    FIELD(iprop3d_texture_view, key), NAME(iprop3d_texture_view),
    FIELD(iprop3d_texture_view, asset_width), FIELD(iprop3d_texture_view, asset_height),
    FIELD(iprop3d_texture_view, width), FIELD(iprop3d_texture_view, height),
    FIELD(iprop3d_texture_view, byte_count), FIELD(iprop3d_texture_view, row_stride),
    FIELD(iprop3d_texture_view, pixel_format), FIELD(iprop3d_texture_view, alpha_mode),
    FIELD(iprop3d_texture_view, origin), FIELD(iprop3d_texture_view, image_count),
    FIELD(iprop3d_texture_view, pixels)};
static const field image_fields[] = {
    FIELD(iprop_aux_image_view_ex, image_index), FIELD(iprop_aux_image_view_ex, kind),
    FIELD(iprop_aux_image_view_ex, source), FIELD(iprop_aux_image_view_ex, width),
    FIELD(iprop_aux_image_view_ex, height), FIELD(iprop_aux_image_view_ex, byte_count),
    FIELD(iprop_aux_image_view_ex, row_stride), FIELD(iprop_aux_image_view_ex, pixel_format),
    FIELD(iprop_aux_image_view_ex, alpha_mode), FIELD(iprop_aux_image_view_ex, origin),
    NAME(iprop_aux_image_view_ex), FIELD(iprop_aux_image_view_ex, pixels)};

/* What the queries below read. `fail` selects a call that must be rejected. */
static const iprop3d_model* model;
static const iprop3d_result* result;
static const iprop3d_result* posted; /* a result with outputs */

static iprop_status_ex init_library(void* out, uint32_t n) { return iprop3d_library_info_init((iprop3d_library_info*)out, n); }
static iprop_status_ex init_model(void* out, uint32_t n) { return iprop3d_model_info_init((iprop3d_model_info*)out, n); }
static iprop_status_ex init_skin(void* out, uint32_t n) { return iprop3d_skin_program_info_init((iprop3d_skin_program_info*)out, n); }
static iprop_status_ex init_options(void* out, uint32_t n) { return iprop3d_generate_options_init((iprop3d_generate_options*)out, n); }
static iprop_status_ex init_result(void* out, uint32_t n) { return iprop3d_result_info_init((iprop3d_result_info*)out, n); }
static iprop_status_ex init_surface(void* out, uint32_t n) { return iprop3d_surface_view_init((iprop3d_surface_view*)out, n); }
static iprop_status_ex init_texture(void* out, uint32_t n) { return iprop3d_texture_view_init((iprop3d_texture_view*)out, n); }
static iprop_status_ex init_image(void* out, uint32_t n) { return iprop_aux_image_view_ex_init((iprop_aux_image_view_ex*)out, n); }
static iprop_status_ex init_output(void* out, uint32_t n) { return iprop3d_output_view_init((iprop3d_output_view*)out, n); }

static iprop_status_ex query_library(void* out, int fail) {
    (void)fail;
    return iprop3d_library_query((iprop3d_library_info*)out, NULL);
}
static iprop_status_ex query_model(void* out, int fail) {
    return iprop3d_model_query(fail ? NULL : model, (iprop3d_model_info*)out, NULL);
}
static iprop_status_ex query_skin(void* out, int fail) {
    return fail == 1 ? iprop3d_model_skin_program(NULL, 0, (iprop3d_skin_program_info*)out, NULL)
                     : iprop3d_model_skin_program(model, fail ? 1 : 0, (iprop3d_skin_program_info*)out, NULL);
}
static iprop_status_ex query_result(void* out, int fail) {
    return iprop3d_result_query(fail ? NULL : result, (iprop3d_result_info*)out, NULL);
}
static iprop_status_ex query_surface(void* out, int fail) {
    return fail == 1 ? iprop3d_result_surface(NULL, 2, (iprop3d_surface_view*)out, NULL)
                     : iprop3d_result_surface(result, fail ? 4 : 2, (iprop3d_surface_view*)out, NULL);
}
static iprop_status_ex query_texture(void* out, int fail) {
    return fail == 1 ? iprop3d_result_texture(NULL, 0, (iprop3d_texture_view*)out, NULL)
                     : iprop3d_result_texture(result, fail ? 2 : 0, (iprop3d_texture_view*)out, NULL);
}
static iprop_status_ex query_output(void* out, int fail) {
    return fail == 1 ? iprop3d_result_output(NULL, 0, (iprop3d_output_view*)out, NULL)
                     : iprop3d_result_output(posted, fail ? 9 : 0, (iprop3d_output_view*)out, NULL);
}
static iprop_status_ex query_image(void* out, int fail) {
    return fail == 1 ? iprop3d_result_texture_image(NULL, 0, 0, (iprop_aux_image_view_ex*)out, NULL)
         : fail == 2 ? iprop3d_result_texture_image(result, 0, 1, (iprop_aux_image_view_ex*)out, NULL)
                     : iprop3d_result_texture_image(result, 0, 0, (iprop_aux_image_view_ex*)out, NULL);
}

static int inside(const field* fields, size_t count, uint32_t capacity, size_t byte) {
    size_t f;
    for (f = 0; f < count; ++f)
        if (fields[f].offset + fields[f].size <= capacity && byte >= fields[f].offset &&
            byte < fields[f].offset + fields[f].size) return 1;
    return 0;
}

/* `flags` is whether the descriptor has an input flags field at offset 4;
 * `query` is NULL for a descriptor that is only an input. */
static int prefixes(size_t size, const field* fields, size_t count, int flags,
                    iprop_status_ex (*init)(void*, uint32_t), iprop_status_ex (*query)(void*, int)) {
    unsigned char expected[160], before[176];
    uint32_t capacity;
    CHECK(size <= sizeof(expected));
    if (query != NULL) {
        memset(expected, 0, sizeof(expected));
        CHECK(init(expected, (uint32_t)size) == IPROP_EX_OK && query(expected, 0) == IPROP_EX_OK);
    }
    CHECK(init(NULL, (uint32_t)size) == IPROP_EX_E_ARGUMENT);
    for (capacity = 0; capacity <= size + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        size_t byte;
        int fail;
        CHECK(storage != NULL);

        /* The initializer: the size, then zeroes in complete known fields. */
        memset(storage, 0xa5, allocation);
        CHECK(init(storage, capacity) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            if (flags && capacity >= 8 && byte >= 4 && byte < 8) want = 0;
            if (inside(fields, count, capacity, byte)) want = 0;
            CHECK(storage[byte] == want);
        }
        if (query == NULL) { free(storage); continue; }

        /* The query: complete output fields, everything else as it was. */
        memset(storage, 0xa5, allocation);
        memcpy(storage, &capacity, 4);
        if (flags && capacity >= 8) memset(storage + 4, 0, 4);
        memcpy(before, storage, allocation);
        CHECK(query(storage, 0) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte)
            CHECK(storage[byte] == (inside(fields, count, capacity, byte) ? expected[byte] : before[byte]));

        /* Failures leave the output alone: a null handle, an index out of
         * range and, where the descriptor has them, nonzero flags. */
        memcpy(before, storage, allocation);
        for (fail = 1; fail <= 2; ++fail) {
            if (query == query_library) break;
            CHECK(query(storage, fail) == IPROP_EX_E_ARGUMENT);
            CHECK(memcmp(storage, before, allocation) == 0);
        }
        if (flags && capacity >= 8) {
            storage[4] = 1;
            memcpy(before, storage, allocation);
            CHECK(query(storage, 0) == IPROP_EX_E_ARGUMENT);
            CHECK(memcmp(storage, before, allocation) == 0);
        }
        free(storage);
    }
    return 0;
}

int main(int argc, char** argv) {
    char path[1024];
    uint8_t* bytes; size_t length;
    iprop3d_model* loaded = NULL;
    iprop3d_result *made = NULL, *zero = NULL, *prefix = NULL, *outputs_result = NULL;
    iprop3d_model* outputs_model = NULL;
    iprop3d_generate_options options;
    iprop3d_result_info a, b;
    struct { uint32_t struct_size, flags, seed, tail; } wide;
    uint32_t only_size = 4;
    CHECK(argc == 2);
    CHECK(snprintf(path, sizeof(path), "%s/crate.iprop3d", argv[1]) < (int)sizeof(path));
    CHECK(read_fixture(path, &bytes, &length) == 0);
    CHECK(iprop3d_model_load(bytes, (uint64_t)length, &loaded, NULL) == IPROP_EX_OK);
    free(bytes);
    CHECK(iprop3d_generate_options_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 9;
    CHECK(iprop3d_generate(loaded, &options, NULL, NULL, &made, NULL) == IPROP_EX_OK);
    model = loaded;
    result = made;

    CHECK(prefixes(sizeof(iprop3d_library_info), library_fields, COUNT(library_fields), 0, init_library, query_library) == 0);
    CHECK(prefixes(sizeof(iprop3d_model_info), model_fields, COUNT(model_fields), 1, init_model, query_model) == 0);
    CHECK(prefixes(sizeof(iprop3d_skin_program_info), skin_fields, COUNT(skin_fields), 1, init_skin, query_skin) == 0);
    CHECK(prefixes(sizeof(iprop3d_generate_options), options_fields, COUNT(options_fields), 1, init_options, NULL) == 0);
    CHECK(prefixes(sizeof(iprop3d_result_info), result_fields, COUNT(result_fields), 1, init_result, query_result) == 0);
    CHECK(prefixes(sizeof(iprop3d_surface_view), surface_fields, COUNT(surface_fields), 1, init_surface, query_surface) == 0);
    CHECK(prefixes(sizeof(iprop3d_texture_view), texture_fields, COUNT(texture_fields), 1, init_texture, query_texture) == 0);
    CHECK(prefixes(sizeof(iprop_aux_image_view_ex), image_fields, COUNT(image_fields), 1, init_image, query_image) == 0);
    /* 3D outputs (API 0.2). */
    CHECK(snprintf(path, sizeof(path), "%s/outputs.iprop3d", argv[1]) < (int)sizeof(path));
    CHECK(read_fixture(path, &bytes, &length) == 0);
    CHECK(iprop3d_model_load(bytes, (uint64_t)length, &outputs_model, NULL) == IPROP_EX_OK);
    free(bytes);
    CHECK(iprop3d_generate(outputs_model, &options, NULL, NULL, &outputs_result, NULL) == IPROP_EX_OK);
    posted = outputs_result;
    CHECK(prefixes(sizeof(iprop3d_output_view), output_fields, COUNT(output_fields), 1, init_output, query_output) == 0);
    iprop3d_result_free(outputs_result);
    iprop3d_model_free(outputs_model);
    CHECK(iprop3d_library_query(NULL, NULL) == IPROP_EX_E_ARGUMENT);

    /* Options as input: a four-byte prefix is seed zero, and an unknown tail
     * requests nothing. Nonzero flags and a size below four are rejected. */
    options.seed = 0;
    CHECK(iprop3d_generate(loaded, &options, NULL, NULL, &zero, NULL) == IPROP_EX_OK);
    CHECK(iprop3d_generate(loaded, (const iprop3d_generate_options*)(const void*)&only_size, NULL, NULL, &prefix, NULL) == IPROP_EX_OK);
    /* The tail padding of the info is the caller's: zero it to compare. */
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    CHECK(iprop3d_result_info_init(&a, sizeof(a)) == IPROP_EX_OK && iprop3d_result_info_init(&b, sizeof(b)) == IPROP_EX_OK);
    CHECK(iprop3d_result_query(zero, &a, NULL) == IPROP_EX_OK && iprop3d_result_query(prefix, &b, NULL) == IPROP_EX_OK);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    iprop3d_result_free(prefix); prefix = NULL;
    wide.struct_size = sizeof(wide); wide.flags = 0; wide.seed = 0; wide.tail = 0xdeadbeefu;
    CHECK(iprop3d_generate(loaded, (const iprop3d_generate_options*)(const void*)&wide, NULL, NULL, &prefix, NULL) == IPROP_EX_OK);
    CHECK(iprop3d_result_query(prefix, &b, NULL) == IPROP_EX_OK && memcmp(&a, &b, sizeof(a)) == 0);
    iprop3d_result_free(prefix); prefix = NULL;
    options.flags = 1;
    CHECK(iprop3d_generate(loaded, &options, NULL, NULL, &prefix, NULL) == IPROP_EX_E_ARGUMENT && prefix == NULL);
    options.flags = 0; options.struct_size = 3;
    CHECK(iprop3d_generate(loaded, &options, NULL, NULL, &prefix, NULL) == IPROP_EX_E_ARGUMENT && prefix == NULL);
    /* A complete flags field is read even when the seed is cut off. */
    options.struct_size = 8; options.flags = 2;
    CHECK(iprop3d_generate(loaded, &options, NULL, NULL, &prefix, NULL) == IPROP_EX_E_ARGUMENT && prefix == NULL);

    iprop3d_result_free(made); iprop3d_result_free(zero);
    iprop3d_model_free(loaded);
    puts("capi3d descriptors: OK");
    return 0;
}
