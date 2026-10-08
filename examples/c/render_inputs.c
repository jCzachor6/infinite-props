/* Renders one asset with host-chosen inputs: a checked load, a parameter set
 * filled through the typed named setters, generation, the borrowed image view
 * written as an RGBA PAM file and the result statistics. Every C API failure
 * prints its structured status, reason and stage.
 *
 *   render_inputs file.iprop out.pam [name=value ...]
 *
 * Values follow the declared input type: #rrggbb or #rrggbbaa for color, a
 * decimal for f32, an integer for u32 (a negative one stores its i32 bits). */
#include "iprop.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void report(const char* operation, const iprop_error_ex* error) {
    fprintf(stderr, "%s failed: %s (status %" PRIu32 ", reason %" PRIu32 ", stage %" PRIu32 ")",
            operation, error->message_buffer[0] != '\0' ? error->message_buffer : "no message",
            error->status, error->reason, error->stage);
    if (error->location_flags & IPROP_LOCATION_FUNCTION) fprintf(stderr, " in function %" PRIu32, error->function_index);
    if (error->location_flags & IPROP_LOCATION_PC) fprintf(stderr, " at pc %" PRIu32, error->pc);
    if (error->location_flags & IPROP_LOCATION_DEBUG_LINE) fprintf(stderr, " on line %" PRIu32, error->debug_line);
    fputc('\n', stderr);
}

/* Parses "name=value" by the input's declared type and sets it. */
static int set_input(iprop_parameters* parameters, const char* argument, iprop_error_ex* error) {
    const char* equals = strchr(argument, '=');
    const char* text;
    iprop_utf8_view name;
    iprop_parameter_value_ex declared;
    iprop_status_ex status;
    char* end;
    int valid;
    if (equals == NULL) {
        fprintf(stderr, "expected name=value, got %s\n", argument);
        return 0;
    }
    name.data = argument;
    name.byte_count = (uint32_t)(equals - argument);
    text = equals + 1;
    if (iprop_parameter_value_ex_init(&declared, sizeof(declared)) != IPROP_EX_OK ||
        iprop_parameters_get_named(parameters, name, &declared, error) != IPROP_EX_OK) {
        report(argument, error);
        return 0;
    }
    errno = 0;
    if (declared.type == IPROP_INPUT_COLOR) {
        const size_t length = strlen(text);
        unsigned long rgba;
        iprop_color_rgba8 color;
        valid = text[0] == '#' && (length == 7 || length == 9) &&
                strspn(text + 1, "0123456789abcdefABCDEF") == length - 1;
        if (!valid) goto invalid;
        rgba = strtoul(text + 1, NULL, 16);
        if (length == 7) rgba = rgba << 8 | 0xffu;
        color.r = (uint8_t)(rgba >> 24);
        color.g = (uint8_t)(rgba >> 16);
        color.b = (uint8_t)(rgba >> 8);
        color.a = (uint8_t)rgba;
        status = iprop_parameters_set_color_named(parameters, name, color, error);
    } else if (declared.type == IPROP_INPUT_F32) {
        const float value = strtof(text, &end);
        if (end == text || *end != '\0' || errno != 0) goto invalid;
        status = iprop_parameters_set_f32_named(parameters, name, value, error);
    } else {
        const long long value = strtoll(text, &end, 10);
        if (end == text || *end != '\0' || errno != 0 || value < INT32_MIN || value > (long long)UINT32_MAX)
            goto invalid;
        status = value < 0 ? iprop_parameters_set_i32_named(parameters, name, (int32_t)value, error)
                           : iprop_parameters_set_u32_named(parameters, name, (uint32_t)value, error);
    }
    if (status != IPROP_EX_OK) {
        report(argument, error);
        return 0;
    }
    return 1;
invalid:
    fprintf(stderr, "%s: the value does not fit the declared input type\n", argument);
    return 0;
}

static int write_pam(const char* path, const iprop_image_view_ex* image) {
    FILE* file;
    uint32_t y;
    int written;
    if (image->pixel_format != IPROP_PIXEL_FORMAT_RGBA8 || image->origin != IPROP_IMAGE_ORIGIN_TOP_LEFT) return 0;
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    written = fprintf(file, "P7\nWIDTH %" PRIu32 "\nHEIGHT %" PRIu32 "\nDEPTH 4\nMAXVAL 255\n"
                      "TUPLTYPE RGB_ALPHA\nENDHDR\n", image->width, image->height) > 0;
    for (y = 0; written && y < image->height; ++y)
        written = fwrite(image->pixels + y * image->row_stride, 4, image->width, file) == image->width;
    return fclose(file) == 0 && written;
}

static uint8_t* read_binary(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    uint8_t* bytes = NULL;
    long length;
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (uintmax_t)length > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) goto done;
    *size = (size_t)length;
    bytes = (uint8_t*)malloc(*size != 0 ? *size : 1);
    if (bytes != NULL && fread(bytes, 1, *size, file) != *size) {
        free(bytes);
        bytes = NULL;
    }
done:
    fclose(file);
    return bytes;
}

int main(int argc, char** argv) {
    size_t size = 0;
    uint8_t* bytes;
    iprop_program* program = NULL;
    iprop_parameters* parameters = NULL;
    iprop_result* result = NULL;
    iprop_generate_options_ex options;
    iprop_image_view_ex image;
    iprop_result_stats_ex stats;
    iprop_error_ex error;
    char message[512] = { 0 };
    int i, success = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: render_inputs file.iprop out.pam [name=value ...]\n");
        return 2;
    }
    bytes = read_binary(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    if (iprop_error_ex_init(&error, sizeof(error)) != IPROP_EX_OK ||
        iprop_generate_options_ex_init(&options, sizeof(options)) != IPROP_EX_OK ||
        iprop_image_view_ex_init(&image, sizeof(image)) != IPROP_EX_OK ||
        iprop_result_stats_ex_init(&stats, sizeof(stats)) != IPROP_EX_OK) {
        free(bytes);
        return 1;
    }
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    if (iprop_program_load_ex(bytes, size, &program, &error) != IPROP_EX_OK) {
        report("load", &error);
    } else if (iprop_parameters_create(program, &parameters, &error) != IPROP_EX_OK) {
        report("parameters", &error);
    } else {
        for (i = 3; i < argc && set_input(parameters, argv[i], &error); ++i) {}
        if (i < argc) {
            /* set_input reported the rejected argument. */
        } else if (iprop_generate_ex(program, &options, parameters, &result, &error) != IPROP_EX_OK) {
            report("generate", &error);
        } else if (iprop_result_query_image(result, &image, &error) != IPROP_EX_OK ||
                   iprop_result_query_stats(result, &stats, &error) != IPROP_EX_OK) {
            report("result query", &error);
        } else if (!write_pam(argv[2], &image)) {
            fprintf(stderr, "cannot write %s\n", argv[2]);
        } else {
            printf("image=%" PRIu32 "x%" PRIu32 " fuel_used=%" PRIu64 "\n", image.width, image.height,
                   stats.fuel_used);
            success = 1;
        }
    }
    free(bytes);
    iprop_result_free(result);
    iprop_parameters_free(parameters);
    iprop_program_free(program);
    return success && !ferror(stdout) ? 0 : 1;
}
