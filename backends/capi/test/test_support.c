#include "test_support.h"

#include <stdlib.h>
#include <string.h>

uint8_t* read_file(const char* path, size_t* out_size) {
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

void check_point(const iprop_image* image, uint32_t slot, float x,
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
