#include <iprop.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 2 || IPROP_API_VERSION_MAJOR != 0 || IPROP_API_VERSION_MINOR < 2) return 2;
    FILE* file = fopen(argv[1], "rb");
    if (file == NULL) return 2;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 2; }
    const long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return 2; }
    uint8_t* bytes = (uint8_t*)malloc((size_t)length);
    if (bytes == NULL) { fclose(file); return 2; }
    const size_t count = fread(bytes, 1, (size_t)length, file);
    fclose(file);
    if (count != (size_t)length) { free(bytes); return 2; }
    char error[512] = {0};
    iprop_program* program = NULL;
    const iprop_status loaded = iprop_program_load(bytes, count, &program, error, sizeof(error));
    free(bytes);
    if (loaded != IPROP_OK) { fprintf(stderr, "load: %s\n", error); return 1; }
    iprop_generate_options options = {0};
    options.seed = 26;
    iprop_result* result = NULL;
    const iprop_status generated = iprop_generate(program, &options, &result, error, sizeof(error));
    iprop_program_free(program);
    if (generated != IPROP_OK) { fprintf(stderr, "generate: %s\n", error); return 1; }
    iprop_image image;
    iprop_result_image(result, &image);
    uint32_t hash = UINT32_C(0x811c9dc5);
    for (size_t i = 0; i < (size_t)image.width * image.height * 4; ++i) {
        hash = (hash ^ image.pixels[i]) * UINT32_C(0x01000193);
    }
    /* The frozen portrait seed 26 pin, including transparent RGB. The
       borrowed output remains valid after its program has been freed. */
    int valid = image.width == 64 && image.height == 64 && hash == UINT32_C(1022392143);
    if (image.output_count < 1) valid = 0;
    else {
        const iprop_output* pupil = &image.outputs[0];
        valid = valid && pupil->owner == 0 && strcmp(pupil->name, "pupilCenter") == 0 &&
            pupil->kind == IPROP_OUTPUT_POINT && pupil->count == 1 &&
            pupil->values[0] == 27.f && pupil->values[1] == 28.f;
    }
    iprop_result_free(result);
    if (!valid) { fprintf(stderr, "installed consumer: golden/ownership mismatch\n"); return 1; }
    puts("installed C API consumer: PASS");
    return 0;
}
