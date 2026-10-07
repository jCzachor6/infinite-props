/* Shared prelude of the single-executable C API tests, valid C99 and C++.
 * CHECK returns 1 from the enclosing int function at the first failure. The
 * conformance runner keeps its own failure-counting CHECK in test_support.h. */
#ifndef IPROP_CAPI_TEST_CHECK_H
#define IPROP_CAPI_TEST_CHECK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* Reads a whole nonempty file into a malloc'd buffer that the caller frees. */
static inline int read_fixture(const char* path, uint8_t** bytes, size_t* size) {
    FILE* file = fopen(path, "rb");
    long length;
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    *bytes = (uint8_t*)malloc((size_t)length);
    CHECK(*bytes != NULL && fread(*bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    *size = (size_t)length;
    return 0;
}

static inline uint32_t read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static inline void write_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value; bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16); bytes[3] = (uint8_t)(value >> 24);
}

static inline uint32_t float_bits(const float* value) {
    uint32_t bits;
    memcpy(&bits, value, 4);
    return bits;
}

#endif
