/* A reflection-only host model. Copy borrowed names and exact defaults,
 * then release the program before displaying the owned panel rows.
 * No ranges or source signedness can be inferred from the binary schema. */
#include "iprop.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char panel_requires_binary32[(sizeof(float) == 4 && sizeof(uint32_t) == 4) ? 1 : -1];

typedef struct {
    uint32_t root_id;
    iprop_input_type type;
    uint8_t* name_bytes;
    uint32_t name_byte_count;
    iprop_color_rgba8 color_default;
    uint32_t scalar_default_bits;
} panel_row;

typedef struct {
    panel_row* rows;
    uint32_t count;
} panel_model;

static void panel_free(panel_model* panel) {
    uint32_t i;
    for (i = 0; i < panel->count; ++i) free(panel->rows[i].name_bytes);
    free(panel->rows);
    panel->rows = NULL;
    panel->count = 0;
}

static int panel_copy(const iprop_program* program, panel_model* panel,
                      iprop_error_ex* error) {
    uint32_t count, i;
    if (iprop_program_input_count(program, &count, error) != IPROP_EX_OK) return 0;
    if (count == 0) return 1;
    if (SIZE_MAX / count < sizeof(panel_row)) return 0;
    panel->rows = (panel_row*)calloc(count, sizeof(panel_row));
    if (panel->rows == NULL) return 0;
    panel->count = count;
    for (i = 0; i < count; ++i) {
        iprop_input_info_ex input;
        panel_row* row = &panel->rows[i];
        if (iprop_input_info_ex_init(&input, sizeof(input)) != IPROP_EX_OK ||
            iprop_program_input_get(program, i, &input, error) != IPROP_EX_OK) return 0;
        row->root_id = input.input_index;
        row->type = input.type;
        row->name_byte_count = input.name.byte_count;
        if (row->name_byte_count != 0) {
            row->name_bytes = (uint8_t*)malloc(row->name_byte_count);
            if (row->name_bytes == NULL) return 0;
            memcpy(row->name_bytes, input.name.data, row->name_byte_count);
        }
        switch (row->type) {
        case IPROP_INPUT_COLOR:
            row->color_default = input.color_default;
            break;
        case IPROP_INPUT_F32:
            /* memcpy keeps negative zero, subnormals and NaN payloads exact. */
            memcpy(&row->scalar_default_bits, &input.f32_default, sizeof(uint32_t));
            break;
        case IPROP_INPUT_U32:
            row->scalar_default_bits = input.u32_default;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static void panel_display(const panel_model* panel) {
    uint32_t i, j;
    printf("program_freed=1\ninputs=%" PRIu32 "\n", panel->count);
    for (i = 0; i < panel->count; ++i) {
        const panel_row* row = &panel->rows[i];
        const char* type = row->type == IPROP_INPUT_COLOR ? "color"
                         : row->type == IPROP_INPUT_F32 ? "f32" : "u32";
        printf("input=%" PRIu32 " type=%s name_bytes=%" PRIu32 " name_hex=",
               row->root_id, type, row->name_byte_count);
        for (j = 0; j < row->name_byte_count; ++j) printf("%02x", (unsigned)row->name_bytes[j]);
        if (row->type == IPROP_INPUT_COLOR) {
            printf(" default_rgba=%02x%02x%02x%02x\n",
                   (unsigned)row->color_default.r, (unsigned)row->color_default.g,
                   (unsigned)row->color_default.b, (unsigned)row->color_default.a);
        } else {
            printf(" default_bits=%08" PRIx32, row->scalar_default_bits);
            if (row->type == IPROP_INPUT_U32) printf(" default_u32=%" PRIu32, row->scalar_default_bits);
            putchar('\n');
        }
    }
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
    panel_model panel = { NULL, 0 };
    iprop_error_ex error;
    char message[512] = { 0 };
    int success;
    if (argc != 2) {
        fprintf(stderr, "usage: parameter_panel file.iprop\n");
        return 2;
    }
    bytes = read_binary(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    if (iprop_error_ex_init(&error, sizeof(error)) != IPROP_EX_OK) {
        free(bytes);
        return 1;
    }
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    success = iprop_program_load_ex(bytes, size, &program, &error) == IPROP_EX_OK;
    free(bytes);
    if (success) success = panel_copy(program, &panel, &error);
    /* All panel data must survive this call independently of program views. */
    iprop_program_free(program);
    if (success) panel_display(&panel);
    else fprintf(stderr, "panel load/copy failed: %s\n", message[0] != '\0' ? message : "host allocation or schema error");
    panel_free(&panel);
    return success && !ferror(stdout) ? 0 : 1;
}
