#include "iprop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "output declarations: line %d: %s\n", __LINE__, #c); return 1; } } while (0)
typedef char output_kind_is_fixed_width[(sizeof(iprop_output_kind_ex) == 4) ? 1 : -1];

static int read_fixture(const char* path, uint8_t** out, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    *out = (uint8_t*)malloc((size_t)length);
    CHECK(*out != NULL && fread(*out, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    *size = (size_t)length;
    return 0;
}
static uint32_t read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* Independent plain format-0.1 binary with a root singular point and one
 * uncalled part's empty rect list sharing the exact supplied name. */
typedef struct { uint8_t bytes[256]; uint32_t size; } buffer;
static void put8(buffer* out, uint8_t value) { out->bytes[out->size++] = value; }
static void put16(buffer* out, uint16_t value) { put8(out, (uint8_t)value); put8(out, (uint8_t)(value >> 8)); }
static void put32(buffer* out, uint32_t value) { put16(out, (uint16_t)value); put16(out, (uint16_t)(value >> 16)); }
static void append(buffer* out, const void* data, uint32_t length) {
    if (length != 0) memcpy(out->bytes + out->size, data, length);
    out->size += length;
}
static buffer format01(const char* name, uint32_t name_length) {
    buffer meta = {{0}, 0}, pool = {{0}, 0}, code = {{0}, 0}, file = {{0}, 0};
    uint32_t i, offset = 56;
    put32(&meta, 1); put32(&meta, 1); put32(&meta, 0); put32(&meta, 0);
    put32(&meta, 1); put32(&meta, 1); put32(&meta, 1); put32(&meta, 0);
    put32(&meta, 0); put32(&meta, 0); put16(&meta, 0); put16(&meta, 1);
    put8(&meta, 0); put8(&meta, 0); put16(&meta, 0); put32(&meta, 1);
    put32(&pool, 2); put32(&pool, 4); append(&pool, "draw", 4);
    put32(&pool, name_length); append(&pool, name, name_length);
    put32(&pool, 0); put32(&pool, 0); put32(&pool, 0);
    put32(&code, 2);
    for (i = 0; i < 2; ++i) {
        put32(&code, i == 0 ? 0 : UINT32_MAX); put32(&code, 0); put32(&code, 0);
        put16(&code, 2); put16(&code, 0); put16(&code, 0); put16(&code, 0);
    }
    put32(&code, 1); put32(&code, 1); put32(&code, 1); put32(&code, 1);
    put32(&code, 0); put32(&code, 0); put16(&code, 0); put16(&code, 1);
    put8(&code, 1); put8(&code, 1); put16(&code, 0); put32(&code, 1);
    append(&file, "IPRP", 4); put16(&file, 0); put16(&file, 1);
    put32(&file, 0); put32(&file, 3); put32(&file, 0);
    put32(&file, 1); put32(&file, offset); put32(&file, meta.size); offset += meta.size;
    put32(&file, 2); put32(&file, offset); put32(&file, pool.size); offset += pool.size;
    put32(&file, 3); put32(&file, offset); put32(&file, code.size);
    append(&file, meta.bytes, meta.size); append(&file, pool.bytes, pool.size); append(&file, code.bytes, code.size);
    return file;
}

int main(int argc, char** argv) {
    uint8_t *bytes, *other_bytes;
    size_t size, other_size;
    iprop_program *program = NULL, *empty = NULL;
    iprop_output_info_ex info, before_info, expected[18];
    iprop_error_ex error;
    iprop_library_info_ex library;
    char message[128];
    uint32_t count = UINT32_MAX, index = UINT32_MAX, capacity, i, variant;
    const char* names[] = {"anchor", "collider", "zone", "anchors", "colliders", "zones"};
    CHECK(argc == 5);
    CHECK(read_fixture(argv[1], &bytes, &size) == 0);
    CHECK(read_u32(bytes + 8) == 0);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    CHECK(iprop_program_load_ex(bytes, size, &program, &error) == IPROP_EX_OK);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library, &error) == IPROP_EX_OK);
    CHECK((library.capabilities & IPROP_CAP_OUTPUT_DECLARATIONS) != 0);
    CHECK(IPROP_OUTPUT_KIND_POINT == 0 && IPROP_OUTPUT_KIND_RECT == 1 && IPROP_OUTPUT_KIND_CIRCLE == 2);
    /* Reflection happens before any generation. The middle part owns no slots,
     * so the following owner's number is 3, not a compacted output-owner ID. */
    CHECK(iprop_program_output_count(program, &count, &error) == IPROP_EX_OK && count == 18);
    for (i = 0; i < count; ++i) {
        const uint32_t local = i % 6, owner = i < 6 ? 0 : i < 12 ? 1 : 3;
        const uint32_t is_list = owner == 3 && (local == 0 || local == 3) ? local == 0 : local >= 3;
        CHECK(iprop_output_info_ex_init(&expected[i], sizeof(expected[i])) == IPROP_EX_OK);
        CHECK(iprop_program_output_get(program, i, &expected[i], &error) == IPROP_EX_OK);
        CHECK(expected[i].output_index == i && expected[i].owner == owner);
        CHECK(expected[i].kind == local % 3 && expected[i].is_list == is_list);
        CHECK(expected[i].name.byte_count == strlen(names[local]));
        CHECK(memcmp(expected[i].name.data, names[local], expected[i].name.byte_count) == 0);
        CHECK(iprop_program_output_find(program, owner, expected[i].name, &index, &error) == IPROP_EX_OK && index == i);
    }
    /* Debug data and compression do not remove or rename declarations. */
    for (variant = 2; variant <= 3; ++variant) {
        iprop_program* encoded = NULL;
        CHECK(read_fixture(argv[variant], &other_bytes, &other_size) == 0);
        CHECK(read_u32(other_bytes + 8) == (variant == 2 ? 1u : 3u));
        CHECK(iprop_program_load_ex(other_bytes, other_size, &encoded, &error) == IPROP_EX_OK);
        memset(other_bytes, 0, other_size); free(other_bytes);
        CHECK(iprop_program_output_count(encoded, &count, &error) == IPROP_EX_OK && count == 18);
        for (i = 0; i < count; ++i) {
            CHECK(iprop_output_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
            CHECK(iprop_program_output_get(encoded, i, &info, &error) == IPROP_EX_OK);
            CHECK(info.output_index == i && info.owner == expected[i].owner && info.kind == expected[i].kind && info.is_list == expected[i].is_list);
            CHECK(info.name.byte_count == expected[i].name.byte_count && memcmp(info.name.data, expected[i].name.data, info.name.byte_count) == 0);
        }
        iprop_program_free(encoded);
    }
    CHECK(read_fixture(argv[4], &other_bytes, &other_size) == 0);
    CHECK(iprop_program_load_ex(other_bytes, other_size, &empty, &error) == IPROP_EX_OK);
    free(other_bytes);
    CHECK(iprop_program_output_count(empty, &count, NULL) == IPROP_EX_OK && count == 0);

    for (capacity = 0; capacity <= sizeof(info) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(info) + 16];
        size_t byte, field;
        const size_t starts[] = {offsetof(iprop_output_info_ex, output_index), offsetof(iprop_output_info_ex, owner),
            offsetof(iprop_output_info_ex, kind), offsetof(iprop_output_info_ex, is_list),
            offsetof(iprop_output_info_ex, name) + offsetof(iprop_utf8_view, data),
            offsetof(iprop_output_info_ex, name) + offsetof(iprop_utf8_view, byte_count)};
        const size_t widths[] = {4, 4, 4, 4, sizeof(const char*), 4};
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        if (capacity >= 8) memset(storage + offsetof(iprop_output_info_ex, flags), 0, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_program_output_get(program, UINT32_MAX, (iprop_output_info_ex*)storage, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(storage, before, allocation) == 0);
        CHECK(iprop_program_output_get(program, 12, (iprop_output_info_ex*)storage, &error) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field])
                    want = ((const unsigned char*)&expected[12])[byte];
            CHECK(storage[byte] == want);
        }
        memset(storage, 0xa5, allocation);
        CHECK(iprop_output_info_ex_init((iprop_output_info_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            if (capacity >= 8 && byte >= offsetof(iprop_output_info_ex, flags) && byte < offsetof(iprop_output_info_ex, flags) + 4) want = 0;
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field]) want = 0;
            CHECK(storage[byte] == want);
        }
        free(storage);
    }
    CHECK(iprop_output_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(iprop_output_info_ex_init(NULL, sizeof(info)) == IPROP_EX_E_ARGUMENT);
    memcpy(&before_info, &info, sizeof(info));
    CHECK(iprop_program_output_get(NULL, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_output_get(empty, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&info, &before_info, sizeof(info)) == 0);
    CHECK(iprop_program_output_get(program, 0, NULL, &error) == IPROP_EX_E_ARGUMENT);
    for (i = 0; i < 2; ++i) {
        info.flags = i == 0 ? 1 : UINT32_MAX; memcpy(&before_info, &info, sizeof(info));
        CHECK(iprop_program_output_get(program, 0, &info, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&info, &before_info, sizeof(info)) == 0);
    }
    info.flags = 0; memcpy(&before_info, &info, sizeof(info));
    index = count = UINT32_MAX;
    CHECK(iprop_program_output_count(NULL, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_output_count(program, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_output_find(NULL, 0, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_output_find(program, 0, expected[0].name, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_output_find(program, 4, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_output_find(program, UINT32_MAX, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_output_find(program, 2, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(strcmp(message, "output name was not found for this owner") == 0);
    CHECK(iprop_program_output_find(program, 0, (iprop_utf8_view){"ANCHOR", 6}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_output_find(program, 0, (iprop_utf8_view){NULL, 1}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_output_find(program, 0, (iprop_utf8_view){NULL, 0}, &index, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strcmp(message, "output name was not found for this owner") == 0);
    {
        const unsigned char invalid[][4] = {{0x80}, {0xc0,0xaf}, {0xe2,0x82}, {0xed,0xa0,0x80}, {0xf4,0x90,0x80,0x80}};
        const uint32_t lengths[] = {1, 2, 2, 3, 4};
        for (i = 0; i < 5; ++i) {
            char* exact = (char*)malloc(lengths[i]);
            CHECK(exact != NULL); memcpy(exact, invalid[i], lengths[i]);
            CHECK(iprop_program_output_find(program, 0, (iprop_utf8_view){exact, lengths[i]}, &index, &error) == IPROP_EX_E_ARGUMENT);
            CHECK(index == UINT32_MAX && error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
            free(exact);
        }
    }
    /* The old binary format has all required declaration metadata, including
     * duplicate cross-owner names, empty declarations and valid empty names. */
    for (variant = 0; variant < 2; ++variant) {
        const buffer old = format01(variant == 0 ? "same" : NULL, variant == 0 ? 4 : 0);
        iprop_program* original = NULL;
        CHECK(iprop_program_load_ex(old.bytes, old.size, &original, &error) == IPROP_EX_OK);
        CHECK(iprop_program_output_count(original, &count, &error) == IPROP_EX_OK && count == 2);
        {
            iprop_program_info_ex metadata;
            CHECK(iprop_program_info_ex_init(&metadata, sizeof(metadata)) == IPROP_EX_OK);
            CHECK(iprop_program_query(original, &metadata, &error) == IPROP_EX_OK);
            CHECK(metadata.format_major == 0 && metadata.format_minor == 1 && metadata.container_flags == 0);
            CHECK(metadata.has_emission == 0 && metadata.emission_color.r == 0 && metadata.emission_color.g == 0 &&
                  metadata.emission_color.b == 0 && metadata.emission_color.a == 0 &&
                  metadata.emission_offset_x == 0.f && metadata.emission_offset_y == 0.f && metadata.emission_radius == 0.f);
        }
        {
            iprop_part_info_ex part;
            CHECK(iprop_part_info_ex_init(&part, sizeof(part)) == IPROP_EX_OK);
            CHECK(iprop_program_part_count(original, &count, &error) == IPROP_EX_OK && count == 1);
            CHECK(iprop_program_part_get(original, 0, &part, &error) == IPROP_EX_OK);
            CHECK(part.part_index == 0 && part.owner == 1 && part.size_x == 1 && part.size_y == 1);
            CHECK(part.origin_x == 0.f && part.origin_y == 0.f && part.input_count == 0);
            CHECK(part.output_begin == 1 && part.output_count == 1);
            CHECK(part.name_present == 0 && part.name.data == NULL && part.name.byte_count == 0);
            CHECK(iprop_program_part_input_count(original, 0, &count, &error) == IPROP_EX_OK && count == 0);
        }
        for (i = 0; i < 2; ++i) {
            CHECK(iprop_program_output_get(original, i, &info, &error) == IPROP_EX_OK);
            CHECK(info.owner == i && info.kind == i && info.is_list == i);
            CHECK(info.name.byte_count == (variant == 0 ? 4u : 0u));
            CHECK(iprop_program_output_find(original, i, (iprop_utf8_view){variant == 0 ? "same" : NULL, variant == 0 ? 4u : 0u}, &index, &error) == IPROP_EX_OK && index == i);
        }
        iprop_program_free(original);
    }
    {
        const char embedded[] = {(char)0xc3, (char)0xa9, '\0', 'x', 'y', 'z', '\0'};
        uint8_t* modified = (uint8_t*)malloc(size);
        size_t at = 0;
        uint32_t strings, section;
        int replaced = 0;
        iprop_program* binary = NULL;
        CHECK(modified != NULL); memcpy(modified, bytes, size);
        for (section = 0; section < read_u32(modified + 12); ++section)
            if (read_u32(modified + 20 + section * 12) == 2) at = read_u32(modified + 24 + section * 12);
        CHECK(at != 0 && at + 4 <= size); strings = read_u32(modified + at); at += 4;
        for (i = 0; i < strings; ++i) {
            uint32_t length;
            CHECK(at + 4 <= size); length = read_u32(modified + at); at += 4; CHECK(length <= size - at);
            if (length == 6 && memcmp(modified + at, "anchor", 6) == 0) {
                memcpy(modified + at, embedded, 6); ++replaced;
            }
            at += length;
        }
        CHECK(replaced != 0);
        CHECK(iprop_program_load_ex(modified, size, &binary, &error) == IPROP_EX_OK);
        memset(modified, 0, size); free(modified);
        for (i = 0; i < 3; ++i) {
            const uint32_t owner = i == 2 ? 3 : i;
            CHECK(iprop_program_output_get(binary, i * 6, &info, &error) == IPROP_EX_OK);
            CHECK(info.name.byte_count == 6 && memcmp(info.name.data, embedded, 6) == 0);
            CHECK(iprop_program_output_find(binary, owner, (iprop_utf8_view){embedded, 6}, &index, &error) == IPROP_EX_OK && index == i * 6);
            index = UINT32_MAX;
            CHECK(iprop_program_output_find(binary, owner, (iprop_utf8_view){embedded, 2}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
            CHECK(iprop_program_output_find(binary, owner, (iprop_utf8_view){embedded, 3}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
            CHECK(iprop_program_output_find(binary, owner, (iprop_utf8_view){embedded, 7}, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        }
        iprop_program_free(binary);
    }
    for (capacity = 0; capacity <= sizeof(error) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        (void)iprop_error_ex_init((iprop_error_ex*)storage, capacity);
        memcpy(&info, &expected[0], sizeof(info)); memcpy(&before_info, &info, sizeof(info));
        CHECK(iprop_program_output_get(program, 12, &info, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(capacity >= 4 || memcmp(&info, &before_info, sizeof(info)) == 0);
        free(storage);
    }
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    for (capacity = 0; capacity <= sizeof("output owner is out of range"); ++capacity) {
        char* exact = (char*)malloc(capacity == 0 ? 1 : capacity);
        CHECK(exact != NULL); memset(exact, 0x5a, capacity == 0 ? 1 : capacity);
        error.message_buffer = exact; error.message_capacity = capacity;
        index = UINT32_MAX;
        CHECK(iprop_program_output_find(program, UINT32_MAX, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(error.message_required_bytes == strlen("output owner is out of range"));
        CHECK(error.message_truncated == (capacity < sizeof("output owner is out of range")));
        if (capacity == 0) CHECK(exact[0] == 0x5a);
        else {
            const size_t copied = capacity - 1 < error.message_required_bytes ? capacity - 1 : (size_t)error.message_required_bytes;
            CHECK(memcmp(exact, "output owner is out of range", copied) == 0 && exact[copied] == '\0');
        }
        CHECK(iprop_program_output_find(program, 0, expected[0].name, &index, &error) == IPROP_EX_OK && index == 0);
        CHECK(error.status == IPROP_EX_OK && error.message_required_bytes == 0 && error.message_truncated == 0);
        CHECK(capacity == 0 ? exact[0] == 0x5a : exact[0] == '\0'); free(exact);
    }
    error.message_buffer = NULL; error.message_capacity = 1;
    count = index = UINT32_MAX; memcpy(&before_info, &info, sizeof(info));
    CHECK(iprop_program_output_count(program, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
    CHECK(iprop_program_output_find(program, 0, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
    CHECK(iprop_program_output_get(program, 0, &info, &error) == IPROP_EX_E_ARGUMENT && memcmp(&info, &before_info, sizeof(info)) == 0);
    {
        iprop_error_ex unchanged;
        error.message_buffer = (char*)&count; error.message_capacity = 1; memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_output_count(program, &count, &error) == IPROP_EX_E_ARGUMENT && count == UINT32_MAX);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
        error.message_buffer = (char*)&index; memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_output_find(program, 0, expected[0].name, &index, &error) == IPROP_EX_E_ARGUMENT && index == UINT32_MAX);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
        error.message_buffer = (char*)&info;
        CHECK(iprop_program_output_get(program, 0, &info, &error) == IPROP_EX_E_ARGUMENT && memcmp(&info, &before_info, sizeof(info)) == 0);
        error.message_buffer = message; error.message_capacity = sizeof(message); memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_output_get(program, 0, (iprop_output_info_ex*)&error, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_program_output_find(program, 0, expected[0].name, &error.stage, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
    }
    {
        char writable[] = "anchor";
        error.message_buffer = writable; error.message_capacity = sizeof(writable);
        CHECK(iprop_program_output_find(program, 3, (iprop_utf8_view){writable, 6}, &index, &error) == IPROP_EX_OK && index == 12 && writable[0] == '\0');
        memcpy(&index, "zone", 4);
        CHECK(iprop_program_output_find(program, 0, (iprop_utf8_view){(const char*)&index, 4}, &index, NULL) == IPROP_EX_OK && index == 2);
    }
    error.message_buffer = message; error.message_capacity = sizeof(message);
    memset(bytes, 0, size); free(bytes);
    /* The frozen generation projection exposes these same global declarations,
     * retaining unassigned singulars/lists and the skipped empty owner. */
    {
        iprop_generate_options options = {0};
        iprop_result* result = NULL;
        iprop_image image = {0};
        CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
        iprop_result_image(result, &image);
        CHECK(image.output_count == 18);
        for (i = 0; i < image.output_count; ++i) {
            const iprop_output* old = &image.outputs[i];
            CHECK(old->owner == expected[i].owner && (uint32_t)old->kind == expected[i].kind && (uint32_t)old->is_list == expected[i].is_list);
            CHECK(strlen(old->name) == expected[i].name.byte_count && memcmp(old->name, expected[i].name.data, expected[i].name.byte_count) == 0);
            CHECK(old->count == (i >= 6 && i < 9 ? 1u : i == 12 ? 2u : 0u));
            CHECK(iprop_program_output_get(program, i, &info, NULL) == IPROP_EX_OK && info.output_index == i);
        }
        iprop_result_free(result);
    }
    iprop_program_free(empty); iprop_program_free(program);
    puts("C99 output declarations: root/parts/empty owners, exact global schema, formats/UTF-8, prefixes and transactions: PASS");
    return 0;
}
