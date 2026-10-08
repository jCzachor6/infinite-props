#include "iprop.h"
#include "check.h"

int main(int argc, char** argv) {
    uint8_t *bytes, *other_bytes;
    size_t size, other_size;
    iprop_program *program = NULL, *empty = NULL;
    iprop_part_info_ex part, before_part, expected[3];
    iprop_input_info_ex input, before_input, color_input, root_input;
    iprop_output_info_ex output;
    iprop_error_ex error;
    iprop_library_info_ex library;
    char message[128];
    const uint32_t widths[] = {4, 2, 8}, heights[] = {2, 3, 4};
    const uint32_t origin_x[] = {UINT32_C(0x80000000), UINT32_C(0x3fc00000), UINT32_C(0xc0400000)};
    const uint32_t origin_y[] = {UINT32_C(0x3f000000), UINT32_C(0xc0000000), UINT32_C(0x40800000)};
    const uint32_t input_counts[] = {4, 0, 3}, output_counts[] = {2, 0, 1}, output_begins[] = {1, 3, 3};
    const char* names[] = {"first", "empty", "second"};
    uint32_t count = UINT32_MAX, index = UINT32_MAX, capacity, i, j, variant;
    CHECK(argc == 6);
    CHECK(read_fixture(argv[1], &bytes, &size) == 0 && read_u32(bytes + 8) == 0);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    CHECK(iprop_program_load_ex(bytes, size, &program, &error) == IPROP_EX_OK);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library, &error) == IPROP_EX_OK);
    CHECK((library.capabilities & IPROP_CAP_PART_REFLECTION) != 0);
    CHECK(iprop_program_part_count(program, &count, &error) == IPROP_EX_OK && count == 3);
    CHECK(iprop_input_info_ex_init(&input, sizeof(input)) == IPROP_EX_OK);
    CHECK(iprop_input_info_ex_init(&color_input, sizeof(color_input)) == IPROP_EX_OK);
    CHECK(iprop_input_info_ex_init(&root_input, sizeof(root_input)) == IPROP_EX_OK);
    CHECK(iprop_program_input_get(program, 0, &root_input, &error) == IPROP_EX_OK);
    CHECK(root_input.type == IPROP_INPUT_U32 && root_input.u32_default == 123);
    for (i = 0; i < 3; ++i) {
        CHECK(iprop_part_info_ex_init(&expected[i], sizeof(expected[i])) == IPROP_EX_OK);
        CHECK(iprop_program_part_get(program, i, &expected[i], &error) == IPROP_EX_OK);
        CHECK(expected[i].part_index == i && expected[i].owner == i + 1);
        CHECK(expected[i].size_x == widths[i] && expected[i].size_y == heights[i]);
        CHECK(float_bits(&expected[i].origin_x) == origin_x[i] && float_bits(&expected[i].origin_y) == origin_y[i]);
        CHECK(expected[i].input_count == input_counts[i] && expected[i].output_count == output_counts[i] && expected[i].output_begin == output_begins[i]);
        /* Compiled v0.5 files name each instance by its alias path. */
        CHECK(expected[i].name_present == 1 && expected[i].name.byte_count == strlen(names[i]) &&
              memcmp(expected[i].name.data, names[i], strlen(names[i])) == 0);
        CHECK(iprop_program_part_input_count(program, i, &count, &error) == IPROP_EX_OK && count == input_counts[i]);
        for (j = 0; j < count; ++j) {
            CHECK(iprop_program_part_input_get(program, i, j, &input, &error) == IPROP_EX_OK && input.input_index == j);
            CHECK(iprop_program_part_input_find(program, i, input.name, &index, &error) == IPROP_EX_OK && index == j);
        }
        for (j = 0; j < expected[i].output_count; ++j) {
            CHECK(iprop_output_info_ex_init(&output, sizeof(output)) == IPROP_EX_OK);
            CHECK(iprop_program_output_get(program, expected[i].output_begin + j, &output, &error) == IPROP_EX_OK);
            CHECK(output.owner == expected[i].owner && output.output_index == expected[i].output_begin + j);
            CHECK(iprop_program_output_find(program, expected[i].owner, output.name, &index, &error) == IPROP_EX_OK && index == output.output_index);
        }
    }
    CHECK(iprop_program_part_input_get(program, 0, 0, &color_input, &error) == IPROP_EX_OK);
    CHECK(color_input.type == IPROP_INPUT_COLOR && color_input.input_index == root_input.input_index);
    CHECK(color_input.color_default.r == 0x12 && color_input.color_default.g == 0x34 && color_input.color_default.b == 0x56 && color_input.color_default.a == 0x78);
    CHECK(iprop_program_part_input_get(program, 0, 1, &input, &error) == IPROP_EX_OK);
    CHECK(input.type == IPROP_INPUT_F32 && float_bits(&input.f32_default) == UINT32_C(0x80000000));
    CHECK(iprop_program_part_input_get(program, 0, 2, &input, &error) == IPROP_EX_OK && input.type == IPROP_INPUT_U32 && input.u32_default == UINT32_MAX);
    CHECK(iprop_program_part_input_get(program, 0, 3, &input, &error) == IPROP_EX_OK && input.type == IPROP_INPUT_U32 && input.u32_default == UINT32_C(0xfffffff9));
    CHECK(iprop_program_part_input_get(program, 2, 0, &input, &error) == IPROP_EX_OK && input.type == IPROP_INPUT_U32 && input.u32_default == 7);
    CHECK(iprop_program_part_input_find(program, 2, color_input.name, &index, &error) == IPROP_EX_OK && index == 0);
    CHECK(iprop_program_part_input_find(program, 1, color_input.name, &index, &error) == IPROP_EX_E_ARGUMENT);

    /* Debug/plain and debug/compressed encodings preserve geometry, complete
     * signatures and instance names. */
    for (variant = 2; variant <= 3; ++variant) {
        iprop_program* encoded = NULL;
        CHECK(read_fixture(argv[variant], &other_bytes, &other_size) == 0);
        CHECK(read_u32(other_bytes + 8) == (variant == 2 ? 1u : 3u));
        CHECK(iprop_program_load_ex(other_bytes, other_size, &encoded, &error) == IPROP_EX_OK);
        memset(other_bytes, 0, other_size); free(other_bytes);
        for (i = 0; i < 3; ++i) {
            CHECK(iprop_part_info_ex_init(&part, sizeof(part)) == IPROP_EX_OK);
            CHECK(iprop_program_part_get(encoded, i, &part, &error) == IPROP_EX_OK);
            CHECK(part.owner == expected[i].owner && part.size_x == widths[i] && part.size_y == heights[i]);
            CHECK(float_bits(&part.origin_x) == origin_x[i] && float_bits(&part.origin_y) == origin_y[i]);
            CHECK(part.input_count == input_counts[i] && part.output_count == output_counts[i] && part.output_begin == output_begins[i]);
            CHECK(part.name_present == 1 && part.name.byte_count == strlen(names[i]) &&
                  memcmp(part.name.data, names[i], strlen(names[i])) == 0);
            CHECK(iprop_program_part_input_count(encoded, i, &count, &error) == IPROP_EX_OK && count == input_counts[i]);
            for (j = 0; j < count; ++j) {
                CHECK(iprop_program_part_input_get(encoded, i, j, &input, &error) == IPROP_EX_OK);
                CHECK(iprop_program_part_input_find(encoded, i, input.name, &index, &error) == IPROP_EX_OK && index == j);
            }
        }
        iprop_program_free(encoded);
    }
    CHECK(read_fixture(argv[4], &other_bytes, &other_size) == 0);
    CHECK(iprop_program_load_ex(other_bytes, other_size, &empty, &error) == IPROP_EX_OK); free(other_bytes);
    CHECK(iprop_program_part_count(empty, &count, NULL) == IPROP_EX_OK && count == 0);

    /* Without a parts section no name is stored or inferred from symbols. */
    {
        iprop_program* unnamed = NULL;
        CHECK(read_fixture(argv[5], &other_bytes, &other_size) == 0);
        CHECK(iprop_program_load_ex(other_bytes, other_size, &unnamed, &error) == IPROP_EX_OK); free(other_bytes);
        CHECK(iprop_part_info_ex_init(&part, sizeof(part)) == IPROP_EX_OK);
        CHECK(iprop_program_part_get(unnamed, 0, &part, &error) == IPROP_EX_OK);
        CHECK(part.name_present == 0 && part.name.data == NULL && part.name.byte_count == 0);
        iprop_program_free(unnamed);
    }

    /* Every exact part descriptor prefix/tail, including native name padding. */
    for (capacity = 0; capacity <= sizeof(part) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(part) + 16];
        size_t byte, field;
        const size_t starts[] = {offsetof(iprop_part_info_ex, part_index), offsetof(iprop_part_info_ex, owner),
            offsetof(iprop_part_info_ex, size_x), offsetof(iprop_part_info_ex, size_y), offsetof(iprop_part_info_ex, origin_x), offsetof(iprop_part_info_ex, origin_y),
            offsetof(iprop_part_info_ex, input_count), offsetof(iprop_part_info_ex, output_begin), offsetof(iprop_part_info_ex, output_count), offsetof(iprop_part_info_ex, name_present),
            offsetof(iprop_part_info_ex, name) + offsetof(iprop_utf8_view, data), offsetof(iprop_part_info_ex, name) + offsetof(iprop_utf8_view, byte_count)};
        const size_t lengths[] = {4,4,4,4,4,4,4,4,4,4,sizeof(const char*),4};
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        if (capacity >= 8) memset(storage + offsetof(iprop_part_info_ex, flags), 0, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_program_part_get(program, UINT32_MAX, (iprop_part_info_ex*)storage, &error) == IPROP_EX_E_ARGUMENT && memcmp(storage, before, allocation) == 0);
        CHECK(iprop_program_part_get(program, 0, (iprop_part_info_ex*)storage, &error) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + lengths[field] <= capacity && byte >= starts[field] && byte < starts[field] + lengths[field]) want = ((const unsigned char*)&expected[0])[byte];
            CHECK(storage[byte] == want);
        }
        memset(storage, 0xa5, allocation);
        CHECK(iprop_part_info_ex_init((iprop_part_info_ex*)storage, capacity) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            if (capacity >= 8 && byte >= offsetof(iprop_part_info_ex, flags) && byte < offsetof(iprop_part_info_ex, flags) + 4) want = 0;
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + lengths[field] <= capacity && byte >= starts[field] && byte < starts[field] + lengths[field]) want = 0;
            CHECK(storage[byte] == want);
        }
        free(storage);
    }
    /* The shared input writer has the same exact bounds for a selected part. */
    for (capacity = 0; capacity <= sizeof(input) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(input) + 16];
        size_t byte, field;
        const size_t starts[] = {offsetof(iprop_input_info_ex, input_index), offsetof(iprop_input_info_ex, type),
            offsetof(iprop_input_info_ex, name) + offsetof(iprop_utf8_view, data), offsetof(iprop_input_info_ex, name) + offsetof(iprop_utf8_view, byte_count),
            offsetof(iprop_input_info_ex, color_default), offsetof(iprop_input_info_ex, f32_default), offsetof(iprop_input_info_ex, u32_default)};
        const size_t lengths[] = {4,4,sizeof(const char*),4,4,4,4};
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        if (capacity >= 8) memset(storage + offsetof(iprop_input_info_ex, flags), 0, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_program_part_input_get(program, 0, UINT32_MAX, (iprop_input_info_ex*)storage, &error) == IPROP_EX_E_ARGUMENT && memcmp(storage, before, allocation) == 0);
        CHECK(iprop_program_part_input_get(program, 0, 0, (iprop_input_info_ex*)storage, &error) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + lengths[field] <= capacity && byte >= starts[field] && byte < starts[field] + lengths[field]) want = ((const unsigned char*)&color_input)[byte];
            CHECK(storage[byte] == want);
        }
        free(storage);
    }
    CHECK(iprop_part_info_ex_init(&part, sizeof(part)) == IPROP_EX_OK);
    CHECK(iprop_part_info_ex_init(NULL, sizeof(part)) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_get(program, 0, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_get(program, 0, 0, NULL, &error) == IPROP_EX_E_ARGUMENT);
    memcpy(&before_part, &part, sizeof(part)); memcpy(&before_input, &input, sizeof(input));
    CHECK(iprop_program_part_get(NULL, 0, &part, &error) == IPROP_EX_E_ARGUMENT && memcmp(&part, &before_part, sizeof(part)) == 0);
    CHECK(iprop_program_part_get(empty, 0, &part, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_get(program, 3, 0, &input, &error) == IPROP_EX_E_ARGUMENT && memcmp(&input, &before_input, sizeof(input)) == 0);
    CHECK(iprop_program_part_input_get(program, 1, 0, &input, &error) == IPROP_EX_E_ARGUMENT);
    part.flags = input.flags = 1;
    memcpy(&before_part, &part, sizeof(part)); memcpy(&before_input, &input, sizeof(input));
    CHECK(iprop_program_part_get(program, 0, &part, &error) == IPROP_EX_E_ARGUMENT && memcmp(&part, &before_part, sizeof(part)) == 0);
    CHECK(iprop_program_part_input_get(program, 0, 0, &input, &error) == IPROP_EX_E_ARGUMENT && memcmp(&input, &before_input, sizeof(input)) == 0);
    part.flags = input.flags = 0;
    count = index = UINT32_MAX;
    CHECK(iprop_program_part_count(NULL, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_part_count(program, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_count(program, 3, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_part_input_count(program, 0, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_find(program, 3, color_input.name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_part_input_find(NULL, 0, color_input.name, &index, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_find(program, 0, color_input.name, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_find(program, 0, (iprop_utf8_view){NULL, 1}, &index, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_part_input_find(program, 0, (iprop_utf8_view){NULL, 0}, &index, &error) == IPROP_EX_E_ARGUMENT);
    {
        const char invalid[] = {'\xc0', '\xaf'};
        CHECK(iprop_program_part_input_find(program, 0, (iprop_utf8_view){invalid, 2}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
    }
    /* Exact embedded-NUL names are shared binary strings but indices/types
     * still come from the selected root/part signature. */
    {
        const char embedded[] = {'\xc3', '\xa9', '\0', 'x', '\0'};
        uint8_t* modified = (uint8_t*)malloc(size);
        size_t at = 0, name_at = 0;
        uint32_t strings, section, const_entry = 0;
        iprop_program* binary = NULL;
        CHECK(modified != NULL); memcpy(modified, bytes, size);
        for (section = 0; section < read_u32(modified + 12); ++section)
            if (read_u32(modified + 20 + section * 12) == 2) { const_entry = 20 + section * 12; at = read_u32(modified + const_entry + 4); }
        CHECK(at != 0 && at + 4 <= size); strings = read_u32(modified + at); at += 4;
        for (i = 0; i < strings; ++i) {
            uint32_t length;
            CHECK(at + 4 <= size); length = read_u32(modified + at); at += 4; CHECK(length <= size - at);
            if (length == 4 && memcmp(modified + at, "base", 4) == 0) { CHECK(name_at == 0); name_at = at; memcpy(modified + at, embedded, 4); }
            at += length;
        }
        CHECK(name_at != 0);
        CHECK(iprop_program_load_ex(modified, size, &binary, &error) == IPROP_EX_OK);
        for (i = 0; i < 3; i += 2) {
            CHECK(iprop_program_part_input_get(binary, i, 0, &input, &error) == IPROP_EX_OK);
            CHECK(input.name.byte_count == 4 && memcmp(input.name.data, embedded, 4) == 0);
            CHECK(iprop_program_part_input_find(binary, i, (iprop_utf8_view){embedded, 4}, &index, &error) == IPROP_EX_OK && index == 0);
            index = UINT32_MAX;
            CHECK(iprop_program_part_input_find(binary, i, (iprop_utf8_view){embedded, 2}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
            CHECK(iprop_program_part_input_find(binary, i, (iprop_utf8_view){embedded, 3}, &index, &error) == IPROP_EX_E_ARGUMENT);
            CHECK(iprop_program_part_input_find(binary, i, (iprop_utf8_view){embedded, 5}, &index, &error) == IPROP_EX_E_ARGUMENT);
        }
        iprop_program_free(binary);
        /* Remove exactly the four bytes of the name, updating the section
         * bounds. Empty names are valid stored declarations, not absence. */
        write_u32(modified + name_at - 4, 0);
        memmove(modified + name_at, modified + name_at + 4, size - name_at - 4);
        write_u32(modified + const_entry + 8, read_u32(modified + const_entry + 8) - 4);
        for (section = 0; section < read_u32(modified + 12); ++section) {
            const size_t entry = 20 + section * 12;
            if (read_u32(modified + entry + 4) > name_at) write_u32(modified + entry + 4, read_u32(modified + entry + 4) - 4);
        }
        binary = NULL; CHECK(iprop_program_load_ex(modified, size - 4, &binary, &error) == IPROP_EX_OK);
        CHECK(iprop_program_input_find(binary, (iprop_utf8_view){NULL, 0}, &index, &error) == IPROP_EX_OK && index == 0);
        for (i = 0; i < 3; i += 2) {
            CHECK(iprop_program_part_input_get(binary, i, 0, &input, &error) == IPROP_EX_OK && input.name.byte_count == 0);
            CHECK(iprop_program_part_input_find(binary, i, (iprop_utf8_view){NULL, 0}, &index, &error) == IPROP_EX_OK && index == 0);
            CHECK(iprop_program_part_input_find(binary, i, (iprop_utf8_view){"base", 0}, &index, &error) == IPROP_EX_OK && index == 0);
        }
        iprop_program_free(binary); free(modified);
    }
    for (capacity = 0; capacity <= sizeof("part index is out of range"); ++capacity) {
        char* exact = (char*)malloc(capacity == 0 ? 1 : capacity);
        CHECK(exact != NULL); memset(exact, 0x5a, capacity == 0 ? 1 : capacity);
        error.message_buffer = exact; error.message_capacity = capacity;
        count = UINT32_MAX;
        CHECK(iprop_program_part_input_count(program, UINT32_MAX, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
        CHECK(error.message_required_bytes == strlen("part index is out of range"));
        CHECK(error.message_truncated == (capacity < sizeof("part index is out of range") ? 1u : 0u));
        if (capacity == 0) CHECK(exact[0] == 0x5a);
        else {
            const size_t copied = capacity - 1 < error.message_required_bytes ? capacity - 1 : (size_t)error.message_required_bytes;
            CHECK(memcmp(exact, "part index is out of range", copied) == 0 && exact[copied] == '\0');
        }
        CHECK(iprop_program_part_get(program, 0, &part, &error) == IPROP_EX_OK);
        CHECK(error.status == IPROP_EX_OK && error.message_required_bytes == 0 && error.message_truncated == 0);
        CHECK(capacity == 0 ? exact[0] == 0x5a : exact[0] == '\0'); free(exact);
    }
    for (capacity = 0; capacity <= sizeof(error) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        (void)iprop_error_ex_init((iprop_error_ex*)storage, capacity);
        memcpy(&part, &expected[0], sizeof(part)); memcpy(&before_part, &part, sizeof(part));
        CHECK(iprop_program_part_get(program, 2, &part, (iprop_error_ex*)storage) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(capacity >= 4 || memcmp(&part, &before_part, sizeof(part)) == 0);
        free(storage);
    }
    /* Malformed/aliased optional error output stops every publication. */
    error.message_buffer = NULL; error.message_capacity = 1; count = index = UINT32_MAX;
    memcpy(&before_part, &part, sizeof(part)); memcpy(&before_input, &input, sizeof(input));
    CHECK(iprop_program_part_count(program, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_part_get(program, 0, &part, &error) == IPROP_EX_E_ARGUMENT && memcmp(&part, &before_part, sizeof(part)) == 0);
    CHECK(iprop_program_part_input_count(program, 0, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_part_input_get(program, 0, 0, &input, &error) == IPROP_EX_E_ARGUMENT && memcmp(&input, &before_input, sizeof(input)) == 0);
    CHECK(iprop_program_part_input_find(program, 0, color_input.name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    {
        iprop_error_ex unchanged;
        error.message_buffer = (char*)&count; error.message_capacity = 1; memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_part_count(program, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
        CHECK(iprop_program_part_input_count(program, 0, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
        error.message_buffer = (char*)&part;
        CHECK(iprop_program_part_get(program, 0, &part, &error) == IPROP_EX_E_ARGUMENT && memcmp(&part, &before_part, sizeof(part)) == 0);
        error.message_buffer = (char*)&input;
        CHECK(iprop_program_part_input_get(program, 0, 0, &input, &error) == IPROP_EX_E_ARGUMENT && memcmp(&input, &before_input, sizeof(input)) == 0);
        error.message_buffer = (char*)&index;
        CHECK(iprop_program_part_input_find(program, 0, color_input.name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        error.message_buffer = message; error.message_capacity = sizeof(message); memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_part_get(program, 0, (iprop_part_info_ex*)&error, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_program_part_input_get(program, 0, 0, (iprop_input_info_ex*)&error, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
    }
    CHECK(iprop_program_part_get(program, 0, &part, &error) == IPROP_EX_OK);
    CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && error.location_flags == 0 && message[0] == '\0');
    CHECK(iprop_program_part_input_get(program, 0, 0, &input, NULL) == IPROP_EX_OK);
    CHECK(iprop_program_part_input_find(program, 0, color_input.name, &index, NULL) == IPROP_EX_OK && index == 0);
    memset(bytes, 0, size); free(bytes);
    CHECK(iprop_program_part_input_find(program, 0, color_input.name, &index, &error) == IPROP_EX_OK && index == 0);
    CHECK(memcmp(color_input.name.data, "base", 4) == 0);
    iprop_program_free(empty); iprop_program_free(program);
    puts("C99 part table/signatures: geometry bits, local indices, empty owner gaps, unavailable names, prefixes/UTF-8/transactions: PASS");
    return 0;
}
