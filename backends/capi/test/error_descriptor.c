#include "iprop.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "error descriptor: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    iprop_error_ex error;
    uint32_t capacity;
    CHECK(iprop_error_ex_init(NULL, sizeof(error)) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    CHECK(error.struct_size == sizeof(error) && error.status == IPROP_EX_OK);
    CHECK(error.reason == IPROP_REASON_NONE && error.stage == IPROP_STAGE_NONE);
    CHECK(error.location_flags == 0 && error.offset_space == IPROP_OFFSET_NONE);
    CHECK(error.byte_offset == 0 && error.function_index == 0 && error.pc == 0 && error.debug_line == 0);
    CHECK(error.message_buffer == NULL && error.message_capacity == 0 &&
          error.message_required_bytes == 0 && error.message_truncated == 0);
    for (capacity = 0; capacity <= sizeof(error) + 16; ++capacity) {
        unsigned char* bytes = (unsigned char*)malloc(capacity + 1);
        size_t i;
        const size_t starts[] = {
            offsetof(iprop_error_ex, struct_size), offsetof(iprop_error_ex, status),
            offsetof(iprop_error_ex, reason), offsetof(iprop_error_ex, stage),
            offsetof(iprop_error_ex, byte_offset), offsetof(iprop_error_ex, location_flags),
            offsetof(iprop_error_ex, offset_space), offsetof(iprop_error_ex, function_index),
            offsetof(iprop_error_ex, pc), offsetof(iprop_error_ex, debug_line),
            offsetof(iprop_error_ex, message_buffer), offsetof(iprop_error_ex, message_capacity),
            offsetof(iprop_error_ex, message_required_bytes), offsetof(iprop_error_ex, message_truncated)};
        const size_t widths[] = {4, 4, 4, 4, 8, 4, 4, 4, 4, 4, sizeof(char*), 4, 8, 4};
        CHECK(bytes != NULL);
        memset(bytes, 0xa5, capacity + 1);
        CHECK(iprop_error_ex_init((iprop_error_ex*)bytes, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (i = 0; i <= capacity; ++i) {
            unsigned char expected = 0xa5;
            size_t field;
            for (field = 0; field < sizeof(starts) / sizeof(starts[0]); ++field) {
                if (starts[field] + widths[field] <= capacity &&
                    i >= starts[field] && i < starts[field] + widths[field])
                    expected = field == 0 ? ((const unsigned char*)&capacity)[i] : 0;
            }
            CHECK(bytes[i] == expected);
        }
        free(bytes);
    }
    puts("C99 sized error initializer, absent locations and exact-capacity canaries: PASS");
    return 0;
}
