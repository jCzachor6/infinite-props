#include "iprop.h"
#include "check.h"

typedef char input_type_is_fixed_width[(sizeof(iprop_input_type) == 4) ? 1 : -1];
typedef char rgba8_is_four_bytes[(sizeof(iprop_color_rgba8) == 4) ? 1 : -1];

static size_t section_offset(const uint8_t* bytes, size_t size, uint32_t id) {
    size_t i;
    uint32_t count;
    if (size < 20) return 0;
    count = read_u32(bytes + 12);
    for (i = 0; i < count && i < (size - 20) / 12; ++i)
        if (read_u32(bytes + 20 + i * 12) == id)
            return read_u32(bytes + 24 + i * 12);
    return 0;
}

int main(int argc, char** argv) {
    uint8_t *bytes, *empty_bytes, *modified;
    size_t size, empty_size;
    iprop_program *program = NULL, *empty = NULL, *legacy = NULL;
    iprop_input_info_ex info, before_info, expected[8];
    iprop_error_ex error;
    iprop_library_info_ex library;
    char message[128];
    const char* names[] = {"tint", "zero", "fraction", "rounded", "high", "negative", "minimum", "maximum"};
    const uint32_t types[] = {IPROP_INPUT_COLOR, IPROP_INPUT_F32, IPROP_INPUT_F32, IPROP_INPUT_F32,
        IPROP_INPUT_U32, IPROP_INPUT_U32, IPROP_INPUT_U32, IPROP_INPUT_U32};
    const uint32_t defaults[] = {0, UINT32_C(0x80000000), UINT32_C(0x3f000000), UINT32_C(0x4b800000),
        UINT32_MAX, UINT32_C(0xfffffff9), UINT32_C(0x80000000), UINT32_C(0x7fffffff)};
    uint32_t count = UINT32_MAX, index = UINT32_MAX, capacity, i;
    CHECK(argc == 3);
    CHECK(read_fixture(argv[1], &bytes, &size) == 0);
    CHECK(read_fixture(argv[2], &empty_bytes, &empty_size) == 0);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    CHECK(iprop_program_load_ex(bytes, size, &program, &error) == IPROP_EX_OK);
    CHECK(iprop_program_load_ex(empty_bytes, empty_size, &empty, &error) == IPROP_EX_OK);
    CHECK(iprop_program_load(bytes, size, &legacy, message, sizeof(message)) == IPROP_OK);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library, &error) == IPROP_EX_OK);
    CHECK((library.capabilities & IPROP_CAP_ROOT_INPUT_REFLECTION) != 0);
    CHECK(IPROP_INPUT_COLOR == 0 && IPROP_INPUT_F32 == 1 && IPROP_INPUT_U32 == 2);
    CHECK(iprop_program_input_count(program, &count, &error) == IPROP_EX_OK && count == 8);
    CHECK(iprop_program_input_count(empty, &count, NULL) == IPROP_EX_OK && count == 0);
    CHECK(iprop_program_input_count(legacy, &count, NULL) == IPROP_EX_OK && count == 8);

    for (i = 0; i < 8; ++i) {
        CHECK(iprop_input_info_ex_init(&expected[i], sizeof(expected[i])) == IPROP_EX_OK);
        CHECK(iprop_program_input_get(program, i, &expected[i], &error) == IPROP_EX_OK);
        CHECK(expected[i].struct_size == sizeof(expected[i]) && expected[i].flags == 0);
        CHECK(expected[i].input_index == i && expected[i].type == types[i]);
        CHECK(expected[i].name.byte_count == strlen(names[i]));
        CHECK(memcmp(expected[i].name.data, names[i], expected[i].name.byte_count) == 0);
        CHECK(iprop_program_input_find(program, expected[i].name, &index, &error) == IPROP_EX_OK && index == i);
        if (i == 0) {
            CHECK(expected[i].color_default.r == 0x12 && expected[i].color_default.g == 0x34);
            CHECK(expected[i].color_default.b == 0x56 && expected[i].color_default.a == 0x78);
        } else {
            const iprop_color_rgba8 zero = {0, 0, 0, 0};
            CHECK(memcmp(&expected[i].color_default, &zero, sizeof(zero)) == 0);
        }
        CHECK(float_bits(&expected[i].f32_default) == (types[i] == IPROP_INPUT_F32 ? defaults[i] : 0));
        CHECK(expected[i].u32_default == (types[i] == IPROP_INPUT_U32 ? defaults[i] : 0));
    }

    /* Exact heap allocations: every boundary through each scalar, leaf pointer,
     * nested length and padding, plus larger future descriptors. */
    for (capacity = 0; capacity <= sizeof(info) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(info) + 16];
        size_t byte, field;
        const size_t starts[] = {offsetof(iprop_input_info_ex, input_index), offsetof(iprop_input_info_ex, type),
            offsetof(iprop_input_info_ex, name) + offsetof(iprop_utf8_view, data),
            offsetof(iprop_input_info_ex, name) + offsetof(iprop_utf8_view, byte_count),
            offsetof(iprop_input_info_ex, color_default), offsetof(iprop_input_info_ex, f32_default),
            offsetof(iprop_input_info_ex, u32_default)};
        const size_t widths[] = {4, 4, sizeof(const char*), 4, 4, 4, 4};
        CHECK(storage != NULL);
        memset(storage, 0xa5, allocation);
        memcpy(storage, &capacity, 4);
        if (capacity >= 8) memset(storage + offsetof(iprop_input_info_ex, flags), 0, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_program_input_get(program, UINT32_MAX, (iprop_input_info_ex*)storage, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(storage, before, allocation) == 0);
        CHECK(iprop_program_input_get(program, 1, (iprop_input_info_ex*)storage, &error) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field])
                    want = ((const unsigned char*)&expected[1])[byte];
            CHECK(storage[byte] == want);
        }
        memset(storage, 0xa5, allocation);
        CHECK(iprop_input_info_ex_init((iprop_input_info_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            if (capacity >= 8 && byte >= offsetof(iprop_input_info_ex, flags) && byte < offsetof(iprop_input_info_ex, flags) + 4) want = 0;
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field]) want = 0;
            CHECK(storage[byte] == want);
        }
        free(storage);
    }

    CHECK(iprop_input_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(iprop_input_info_ex_init(NULL, sizeof(info)) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_input_get(program, 0, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_input_get(NULL, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
    for (i = 0; i < 2; ++i) {
        info.flags = i == 0 ? 1 : UINT32_MAX;
        memcpy(&before_info, &info, sizeof(info));
        CHECK(iprop_program_input_get(program, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&info, &before_info, sizeof(info)) == 0);
    }
    info.flags = 0; memcpy(&before_info, &info, sizeof(info));
    CHECK(iprop_program_input_get(empty, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&info, &before_info, sizeof(info)) == 0);
    count = UINT32_MAX;
    CHECK(iprop_program_input_count(NULL, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_input_count(program, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_input_find(program, expected[0].name, NULL, &error) == IPROP_EX_E_ARGUMENT);
    index = UINT32_MAX;
    CHECK(iprop_program_input_find(NULL, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_input_find(program, (iprop_utf8_view){"TINT", 4}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_input_find(program, (iprop_utf8_view){NULL, 1}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_input_find(program, (iprop_utf8_view){NULL, 0}, &index, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strcmp(message, "input name was not found") == 0);
    CHECK(iprop_program_input_find(program, (iprop_utf8_view){"tint", 0}, &index, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strcmp(message, "input name was not found") == 0);
    {
        const unsigned char invalid[][4] = {{0x80}, {0xc0,0xaf}, {0xe2,0x82}, {0xed,0xa0,0x80}, {0xf4,0x90,0x80,0x80}};
        const uint32_t lengths[] = {1, 2, 2, 3, 4};
        for (i = 0; i < 5; ++i) {
            char* exact = (char*)malloc(lengths[i]);
            CHECK(exact != NULL); memcpy(exact, invalid[i], lengths[i]);
            CHECK(iprop_program_input_find(program, (iprop_utf8_view){exact, lengths[i]}, &index, &error) == IPROP_EX_E_ARGUMENT);
            CHECK(index == UINT32_MAX && error.status == IPROP_EX_E_ARGUMENT);
            CHECK(error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
            free(exact);
        }
    }

    /* These mutations preserve the actual binary's stored lengths and offsets,
     * exercising names/defaults that source identifiers cannot express. */
    modified = (uint8_t*)malloc(size);
    CHECK(modified != NULL); memcpy(modified, bytes, size);
    {
        const char embedded[] = {'\xc3', '\xa9', '\0', 'x', '\0'};
        size_t at = section_offset(modified, size, 2), meta = section_offset(modified, size, 1);
        uint32_t strings;
        int replaced = 0;
        iprop_program* binary = NULL;
        CHECK(at != 0 && at + 4 <= size && meta != 0 && meta + 44 <= size);
        strings = read_u32(modified + at); at += 4;
        for (i = 0; i < strings; ++i) {
            uint32_t length;
            CHECK(at + 4 <= size); length = read_u32(modified + at); at += 4;
            CHECK(length <= size - at);
            if (length == 4 && memcmp(modified + at, "tint", 4) == 0) {
                memcpy(modified + at, embedded, 4); ++replaced;
            }
            at += length;
        }
        CHECK(replaced == 1);
        CHECK(iprop_program_load_ex(modified, size, &binary, &error) == IPROP_EX_OK);
        CHECK(iprop_program_input_get(binary, 0, &info, &error) == IPROP_EX_OK);
        CHECK(info.name.byte_count == 4 && memcmp(info.name.data, embedded, 4) == 0);
        CHECK(iprop_program_input_find(binary, (iprop_utf8_view){embedded, 4}, &index, &error) == IPROP_EX_OK && index == 0);
        index = UINT32_MAX;
        CHECK(iprop_program_input_find(binary, (iprop_utf8_view){embedded, 2}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(iprop_program_input_find(binary, (iprop_utf8_view){embedded, 3}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(iprop_program_input_find(binary, (iprop_utf8_view){embedded, 5}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(iprop_program_input_find(binary, (iprop_utf8_view){"tint", 4}, &index, &error) == IPROP_EX_E_ARGUMENT);
        iprop_program_free(binary);

        /* Two complete metadata entries precede fraction's stored default. */
        CHECK(read_u32(modified + meta + 8) == 0 && modified[meta + 32] == 0);
        CHECK(modified[meta + 42 + 2 * 9] == IPROP_INPUT_F32);
        for (i = 0; i < 3; ++i) {
            const uint32_t patterns[] = {UINT32_C(0x00000001), UINT32_C(0x7fc12345), UINT32_C(0x7f812345)};
            write_u32(modified + meta + 42 + 2 * 9 + 5, patterns[i]);
            binary = NULL;
            CHECK(iprop_program_load_ex(modified, size, &binary, &error) == IPROP_EX_OK);
            CHECK(iprop_program_input_get(binary, 2, &info, &error) == IPROP_EX_OK);
            CHECK(float_bits(&info.f32_default) == patterns[i]);
            iprop_program_free(binary);
        }
    }
    free(modified);

    /* New operations consume valid error prefixes and stop before writing any
     * main output when the error descriptor/message request is malformed. */
    for (capacity = 0; capacity <= sizeof(error) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        (void)iprop_error_ex_init((iprop_error_ex*)storage, capacity);
        memcpy(&info, &expected[1], sizeof(info)); memcpy(&before_info, &info, sizeof(info));
        CHECK(iprop_program_input_get(program, 0, &info, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(capacity >= 4 || memcmp(&info, &before_info, sizeof(info)) == 0);
        free(storage);
    }
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    for (capacity = 0; capacity <= sizeof("input index is out of range"); ++capacity) {
        char* exact = (char*)malloc(capacity == 0 ? 1 : capacity);
        CHECK(exact != NULL); memset(exact, 0x5a, capacity == 0 ? 1 : capacity);
        error.message_buffer = exact; error.message_capacity = capacity;
        CHECK(iprop_program_input_get(program, UINT32_MAX, &info, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(error.message_required_bytes == strlen("input index is out of range"));
        CHECK(error.message_truncated == (capacity < sizeof("input index is out of range") ? 1u : 0u));
        if (capacity == 0) CHECK(exact[0] == 0x5a);
        else {
            const size_t copied = capacity - 1 < error.message_required_bytes ? capacity - 1 : (size_t)error.message_required_bytes;
            CHECK(memcmp(exact, "input index is out of range", copied) == 0 && exact[copied] == '\0');
        }
        CHECK(iprop_program_input_get(program, 0, &info, &error) == IPROP_EX_OK);
        CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && error.location_flags == 0);
        CHECK(error.message_required_bytes == 0 && error.message_truncated == 0);
        CHECK(capacity == 0 ? exact[0] == 0x5a : exact[0] == '\0');
        free(exact);
    }
    error.message_buffer = NULL; error.message_capacity = 1;
    count = index = UINT32_MAX; memcpy(&before_info, &info, sizeof(info));
    CHECK(iprop_program_input_count(program, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_input_find(program, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_input_get(program, 1, &info, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&info, &before_info, sizeof(info)) == 0);
    {
        iprop_error_ex unchanged;
        error.message_buffer = (char*)&count; error.message_capacity = 1;
        memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_input_count(program, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
        error.message_buffer = (char*)&index;
        memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_input_find(program, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
        error.message_buffer = (char*)&info;
        CHECK(iprop_program_input_get(program, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&info, &before_info, sizeof(info)) == 0);
        error.message_buffer = message; error.message_capacity = sizeof(message);
        memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_input_get(program, 0, (iprop_input_info_ex*)&error, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_program_input_count(program, &error.stage, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
    }
    {
        char writable_name[] = "high";
        error.message_buffer = writable_name; error.message_capacity = sizeof(writable_name);
        CHECK(iprop_program_input_find(program, (iprop_utf8_view){writable_name, 4}, &index, &error) == IPROP_EX_OK && index == 4);
        CHECK(writable_name[0] == '\0');
        /* The input bytes may also be the output scalar: lookup consumes the
         * complete name before publishing the result. */
        memcpy(&index, "high", 4);
        CHECK(iprop_program_input_find(program, (iprop_utf8_view){(const char*)&index, 4}, &index, NULL) == IPROP_EX_OK && index == 4);
    }
    error.message_buffer = message; error.message_capacity = sizeof(message);
    memset(bytes, 0, size); free(bytes); free(empty_bytes);
    /* Borrowed name views survive caller-buffer destruction, subsequent
     * reflection and legacy generation without allocating a copied name. */
    CHECK(iprop_program_input_get(program, 7, &info, &error) == IPROP_EX_OK);
    {
        iprop_generate_options options = {0};
        iprop_result *result = NULL, *legacy_result = NULL;
        iprop_image image = {0}, legacy_image = {0};
        CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
        CHECK(iprop_generate(legacy, &options, &legacy_result, message, sizeof(message)) == IPROP_OK);
        iprop_result_image(result, &image);
        iprop_result_image(legacy_result, &legacy_image);
        CHECK(image.width == 8 && image.height == 8 && image.width == legacy_image.width && image.height == legacy_image.height);
        CHECK(memcmp(image.pixels, legacy_image.pixels, (size_t)image.width * image.height * 4) == 0);
        CHECK(expected[0].name.byte_count == 4 && memcmp(expected[0].name.data, "tint", 4) == 0);
        iprop_result_free(result); iprop_result_free(legacy_result);
    }
    iprop_program_free(legacy); iprop_program_free(empty); iprop_program_free(program);
    puts("C99 root reflection: exact defaults/names, all prefixes, flags, UTF-8/NUL, transactions and aliases: PASS");
    return 0;
}
