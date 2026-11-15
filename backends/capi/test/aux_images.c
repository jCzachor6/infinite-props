#include "iprop.h"
#include "check.h"

/* API 0.7 layouts on LP64, including every public field offset. */
#define LAYOUT(type, size) typedef char layout_##type[(sizeof(type) == (size)) ? 1 : -1]
#define OFFSET(type, field, offset) typedef char offset_##type##_##field[(offsetof(type, field) == (offset)) ? 1 : -1]
LAYOUT(iprop_aux_image_info_ex, 32); LAYOUT(iprop_aux_image_view_ex, 88); LAYOUT(iprop_derived_normal_ex, 20);
OFFSET(iprop_aux_image_info_ex, flags, 4); OFFSET(iprop_aux_image_info_ex, image_index, 8);
OFFSET(iprop_aux_image_info_ex, kind, 12); OFFSET(iprop_aux_image_info_ex, name, 16);
OFFSET(iprop_aux_image_view_ex, image_index, 8); OFFSET(iprop_aux_image_view_ex, kind, 12);
OFFSET(iprop_aux_image_view_ex, source, 16); OFFSET(iprop_aux_image_view_ex, width, 20);
OFFSET(iprop_aux_image_view_ex, height, 24); OFFSET(iprop_aux_image_view_ex, byte_count, 32);
OFFSET(iprop_aux_image_view_ex, row_stride, 40); OFFSET(iprop_aux_image_view_ex, pixel_format, 48);
OFFSET(iprop_aux_image_view_ex, alpha_mode, 52); OFFSET(iprop_aux_image_view_ex, origin, 56);
OFFSET(iprop_aux_image_view_ex, name, 64); OFFSET(iprop_aux_image_view_ex, pixels, 80);
OFFSET(iprop_derived_normal_ex, flags, 4); OFFSET(iprop_derived_normal_ex, depth, 8);
OFFSET(iprop_derived_normal_ex, rounding, 12); OFFSET(iprop_derived_normal_ex, detail, 16);

#define FIELD(name) {offsetof(iprop_aux_image_view_ex, name), sizeof(((iprop_aux_image_view_ex*)0)->name)}
typedef struct { size_t offset, size; } field;
static const field outputs[] = {FIELD(image_index), FIELD(kind), FIELD(source), FIELD(width), FIELD(height),
    FIELD(byte_count), FIELD(row_stride), FIELD(pixel_format), FIELD(alpha_mode), FIELD(origin),
    {offsetof(iprop_aux_image_view_ex, name) + offsetof(iprop_utf8_view, data), sizeof(const char*)},
    {offsetof(iprop_aux_image_view_ex, name) + offsetof(iprop_utf8_view, byte_count), sizeof(uint32_t)},
    FIELD(pixels)};

static int same_name(iprop_utf8_view name, const char* expected) {
    return name.byte_count == strlen(expected) && memcmp(name.data, expected, name.byte_count) == 0;
}

/* Every capacity: only complete fields are written, the rest is preserved. */
static int prefixes(const iprop_result* result, const iprop_aux_image_view_ex* expected) {
    uint32_t capacity;
    for (capacity = 0; capacity <= sizeof(*expected) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(*expected) + 16];
        size_t byte, f;
        CHECK(storage != NULL);
        memset(storage, 0xa5, allocation);
        CHECK(iprop_aux_image_view_ex_init((iprop_aux_image_view_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            if (capacity >= 8 && byte >= 4 && byte < 8) want = 0;
            for (f = 0; f < sizeof(outputs)/sizeof(outputs[0]); ++f)
                if (outputs[f].offset + outputs[f].size <= capacity && byte >= outputs[f].offset &&
                    byte < outputs[f].offset + outputs[f].size) want = 0;
            CHECK(storage[byte] == want);
        }
        memset(storage, 0xa5, allocation);
        memcpy(storage, &capacity, 4);
        if (capacity >= 8) memset(storage + 4, 0, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_result_aux_image_get(result, 0, (iprop_aux_image_view_ex*)storage, NULL) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (f = 0; f < sizeof(outputs)/sizeof(outputs[0]); ++f)
                if (outputs[f].offset + outputs[f].size <= capacity && byte >= outputs[f].offset &&
                    byte < outputs[f].offset + outputs[f].size) want = ((const unsigned char*)expected)[byte];
            CHECK(storage[byte] == want);
        }
        memcpy(before, storage, allocation);
        CHECK(iprop_result_aux_image_get(NULL, 0, (iprop_aux_image_view_ex*)storage, NULL) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_result_aux_image_get(result, 1, (iprop_aux_image_view_ex*)storage, NULL) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(storage, before, allocation) == 0);
        free(storage);
    }
    return 0;
}

static int load(const char* path, iprop_program** program) {
    uint8_t* bytes; size_t length;
    CHECK(read_fixture(path, &bytes, &length) == 0);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, program, NULL) == IPROP_EX_OK);
    free(bytes);
    return 0;
}

