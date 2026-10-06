#include "iprop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "extended metadata: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* Pin the entire original prefix without relying on a platform's alignment. */
typedef struct {
    uint32_t struct_size, size_x, size_y;
    float origin_x, origin_y;
    uint32_t variants, archetypes;
    uint64_t fuel;
} old_program_info;
#define SAME_OFFSET(field) typedef char old_offset_##field[(offsetof(old_program_info, field) == offsetof(iprop_program_info_ex, field)) ? 1 : -1]
SAME_OFFSET(struct_size); SAME_OFFSET(size_x); SAME_OFFSET(size_y); SAME_OFFSET(origin_x);
SAME_OFFSET(origin_y); SAME_OFFSET(variants); SAME_OFFSET(archetypes); SAME_OFFSET(fuel);
#undef SAME_OFFSET
typedef char extension_begins_after_old_size[(offsetof(iprop_program_info_ex, has_emission) == sizeof(old_program_info)) ? 1 : -1];

static uint32_t read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static void write_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value; bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16); bytes[3] = (uint8_t)(value >> 24);
}
static uint32_t bits(const float* value) { uint32_t result; memcpy(&result, value, 4); return result; }
static int read_fixture(const char* path, uint8_t** bytes, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file); CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    *bytes = (uint8_t*)malloc((size_t)length);
    CHECK(*bytes != NULL && fread(*bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0); *size = (size_t)length; return 0;
}

static int exact_prefixes(iprop_program* program, const iprop_program_info_ex* expected, iprop_error_ex* error) {
    uint32_t capacity;
    const size_t starts[] = {offsetof(iprop_program_info_ex, size_x), offsetof(iprop_program_info_ex, size_y),
        offsetof(iprop_program_info_ex, origin_x), offsetof(iprop_program_info_ex, origin_y),
        offsetof(iprop_program_info_ex, variants), offsetof(iprop_program_info_ex, archetypes),
        offsetof(iprop_program_info_ex, fuel), offsetof(iprop_program_info_ex, has_emission),
        offsetof(iprop_program_info_ex, emission_color), offsetof(iprop_program_info_ex, emission_offset_x),
        offsetof(iprop_program_info_ex, emission_offset_y), offsetof(iprop_program_info_ex, emission_radius),
        offsetof(iprop_program_info_ex, format_major), offsetof(iprop_program_info_ex, format_minor),
        offsetof(iprop_program_info_ex, container_flags)};
    const size_t widths[] = {4,4,4,4,4,4,8,4,4,4,4,4,4,4,4};
    for (capacity = 0; capacity <= sizeof(*expected) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(*expected) + 16];
        size_t byte, field;
        CHECK(storage != NULL); memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_program_query(NULL, (iprop_program_info_ex*)storage, error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(storage, before, allocation) == 0);
        CHECK(iprop_program_query(program, (iprop_program_info_ex*)storage, error) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field]) want = ((const unsigned char*)expected)[byte];
            CHECK(storage[byte] == want);
        }
        memset(storage, 0xa5, allocation);
        CHECK(iprop_program_info_ex_init((iprop_program_info_ex*)storage, capacity) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field]) want = 0;
            CHECK(storage[byte] == want);
        }
        free(storage);
    }
    return 0;
}

