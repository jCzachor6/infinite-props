#include "test_support.h"

#include <stdlib.h>
#include <string.h>

void check_errors(const char* program_dir) {
    char err[512];

    /* The error paths: junk bytes, null arguments, and the reported
       message. */
    {
        iprop_program* program = NULL;
        const uint8_t junk[] = {1, 2, 3};
        const iprop_status status =
            iprop_program_load(junk, sizeof(junk), &program, err, sizeof(err));
        CHECK(status == IPROP_E_FORMAT,
              "junk bytes must fail with FORMAT, got %d", (int)status);
        CHECK(err[0] != '\0', "the error message must be reported");
        CHECK(iprop_program_load(NULL, 4, &program, err, sizeof(err)) ==
                  IPROP_E_ARGUMENT,
              "null bytes must fail with ARGUMENT");
        CHECK(iprop_generate(NULL, NULL, NULL, NULL, 0) == IPROP_E_ARGUMENT,
              "null handles must fail with ARGUMENT");
    }

    /* R1: a skipped arm must not change the validator's view of the
       scalar type. Exercise both vector overloads through the C ABI. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/vector-branch.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        CHECK(bytes != NULL, "cannot read vector branch fixture");
        if (bytes != NULL) {
            const uint8_t needle[] = {
                0x5b,0,0,0, 0x11,0,0,0, 0x10,0,0,0, 0x03,0,0,0,
                0x16,0,0,0, 0x11,0,0,0, 0x05,0,0,0, 0x5e,0,0,0
            };
            size_t matches = 0;
            for (size_t i = 0; i + sizeof(needle) <= length; ++i) {
                if (memcmp(bytes + i, needle, sizeof(needle)) != 0) continue;
                ++matches;
                bytes[i + 4] = 0x10; /* scalar is now i32 on the skipped arm */
                for (uint8_t opcode = 0x5e; opcode <= 0x5f; ++opcode) {
                    bytes[i + 28] = opcode;
                    iprop_program* program = NULL;
                    const iprop_status status = iprop_program_load(
                        bytes, length, &program, err, sizeof(err));
                    CHECK(status == IPROP_E_FORMAT && program == NULL,
                          "bad vector branch must be rejected before execution");
                    CHECK(strstr(err, "stack types") != NULL,
                          "expected a branch type diagnostic, got %s", err);
                    iprop_program_free(program);
                }
            }
            CHECK(matches == 1, "vector branch instruction sequence changed");
            free(bytes);
        }
    }
}
