#include "test_support.h"

#include <stdlib.h>
#include <string.h>

void check_generation(const char* program_dir) {
    char err[512];

    /* Format 0.6 math still uses the frozen host geometry and input layouts. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/math-bounds.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_generate_options options = {0};
        options.seed = 26;
        const uint32_t expected[] = {UINT32_C(0x3f800000), UINT32_C(0x3f800000),
            UINT32_C(0x3f19999a), UINT32_C(0x3f4ccccd), 0, 0,
            UINT32_C(0x40000000), UINT32_C(0x40000000)};
        CHECK(bytes != NULL, "cannot read format 0.6 math fixture");
        if (bytes != NULL) {
            CHECK(iprop_program_load(bytes, length, &program, err, sizeof(err)) == IPROP_OK,
                  "format 0.6 math load: %s", err);
            free(bytes);
        }
        if (program != NULL) {
            CHECK(iprop_generate(program, &options, &result, err, sizeof(err)) == IPROP_OK,
                  "format 0.6 math generation: %s", err);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.output_count == 4, "format 0.6 host output count");
            for (uint32_t i = 0; i < image.output_count; ++i) {
                const iprop_output* output = &image.outputs[i];
                CHECK(output->kind == IPROP_OUTPUT_POINT && output->owner == 0 &&
                      !output->is_list && output->count == 1, "format 0.6 host output metadata");
                if (i < 4 && output->count == 1) {
                    uint32_t bits[2];
                    memcpy(bits, output->values, sizeof(bits));
                    CHECK(bits[0] == expected[i * 2] && bits[1] == expected[i * 2 + 1],
                          "format 0.6 math output bits at slot %u", i);
                }
            }
            CHECK(image.pixels[0] == 0x20 && image.pixels[1] == 0xc0 && image.pixels[2] == 0x80,
                  "format 0.6 math pixel");
            iprop_result_free(result);
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
       A second generation and freeing both it and the program must leave
       the first result's pixels, names, values and borrowed pointers valid. */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/output-owners.iprop", program_dir);
        size_t length = 0;
        uint8_t* bytes = read_file(path, &length);
        iprop_program* program = NULL;
        iprop_result* result = NULL;
        iprop_image first = {0};
        uint8_t first_pixels[8 * 8 * 4] = {0};
        int have_pixels = 0;
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
            if (result != NULL) {
                iprop_result_image(result, &first);
                have_pixels = first.width == 8 && first.height == 8 && first.pixels != NULL;
                CHECK(have_pixels, "owner fixture first image");
                if (have_pixels) memcpy(first_pixels, first.pixels, sizeof(first_pixels));
            }
            iprop_result* second = NULL;
            options.seed = 17;
            CHECK(iprop_generate(program, &options, &second, err, sizeof(err)) == IPROP_OK,
                  "second owner fixture generation: %s", err);
            CHECK(second != NULL && second != result, "independent result handles");
            iprop_result_free(second);
            iprop_program_free(program);
        }
        if (result != NULL) {
            iprop_image image;
            iprop_result_image(result, &image);
            CHECK(image.pixels == first.pixels && image.outputs == first.outputs,
                  "first borrowed views remain stable");
            if (have_pixels) CHECK(memcmp(image.pixels, first_pixels, sizeof(first_pixels)) == 0,
                                   "first image remains unchanged");
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
}
