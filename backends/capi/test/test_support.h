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
/* Decodes an 8-bit RGBA PNG into malloc'd pixels, or returns NULL. */
#ifdef __cplusplus
extern "C"
#endif
unsigned char* read_png_rgba(const char* path, int* width, int* height);
void check_point(const iprop_image* image, uint32_t slot, float x, float y);
size_t check_golden(const char* program_dir, const char* png_dir);
void check_errors(const char* program_dir);
void check_generation(const char* program_dir);

#endif
