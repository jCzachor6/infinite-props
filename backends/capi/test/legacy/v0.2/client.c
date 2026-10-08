/* Frozen C99 client for the six API 0.2 exports. Never replace its header
 * with the current iprop.h: the old executable is the compatibility probe. */
#include "iprop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define EXPECT(condition) do { if (!(condition)) { \
    fprintf(stderr, "legacy API check failed at line %d: %s\n", __LINE__, #condition); \
    ++failures; } } while (0)

static iprop_program* load(const char* path) {
    FILE* file = fopen(path, "rb");
    long length;
    uint8_t* bytes;
    iprop_program* program = NULL;
    char error[256] = {0};
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    bytes = (uint8_t*)malloc((size_t)length);
    if (bytes == NULL) { fclose(file); return NULL; }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes); fclose(file); return NULL;
    }
    fclose(file);
    if (iprop_program_load(bytes, (size_t)length, &program, error, sizeof(error)) != IPROP_OK)
        fprintf(stderr, "legacy load: %s\n", error);
    free(bytes);
    return program;
}

static void check_uniforms(const char* path) {
    iprop_program* program = load(path);
    iprop_generate_options options = {0};
    iprop_uniform values[2] = {{0}};
    iprop_result* result = NULL;
    iprop_result* preserved;
    iprop_image image = {0};
    iprop_meta meta = {0};
    char error[256] = "SENTINEL";
    EXPECT(program != NULL);
    if (program == NULL) return;
    iprop_program_meta(program, &meta);
    EXPECT(meta.size_x == 8 && meta.size_y == 8 && meta.fuel == 1000000);
    values[0].name = "not_an_input"; values[0].type = IPROP_UNIFORM_U32;
    values[0].u = 99; options.uniforms = values; options.uniform_count = 1;
    EXPECT(iprop_generate(program, &options, &result, error, sizeof(error)) == IPROP_OK);
    EXPECT(strcmp(error, "SENTINEL") == 0);
    if (result != NULL) {
        iprop_result_image(result, &image);
        EXPECT(image.width == 8 && image.height == 8);
        EXPECT(image.pixels[0] == 0x33 && image.pixels[1] == 0x55 && image.pixels[2] == 0xaa);
        iprop_result_free(result); result = NULL;
    }
    values[0].name = "base"; values[0].type = IPROP_UNIFORM_COLOR;
    values[0].r = 255; values[0].a = 255;
    values[1] = values[0]; values[1].r = 0; values[1].g = 255;
    options.uniform_count = 2;
    EXPECT(iprop_generate(program, &options, &result, error, sizeof(error)) == IPROP_OK);
    if (result != NULL) {
        iprop_result_image(result, &image);
        EXPECT(image.pixels[0] == 255 && image.pixels[1] == 0);
    }
    preserved = result;
    options.uniform_count = 1; values[0].type = IPROP_UNIFORM_F32;
    EXPECT(iprop_generate(program, &options, &result, error, sizeof(error)) == IPROP_E_TRAP);
    EXPECT(result == preserved && strstr(error, "type mismatch") != NULL);
    strcpy(error, "SENTINEL"); options.uniforms = NULL;
    EXPECT(iprop_generate(program, &options, &result, error, sizeof(error)) == IPROP_E_ARGUMENT);
    EXPECT(result == preserved && strcmp(error, "SENTINEL") == 0);
    EXPECT(iprop_generate(program, NULL, &result, error, sizeof(error)) == IPROP_E_ARGUMENT);
    EXPECT(iprop_generate(program, &options, NULL, error, sizeof(error)) == IPROP_E_ARGUMENT);
    EXPECT(iprop_program_load(NULL, 1, &program, error, sizeof(error)) == IPROP_E_ARGUMENT);
    EXPECT(strcmp(error, "SENTINEL") == 0);
    iprop_result_free(preserved);
    iprop_program_free(program);
}

static void check_outputs(const char* path) {
    iprop_program* program = load(path);
    iprop_generate_options options = {0};
    iprop_result* result = NULL;
    iprop_image image = {0};
    char error[256] = {0};
    uint32_t i;
    EXPECT(program != NULL);
    if (program == NULL) return;
    EXPECT(iprop_generate(program, &options, &result, error, sizeof(error)) == IPROP_OK);
    iprop_program_free(program);
    if (result == NULL) return;
    iprop_result_image(result, &image);
    EXPECT(image.output_count == 9);
    for (i = 0; i < image.output_count; ++i) {
        const iprop_output* output = &image.outputs[i];
        const char* names[] = {"anchor", "collider", "zone"};
        EXPECT(output->owner == i / 3);
        EXPECT(output->kind == (iprop_output_kind)(i % 3));
        EXPECT(output->name != NULL && output->values != NULL && output->count == 1);
        if (output->name != NULL) EXPECT(strcmp(output->name, names[i % 3]) == 0);
        if (output->values != NULL) EXPECT(output->values[0] == (i < 3 ? 4.f : i < 6 ? 1.f : 2.f));
    }
    iprop_result_free(result);
}

int main(int argc, char** argv) {
    if (argc != 3 || IPROP_API_VERSION_MAJOR != 0 || IPROP_API_VERSION_MINOR != 2) return 2;
    check_uniforms(argv[1]); check_outputs(argv[2]);
    iprop_program_free(NULL); iprop_result_free(NULL);
    iprop_program_meta(NULL, NULL); iprop_result_image(NULL, NULL);
    if (failures) return 1;
    printf("{\"api_major\":0,\"api_minor\":2,\"sizes\":{\"uniform\":%zu,"
           "\"options\":%zu,\"meta\":%zu,\"output\":%zu,\"image\":%zu},"
           "\"status\":\"pass\",\"exports_used\":6}\n",
           sizeof(iprop_uniform), sizeof(iprop_generate_options), sizeof(iprop_meta),
           sizeof(iprop_output), sizeof(iprop_image));
    return 0;
}
