#include "iprop.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "library query: line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define LAYOUT(type, size) typedef char layout_##type[(sizeof(type) == (size)) ? 1 : -1]
#define OFFSET(type, field, offset) typedef char offset_##type##_##field[(offsetof(type, field) == (offset)) ? 1 : -1]

/* Full legacy layouts, not just sizeof: new API must preserve array stride. */
LAYOUT(iprop_uniform, 24); LAYOUT(iprop_generate_options, 40);
LAYOUT(iprop_meta, 32); LAYOUT(iprop_output, 40); LAYOUT(iprop_image, 32);
OFFSET(iprop_uniform, name, 0); OFFSET(iprop_uniform, type, 8);
OFFSET(iprop_uniform, f, 12); OFFSET(iprop_uniform, u, 16);
OFFSET(iprop_uniform, r, 20); OFFSET(iprop_uniform, g, 21);
OFFSET(iprop_uniform, b, 22); OFFSET(iprop_uniform, a, 23);
OFFSET(iprop_generate_options, seed, 0); OFFSET(iprop_generate_options, variant, 4);
OFFSET(iprop_generate_options, archetype, 8); OFFSET(iprop_generate_options, size_x, 12);
OFFSET(iprop_generate_options, size_y, 16); OFFSET(iprop_generate_options, uniforms, 24);
OFFSET(iprop_generate_options, uniform_count, 32);
OFFSET(iprop_meta, size_x, 0); OFFSET(iprop_meta, size_y, 4);
OFFSET(iprop_meta, origin_x, 8); OFFSET(iprop_meta, origin_y, 12);
OFFSET(iprop_meta, variants, 16); OFFSET(iprop_meta, archetypes, 20);
OFFSET(iprop_meta, fuel, 24);
OFFSET(iprop_output, kind, 0); OFFSET(iprop_output, is_list, 4);
OFFSET(iprop_output, name, 8); OFFSET(iprop_output, count, 16);
OFFSET(iprop_output, values, 24); OFFSET(iprop_output, owner, 32);
OFFSET(iprop_image, width, 0); OFFSET(iprop_image, height, 4);
OFFSET(iprop_image, pixels, 8); OFFSET(iprop_image, output_count, 16);
OFFSET(iprop_image, outputs, 24);

int main(void) {
    iprop_library_info_ex info;
    union { iprop_library_info_ex align; unsigned char bytes[sizeof(info) + 16]; } storage;
    uint32_t capacity;
    memset(&info, 0xa5, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    CHECK(iprop_library_query(&info) == IPROP_EX_OK);
    CHECK(info.struct_size == sizeof(info));
    CHECK(info.product_major == IPROP_TEST_PRODUCT_MAJOR);
    CHECK(info.product_minor == IPROP_TEST_PRODUCT_MINOR);
    CHECK(info.product_patch == IPROP_TEST_PRODUCT_PATCH);
    CHECK(info.api_major == 0 && info.api_minor == 3);
    CHECK(info.abi_major == 0 && info.abi_minor == 4);
    CHECK(info.format_major == 0 && info.format_minor_min == 1 && info.format_minor_max == 4);
    CHECK(info.capabilities == (IPROP_CAP_LIBRARY_QUERY | IPROP_CAP_SIZED_DESCRIPTORS |
          IPROP_CAP_PROGRAM_LOAD_EX | IPROP_CAP_PROGRAM_QUERY |
          IPROP_CAP_STRUCTURED_ERRORS | IPROP_CAP_LIBRARY_QUERY_EX |
          IPROP_CAP_ROOT_INPUT_REFLECTION | IPROP_CAP_OUTPUT_DECLARATIONS |
          IPROP_CAP_PART_REFLECTION | IPROP_CAP_METADATA_EXTENDED |
          IPROP_CAP_PARAMETERS | IPROP_CAP_RESULT_STATISTICS | IPROP_CAP_IMAGE_VIEW | IPROP_CAP_PARAMETERS_TYPED |
          IPROP_CAP_PARAMETER_OVERRIDES | IPROP_CAP_GENERATE_EX |
        IPROP_CAP_RESULT_OUTPUTS | IPROP_CAP_TYPED_OUTPUTS));
    CHECK(IPROP_OK == 0 && IPROP_E_OOM == 1 && IPROP_E_FORMAT == 2 &&
          IPROP_E_TRAP == 3 && IPROP_E_ARGUMENT == 4);
    CHECK(IPROP_UNIFORM_COLOR == 0 && IPROP_UNIFORM_F32 == 1 && IPROP_UNIFORM_U32 == 2);
    CHECK(IPROP_OUTPUT_POINT == 0 && IPROP_OUTPUT_RECT == 1 && IPROP_OUTPUT_CIRCLE == 2);
    CHECK(iprop_library_query(NULL) == IPROP_EX_E_ARGUMENT);
    {
        iprop_library_info_ex defaults;
        iprop_generate_options_ex options;
        CHECK(iprop_library_info_ex_init(&defaults, sizeof(defaults)) == IPROP_EX_OK);
        CHECK(defaults.struct_size == sizeof(defaults) && defaults.capabilities == 0 && defaults.api_minor == 0);
        CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
        CHECK(options.struct_size == sizeof(options) && options.flags == 0 && options.seed == 0 && options.size_x == 0);
    }
    for (capacity = 0; capacity <= sizeof(storage.bytes); ++capacity) {
        size_t i;
        memset(storage.bytes, 0xa5, sizeof(storage.bytes));
        memcpy(storage.bytes, &capacity, sizeof(capacity));
        CHECK(iprop_library_query((iprop_library_info_ex*)storage.bytes) ==
              (capacity < sizeof(uint32_t) ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(memcmp(storage.bytes, &capacity, sizeof(capacity)) == 0);
        /* Prefixes cutting through a scalar cannot partially publish it. */
        for (i = sizeof(uint32_t); i < sizeof(storage.bytes); ++i) {
            size_t end;
            if (i < offsetof(iprop_library_info_ex, capabilities))
                end = (i / sizeof(uint32_t) + 1) * sizeof(uint32_t);
            else end = offsetof(iprop_library_info_ex, capabilities) + sizeof(uint64_t);
            if (i >= sizeof(info) || end > capacity)
                CHECK(storage.bytes[i] == 0xa5);
            else CHECK(storage.bytes[i] == ((const unsigned char*)&info)[i]);
        }
        memset(storage.bytes, 0xa5, sizeof(storage.bytes));
        CHECK(iprop_library_info_ex_init((iprop_library_info_ex*)storage.bytes, capacity) ==
              (capacity < sizeof(uint32_t) ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        if (capacity < sizeof(uint32_t)) {
            for (i = 0; i < sizeof(storage.bytes); ++i) CHECK(storage.bytes[i] == 0xa5);
        } else {
            CHECK(memcmp(storage.bytes, &capacity, sizeof(capacity)) == 0);
            for (i = sizeof(uint32_t); i < sizeof(storage.bytes); ++i) {
                size_t end = sizeof(storage.bytes) + 1;
                if (i < offsetof(iprop_library_info_ex, format_minor_max) + sizeof(uint32_t))
                    end = (i / sizeof(uint32_t) + 1) * sizeof(uint32_t);
                else if (i >= offsetof(iprop_library_info_ex, capabilities) && i < sizeof(info))
                    end = offsetof(iprop_library_info_ex, capabilities) + sizeof(uint64_t);
                CHECK(storage.bytes[i] == (end <= capacity ? 0 : 0xa5));
            }
        }
    }
    puts("C99 loaded-library versions, capabilities and prefix/tail canaries: PASS");
    return 0;
}
