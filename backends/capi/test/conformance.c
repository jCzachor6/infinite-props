/*
 * The C-API conformance (task 6.2): the golden corpus run through the
 * C entry point. Each corpus entry is assembled to an .iprop at build
 * time, loaded and generated here through iprop.h only, and compared
 * against the committed golden PNGs and the pinned structured outputs.
 *
 * Usage: capi_conformance <iprop-dir> <png-dir>
 */

#include "test_support.h"

int failures = 0;

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: capi_conformance <iprop-dir> <png-dir>\n");
        return 2;
    }
    const char* program_dir = argv[1];
    const char* png_dir = argv[2];
    const size_t corpus_count = check_golden(program_dir, png_dir);
    check_errors(program_dir);
    check_generation(program_dir);

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("capi conformance: PASS (%zu corpus entries + the error paths)\n",
           corpus_count);
    return 0;
}
