/*
 * The C-API conformance (task 6.2): the golden corpus run through the
 * C entry point. Each corpus entry is assembled to an .iprop at build
 * time, loaded and generated here through iprop.h only, and compared
 * against the committed golden PNGs and the pinned structured outputs.
 *
 * Usage: capi_conformance <iprop-dir> <png-dir>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iprop.h"

#define STBI_ONLY_PNG
#include "stb_image.h"

static int failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);           \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
        }                                                                  \
    } while (0)

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    const long end = ftell(file);
    if (fseek(file, 0, SEEK_SET) != 0 || end < 0) {
        fclose(file);
        return NULL;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)end + 1);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    const size_t got = fread(data, 1, (size_t)end, file);
    fclose(file);
    if (got != (size_t)end) {
        free(data);
        return NULL;
    }
    *out_size = (size_t)end;
    return data;
}

/* Generates one corpus entry through the C API and compares the RGBA
   against the committed golden PNG, pixel-exactly. Returns the result
   handle for the caller's output checks (or NULL). */
static iprop_result* run_case(const char* program_path, const char* png_path,
                              uint32_t seed, char* err, size_t err_size) {
    size_t length = 0;
    uint8_t* bytes = read_file(program_path, &length);
    CHECK(bytes != NULL, "cannot read %s", program_path);
    if (bytes == NULL) {
        return NULL;
    }

    iprop_program* program = NULL;
    const iprop_status load_status =
        iprop_program_load(bytes, length, &program, err, err_size);
    free(bytes);
    CHECK(load_status == IPROP_OK, "load %s: status %d (%s)", program_path,
          (int)load_status, err);
    if (load_status != IPROP_OK) {
        return NULL;
    }

    iprop_generate_options options;
    memset(&options, 0, sizeof(options));
    options.seed = seed;

    iprop_result* result = NULL;
    const iprop_status gen_status =
        iprop_generate(program, &options, &result, err, err_size);
    iprop_program_free(program);
    CHECK(gen_status == IPROP_OK, "generate %s seed %u: status %d (%s)",
          program_path, seed, (int)gen_status, err);
    if (gen_status != IPROP_OK) {
        return NULL;
    }

    iprop_image image;
    iprop_result_image(result, &image);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* png = stbi_load(png_path, &width, &height, &channels, 4);
    CHECK(png != NULL, "cannot read the golden %s", png_path);
    if (png == NULL) {
        return result;
    }

    CHECK((uint32_t)width == image.width && (uint32_t)height == image.height,
          "%s: size %ux%u != golden %dx%d", program_path, image.width,
          image.height, width, height);
    if ((uint32_t)width == image.width && (uint32_t)height == image.height) {
        const size_t count = (size_t)image.width * image.height * 4;
        for (size_t i = 0; i < count; ++i) {
            if (image.pixels[i] != png[i]) {
                const size_t pixel = i / 4;
                CHECK(0, "%s: pixel (%u,%u) channel %u: 0x%02x != 0x%02x",
                      program_path, (unsigned)(pixel % image.width),
                      (unsigned)(pixel / image.width), (unsigned)(i % 4),
                      image.pixels[i], png[i]);
                break;
            }
        }
    }
    stbi_image_free(png);
    return result;
}

static void check_point(const iprop_image* image, uint32_t slot, float x,
                        float y) {
    CHECK(slot < image->output_count, "missing output slot %u", slot);
    if (slot >= image->output_count) {
        return;
    }
    const iprop_output* output = &image->outputs[slot];
    CHECK(output->kind == IPROP_OUTPUT_POINT, "slot %u is not a point", slot);
    CHECK(!output->is_list, "slot %u must be singular", slot);
    CHECK(output->count == 1, "slot %u: count %u", slot, output->count);
    CHECK(output->values != NULL, "slot %u: no values", slot);
    if (output->values != NULL && output->count == 1) {
        CHECK(output->values[0] == x && output->values[1] == y,
              "slot %u: (%.9g, %.9g) != (%.9g, %.9g)", slot,
              output->values[0], output->values[1], x, y);
    }
}