int main(int argc, char** argv) {
    static const uint8_t flat[4] = {128, 128, 255, 255};
    static const uint8_t authored[] = {
        0x40,0xc0,0xff,0xff, 128,128,255,255, 128,128,255,255,
        128,128,255,255, 128,128,255,255, 80,176,191,255
    };
    iprop_library_info_ex library;
    iprop_program *relief = NULL, *plain = NULL;
    iprop_result *first = NULL, *second = NULL, *derived = NULL, *again = NULL, *none = NULL;
    iprop_generate_options_ex options;
    iprop_generate_options legacy = {0};
    iprop_derived_normal_ex request;
    iprop_aux_image_info_ex info, info_before;
    iprop_aux_image_view_ex view, saved, other, before;
    iprop_image_view_ex image;
    iprop_result_stats_ex stats;
    iprop_error_ex error;
    char message[160];
    uint32_t count = 99, index = 99, i;
    CHECK(argc == 3);
    CHECK(sizeof(iprop_aux_image_kind) == 4 && sizeof(iprop_aux_image_source) == 4);
    CHECK(IPROP_AUX_IMAGE_NORMAL == 1 && IPROP_AUX_IMAGE_AUTHORED == 1 && IPROP_AUX_IMAGE_DERIVED == 2);
    CHECK(IPROP_IMAGE_ALPHA_OPAQUE == 2 && IPROP_CAP_AUX_IMAGES == (UINT64_C(1) << 21) &&
          IPROP_CAP_DERIVED_NORMAL == (UINT64_C(1) << 22));
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query(&library) == IPROP_EX_OK && library.api_minor >= 7);
    CHECK((library.capabilities & IPROP_CAP_AUX_IMAGES) != 0 && (library.capabilities & IPROP_CAP_DERIVED_NORMAL) != 0);
    CHECK(load(argv[1], &relief) == 0 && load(argv[2], &plain) == 0);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);

    /* What a program declares. */
    CHECK(iprop_program_aux_image_count(relief, &count, NULL) == IPROP_EX_OK && count == 1);
    CHECK(iprop_program_aux_image_count(plain, &count, NULL) == IPROP_EX_OK && count == 0);
    CHECK(iprop_program_aux_image_count(NULL, &count, &error) == IPROP_EX_E_ARGUMENT && count == 0);
    CHECK(error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(iprop_program_aux_image_count(relief, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_aux_image_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(info.struct_size == sizeof(info) && info.flags == 0 && info.kind == 0 && info.name.data == NULL);
    CHECK(iprop_program_aux_image_get(relief, 0, &info, NULL) == IPROP_EX_OK);
    CHECK(info.image_index == 0 && info.kind == IPROP_AUX_IMAGE_NORMAL && same_name(info.name, "relief"));
    memcpy(&info_before, &info, sizeof(info));
    CHECK(iprop_program_aux_image_get(relief, 1, &info, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_aux_image_get(plain, 0, &info, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_aux_image_get(NULL, 0, &info, NULL) == IPROP_EX_E_ARGUMENT);
    info.flags = 1;
    CHECK(iprop_program_aux_image_get(relief, 0, &info, NULL) == IPROP_EX_E_ARGUMENT);
    info.flags = 0;
    CHECK(memcmp(&info, &info_before, sizeof(info)) == 0);
    CHECK(iprop_program_aux_image_get(relief, 0, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_aux_image_info_ex_init(NULL, 8) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_aux_image_info_ex_init(&info, 3) == IPROP_EX_E_ARGUMENT);

    /* The authored image of a checked generation. */
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 7;
    CHECK(iprop_generate_ex(relief, &options, NULL, &first, &error) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(first, &count, NULL) == IPROP_EX_OK && count == 1);
    CHECK(iprop_result_aux_image_find(first, IPROP_AUX_IMAGE_NORMAL, &index, NULL) == IPROP_EX_OK && index == 0);
    index = 55;
    CHECK(iprop_result_aux_image_find(first, 2, &index, &error) == IPROP_EX_E_ARGUMENT && index == 55);
    CHECK(strstr(message, "no auxiliary image of this kind") != NULL);
    CHECK(iprop_result_aux_image_find(first, IPROP_AUX_IMAGE_NORMAL, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_aux_image_find(NULL, IPROP_AUX_IMAGE_NORMAL, &index, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_aux_image_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_get(first, 0, &view, &error) == IPROP_EX_OK);
    CHECK(view.struct_size == sizeof(view) && view.flags == 0 && view.image_index == 0);
    CHECK(view.kind == IPROP_AUX_IMAGE_NORMAL && view.source == IPROP_AUX_IMAGE_AUTHORED);
    CHECK(view.width == 3 && view.height == 2 && view.byte_count == 24 && view.row_stride == 12);
    CHECK(view.pixel_format == IPROP_PIXEL_FORMAT_RGBA8 && view.alpha_mode == IPROP_IMAGE_ALPHA_OPAQUE);
    CHECK(view.origin == IPROP_IMAGE_ORIGIN_TOP_LEFT && same_name(view.name, "relief"));
    CHECK(view.pixels != NULL && memcmp(view.pixels, authored, sizeof(authored)) == 0);
    memcpy(&saved, &view, sizeof(view));
    CHECK(prefixes(first, &view) == 0);
    /* The main image and its statistics are those of any generation. */
    CHECK(iprop_image_view_ex_init(&image, sizeof(image)) == IPROP_EX_OK);
    CHECK(iprop_result_query_image(first, &image, NULL) == IPROP_EX_OK && image.pixels != view.pixels);
    CHECK(image.width == 3 && image.height == 2 && image.pixels[0] == 0x10 && image.pixels[3] == 255 && image.pixels[7] == 0);
    CHECK(iprop_result_stats_ex_init(&stats, sizeof(stats)) == IPROP_EX_OK);
    CHECK(iprop_result_query_stats(first, &stats, NULL) == IPROP_EX_OK && stats.image_byte_count == 24);

    /* Failures leave the view unchanged. */
    memcpy(&before, &view, sizeof(view));
    view.flags = 1;
    CHECK(iprop_result_aux_image_get(first, 0, &view, &error) == IPROP_EX_E_ARGUMENT);
    view.flags = 0;
    CHECK(memcmp(&view, &before, sizeof(view)) == 0);
    CHECK(iprop_result_aux_image_get(first, 1, &view, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strstr(message, "index is out of range") != NULL && memcmp(&view, &before, sizeof(view)) == 0);
    CHECK(iprop_result_aux_image_get(first, 0, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_aux_image_get(first, 0, &view, (iprop_error_ex*)&view) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&view, &before, sizeof(view)) == 0);
    CHECK(iprop_result_aux_image_count(NULL, &count, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_aux_image_count(first, NULL, NULL) == IPROP_EX_E_ARGUMENT);

    /* A legacy generation publishes it too, at the requested size. */
    legacy.seed = 7; legacy.size_x = 6; legacy.size_y = 4;
    CHECK(iprop_generate(relief, &legacy, &second, message, sizeof(message)) == IPROP_OK);
    CHECK(iprop_aux_image_view_ex_init(&other, sizeof(other)) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_get(second, 0, &other, NULL) == IPROP_EX_OK);
    CHECK(other.width == 6 && other.height == 4 && other.byte_count == 96 && other.pixels != saved.pixels);
    CHECK(memcmp(other.pixels, authored, 4) == 0 && memcmp(other.pixels + 4, flat, 4) == 0);
    CHECK(memcmp(other.pixels + (2 * 6 + 4) * 4, authored + 20, 4) == 0);
    for (i = 0; i < 96; i += 4) CHECK(other.pixels[i + 3] == 255);

    /* A program without a normal output publishes nothing unless asked. */
    CHECK(iprop_generate_ex(plain, &options, NULL, &none, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(none, &count, NULL) == IPROP_EX_OK && count == 0);
    CHECK(iprop_result_aux_image_get(none, 0, &other, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_aux_image_find(none, IPROP_AUX_IMAGE_NORMAL, &index, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, NULL, &again, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(again, &count, NULL) == IPROP_EX_OK && count == 0);
    iprop_result_free(again); again = NULL;

    /* The derived image: deterministic, opaque, named and tagged. */
    CHECK(iprop_derived_normal_ex_init(&request, sizeof(request)) == IPROP_EX_OK);
    CHECK(request.struct_size == sizeof(request) && request.flags == 0 && request.depth == 0 &&
          request.rounding == 0 && request.detail == 0);
    CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &derived, &error) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(derived, &count, NULL) == IPROP_EX_OK && count == 1);
    CHECK(iprop_result_aux_image_get(derived, 0, &other, NULL) == IPROP_EX_OK);
    CHECK(other.kind == IPROP_AUX_IMAGE_NORMAL && other.source == IPROP_AUX_IMAGE_DERIVED && same_name(other.name, "normal"));
    CHECK(other.width == 3 && other.height == 2 && other.alpha_mode == IPROP_IMAGE_ALPHA_OPAQUE);
    for (i = 0; i < 24; i += 4) CHECK(other.pixels[i + 3] == 255);
    CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &again, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_get(again, 0, &before, NULL) == IPROP_EX_OK);
    CHECK(memcmp(before.pixels, other.pixels, 24) == 0);
    /* Its main image is the ordinary one. */
    {
        iprop_image_view_ex a, b;
        CHECK(iprop_image_view_ex_init(&a, sizeof(a)) == IPROP_EX_OK && iprop_image_view_ex_init(&b, sizeof(b)) == IPROP_EX_OK);
        CHECK(iprop_result_query_image(derived, &a, NULL) == IPROP_EX_OK && iprop_result_query_image(none, &b, NULL) == IPROP_EX_OK);
        CHECK(a.byte_count == b.byte_count && memcmp(a.pixels, b.pixels, (size_t)a.byte_count) == 0);
    }
    iprop_result_free(again); again = NULL;
    /* Parameters change it; a prefix that omits them selects the defaults. */
    request.depth = 9.0f; request.rounding = 0.5f; request.detail = 1.0f;
    CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &again, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_get(again, 0, &before, NULL) == IPROP_EX_OK);
    CHECK(memcmp(before.pixels, other.pixels, 24) != 0);
    iprop_result_free(again); again = NULL;
    request.struct_size = offsetof(iprop_derived_normal_ex, depth);
    CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &again, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_get(again, 0, &before, NULL) == IPROP_EX_OK);
    CHECK(memcmp(before.pixels, other.pixels, 24) == 0);
    iprop_result_free(again); again = NULL;
    request.struct_size = sizeof(request);
    /* An authored image wins over the request. */
    request.depth = 0; request.rounding = 0; request.detail = 0;
    CHECK(iprop_generate_derived_normal(relief, &options, NULL, NULL, &request, &again, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(again, &count, NULL) == IPROP_EX_OK && count == 1);
    CHECK(iprop_result_aux_image_get(again, 0, &before, NULL) == IPROP_EX_OK);
    CHECK(before.source == IPROP_AUX_IMAGE_AUTHORED && memcmp(before.pixels, authored, sizeof(authored)) == 0);
    iprop_result_free(again); again = NULL;

    /* Invalid requests fail as arguments before any result exists. */
    {
        static const float bad[][3] = {{-1.0f, 0, 0}, {0, -0.5f, 0}, {0, 64.5f, 0}, {0, 0, -0.1f}, {0, 0, 1.1f}};
        iprop_result* kept = first;
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
            request.depth = bad[i][0]; request.rounding = bad[i][1]; request.detail = bad[i][2];
            CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &kept, &error) == IPROP_EX_E_ARGUMENT);
            CHECK(kept == first && error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);
            CHECK(error.location_flags == 0 && strstr(message, "out of range") != NULL);
        }
        request.depth = 0; request.rounding = 0; request.detail = 0; request.flags = 1;
        CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &kept, &error) == IPROP_EX_E_ARGUMENT && kept == first);
        request.flags = 0; request.struct_size = 3;
        CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, &kept, &error) == IPROP_EX_E_ARGUMENT && kept == first);
        request.struct_size = sizeof(request);
        CHECK(iprop_generate_derived_normal(plain, &options, NULL, NULL, &request, (iprop_result**)&request, NULL) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_derived_normal_ex_init(NULL, 8) == IPROP_EX_E_ARGUMENT && iprop_derived_normal_ex_init(&request, 3) == IPROP_EX_E_ARGUMENT);
    }

    /* Results own their images: they survive the program and other results. */
    iprop_program_free(relief); iprop_program_free(plain);
    iprop_result_free(second); iprop_result_free(none);
    CHECK(iprop_result_aux_image_get(first, 0, &view, NULL) == IPROP_EX_OK && view.pixels == saved.pixels);
    CHECK(memcmp(view.pixels, authored, sizeof(authored)) == 0 && same_name(view.name, "relief"));
    CHECK(iprop_result_aux_image_get(derived, 0, &other, NULL) == IPROP_EX_OK && other.pixels[3] == flat[3]);
    iprop_result_free(first); iprop_result_free(derived);
    puts("C99 auxiliary images: layouts, declarations, authored/legacy/derived images, every capacity and tail, argument failures and lifetimes: PASS");
    return 0;
}
