/* Compiled against the unmodified header captured before R.4. */
#include "frozen-program-info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "old metadata client: line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(int argc, char** argv) {
    FILE* file;
    long length;
    uint8_t* bytes;
    iprop_program* program = NULL;
    iprop_program_info_ex expected;
    iprop_meta legacy;
    unsigned char* exact;
    uint32_t capacity = sizeof(iprop_program_info_ex);
    CHECK(argc == 2);
    file = fopen(argv[1], "rb");
    CHECK(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file); CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    bytes = (uint8_t*)malloc((size_t)length);
    CHECK(bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &program, NULL) == IPROP_EX_OK);
    free(bytes);
    exact = (unsigned char*)malloc(capacity);
    CHECK(exact != NULL);
    memset(exact, 0xa5, capacity);
    CHECK(iprop_program_info_ex_init((iprop_program_info_ex*)exact, capacity) == IPROP_EX_OK);
    CHECK(iprop_program_query(program, (iprop_program_info_ex*)exact, NULL) == IPROP_EX_OK);
    memcpy(&expected, exact, capacity);
    iprop_program_meta(program, &legacy);
    CHECK(expected.struct_size == capacity && expected.size_x == legacy.size_x && expected.size_y == legacy.size_y);
    CHECK(expected.origin_x == legacy.origin_x && expected.origin_y == legacy.origin_y);
    CHECK(expected.variants == legacy.variants && expected.archetypes == legacy.archetypes && expected.fuel == legacy.fuel);
    memset(exact, 0xa5, capacity); memcpy(exact, &capacity, 4);
    CHECK(iprop_program_query(program, (iprop_program_info_ex*)exact, NULL) == IPROP_EX_OK);
    CHECK(memcmp(exact, &expected, offsetof(iprop_program_info_ex, archetypes) + 4) == 0);
    CHECK(memcmp(exact + offsetof(iprop_program_info_ex, fuel), &expected.fuel, 8) == 0);
    free(exact); iprop_program_free(program);
    puts("Frozen pre-R.4 header: exact old-size metadata allocation and legacy prefix: PASS");
    return 0;
}