static void check_outputs(const char* program_name, iprop_result* result) {
    iprop_image image;
    iprop_result_image(result, &image);
    if (strcmp(program_name, "portrait.iprop") == 0) {
        check_point(&image, 0, 27.f, 28.f); /* pupilCenter */
    } else if (strcmp(program_name, "shapes.iprop") == 0) {
        check_point(&image, 0, 20.f, 20.f); /* anchor */
    } else if (strcmp(program_name, "noise.iprop") == 0) {
        CHECK(image.output_count == 0, "noise declares no outputs");
    } else if (strcmp(program_name, "parts.iprop") == 0) {
        check_point(&image, 0, 8.f, 8.f); /* center (root first) */
        check_point(&image, 1, 8.f, 8.f); /* spot (the part's own) */
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: capi_conformance <iprop-dir> <png-dir>\n");
        return 2;
    }
    const char* program_dir = argv[1];
    const char* png_dir = argv[2];
    char err[512];

    /* The metadata accessor, pinned on the portrait's meta section. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/portrait.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        CHECK(bytes != NULL, "cannot read %s", path);
        if (bytes != NULL) {
            iprop_program* program = NULL;
            CHECK(iprop_program_load(bytes, length, &program, err,
                                     sizeof(err)) == IPROP_OK,
                  "load %s: %s", path, err);
            if (program != NULL) {
                iprop_meta meta;
                iprop_program_meta(program, &meta);
                CHECK(meta.size_x == 64 && meta.size_y == 64,
                      "meta size %ux%u", meta.size_x, meta.size_y);
                CHECK(meta.origin_x == 32.f && meta.origin_y == 60.f,
                      "meta origin (%.9g, %.9g)", meta.origin_x,
                      meta.origin_y);
                CHECK(meta.variants == 8, "meta variants %u", meta.variants);
                CHECK(meta.archetypes == 1, "meta archetypes %u",
                      meta.archetypes);
                CHECK(meta.fuel == 2000000, "meta fuel %llu",
                      (unsigned long long)meta.fuel);
                iprop_program_free(program);
            }
            free(bytes);
        }
    }

    struct Case {
        const char* program;
        const char* png;
        uint32_t seed;
    };
    static const struct Case cases[] = {
        {"portrait.iprop", "portrait-seed0.png", 0},
        {"portrait.iprop", "portrait-seed1.png", 1},
        {"portrait.iprop", "portrait-seed2.png", 2},
        {"portrait.iprop", "portrait-seed7.png", 7},
        {"portrait.iprop", "portrait-seed26.png", 26},
        {"shapes.iprop", "shapes.png", 0},
        {"noise.iprop", "noise.png", 0},
        {"parts.iprop", "parts.png", 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char program_path[1024];
        char png_path[1024];
        snprintf(program_path, sizeof(program_path), "%s/%s", program_dir,
                 cases[i].program);
        snprintf(png_path, sizeof(png_path), "%s/%s", png_dir, cases[i].png);
        err[0] = '\0';
        iprop_result* result =
            run_case(program_path, png_path, cases[i].seed, err, sizeof(err));
        if (result != NULL) {
            check_outputs(cases[i].program, result);
            iprop_result_free(result);
        }
    }

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

    /* The uniform overrides: the fixture's three inputs drive three
       diagonal pixels — the defaults, all three overrides, an unknown
       name (ignored) and a type mismatch (a trap). */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/uniforms.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        CHECK(bytes != NULL, "cannot read %s", path);
        if (bytes != NULL) {
            iprop_program* program = NULL;
            CHECK(iprop_program_load(bytes, length, &program, err,
                                     sizeof(err)) == IPROP_OK,
                  "load %s: %s", path, err);
            if (program != NULL) {
                iprop_generate_options options;
                memset(&options, 0, sizeof(options));
                iprop_result* result = NULL;
                iprop_image image;

                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_OK,
                      "uniform defaults: %s", err);
                if (result != NULL) {
                    iprop_result_image(result, &image);
                    CHECK(image.width == 8 && image.height == 8,
                          "fixture size %ux%u", image.width, image.height);
                    CHECK(image.pixels[0] == 0x33 && image.pixels[1] == 0x55 &&
                              image.pixels[2] == 0xaa &&
                              image.pixels[3] == 0xff,
                          "the default colour pixel");
                    iprop_result_free(result);
                }

                iprop_uniform uniforms[3];
                memset(uniforms, 0, sizeof(uniforms));
                uniforms[0].name = "base";
                uniforms[0].type = IPROP_UNIFORM_COLOR;
                uniforms[0].r = 0xff;
                uniforms[0].g = 0xff;
                uniforms[0].b = 0xff;
                uniforms[0].a = 0xff;
                uniforms[1].name = "fx";
                uniforms[1].type = IPROP_UNIFORM_F32;
                uniforms[1].f = 10.f;
                uniforms[2].name = "ux";
                uniforms[2].type = IPROP_UNIFORM_U32;
                uniforms[2].u = 7;
                options.uniforms = uniforms;
                options.uniform_count = 3;
                result = NULL;
                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_OK,
                      "uniform overrides: %s", err);
                if (result != NULL) {
                    iprop_result_image(result, &image);
                    CHECK(image.pixels[0] == 0xff && image.pixels[1] == 0xff &&
                              image.pixels[2] == 0xff &&
                              image.pixels[3] == 0xff,
                          "the overridden colour pixel");
                    /* shade(base, 10) on (1,1) — byte offset 36. */
                    CHECK(image.pixels[36] == 0x3d &&
                              image.pixels[37] == 0x5f &&
                              image.pixels[38] == 0xb4,
                          "the f32 override: shade +10");
                    /* shade(base, 7) on (2,2) — byte offset 72. */
                    CHECK(image.pixels[72] == 0x3a &&
                              image.pixels[73] == 0x5c &&
                              image.pixels[74] == 0xb1,
                          "the u32 override: shade +7");
                    iprop_result_free(result);
                }

                options.uniform_count = 1;
                uniforms[0].name = "not_an_input";
                uniforms[0].type = IPROP_UNIFORM_U32;
                uniforms[0].u = 9;
                result = NULL;
                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_OK,
                      "unknown uniform: %s", err);
                if (result != NULL) {
                    iprop_result_image(result, &image);
                    CHECK(image.pixels[0] == 0x33 && image.pixels[1] == 0x55 &&
                              image.pixels[2] == 0xaa,
                          "the unknown uniform must leave the default in "
                          "place");
                    iprop_result_free(result);
                }

                uniforms[0].name = "base";
                uniforms[0].type = IPROP_UNIFORM_F32;
                uniforms[0].f = 1.f;
                result = NULL;
                CHECK(iprop_generate(program, &options, &result, err,
                                     sizeof(err)) == IPROP_E_TRAP,
                      "the type mismatch must trap, got %s", err);
                CHECK(strstr(err, "type mismatch") != NULL,
                      "the trap message must mention the mismatch: %s", err);

                iprop_program_free(program);
            }
            free(bytes);
        }
    }

    /* Duplicate names in the root and two parts retain distinct owners.
       The result must remain usable after freeing the program. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/output-owners.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0};
        CHECK(bytes != NULL, "cannot read owner fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "owner fixture load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "owner fixture generation: %s", err);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.output_count == 9, "owner fixture slot count");
            const char* names[] = {"anchor", "collider", "zone"};
            for (uint32_t i = 0; i < image.output_count; ++i) {
                const iprop_output* slot = &image.outputs[i];
                CHECK(slot->owner == i / 3, "slot %u owner %u", i, slot->owner);
                CHECK(strcmp(slot->name, names[i % 3]) == 0, "slot %u name", i);
                CHECK(slot->kind == (iprop_output_kind)(i % 3), "slot %u kind", i);
                CHECK(slot->count == 1 && slot->values != NULL, "slot %u values", i);
                if (slot->values != NULL) {
                    CHECK(slot->values[0] == (i < 3 ? 4.f : i < 6 ? 1.f : 2.f),
                          "slot %u value", i);
                }
            }
            iprop_result_free(result);
        }
    }

    /* Compiled 0.2 source: palettes, typed helpers, loop control, math,
       empty lists, variants/archetypes, size and uniform overrides. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/language.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        CHECK(bytes != NULL, "cannot read language fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "language fixture load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            const uint8_t colors[][3] = {{0x11,0x22,0x33}, {0x44,0x55,0x66}, {0x77,0x88,0x99}};
            for (uint32_t v = 0; v < 3; ++v) {
                for (uint32_t a = 0; a < 2; ++a) {
                    iprop_generate_options options = {0};
                    options.seed = 26;
                    options.variant = v;
                    options.archetype = a;
                    /* A non-square size scales raster coordinates (2x, 3x)
                       while structured outputs remain in asset units. */
                    options.size_x = 16;
                    options.size_y = 24;
                    iprop_uniform base = {0};
                    base.name = "base";
                    base.type = IPROP_UNIFORM_COLOR;
                    base.r = 0xee;
                    base.g = 0xdd;
                    base.b = 0xcc;
                    base.a = 0xff;
                    options.uniforms = &base;
                    options.uniform_count = 1;
                    iprop_result* result = NULL;
                    CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                          "language generation v%u a%u: %s", v, a, err);
                    if (result == NULL) continue;
                    iprop_image image;
                    iprop_result_image(result, &image);
                    CHECK(image.width == 16 && image.height == 24, "language size override");
                    CHECK(image.pixels[0] == 0xee && image.pixels[1] == 0xdd &&
                          image.pixels[2] == 0xcc && image.pixels[3] == 0xff,
                          "language uniform override");
                    const size_t pixel = ((size_t)image.width * 3 + (a + 1) * 2) * 4;
                    CHECK(memcmp(image.pixels + pixel, colors[v], 3) == 0 &&
                          image.pixels[pixel + 3] == 0xff, "language palette pixel");
                    CHECK(image.output_count == 4, "language output count");
                    check_point(&image, 0, (float)(v + 1), (float)a + .25f);
                    if (image.output_count == 4) {
                        uint32_t angle_bits = 0;
                        memcpy(&angle_bits, image.outputs[1].values, sizeof(angle_bits));
                        CHECK(angle_bits == UINT32_C(0x3f8db70d) && image.outputs[1].values[1] == 1.f,
                              "language math pins");
                        const iprop_output* list = &image.outputs[2];
                        CHECK(list->is_list && list->count == 2, "language loop list count");
                        if (list->count == 2) {
                            CHECK(list->values[0] == 0.f && list->values[1] == (float)a &&
                                  list->values[2] == 2.f && list->values[3] == (float)a,
                                  "language continue/break values");
                        }
                        CHECK(image.outputs[3].is_list && image.outputs[3].count == 0,
                              "language empty circle list");
                        for (uint32_t i = 0; i < 4; ++i) {
                            CHECK(image.outputs[i].owner == 0, "language root owner");
                        }
                    }
                    iprop_result_free(result);
                }
            }
            iprop_program_free(program);
        }
    }

    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/plant.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0};
        options.seed = 26;
        CHECK(bytes != NULL, "cannot read compiled plant");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "compiled plant load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "compiled plant generation: %s", err);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.width == 64 && image.height == 64, "compiled plant size");
            iprop_result_free(result);
        }
    }

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("capi conformance: PASS (%zu corpus entries + the error paths)\n",
           sizeof(cases) / sizeof(cases[0]));
    return 0;
}
