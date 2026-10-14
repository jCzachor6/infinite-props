#include "iprop.h"
#include "check.h"

static int prefixes(const iprop_result* result, const iprop_result_stats_ex* expected) {
    const size_t offsets[] = {offsetof(iprop_result_stats_ex, fuel_budget),
        offsetof(iprop_result_stats_ex, fuel_remaining), offsetof(iprop_result_stats_ex, fuel_used),
        offsetof(iprop_result_stats_ex, image_byte_count), offsetof(iprop_result_stats_ex, output_element_count)};
    uint32_t capacity;
    for (capacity = 0; capacity <= sizeof(*expected) + 16; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        unsigned char before[sizeof(*expected) + 16];
        size_t byte, field;
        CHECK(storage != NULL);
        memset(storage, 0xa5, allocation);
        CHECK(iprop_result_stats_ex_init((iprop_result_stats_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = 0xa5;
            if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
            if (capacity >= offsetof(iprop_result_stats_ex, flags) + 4 &&
                byte >= offsetof(iprop_result_stats_ex, flags) && byte < offsetof(iprop_result_stats_ex, flags) + 4) want = 0;
            for (field = 0; field < sizeof(offsets)/sizeof(offsets[0]); ++field)
                if (offsets[field] + 8 <= capacity && byte >= offsets[field] && byte < offsets[field] + 8) want = 0;
            CHECK(storage[byte] == want);
        }
        /* Flags are input, including a partial flag field. */
        memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
        if (capacity >= offsetof(iprop_result_stats_ex, flags) + 4) memset(storage + offsetof(iprop_result_stats_ex, flags), 0, 4);
        memcpy(before, storage, allocation);
        CHECK(iprop_result_query_stats(result, (iprop_result_stats_ex*)storage, NULL) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (byte = 0; byte < allocation; ++byte) {
            unsigned char want = before[byte];
            for (field = 0; field < sizeof(offsets)/sizeof(offsets[0]); ++field)
                if (offsets[field] + 8 <= capacity && byte >= offsets[field] && byte < offsets[field] + 8) want = ((const unsigned char*)expected)[byte];
            CHECK(storage[byte] == want);
        }
        memcpy(before, storage, allocation);
        CHECK(iprop_result_query_stats(NULL, (iprop_result_stats_ex*)storage, NULL) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(storage, before, allocation) == 0);
        free(storage);
    }
    return 0;
}

static int errors(const iprop_result* result, const iprop_result_stats_ex* expected) {
    iprop_result_stats_ex out, before;
    iprop_error_ex error, error_before;
    char message[128];
    uint32_t capacity;
    CHECK(iprop_result_stats_ex_init(NULL, 48) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_query_stats(result, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message;
    for (capacity = 0; capacity < 3; ++capacity) {
        error.message_capacity = capacity;
        CHECK(iprop_result_stats_ex_init(&out, sizeof(out)) == IPROP_EX_OK);
        out.flags = UINT32_MAX; memcpy(&before, &out, sizeof(out));
        CHECK(iprop_result_query_stats(result, &out, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&before, &out, sizeof(out)) == 0);
        CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);
        CHECK(error.message_required_bytes > 0 && error.message_truncated == 1);
        if (capacity > 0) CHECK(message[capacity - 1] == '\0');
        out.flags = 0;
        CHECK(iprop_result_query_stats(result, &out, &error) == IPROP_EX_OK);
        CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && error.message_required_bytes == 0 && error.message_truncated == 0);
        if (capacity > 0) CHECK(message[0] == '\0');
        CHECK(memcmp(&out, expected, sizeof(out)) == 0);
    }
    error.message_buffer = NULL; error.message_capacity = 1;
    memset(&out, 0xa5, sizeof(out)); out.struct_size = sizeof(out); out.flags = 0;
    memcpy(&before, &out, sizeof(out));
    CHECK(iprop_result_query_stats(result, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    error.message_buffer = (char*)&out.fuel_used; error.message_capacity = 1;
    memcpy(&error_before, &error, sizeof(error));
    CHECK(iprop_result_query_stats(result, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0 && memcmp(&error, &error_before, sizeof(error)) == 0);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    CHECK(iprop_result_query_stats(result, &out, &error) == IPROP_EX_OK);
    CHECK(error.message_required_bytes == 0 && message[0] == '\0');
    memcpy(&before, &out, sizeof(out));
    CHECK(iprop_result_query_stats(result, &out, (iprop_error_ex*)&out) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&before, &out, sizeof(out)) == 0);
    error.message_buffer = NULL; error.message_capacity = 0;
    CHECK(iprop_result_query_stats(result, &out, &error) == IPROP_EX_OK);
    /* A short error prefix is independently accepted and bounds writes. */
    for (capacity = 0; capacity <= sizeof(error) + 8; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        size_t i;
        CHECK(storage != NULL); memset(storage, 0xa5, allocation);
        CHECK(iprop_error_ex_init((iprop_error_ex*)storage, capacity) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        memcpy(storage, &capacity, 4);
        memset(&out, 0xa5, sizeof(out)); out.struct_size = sizeof(out); out.flags = 0;
        memcpy(&before, &out, sizeof(out));
        CHECK(iprop_result_query_stats(result, &out, (iprop_error_ex*)storage) == (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        if (capacity < 4) CHECK(memcmp(&out, &before, sizeof(out)) == 0);
        else CHECK(memcmp(&out, expected, sizeof(out)) == 0);
        for (i = sizeof(error); i < allocation; ++i) CHECK(storage[i] == 0xa5);
        free(storage);
    }
    return 0;
}

int main(int argc, char** argv) {
    iprop_library_info_ex library;
    int fixture;
    CHECK(argc == 3);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query(&library) == IPROP_EX_OK && (library.capabilities & IPROP_CAP_RESULT_STATISTICS) != 0);
    for (fixture = 1; fixture <= 2; ++fixture) {
        size_t length;
        uint8_t* bytes;
        iprop_program* program = NULL;
        iprop_result *first = NULL, *second = NULL;
        iprop_generate_options options = {0};
        iprop_result_stats_ex stats, saved;
        iprop_program_info_ex meta;
        char message[128];
        CHECK(read_fixture(argv[fixture], &bytes, &length) == 0);
        CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &program, NULL) == IPROP_EX_OK);
        free(bytes);
        CHECK(iprop_program_info_ex_init(&meta, sizeof(meta)) == IPROP_EX_OK);
        CHECK(iprop_program_query(program, &meta, NULL) == IPROP_EX_OK);
        options.seed = 7; options.size_x = 13; options.size_y = 11;
        CHECK(iprop_generate(program, &options, &first, message, sizeof(message)) == IPROP_OK);
        memset(&stats, 0, sizeof(stats));
        CHECK(iprop_result_stats_ex_init(&stats, sizeof(stats)) == IPROP_EX_OK);
        CHECK(iprop_result_query_stats(first, &stats, NULL) == IPROP_EX_OK);
        CHECK(stats.fuel_budget == meta.fuel && stats.fuel_remaining <= stats.fuel_budget);
        /* The existing VM charges no fuel for the empty draw's return. */
        CHECK(fixture == 1 ? stats.fuel_used > 0 : stats.fuel_used == 0);
        CHECK(stats.fuel_used == stats.fuel_budget - stats.fuel_remaining);
        CHECK(stats.image_byte_count == UINT64_C(13) * 11 * 4 && stats.output_element_count == (fixture == 1 ? 5u : 0u));
        memcpy(&saved, &stats, sizeof(stats));
        options.seed = 26;
        CHECK(iprop_generate(program, &options, &second, message, sizeof(message)) == IPROP_OK);
        iprop_result_free(second);
        {
            iprop_result* failed = first;
            options.size_x = UINT32_MAX;
            CHECK(iprop_generate(program, &options, &failed, message, sizeof(message)) == IPROP_E_TRAP);
            CHECK(failed == first);
        }
        iprop_program_free(program);
        CHECK(iprop_result_query_stats(first, &stats, NULL) == IPROP_EX_OK && memcmp(&saved, &stats, sizeof(stats)) == 0);
        CHECK(prefixes(first, &stats) == 0 && errors(first, &stats) == 0);
        iprop_result_free(first);
        CHECK(saved.image_byte_count == UINT64_C(13) * 11 * 4);
    }
    puts("C99 result statistics: prefixes/flags/tails, fuel/pixels/elements, lifetimes and bounded errors: PASS");
    return 0;
}