int main(int argc, char** argv) {
    uint8_t *bytes, *plain = NULL;
    size_t size, plain_size = 0;
    uint32_t variant, section, meta_entry = 0, meta_at = 0, physical_major, physical_minor;
    iprop_program *program = NULL, *emissive = NULL;
    iprop_program_info_ex info, before;
    iprop_error_ex error;
    iprop_library_info_ex library;
    char message[64];
    CHECK(argc == 4);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library, &error) == IPROP_EX_OK && (library.capabilities & IPROP_CAP_METADATA_EXTENDED) != 0);
    for (variant = 1; variant <= 3; ++variant) {
        CHECK(read_fixture(argv[variant], &bytes, &size) == 0);
        CHECK(read_u32(bytes + 8) == (variant == 1 ? 0u : variant == 2 ? 1u : 3u));
        CHECK(iprop_program_load_ex(bytes, size, &program, &error) == IPROP_EX_OK);
        CHECK(iprop_program_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
        CHECK(iprop_program_query(program, &info, &error) == IPROP_EX_OK);
        CHECK(info.format_major == ((uint32_t)bytes[4] | (uint32_t)bytes[5] << 8));
        CHECK(info.format_minor == ((uint32_t)bytes[6] | (uint32_t)bytes[7] << 8));
        CHECK(info.container_flags == read_u32(bytes + 8));
        CHECK(info.has_emission == 0 && info.emission_color.r == 0 && info.emission_color.g == 0 && info.emission_color.b == 0 && info.emission_color.a == 0);
        CHECK(bits(&info.emission_offset_x) == 0 && bits(&info.emission_offset_y) == 0 && bits(&info.emission_radius) == 0);
        CHECK(exact_prefixes(program, &info, &error) == 0);
        iprop_program_free(program); program = NULL;
        if (variant == 1) { plain = bytes; plain_size = size; } else free(bytes);
    }
    /* Add a real emission block to a stripped physical container. Rebase only
     * later section offsets; no debug addresses or source metadata are involved. */
    CHECK(plain != NULL);
    physical_major = (uint32_t)plain[4] | (uint32_t)plain[5] << 8;
    physical_minor = (uint32_t)plain[6] | (uint32_t)plain[7] << 8;
    for (section = 0; section < read_u32(plain + 12); ++section)
        if (read_u32(plain + 20 + section * 12) == 1) { meta_entry = 20 + section * 12; meta_at = read_u32(plain + meta_entry + 4); }
    CHECK(meta_entry != 0 && meta_at + 40 <= plain_size && plain[meta_at + 32] == 0);
    bytes = (uint8_t*)malloc(plain_size + 16); CHECK(bytes != NULL);
    memcpy(bytes, plain, meta_at + 40);
    memcpy(bytes + meta_at + 56, plain + meta_at + 40, plain_size - meta_at - 40);
    bytes[meta_at + 32] = 1;
    bytes[meta_at + 40] = 0x12; bytes[meta_at + 41] = 0x34; bytes[meta_at + 42] = 0x56; bytes[meta_at + 43] = 0x78;
    write_u32(bytes + meta_at + 44, UINT32_C(0x80000000));
    write_u32(bytes + meta_at + 48, UINT32_C(0x00000001));
    write_u32(bytes + meta_at + 52, UINT32_C(0x7fc12345));
    write_u32(bytes + meta_entry + 8, read_u32(bytes + meta_entry + 8) + 16);
    for (section = 0; section < read_u32(bytes + 12); ++section) {
        const size_t entry = 20 + section * 12;
        if (read_u32(bytes + entry + 4) >= meta_at + 40) write_u32(bytes + entry + 4, read_u32(bytes + entry + 4) + 16);
    }
    CHECK(iprop_program_load_ex(bytes, plain_size + 16, &emissive, &error) == IPROP_EX_OK);
    memset(bytes, 0, plain_size + 16); free(bytes); free(plain);
    CHECK(iprop_program_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(iprop_program_query(emissive, &info, &error) == IPROP_EX_OK);
    CHECK(info.has_emission == 1 && info.emission_color.r == 0x12 && info.emission_color.g == 0x34 && info.emission_color.b == 0x56 && info.emission_color.a == 0x78);
    CHECK(bits(&info.emission_offset_x) == UINT32_C(0x80000000) && bits(&info.emission_offset_y) == 1 && bits(&info.emission_radius) == UINT32_C(0x7fc12345));
    CHECK(info.format_major == physical_major && info.format_minor == physical_minor && info.container_flags == 0);
    CHECK(exact_prefixes(emissive, &info, &error) == 0);
    memcpy(&before, &info, sizeof(info));
    error.message_buffer = (char*)&info.emission_color; error.message_capacity = 1;
    CHECK(iprop_program_query(emissive, &info, &error) == IPROP_EX_E_ARGUMENT && memcmp(&before, &info, sizeof(info)) == 0);
    error.message_buffer = NULL; error.message_capacity = 1;
    CHECK(iprop_program_query(emissive, &info, &error) == IPROP_EX_E_ARGUMENT && memcmp(&before, &info, sizeof(info)) == 0);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    CHECK(iprop_program_query(NULL, &info, &error) == IPROP_EX_E_ARGUMENT && memcmp(&before, &info, sizeof(info)) == 0);
    CHECK(iprop_program_query(emissive, &info, &error) == IPROP_EX_OK && error.status == IPROP_EX_OK && message[0] == '\0');
    CHECK(iprop_program_query(emissive, &info, NULL) == IPROP_EX_OK);
    iprop_program_free(emissive);
    puts("C99 extended metadata: physical format, exact emission bits, frozen offsets, prefixes/padding/tail, errors/aliases: PASS");
    return 0;
}
