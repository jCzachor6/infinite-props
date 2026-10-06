#include "iprop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "checked API: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char** argv) {
    FILE* file;
    long length;
    uint8_t* bytes;
    const uint8_t junk[] = {1, 2, 3};
    iprop_program* program = NULL;
    iprop_error_ex error;
    iprop_program_info_ex info;
    iprop_library_info_ex library;
    iprop_meta old;
    char message[128];
    uint32_t capacity;
    CHECK(argc == 2);
    file = fopen(argv[1], "rb");
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = (uint8_t*)malloc((size_t)length);
    CHECK(bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    CHECK(iprop_program_load_ex(NULL, 0, &program, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(program == NULL && error.stage == IPROP_STAGE_ARGUMENTS && message[0] != '\0');
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_load_ex(bytes, 0, &program, &error) == IPROP_EX_E_FORMAT);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &program, &error) == IPROP_EX_OK);
    CHECK(program != NULL && error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE);
    CHECK(error.location_flags == 0 && error.message_required_bytes == 0 && error.message_truncated == 0);
    CHECK(message[0] == '\0');
    memset(&info, 0xa5, sizeof(info));
    CHECK(iprop_program_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(iprop_program_query(program, &info, &error) == IPROP_EX_OK);
    iprop_program_meta(program, &old);
    CHECK(info.size_x == old.size_x && info.size_y == old.size_y);
    CHECK(info.origin_x == old.origin_x && info.origin_y == old.origin_y);
    CHECK(info.variants == old.variants && info.archetypes == old.archetypes && info.fuel == old.fuel);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library, &error) == IPROP_EX_OK);
    CHECK((library.capabilities & (IPROP_CAP_PROGRAM_LOAD_EX | IPROP_CAP_PROGRAM_QUERY |
          IPROP_CAP_STRUCTURED_ERRORS | IPROP_CAP_LIBRARY_QUERY_EX)) ==
          (IPROP_CAP_PROGRAM_LOAD_EX | IPROP_CAP_PROGRAM_QUERY | IPROP_CAP_STRUCTURED_ERRORS | IPROP_CAP_LIBRARY_QUERY_EX));

    /* Exact program-query allocations exercise complete fields, padding and
     * unknown tails. Failure and initializers also preserve every absent byte. */
    for (capacity = 0; capacity <= sizeof(info) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(info) + 16];
        size_t byte, field;
        const size_t starts[] = {offsetof(iprop_program_info_ex, size_x), offsetof(iprop_program_info_ex, size_y),
            offsetof(iprop_program_info_ex, origin_x), offsetof(iprop_program_info_ex, origin_y),
            offsetof(iprop_program_info_ex, variants), offsetof(iprop_program_info_ex, archetypes),
            offsetof(iprop_program_info_ex, fuel), offsetof(iprop_program_info_ex, has_emission),
            offsetof(iprop_program_info_ex, emission_color), offsetof(iprop_program_info_ex, emission_offset_x),
            offsetof(iprop_program_info_ex, emission_offset_y), offsetof(iprop_program_info_ex, emission_radius),
            offsetof(iprop_program_info_ex, format_major), offsetof(iprop_program_info_ex, format_minor),
            offsetof(iprop_program_info_ex, container_flags)};
        const size_t widths[] = {4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4};
        CHECK(storage != NULL);
        memset(storage, 0xa5, allocation);
        memcpy(storage, &capacity, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_program_query(NULL, (iprop_program_info_ex*)storage, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(storage, before, allocation) == 0);
        CHECK(iprop_program_query(program, (iprop_program_info_ex*)storage, &error) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char expected = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field])
                    expected = ((const unsigned char*)&info)[byte];
            CHECK(storage[byte] == expected);
        }
        memcpy(storage, before, allocation);
        CHECK(iprop_program_info_ex_init((iprop_program_info_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 4; byte < allocation; ++byte) {
            unsigned char expected = before[byte];
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field)
                if (starts[field] + widths[field] <= capacity && byte >= starts[field] && byte < starts[field] + widths[field]) expected = 0;
            CHECK(storage[byte] == expected);
        }
        free(storage);
    }
    CHECK(iprop_program_info_ex_init(NULL, sizeof(info)) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_program_query(program, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_library_query_ex(NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_INVALID_ARGUMENT);

    /* Every error descriptor prefix is optional when valid, including four
     * bytes only. Invalid capacities stop publication of a replacement handle. */
    for (capacity = 0; capacity <= sizeof(error) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        iprop_program* candidate = program;
        CHECK(storage != NULL);
        memset(storage, 0xa5, allocation);
        memcpy(storage, &capacity, 4);
        (void)iprop_error_ex_init((iprop_error_ex*)storage, capacity);
        CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &candidate, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(memcmp(storage, &capacity, 4) == 0);
        if (capacity < 4) CHECK(candidate == program);
        else { CHECK(candidate != NULL && candidate != program); iprop_program_free(candidate); }
        free(storage);
    }
    {
        iprop_program* candidate = program;
        uint64_t required;
        CHECK(iprop_program_load_ex(junk, sizeof(junk), &candidate, &error) == IPROP_EX_E_FORMAT);
        required = error.message_required_bytes;
        CHECK(candidate == program && required > 0 && required < sizeof(message));
        for (capacity = 0; capacity <= required + 1; ++capacity) {
            const size_t allocation = capacity == 0 ? 1 : capacity;
            char* text = (char*)malloc(allocation);
            CHECK(text != NULL);
            memset(text, 0xa5, allocation);
            error.message_buffer = text;
            error.message_capacity = capacity;
            CHECK(iprop_program_load_ex(junk, sizeof(junk), &candidate, &error) == IPROP_EX_E_FORMAT);
            CHECK(candidate == program && error.message_required_bytes == required);
            CHECK(error.message_truncated == (capacity <= required ? 1u : 0u));
            if (capacity == 0) CHECK((unsigned char)text[0] == 0xa5);
            else CHECK(strlen(text) == (required < capacity - 1 ? (size_t)required : capacity - 1));
            free(text);
        }
        error.message_buffer = NULL;
        error.message_capacity = 12;
        CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &candidate, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(candidate == program && error.reason == IPROP_REASON_INVALID_ARGUMENT && error.location_flags == 0);
        {
            iprop_program_info_ex preserved;
            memcpy(&preserved, &info, sizeof(info));
            CHECK(iprop_program_query(program, &preserved, &error) == IPROP_EX_E_ARGUMENT);
            CHECK(memcmp(&preserved, &info, sizeof(info)) == 0);
        }
        /* Error text could otherwise corrupt the published pointer on success. */
        error.message_buffer = (char*)&candidate;
        error.message_capacity = 1;
        {
            iprop_error_ex unchanged;
            memcpy(&unchanged, &error, sizeof(error));
            CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &candidate, &error) == IPROP_EX_E_ARGUMENT);
            CHECK(candidate == program && memcmp(&error, &unchanged, sizeof(error)) == 0);
        }
        error.message_buffer = (char*)&info;
        error.message_capacity = 1;
        CHECK(iprop_program_query(program, &info, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(info.size_x == old.size_x);
        error.message_buffer = (char*)&error;
        error.message_capacity = 1;
        CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &candidate, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(candidate == program);
    }
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    {
        iprop_error_ex unchanged;
        memcpy(&unchanged, &error, sizeof(error));
        CHECK(iprop_program_query(program, (iprop_program_info_ex*)&error, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_library_query_ex((iprop_library_info_ex*)&error, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(iprop_program_load_ex(bytes, (uint64_t)length, (iprop_program**)&error.byte_offset, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&error, &unchanged, sizeof(error)) == 0);
    }
    CHECK(iprop_program_query(program, &info, &error) == IPROP_EX_OK);
    CHECK(message[0] == '\0' && error.message_required_bytes == 0 && error.location_flags == 0);
    CHECK(iprop_program_query(program, &info, NULL) == IPROP_EX_OK);
    CHECK(iprop_library_query_ex(&library, NULL) == IPROP_EX_OK);
    {
        iprop_generate_options options = {0};
        iprop_uniform uniform = {0};
        iprop_result* result = NULL;
        uniform.name = "base";
        uniform.type = (iprop_uniform_type)99;
        options.uniforms = &uniform;
        options.uniform_count = 1;
        strcpy(message, "SENTINEL");
        CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_E_ARGUMENT);
        CHECK(result == NULL && strcmp(message, "SENTINEL") == 0);
        /* Preserve the released implementation's negative-tag fallback. This
         * is an observed compatibility check, not a new accepted input type. */
        uniform.type = (iprop_uniform_type)-1;
        CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
        CHECK(result != NULL && strcmp(message, "SENTINEL") == 0);
        {
            iprop_image image = {0};
            iprop_result_image(result, &image);
            CHECK(image.pixels[0] == 0 && image.pixels[1] == 0 && image.pixels[2] == 0 && image.pixels[3] == 0);
        }
        iprop_result_free(result);
    }
    /* Loaded storage is independent of the caller's byte array. */
    memset(bytes, 0, (size_t)length);
    free(bytes);
    CHECK(iprop_program_query(program, &info, &error) == IPROP_EX_OK);
    {
        iprop_generate_options options = {0};
        iprop_result* result = NULL;
        iprop_image image = {0};
        CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
        iprop_result_image(result, &image);
        CHECK(image.pixels[0] == 0x33 && image.pixels[1] == 0x55 && image.pixels[2] == 0xaa);
        iprop_result_free(result);
    }
    iprop_program_free(program);
    puts("C99 checked load/query, exact prefixes, message bounds, resets, output transactions and alias rejection: PASS");
    return 0;
}
