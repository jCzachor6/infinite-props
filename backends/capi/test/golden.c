#include "test_support.h"

#include <stdlib.h>
#include <string.h>

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
    unsigned char* png = read_png_rgba(png_path, &width, &height);
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
    free(png);
    return result;
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

size_t check_golden(const char* program_dir, const char* png_dir) {
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
    return sizeof(cases) / sizeof(cases[0]);
}
