#ifndef IPROP_CAPI_TEST_SUPPORT_H
#define IPROP_CAPI_TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "iprop.h"

extern int failures;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);           \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
        }                                                                  \
    } while (0)

uint8_t* read_file(const char* path, size_t* out_size);
void check_point(const iprop_image* image, uint32_t slot, float x, float y);
size_t check_golden(const char* program_dir, const char* png_dir);
void check_errors(const char* program_dir);
void check_generation(const char* program_dir);

#endif
