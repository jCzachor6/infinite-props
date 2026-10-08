#include "iprop.h"
#include "check.h"

#define FIELD(n) {offsetof(iprop_result_output_view_ex, n), sizeof(((iprop_result_output_view_ex*)0)->n)}
typedef struct { size_t offset, size; } field;
static const field outputs[] = {FIELD(output_index), FIELD(owner), FIELD(kind), FIELD(is_list),
    FIELD(element_count), FIELD(stride_floats),
    {offsetof(iprop_result_output_view_ex, name) + offsetof(iprop_utf8_view, data), sizeof(const char*)},
    {offsetof(iprop_result_output_view_ex, name) + offsetof(iprop_utf8_view, byte_count), sizeof(uint32_t)}, FIELD(values)};
static const field inputs[] = {FIELD(struct_size), FIELD(flags)};

static iprop_status_ex typed(const iprop_result* result, uint32_t slot, uint32_t element,
                            uint32_t kind, void* out, uint32_t capacity, iprop_error_ex* error) {
    if (kind == IPROP_OUTPUT_KIND_POINT)
        return iprop_result_output_point(result, slot, element, (iprop_point_value*)out, capacity, error);
    if (kind == IPROP_OUTPUT_KIND_RECT)
        return iprop_result_output_rect(result, slot, element, (iprop_rect_value*)out, capacity, error);
    return iprop_result_output_circle(result, slot, element, (iprop_circle_value*)out, capacity, error);
}
static uint32_t record_size(uint32_t kind) {
    return kind == IPROP_OUTPUT_KIND_POINT ? sizeof(iprop_point_value) :
        kind == IPROP_OUTPUT_KIND_RECT ? sizeof(iprop_rect_value) : sizeof(iprop_circle_value);
}
static int prefixes(const iprop_result* result, const iprop_result_output_view_ex* expected) {
    const iprop_result_output_view_ex defaults = {0};
    uint32_t capacity;
    unsigned alignment;
    for (alignment = 0; alignment < 2; ++alignment) {
        for (capacity = 0; capacity <= sizeof(*expected) + 16; ++capacity) {
            const size_t allocation = capacity < 4 ? 4 : capacity;
            unsigned char* raw = (unsigned char*)malloc(allocation + alignment);
            unsigned char* storage;
            unsigned char before[sizeof(*expected) + 16];
            size_t byte, f;
            CHECK(raw != NULL); storage = raw + alignment;
            memset(raw, 0xa5, allocation + alignment);
            CHECK(iprop_result_output_view_ex_init((iprop_result_output_view_ex*)storage, capacity) ==
                  (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
            for (byte = 0; byte < allocation; ++byte) {
                unsigned char want = 0xa5;
                if (capacity >= 4 && byte < 4) want = ((const unsigned char*)&capacity)[byte];
                for (f = 1; f < sizeof(inputs)/sizeof(inputs[0]); ++f)
                    if (inputs[f].offset + inputs[f].size <= capacity && byte >= inputs[f].offset &&
                        byte < inputs[f].offset + inputs[f].size) want = ((const unsigned char*)&defaults)[byte];
                for (f = 0; f < sizeof(outputs)/sizeof(outputs[0]); ++f)
                    if (outputs[f].offset + outputs[f].size <= capacity && byte >= outputs[f].offset &&
                        byte < outputs[f].offset + outputs[f].size) want = ((const unsigned char*)&defaults)[byte];
                CHECK(storage[byte] == want);
            }
            if (alignment) CHECK(raw[0] == 0xa5);
            memset(storage, 0xa5, allocation); memcpy(storage, &capacity, 4);
            if (capacity >= 8) memset(storage + offsetof(iprop_result_output_view_ex, flags), 0, 4);
            memcpy(before, storage, allocation);
            CHECK(iprop_result_output_get(result, UINT32_MAX, (iprop_result_output_view_ex*)storage, NULL) == IPROP_EX_E_ARGUMENT);
            CHECK(memcmp(storage, before, allocation) == 0);
            CHECK(iprop_result_output_get(result, expected->output_index, (iprop_result_output_view_ex*)storage, NULL) ==
                  (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
            for (byte = 0; byte < allocation; ++byte) {
                unsigned char want = before[byte];
                for (f = 0; f < sizeof(outputs)/sizeof(outputs[0]); ++f)
                    if (outputs[f].offset + outputs[f].size <= capacity && byte >= outputs[f].offset &&
                        byte < outputs[f].offset + outputs[f].size) want = ((const unsigned char*)expected)[byte];
                CHECK(storage[byte] == want);
            }
            if (alignment) CHECK(raw[0] == 0xa5);
            memcpy(before, storage, allocation);
            CHECK(iprop_result_output_get(NULL, 0, (iprop_result_output_view_ex*)storage, NULL) == IPROP_EX_E_ARGUMENT);
            CHECK(memcmp(storage, before, allocation) == 0);
            free(raw);
        }
    }
    return 0;
}
static int typed_prefixes(const iprop_result* result, uint32_t slot,
                          const iprop_result_output_view_ex* view) {
    uint32_t capacity, element;
    unsigned alignment;
    const uint32_t extent = record_size(view->kind);
    for (element = 0; element < view->element_count; ++element) {
        for (alignment = 0; alignment < 2; ++alignment) {
            for (capacity = 0; capacity <= extent + 16; ++capacity) {
                const size_t allocation = capacity ? capacity : 1;
                unsigned char* raw = (unsigned char*)malloc(allocation + alignment);
                unsigned char* storage;
                size_t byte;
                CHECK(raw != NULL); storage = raw + alignment;
                memset(raw, 0xa5, allocation + alignment);
                CHECK(typed(result, slot, element, view->kind, storage, capacity, NULL) ==
                      (capacity < extent ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
                for (byte = 0; byte < allocation; ++byte) {
                    const unsigned char want = capacity >= extent && byte < extent ?
                        ((const unsigned char*)(view->values + element * view->stride_floats))[byte] : 0xa5;
                    CHECK(storage[byte] == want);
                }
                if (alignment) CHECK(raw[0] == 0xa5);
                free(raw);
            }
        }
    }
    return 0;
}
static int errors(const iprop_result* result) {
    iprop_result_output_view_ex out, before;
    iprop_error_ex error, error_before;
    uint32_t id = UINT32_MAX, capacity, kind;
    char message[128];
    const char bad[] = {'\xc5'};
    CHECK(iprop_result_output_view_ex_init(NULL, 4) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_output_get(result, 0, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_output_count(NULL, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_count(result, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){"anchor", 6}, NULL, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_result_output_find(NULL, 0, (iprop_utf8_view){"anchor", 6}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_find(result, UINT32_MAX, (iprop_utf8_view){"anchor", 6}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_find(result, 2, (iprop_utf8_view){"anchor", 6}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){"missing", 7}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){NULL, 1}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){bad, 1}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message;
    CHECK(iprop_result_output_view_ex_init(&out, sizeof(out)) == IPROP_EX_OK);
    for (capacity = 0; capacity <= 2; ++capacity) {
        error.message_capacity = capacity;
        out.flags = UINT32_MAX; memcpy(&before, &out, sizeof(out)); memset(message, 0xa5, sizeof(message));
        CHECK(iprop_result_output_get(result, 0, &out, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&out, &before, sizeof(out)) == 0);
        CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_INVALID_ARGUMENT &&
              error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
        CHECK(error.message_required_bytes > 2 && error.message_truncated == 1);
        if (capacity) CHECK(message[capacity - 1] == '\0');
        CHECK((unsigned char)message[capacity] == 0xa5);
        out.flags = 0;
        CHECK(iprop_result_output_get(result, 0, &out, &error) == IPROP_EX_OK);
        CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && error.stage == IPROP_STAGE_NONE &&
              error.location_flags == 0 && error.message_required_bytes == 0 && error.message_truncated == 0);
        if (capacity) CHECK(message[0] == '\0');
    }
    error.message_capacity = sizeof(message);
    CHECK(iprop_result_output_find(result, 2, (iprop_utf8_view){"anchor", 6}, &id, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strstr(message, "not found") != NULL);
    CHECK(iprop_result_output_find(result, 4, (iprop_utf8_view){"anchor", 6}, &id, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(strstr(message, "owner is out of range") != NULL);
    memcpy(&before, &out, sizeof(out));
    error.message_buffer = NULL; error.message_capacity = 1;
    CHECK(iprop_result_output_get(result, 0, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    error.message_buffer = (char*)&out.values;
    memcpy(&error_before, &error, sizeof(error));
    CHECK(iprop_result_output_get(result, 0, &out, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0 && memcmp(&error, &error_before, sizeof(error)) == 0);
    CHECK(iprop_result_output_get(result, 0, &out, (iprop_error_ex*)&out) == IPROP_EX_E_ARGUMENT);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
    error.message_buffer = (char*)&error.status;
    memcpy(&error_before, &error, sizeof(error));
    CHECK(iprop_result_output_count(result, &id, &error) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(memcmp(&error, &error_before, sizeof(error)) == 0);
    error.message_buffer = (char*)&id;
    memcpy(&error_before, &error, sizeof(error));
    CHECK(iprop_result_output_count(result, &id, &error) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(memcmp(&error, &error_before, sizeof(error)) == 0);
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){"anchor", 6}, &id, &error) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(memcmp(&error, &error_before, sizeof(error)) == 0);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    {
        union { iprop_result_output_view_ex align; unsigned char bytes[sizeof(out) + 17]; } extended;
        unsigned char snapshot[sizeof(extended.bytes)];
        const uint32_t claimed = sizeof(out) + 16;
        memset(extended.bytes, 0xa5, sizeof(extended.bytes));
        CHECK(iprop_result_output_view_ex_init((iprop_result_output_view_ex*)extended.bytes, claimed) == IPROP_EX_OK);
        memcpy(snapshot, extended.bytes, sizeof(snapshot));
        error.message_buffer = (char*)extended.bytes + claimed - 1; error.message_capacity = 1;
        memcpy(&error_before, &error, sizeof(error));
        CHECK(iprop_result_output_get(result, 0, (iprop_result_output_view_ex*)extended.bytes, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(extended.bytes, snapshot, sizeof(snapshot)) == 0 && memcmp(&error, &error_before, sizeof(error)) == 0);
        error.message_buffer = (char*)extended.bytes + claimed;
        CHECK(iprop_result_output_get(result, 0, (iprop_result_output_view_ex*)extended.bytes, &error) == IPROP_EX_OK);
        CHECK(extended.bytes[claimed] == 0);
        for (capacity = sizeof(out); capacity < claimed; ++capacity) CHECK(extended.bytes[capacity] == 0xa5);
        error.message_buffer = message; error.message_capacity = sizeof(message);
    }
    for (kind = 0; kind < 3; ++kind) {
        union { iprop_rect_value align; unsigned char bytes[sizeof(iprop_rect_value) + 17]; } leaf;
        unsigned char snapshot[sizeof(leaf.bytes)];
        const uint32_t slot = kind + 1, extent = record_size(kind), claimed = extent + 16;
        const uint32_t good_slot = kind == 0 ? 3 : kind;
        memset(leaf.bytes, 0xa5, sizeof(leaf.bytes)); memcpy(snapshot, leaf.bytes, sizeof(snapshot));
        CHECK(typed(result, UINT32_MAX, 0, kind, leaf.bytes, claimed, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(leaf.bytes, snapshot, sizeof(snapshot)) == 0);
        CHECK(typed(result, slot, 0, kind, leaf.bytes, claimed, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(leaf.bytes, snapshot, sizeof(snapshot)) == 0);
        CHECK(typed(result, good_slot, UINT32_MAX, kind, leaf.bytes, claimed, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(leaf.bytes, snapshot, sizeof(snapshot)) == 0);
        CHECK(error.status == IPROP_EX_E_ARGUMENT && error.reason == IPROP_REASON_INVALID_ARGUMENT &&
              error.stage == IPROP_STAGE_ARGUMENTS && error.location_flags == 0);
        CHECK(typed(NULL, good_slot, 0, kind, leaf.bytes, claimed, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(leaf.bytes, snapshot, sizeof(snapshot)) == 0);
        CHECK(typed(result, good_slot, 0, kind, NULL, claimed, &error) == IPROP_EX_E_ARGUMENT);
        error.message_buffer = (char*)leaf.bytes + claimed - 1; error.message_capacity = 1;
        memcpy(&error_before, &error, sizeof(error));
        CHECK(typed(result, good_slot, 0, kind, leaf.bytes, claimed, &error) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(leaf.bytes, snapshot, sizeof(snapshot)) == 0 && memcmp(&error, &error_before, sizeof(error)) == 0);
        CHECK(typed(result, good_slot, 0, kind, leaf.bytes, claimed, (iprop_error_ex*)leaf.bytes) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(leaf.bytes, snapshot, sizeof(snapshot)) == 0);
        error.message_buffer = (char*)leaf.bytes + claimed;
        CHECK(typed(result, good_slot, 0, kind, leaf.bytes, claimed, &error) == IPROP_EX_OK);
        CHECK(leaf.bytes[claimed] == 0);
        for (capacity = extent; capacity < claimed; ++capacity) CHECK(leaf.bytes[capacity] == 0xa5);
        error.message_capacity = 0; error.message_buffer = (char*)leaf.bytes;
        CHECK(typed(result, good_slot, 0, kind, leaf.bytes, claimed, &error) == IPROP_EX_OK);
        error.message_buffer = message; error.message_capacity = sizeof(message);
    }
    /* Error capacity is negotiated independently by every operation. An
     * invalid prefix prevents publication, including otherwise valid queries. */
    for (capacity = 0; capacity <= sizeof(error) + 8; ++capacity) {
        const size_t allocation = capacity < 4 ? 4 : capacity;
        unsigned char* storage = (unsigned char*)malloc(allocation);
        iprop_rect_value rectangle, rectangle_before;
        size_t byte;
        CHECK(storage != NULL); memset(storage, 0xa5, allocation);
        CHECK(iprop_error_ex_init((iprop_error_ex*)storage, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        memcpy(storage, &capacity, 4);
        CHECK(iprop_result_output_view_ex_init(&out, sizeof(out)) == IPROP_EX_OK);
        memcpy(&before, &out, sizeof(out));
        CHECK(iprop_result_output_get(result, 0, &out, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        if (capacity < 4) CHECK(memcmp(&out, &before, sizeof(out)) == 0);
        else CHECK(out.output_index == 0 && out.kind == IPROP_OUTPUT_KIND_POINT);
        id = UINT32_MAX;
        CHECK(iprop_result_output_count(result, &id, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(id == (capacity < 4 ? UINT32_MAX : 18));
        id = UINT32_MAX;
        CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){"anchor",6}, &id, (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        CHECK(id == (capacity < 4 ? UINT32_MAX : 0));
        memset(&rectangle, 0xa5, sizeof(rectangle)); memcpy(&rectangle_before, &rectangle, sizeof(rectangle));
        CHECK(iprop_result_output_rect(result, 1, 0, &rectangle, sizeof(rectangle), (iprop_error_ex*)storage) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        if (capacity < 4) CHECK(memcmp(&rectangle, &rectangle_before, sizeof(rectangle)) == 0);
        else CHECK(rectangle.x == 4.0f && rectangle.y == 5.0f && rectangle.w == 6.0f && rectangle.h == 7.0f);
        memcpy(&rectangle_before, &rectangle, sizeof(rectangle));
        CHECK(iprop_result_output_rect(result, 1, 1, &rectangle, sizeof(rectangle), (iprop_error_ex*)storage) == IPROP_EX_E_ARGUMENT);
        CHECK(memcmp(&rectangle, &rectangle_before, sizeof(rectangle)) == 0);
        for (byte = sizeof(error); byte < allocation; ++byte) CHECK(storage[byte] == 0xa5);
        free(storage);
    }
    return 0;
}

/* Independent format-0.1 fixture, §4/5/6/9 wire bytes: const.f32 0x11,
 * out.point/rect/circle 0x90/91/92. No compiler optimizer or source float
 * spelling can canonicalize the raw-bit oracle; two uncalled parts include
 * an empty owner followed by named singular declarations. */
typedef struct { uint8_t bytes[1024]; uint32_t size; } buffer;
static void put8(buffer* out, uint8_t v) { out->bytes[out->size++] = v; }
static void put16(buffer* out, uint16_t v) { put8(out, (uint8_t)v); put8(out, (uint8_t)(v >> 8)); }
static void put32(buffer* out, uint32_t v) { put16(out, (uint16_t)v); put16(out, (uint16_t)(v >> 16)); }
static void append(buffer* out, const void* data, uint32_t size) {
    if (size) memcpy(out->bytes + out->size, data, size);
    out->size += size;
}
static void decl(buffer* out, uint8_t kind, uint32_t name) {
    put8(out, kind); put8(out, 0); put16(out, 0); put32(out, name);
}
static const uint32_t raw_bits[] = {UINT32_C(0x80000000), 1,
    UINT32_C(0x7fc12345), UINT32_C(0xff800000), UINT32_C(0x7f800000), UINT32_C(0xffc12345),
    UINT32_C(0x80000001), UINT32_C(0x3f800000), UINT32_C(0x80000000)};
static const char exact_name[] = "p\xc5\x82\0suffix";
static buffer binary_fixture(int write_values) {
    buffer meta = {{0},0}, pool = {{0},0}, code = {{0},0}, file = {{0},0};
    uint32_t i, k = 0, offset = 56;
    put32(&meta, 1); put32(&meta, 1); put32(&meta, 0); put32(&meta, 0);
    put32(&meta, 1); put32(&meta, 1); put32(&meta, 100); put32(&meta, 0);
    put32(&meta, 0); put32(&meta, 0); put16(&meta, 0); put16(&meta, 3);
    for (i = 0; i < 3; ++i) decl(&meta, (uint8_t)i, i + 1);
    put32(&pool, 4);
    put32(&pool, 4); append(&pool, "draw", 4);
    put32(&pool, sizeof(exact_name) - 1); append(&pool, exact_name, sizeof(exact_name) - 1);
    put32(&pool, 0); put32(&pool, 1); append(&pool, "c", 1);
    put32(&pool, 0); put32(&pool, 9);
    for (i = 0; i < 9; ++i) put32(&pool, raw_bits[i]);
    put32(&pool, 0);
    put32(&code, 3);
    for (i = 0; i < 3; ++i) {
        const uint32_t length = write_values ? 48 : 0;
        put32(&code, i == 0 ? 0 : UINT32_MAX); put32(&code, i == 0 ? 0 : length); put32(&code, i == 0 ? length : 0);
        put16(&code, 2); put16(&code, 0); put16(&code, 0); put16(&code, 0);
    }
    if (write_values) for (i = 0; i < 3; ++i) {
        uint32_t component, stride = i == 0 ? 2 : i == 1 ? 4 : 3;
        for (component = 0; component < stride; ++component) {
            put8(&code, 0x11); put8(&code, 0); put16(&code, (uint16_t)k++);
        }
        put8(&code, (uint8_t)(0x90 + i)); put8(&code, 0); put16(&code, (uint16_t)i);
    }
    put32(&code, 2);
    for (i = 0; i < 2; ++i) {
        uint32_t kind;
        put32(&code, i + 1); put32(&code, 1); put32(&code, 1); put32(&code, 0); put32(&code, 0);
        put16(&code, 0); put16(&code, i == 0 ? 0 : 3);
        if (i) for (kind = 0; kind < 3; ++kind) decl(&code, (uint8_t)kind, kind + 1);
    }
    append(&file, "IPRP", 4); put16(&file, 0); put16(&file, 1);
    put32(&file, 0); put32(&file, 3); put32(&file, 0);
    put32(&file, 1); put32(&file, offset); put32(&file, meta.size); offset += meta.size;
    put32(&file, 2); put32(&file, offset); put32(&file, pool.size); offset += pool.size;
    put32(&file, 3); put32(&file, offset); put32(&file, code.size);
    append(&file, meta.bytes, meta.size); append(&file, pool.bytes, pool.size); append(&file, code.bytes, code.size);
    return file;
}
static int raw_values(void) {
    buffer file = binary_fixture(1);
    iprop_program* program = NULL;
    iprop_result* result = NULL;
    iprop_result_output_view_ex view;
    iprop_generate_options options = {0};
    uint32_t count, kind, id = UINT32_MAX, begin = 0;
    unsigned char copy[sizeof(iprop_rect_value)];
    char message[128];
    CHECK(iprop_program_load_ex(file.bytes, file.size, &program, NULL) == IPROP_EX_OK);
    memset(file.bytes, 0, file.size);
    CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
    iprop_program_free(program);
    CHECK(iprop_result_output_count(result, &count, NULL) == IPROP_EX_OK && count == 6);
    for (kind = 0; kind < 3; ++kind) {
        const iprop_utf8_view name = kind == 0 ? (iprop_utf8_view){exact_name, sizeof(exact_name)-1} :
            kind == 1 ? (iprop_utf8_view){NULL, 0} : (iprop_utf8_view){"c", 1};
        CHECK(iprop_result_output_find(result, 0, name, &id, NULL) == IPROP_EX_OK && id == kind);
        CHECK(iprop_result_output_find(result, 2, name, &id, NULL) == IPROP_EX_OK && id == kind + 3);
        CHECK(iprop_result_output_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK);
        CHECK(iprop_result_output_get(result, kind, &view, NULL) == IPROP_EX_OK);
        CHECK(view.name.byte_count == name.byte_count && (name.byte_count == 0 || memcmp(view.name.data, name.data, name.byte_count) == 0));
        CHECK(view.element_count == 1 && view.kind == kind && view.is_list == 0);
        CHECK(prefixes(result, &view) == 0 && typed_prefixes(result, kind, &view) == 0);
        memset(copy, 0xa5, sizeof(copy));
        CHECK(typed(result, kind, 0, kind, copy, sizeof(copy), NULL) == IPROP_EX_OK);
        CHECK(memcmp(copy, raw_bits + begin, record_size(kind)) == 0);
        begin += view.stride_floats;
        CHECK(iprop_result_output_get(result, kind + 3, &view, NULL) == IPROP_EX_OK);
        CHECK(view.owner == 2 && view.element_count == 0 && view.values != NULL);
        CHECK(typed(result, kind + 3, 0, kind, copy, sizeof(copy), NULL) == IPROP_EX_E_ARGUMENT);
    }
    id = UINT32_MAX;
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){exact_name, 3}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_find(result, 0, (iprop_utf8_view){exact_name, 2}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    iprop_result_free(result);
    /* Typed copies have no handle dependency. */
    CHECK(memcmp(copy, raw_bits + 6, sizeof(iprop_circle_value)) == 0);
    file = binary_fixture(0);
    CHECK(iprop_program_load_ex(file.bytes, file.size, &program, NULL) == IPROP_EX_OK);
    CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
    iprop_program_free(program);
    CHECK(iprop_result_output_count(result, &count, NULL) == IPROP_EX_OK && count == 6);
    for (kind = 0; kind < count; ++kind) {
        CHECK(iprop_result_output_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK);
        CHECK(iprop_result_output_get(result, kind, &view, NULL) == IPROP_EX_OK);
        CHECK(view.element_count == 0 && view.values == NULL);
        CHECK(prefixes(result, &view) == 0);
        CHECK(typed(result, kind, 0, view.kind, copy, sizeof(copy), NULL) == IPROP_EX_E_ARGUMENT);
    }
    iprop_result_free(result);
    return 0;
}
static int slots(const iprop_program* program, const iprop_result* result, uint32_t variant) {
    static const char* names[] = {"anchor", "collider", "zone", "anchors", "colliders", "zones"};
    static const float point[8][2] = {{1,2},{2,7},{7,6},{6,1},{7,2},{1,6},{2,1},{6,7}};
    static const float rect[8][4] = {{1,2,3,4},{2,4,4,3},{4,2,3,4},{2,1,4,3},
        {4,2,3,4},{1,2,3,4},{2,1,4,3},{2,4,4,3}};
    static const uint32_t transform[] = {0,1,2,3,4,7,5,6};
    iprop_image legacy = {0};
    iprop_result_output_view_ex view;
    uint32_t count = UINT32_MAX, id, i;
    iprop_result_image(result, &legacy);
    CHECK(iprop_result_output_count(result, &count, NULL) == IPROP_EX_OK && count == 18 && legacy.output_count == count);
    for (i = 0; i < count; ++i) {
        const uint32_t owner = i < 6 ? 0 : i < 12 ? 1 : 3, local = i % 6;
        const iprop_output* old = legacy.outputs + i;
        iprop_output_info_ex declaration;
        uint32_t expected_count = owner == 0 ? (local == 0 ? (variant != 0) : local < 3 ? 1 : 2) :
            owner == 1 ? (local < 3 ? 1 : 0) : (local == 0 ? 2 : 0);
        CHECK(iprop_result_output_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK);
        CHECK(iprop_result_output_get(result, i, &view, NULL) == IPROP_EX_OK);
        CHECK(view.output_index == i && view.owner == owner && view.kind == local % 3 && view.element_count == expected_count);
        CHECK(view.name.data == old->name && view.name.byte_count == strlen(names[local]) &&
              memcmp(view.name.data, names[local], view.name.byte_count) == 0);
        CHECK(view.values == old->values && view.owner == old->owner && view.kind == (uint32_t)old->kind &&
              view.is_list == (uint32_t)old->is_list && view.element_count == old->count);
        CHECK(view.stride_floats == (local % 3 == 0 ? 2u : local % 3 == 1 ? 4u : 3u));
        CHECK(iprop_result_output_find(result, owner, view.name, &id, NULL) == IPROP_EX_OK && id == i);
        if (program != NULL) {
            CHECK(iprop_output_info_ex_init(&declaration, sizeof(declaration)) == IPROP_EX_OK);
            CHECK(iprop_program_output_get(program, i, &declaration, NULL) == IPROP_EX_OK);
            CHECK(declaration.output_index == i && declaration.owner == view.owner && declaration.kind == view.kind &&
                  declaration.is_list == view.is_list && declaration.name.byte_count == view.name.byte_count);
        }
        CHECK(typed_prefixes(result, i, &view) == 0);
        {
            unsigned char copied[sizeof(iprop_rect_value)];
            memset(copied, 0xa5, sizeof(copied));
            CHECK(typed(result, i, view.element_count, view.kind, copied, sizeof(copied), NULL) == IPROP_EX_E_ARGUMENT);
            for (id = 0; id < sizeof(copied); ++id) CHECK(copied[id] == 0xa5);
        }
        if (expected_count == 0) CHECK(view.values != NULL); /* exact nonempty packed array begin pointer */
        /* Part values are published in root coordinates: the D4 image in
           the part, then the blit origin {3, 4} (spec v0.5 section 6). */
        if (i == 6 || i == 8) {
            CHECK(view.values[0] == point[transform[variant]][0] + 3.0f &&
                  view.values[1] == point[transform[variant]][1] + 4.0f);
        }
        if (i == 7) {
            CHECK(view.values[0] == rect[transform[variant]][0] + 3.0f && view.values[1] == rect[transform[variant]][1] + 4.0f &&
                  view.values[2] == rect[transform[variant]][2] && view.values[3] == rect[transform[variant]][3]);
        }
        if (i == 8) CHECK(view.values[2] == 3.0f);
        if (i == 0 && variant) CHECK(view.values[0] == 5.0f && view.values[1] == 6.0f);
        if (i == 3 || i == 4 || i == 5) {
            CHECK(view.values[0] == 1.0f && view.values[1] == 2.0f);
            CHECK(view.values[view.stride_floats] == 11.0f && view.values[view.stride_floats + 1] == 12.0f);
        }
    }
    return 0;
}
int main(int argc, char** argv) {
    uint8_t* bytes;
    size_t size;
    iprop_program *program = NULL, *empty_program = NULL;
    iprop_result *result = NULL, *later = NULL, *empty = NULL;
    iprop_library_info_ex library;
    iprop_generate_options options = {0};
    iprop_result_output_view_ex saved, view;
    iprop_point_value copied;
    uint32_t variant, count = UINT32_MAX, id = UINT32_MAX;
    char message[128];
    CHECK(argc == 3);
    CHECK(sizeof(iprop_point_value) == 8 && sizeof(iprop_rect_value) == 16 && sizeof(iprop_circle_value) == 12);
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query(&library) == IPROP_EX_OK &&
          (library.capabilities & IPROP_CAP_RESULT_OUTPUTS) != 0 && (library.capabilities & IPROP_CAP_TYPED_OUTPUTS) != 0);
    CHECK(read_fixture(argv[1], &bytes, &size) == 0);
    CHECK(iprop_program_load_ex(bytes, size, &program, NULL) == IPROP_EX_OK); memset(bytes, 0, size); free(bytes);
    for (variant = 0; variant < 8; ++variant) {
        options.variant = variant; options.seed = 19 + variant;
        CHECK(iprop_generate(program, &options, &later, message, sizeof(message)) == IPROP_OK);
        CHECK(slots(program, later, variant) == 0);
        iprop_result_free(later);
    }
    options.variant = 0;
    CHECK(iprop_generate(program, &options, &result, message, sizeof(message)) == IPROP_OK);
    CHECK(iprop_result_output_view_ex_init(&saved, sizeof(saved)) == IPROP_EX_OK);
    CHECK(iprop_result_output_get(result, 3, &saved, NULL) == IPROP_EX_OK);
    CHECK(prefixes(result, &saved) == 0 && errors(result) == 0);
    CHECK(iprop_result_output_point(result, 3, 1, &copied, sizeof(copied), NULL) == IPROP_EX_OK);
    options.variant = 7;
    CHECK(iprop_generate(program, &options, &later, message, sizeof(message)) == IPROP_OK);
    iprop_result_free(later);
    {
        iprop_result* failed = result;
        options.variant = UINT32_MAX;
        CHECK(iprop_generate(program, &options, &failed, message, sizeof(message)) == IPROP_E_TRAP && failed == result);
    }
    iprop_program_free(program);
    CHECK(slots(NULL, result, 0) == 0 && errors(result) == 0);
    CHECK(iprop_result_output_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK);
    CHECK(iprop_result_output_get(result, 3, &view, NULL) == IPROP_EX_OK);
    CHECK(view.values == saved.values && view.name.data == saved.name.data && view.name.byte_count == saved.name.byte_count);
    CHECK(view.values[0] == 1.0f && view.values[2] == 11.0f);
    iprop_result_free(result);
    CHECK(copied.x == 11.0f && copied.y == 12.0f);
    CHECK(read_fixture(argv[2], &bytes, &size) == 0);
    CHECK(iprop_program_load_ex(bytes, size, &empty_program, NULL) == IPROP_EX_OK); free(bytes);
    options.variant = 0;
    CHECK(iprop_generate(empty_program, &options, &empty, message, sizeof(message)) == IPROP_OK);
    iprop_program_free(empty_program);
    CHECK(iprop_result_output_count(empty, &count, NULL) == IPROP_EX_OK && count == 0);
    CHECK(iprop_result_output_find(empty, 0, (iprop_utf8_view){NULL,0}, &id, NULL) == IPROP_EX_E_ARGUMENT && id == UINT32_MAX);
    CHECK(iprop_result_output_get(empty, 0, &view, NULL) == IPROP_EX_E_ARGUMENT);
    iprop_result_free(empty);
    CHECK(raw_values() == 0);
    puts("C99 result outputs: exact legacy slots/pointers, helper/D4 values, owners/names, all prefixes/typed capacities, aliases/errors, lifetime and raw float bits: PASS");
    return 0;
}
