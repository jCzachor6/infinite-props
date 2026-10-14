#include "iprop.h"
#include "check.h"

#define FIELD(name) {offsetof(iprop_image_view_ex, name), sizeof(((iprop_image_view_ex*)0)->name)}

typedef struct { size_t offset, size; } field;
static const field outputs[] = {FIELD(width), FIELD(height), FIELD(byte_count), FIELD(row_stride),
    FIELD(pixel_format), FIELD(alpha_mode), FIELD(origin), FIELD(pixels)};
static const field inputs[] = {FIELD(struct_size), FIELD(flags)};

/* Every byte of actual allocation capacity is checked; partial fields,
 * native padding and future tails are not inferred from sizeof alone. */
static int prefixes(const iprop_result* result, const iprop_image_view_ex* expected) {
    const iprop_image_view_ex defaults = {0};
    uint32_t capacity;
    unsigned alignment;
    for (alignment = 0; alignment < 2; ++alignment) {
        for (capacity = 0; capacity <= sizeof(*expected) + 16; ++capacity) {
            const size_t allocation = capacity < 4 ? 4 : capacity;
            unsigned char* raw = (unsigned char*)malloc(allocation + alignment);
            unsigned char* storage;
            unsigned char before[sizeof(*expected) + 16];
            size_t byte, f;
            CHECK(raw != NULL);
            storage = raw + alignment;
            memset(raw, 0xa5, allocation + alignment);
            CHECK(iprop_image_view_ex_init((iprop_image_view_ex*)storage, capacity) ==
                  (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
            for (byte = 0; byte < allocation; ++byte) {
                unsigned char want = 0xa5;
                if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
                for (f = 1; f < sizeof(inputs)/sizeof(inputs[0]); ++f)
                    if (inputs[f].offset + inputs[f].size <= capacity && byte >= inputs[f].offset &&
                        byte < inputs[f].offset + inputs[f].size) want = ((const unsigned char*)&defaults)[byte];
                for (f = 0; f < sizeof(outputs)/sizeof(outputs[0]); ++f)
                    if (outputs[f].offset + outputs[f].size <= capacity && byte >= outputs[f].offset &&
                        byte < outputs[f].offset + outputs[f].size) want = ((const unsigned char*)&defaults)[byte];
                CHECK(storage[byte] == want);
            }
            if (alignment) CHECK(raw[0] == 0xa5);
            memset(storage, 0xa5, allocation);
            memcpy(storage, &capacity, 4);
            if (capacity >= offsetof(iprop_image_view_ex, flags) + 4)
                memset(storage + offsetof(iprop_image_view_ex, flags), 0, 4);
            memcpy(before, storage, allocation);
            CHECK(iprop_result_query_image(result, (iprop_image_view_ex*)storage, NULL) ==
                  (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
            for (byte = 0; byte < allocation; ++byte) {
                unsigned char want = before[byte];
                for (f = 0; f < sizeof(outputs)/sizeof(outputs[0]); ++f)
                    if (outputs[f].offset + outputs[f].size <= capacity && byte >= outputs[f].offset &&
                        byte < outputs[f].offset + outputs[f].size) want = ((const unsigned char*)expected)[byte];
                CHECK(storage[byte] == want);
            }
            if (alignment) CHECK(raw[0] == 0xa5);
            memcpy(before, storage, allocation);
            CHECK(iprop_result_query_image(NULL, (iprop_image_view_ex*)storage, NULL) == IPROP_EX_E_ARGUMENT);
            CHECK(memcmp(storage, before, allocation) == 0);
            free(raw);
        }
    }
    return 0;
}

static int errors(const iprop_result* result) {
    iprop_image_view_ex out, before;
    iprop_error_ex error, error_before;
    char message[128];
    uint32_t capacity;
    CHECK(iprop_image_view_ex_init(NULL, 4) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_query_image(result, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_query_image(NULL, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message;
    for (capacity = 0; capacity <= 2; ++capacity) {
        error.message_capacity = capacity;
        CHECK(iprop_image_view_ex_init(&out, sizeof(out)) == IPROP_EX_OK);
        out.flags = UINT32_MAX;
        memcpy(&before, &out, sizeof(out));
        memset(message, 0xa5, sizeof(message));
        CHECK(iprop_result_query_image(result, &out, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&before, &out, sizeof(out)) == 0);
        CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_INVALID_ARGUMENT &&
              error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
        CHECK(error.message_required_bytes > 2 && error.message_truncated == 1);
        if (capacity > 0) CHECK(message[capacity - 1] == '\0');
        CHECK((unsigned char)message[capacity] == 0xa5);
        out.flags = 0;
        CHECK(iprop_result_query_image(result, &out, &error) == IPROP_EX_OK);
        CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && error.stage == IPROP_STAGE_NONE);
        CHECK(error.message_required_bytes == 0 && error.message_truncated == 0 && error.location_flags == 0);
        if (capacity > 0) CHECK(message[0] == '\0');
    }
    error.message_capacity = sizeof(message);
    CHECK(iprop_result_query_image(NULL, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strstr(message, "result is null") != NULL && error.message_truncated == 0);
    CHECK(iprop_result_query_image(result, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);

    /* Error binding fails before publishing a valid query. */
    error.message_buffer = NULL;
    error.message_capacity = 1;
    memset(&out, 0xa5, sizeof(out)); out.struct_size = sizeof(out); out.flags = 0;
    memcpy(&before, &out, sizeof(out));
    CHECK(iprop_result_query_image(result, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);

    /* Complete descriptor or message/output aliases preserve both outputs. */
    error.message_buffer = (char*)&out.width; error.message_capacity = 1;
    memcpy(&error_before, &error, sizeof(error));
    CHECK(iprop_result_query_image(result, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0 && memcmp(&error, &error_before, sizeof(error)) == 0);
    CHECK(iprop_result_query_image(result, &out, (iprop_error_ex*)&out) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    CHECK(iprop_result_query_image(result, &out, (iprop_error_ex*)((unsigned char*)&out + 1)) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    error.message_buffer = (char*)&error.status;
    memcpy(&error_before, &error, sizeof(error));
    CHECK(iprop_result_query_image(result, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0 && memcmp(&error, &error_before, sizeof(error)) == 0);

    /* A zero-length message span touches nothing and may point into output. */
    error.message_buffer = (char*)&out.width; error.message_capacity = 0;
    CHECK(iprop_result_query_image(result, &out, &error) == IPROP_EX_OK);
    CHECK(error.message_required_bytes == 0 && error.message_truncated == 0);

    /* Protect the entire claimed future allocation, including unknown tail;
     * a message span immediately beyond it is separately valid storage. */
    {
        union { iprop_image_view_ex align; unsigned char bytes[sizeof(out) + 17]; } extended;
        unsigned char snapshot[sizeof(extended.bytes)];
        const uint32_t extent = sizeof(out) + 16;
        memset(extended.bytes, 0xa5, sizeof(extended.bytes));
        CHECK(iprop_image_view_ex_init((iprop_image_view_ex*)extended.bytes, extent) == IPROP_EX_OK);
        memcpy(snapshot, extended.bytes, sizeof(snapshot));
        error.message_buffer = (char*)extended.bytes + extent - 1; error.message_capacity = 1;
        memcpy(&error_before, &error, sizeof(error));
        CHECK(iprop_result_query_image(result, (iprop_image_view_ex*)extended.bytes, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(extended.bytes, snapshot, sizeof(snapshot)) == 0);
        CHECK(memcmp(&error, &error_before, sizeof(error)) == 0);
        error.message_buffer = (char*)extended.bytes + extent;
        CHECK(iprop_result_query_image(result, (iprop_image_view_ex*)extended.bytes, &error) == IPROP_EX_OK);
        CHECK(extended.bytes[extent] == 0);
        {
            size_t i;
            for (i = sizeof(out); i < extent; ++i) CHECK(extended.bytes[i] == 0xa5);
        }
    }

    /* Every independently sized error prefix, including partial pointer and
     * capacity fields, is accepted only when valid and cannot alter tails. */
    for (capacity = 0; capacity <= sizeof(error) + 8; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        size_t i;
        CHECK(storage != NULL);
        memset(storage, 0xa5, allocation);
        CHECK(iprop_error_ex_init((iprop_error_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        memcpy(storage, &capacity, 4);
        memset(&out, 0xa5, sizeof(out)); out.struct_size = sizeof(out); out.flags = 0;
        memcpy(&before, &out, sizeof(out));
        CHECK(iprop_result_query_image(result, &out, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        if (capacity < 4) CHECK(memcmp(&out, &before, sizeof(out)) == 0);
        else CHECK(out.pixels != NULL && out.pixel_format == IPROP_PIXEL_FORMAT_RGBA8);
        for (i = sizeof(error); i < allocation; ++i) CHECK(storage[i] == 0xa5);
        out.flags = 1; memcpy(&before, &out, sizeof(out));
        CHECK(iprop_result_query_image(result, &out, (iprop_error_ex*)storage) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&out, &before, sizeof(out)) == 0);
        for (i = sizeof(error); i < allocation; ++i) CHECK(storage[i] == 0xa5);
        free(storage);
    }
    return 0;
}

static int image(const iprop_result* result, uint32_t width, uint32_t height,
                 iprop_image_view_ex* out) {
    iprop_image legacy = {0};
    iprop_result_stats_ex stats;
    CHECK(iprop_image_view_ex_init(out, sizeof(*out)) == IPROP_EX_OK);
    CHECK(iprop_result_query_image(result, out, NULL) == IPROP_EX_OK);
    iprop_result_image(result, &legacy);
    CHECK(out->struct_size == sizeof(*out) && out->flags == 0);
    CHECK(out->width == width && out->height == height);
    CHECK(out->pixels == legacy.pixels && out->width == legacy.width && out->height == legacy.height);
    CHECK(out->byte_count == (uint64_t)width * height * 4 && out->row_stride == (uint64_t)width * 4);
    CHECK(out->pixel_format == IPROP_PIXEL_FORMAT_RGBA8 && out->alpha_mode == IPROP_IMAGE_ALPHA_LEGACY_DESTINATION);
    CHECK(out->origin == IPROP_IMAGE_ORIGIN_TOP_LEFT);
    CHECK(iprop_result_stats_ex_init(&stats, sizeof(stats)) == IPROP_EX_OK);
    CHECK(iprop_result_query_stats(result, &stats, NULL) == IPROP_EX_OK && out->byte_count == stats.image_byte_count);
    return 0;
}

int main(int argc, char** argv) {
    static const uint8_t expected[] = {
        17,34,51,255, 100,50,25,0, 110,70,54,255,
        1,2,3,255, 171,205,239,255, 10,11,12,255
    };
    uint8_t override[6*4*4] = {0};
    size_t length;
    uint8_t* bytes;
    iprop_library_info_ex library;
    iprop_program_info_ex meta;
    iprop_program* program = NULL;
    iprop_result *first = NULL, *second = NULL;
    iprop_generate_options options = {0};
    iprop_image_view_ex view, saved, other;
    char message[128];
    size_t x, y;
    CHECK(argc == 2);
    CHECK(sizeof(iprop_pixel_format) == 4 && sizeof(iprop_image_alpha_mode) == 4 && sizeof(iprop_image_origin) == 4);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query(&library) == IPROP_EX_OK && (library.capabilities & IPROP_CAP_IMAGE_VIEW) != 0);
    CHECK(read_fixture(argv[1], &bytes, &length) == 0);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &program, NULL) == IPROP_EX_OK);
    free(bytes);
    CHECK(iprop_program_info_ex_init(&meta, sizeof(meta)) == IPROP_EX_OK);
    CHECK(iprop_program_query(program, &meta, NULL) == IPROP_EX_OK && meta.origin_x == 999 && meta.origin_y == -999);
    CHECK(iprop_generate(program, &options, &first, message, sizeof(message)) == IPROP_OK);
    CHECK(image(first, 3, 2, &view) == 0);
    CHECK(memcmp(view.pixels, expected, sizeof(expected)) == 0);
    memcpy(&saved, &view, sizeof(view));
    CHECK(prefixes(first, &view) == 0 && errors(first) == 0);
    options.seed = 26; options.size_x = 6; options.size_y = 4;
    CHECK(iprop_generate(program, &options, &second, message, sizeof(message)) == IPROP_OK);
    CHECK(image(second, 6, 4, &other) == 0 && other.pixels != saved.pixels);
    for (y = 0; y < 2; ++y) for (x = 0; x < 3; ++x)
        memcpy(override + ((y*2)*6 + x*2)*4, expected + (y*3 + x)*4, 4);
    CHECK(memcmp(other.pixels, override, sizeof(override)) == 0);
    CHECK(memcmp(saved.pixels, expected, sizeof(expected)) == 0);
    {
        iprop_result* failed = first;
        options.size_x = UINT32_MAX;
        CHECK(iprop_generate(program, &options, &failed, message, sizeof(message)) == IPROP_E_TRAP && failed == first);
    }
    iprop_program_free(program);
    CHECK(image(first, 3, 2, &view) == 0 && view.pixels == saved.pixels);
    CHECK(memcmp(view.pixels, expected, sizeof(expected)) == 0);
    iprop_result_free(second);
    CHECK(iprop_result_query_image(first, &view, NULL) == IPROP_EX_OK && view.pixels == saved.pixels);
    CHECK(memcmp(view.pixels, expected, sizeof(expected)) == 0);
    iprop_result_free(first);
    puts("C99 image view: every capacity/layout/tail, checked errors/aliases, exact legacy storage/lifetimes, override size and legacy alpha raw bytes: PASS");
    return 0;
}
