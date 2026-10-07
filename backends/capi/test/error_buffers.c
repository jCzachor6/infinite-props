#include "iprop.h"
#include "check.h"

int main(int argc, char** argv) {
    const uint8_t junk[] = {1, 2, 3};
    char message[512];
    char untouched[] = "SENTINEL";
    iprop_program* program = NULL;
    iprop_result* result = NULL;
    iprop_generate_options options = {0};
    iprop_uniform uniform = {0};
    size_t capacity, length;
    size_t file_size;
    uint8_t* bytes;
    CHECK(argc == 2);
    CHECK(iprop_program_load(junk, sizeof(junk), &program, message, sizeof(message)) == IPROP_E_FORMAT);
    CHECK(program == NULL);
    length = strlen(message);
    CHECK(length > 0 && length < sizeof(message) - 1);

    /* Exact allocations catch any write past the client's capacity under ASan.
     * The legacy policy is the same byte prefix for both shared/static C99. */
    for (capacity = 0; capacity <= length + 2; ++capacity) {
        char* text = (char*)malloc(capacity == 0 ? 1 : capacity);
        const size_t copied = capacity == 0 ? 0 :
            (length < capacity - 1 ? length : capacity - 1);
        CHECK(text != NULL);
        memset(text, 0xa5, capacity == 0 ? 1 : capacity);
        CHECK(iprop_program_load(junk, sizeof(junk), &program, text, capacity) == IPROP_E_FORMAT);
        CHECK(program == NULL);
        if (capacity == 0) CHECK((unsigned char)text[0] == 0xa5);
        else {
            CHECK(memcmp(text, message, copied) == 0 && text[copied] == '\0');
            CHECK(strlen(text) == copied);
            if (copied + 1 < capacity) CHECK((unsigned char)text[copied + 1] == 0xa5);
        }
        free(text);
    }
    CHECK(iprop_program_load(junk, sizeof(junk), &program, NULL, 0) == IPROP_E_FORMAT);
    CHECK(iprop_program_load(junk, sizeof(junk), &program, NULL, 12) == IPROP_E_FORMAT);
    CHECK(iprop_program_load(NULL, 4, &program, untouched, sizeof(untouched)) == IPROP_E_ARGUMENT);
    CHECK(strcmp(untouched, "SENTINEL") == 0);

    CHECK(read_fixture(argv[1], &bytes, &file_size) == 0);
    CHECK(iprop_program_load(bytes, file_size, &program, untouched, sizeof(untouched)) == IPROP_OK);
    CHECK(program != NULL && strcmp(untouched, "SENTINEL") == 0);
    free(bytes);
    {
        iprop_program* preserved = program;
        CHECK(iprop_program_load(junk, sizeof(junk), &preserved, message, sizeof(message)) == IPROP_E_FORMAT);
        CHECK(preserved == program);
    }
    CHECK(iprop_generate(program, &options, &result, untouched, sizeof(untouched)) == IPROP_OK);
    CHECK(result != NULL && strcmp(untouched, "SENTINEL") == 0);
    uniform.name = NULL;
    uniform.type = IPROP_UNIFORM_COLOR;
    options.uniforms = &uniform;
    options.uniform_count = 1;
    {
        iprop_result* preserved = result;
        CHECK(iprop_generate(program, &options, &preserved, untouched, sizeof(untouched)) == IPROP_E_ARGUMENT);
        CHECK(preserved == result && strcmp(untouched, "SENTINEL") == 0);
        uniform.name = "base";
        uniform.type = IPROP_UNIFORM_F32; /* fixture declares color: legacy TRAP */
        CHECK(iprop_generate(program, &options, &preserved, message, sizeof(message)) == IPROP_E_TRAP);
        CHECK(preserved == result && message[0] != '\0');
    }
    iprop_result_free(result);
    iprop_program_free(program);
    puts("C99 legacy message bounds, untouched success/arguments and failure handles: PASS");
    return 0;
}
