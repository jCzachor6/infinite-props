/* Failures through the model API: structured errors with their stage, reason
 * and location, the limits of the host, and the rule that a failed call
 * publishes nothing and leaves the caller's outputs as they were. */
#include "iprop3d.h"
#include "check.h"

static iprop_utf8_view text(const char* value) {
    iprop_utf8_view view;
    view.data = value;
    view.byte_count = (uint32_t)strlen(value);
    return view;
}

static int read_named(const char* directory, const char* name, uint8_t** bytes, size_t* length) {
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    return read_fixture(path, bytes, length);
}

static uint32_t cancel_now(void* context) {
    ++*(uint32_t*)context;
    return 1;
}

static uint32_t never_cancel(void* context) {
    ++*(uint32_t*)context;
    return 0;
}

/* A generation that must fail as a trap at EXECUTE with `reason`, leaving
 * the caller's result pointer alone. The message goes to `message`. */
static int trapped(const iprop3d_model* model, const iprop_parameters* parameters,
                   const iprop_execution_limits_ex* limits, uint32_t reason, iprop_error_ex* error) {
    iprop3d_generate_options options;
    iprop3d_result* previous = (iprop3d_result*)(void*)&previous;
    iprop3d_result* out = previous;
    CHECK(iprop3d_generate_options_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 3;
    CHECK(iprop3d_generate(model, &options, parameters, limits, &out, error) == IPROP_EX_E_TRAP);
    CHECK(out == previous);
    CHECK(error->status == IPROP_EX_E_TRAP && error->reason == reason && error->stage == IPROP_STAGE_EXECUTE);
    return 0;
}

int main(int argc, char** argv) {
    uint8_t *crate_bytes, *debug_bytes, *tile_bytes;
    size_t crate_length, debug_length, tile_length, i;
    iprop3d_model *crate = NULL, *debug = NULL, *other = NULL;
    iprop3d_model* untouched = (iprop3d_model*)(void*)&untouched;
    iprop3d_model* out_model = untouched;
    iprop_program* program = NULL;
    iprop_parameters *parameters = NULL, *foreign = NULL;
    iprop3d_generate_options options;
    iprop_execution_limits_ex limits;
    iprop_cancellation* token = NULL;
    iprop3d_result* result = NULL;
    iprop3d_result_info info;
    iprop_error_ex error;
    char message[256], small[8];
    uint32_t polls = 0, count = 7;
    static const uint8_t garbage[16] = {'I', 'P', '3', 'D', 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    CHECK(argc == 2);
    CHECK(read_named(argv[1], "crate.iprop3d", &crate_bytes, &crate_length) == 0);
    CHECK(read_named(argv[1], "crate-debug.iprop3d", &debug_bytes, &debug_length) == 0);
    CHECK(read_named(argv[1], "tile.iprop", &tile_bytes, &tile_length) == 0);
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    error.message_buffer = message; error.message_capacity = sizeof(message);

    /* ---- loading ---- */
    CHECK(iprop3d_model_load(NULL, 4, &out_model, &error) == IPROP_EX_E_ARGUMENT && out_model == untouched);
    CHECK(error.reason == IPROP_REASON_INVALID_ARGUMENT && error.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(strcmp(message, "bytes is null") == 0);
    CHECK(iprop3d_model_load(crate_bytes, (uint64_t)crate_length, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_model_load(crate_bytes, 0, &out_model, &error) == IPROP_EX_E_FORMAT && out_model == untouched);
    CHECK(error.stage == IPROP_STAGE_LOAD);
    CHECK(iprop3d_model_load(garbage, sizeof(garbage), &out_model, &error) == IPROP_EX_E_FORMAT && out_model == untouched);
    /* Every truncation is a located format error and never a handle. */
    for (i = 0; i < debug_length; i += (i < 64 ? 1 : 37)) {
        CHECK(iprop3d_model_load(debug_bytes, (uint64_t)i, &out_model, &error) == IPROP_EX_E_FORMAT);
        CHECK(out_model == untouched && error.status == IPROP_EX_E_FORMAT);
        CHECK(error.stage == IPROP_STAGE_LOAD || error.stage == IPROP_STAGE_VALIDATE);
    }
    /* A 2D program is no model and a model no 2D program: each loader
     * stops at the other's magic. */
    CHECK(iprop3d_model_load(tile_bytes, (uint64_t)tile_length, &out_model, &error) == IPROP_EX_E_FORMAT);
    CHECK(out_model == untouched && error.reason == IPROP_REASON_BAD_MAGIC && error.stage == IPROP_STAGE_LOAD);
    CHECK(strstr(message, "2D .iprop program") != NULL);
    CHECK(iprop_program_load_ex(crate_bytes, (uint64_t)crate_length, &program, &error) == IPROP_EX_E_FORMAT);
    CHECK(program == NULL && error.reason == IPROP_REASON_BAD_MAGIC);
    /* A damaged instruction is found by validation, with its byte offset. */
    {
        uint8_t* copy = (uint8_t*)malloc(debug_length);
        size_t rejected = 0;
        CHECK(copy != NULL);
        for (i = 24; i < debug_length; ++i) {
            memcpy(copy, debug_bytes, debug_length);
            copy[i] ^= 0x5a;
            other = NULL;
            if (iprop3d_model_load(copy, (uint64_t)debug_length, &other, &error) != IPROP_EX_OK) {
                CHECK(other == NULL && error.status == IPROP_EX_E_FORMAT);
                ++rejected;
            }
            iprop3d_model_free(other);
        }
        /* Not every flipped byte matters (a colour, a debug line), most do. */
        CHECK(rejected > debug_length / 4);
        free(copy);
    }
    CHECK(iprop3d_model_load(crate_bytes, (uint64_t)crate_length, &crate, &error) == IPROP_EX_OK && crate != NULL);
    CHECK(error.status == IPROP_EX_OK && error.reason == IPROP_REASON_NONE && message[0] == '\0');
    CHECK(iprop3d_model_load(debug_bytes, (uint64_t)debug_length, &debug, NULL) == IPROP_EX_OK);

    /* ---- arguments of the other calls ---- */
    CHECK(iprop3d_model_input_count(NULL, &count, &error) == IPROP_EX_E_ARGUMENT && count == 7);
    CHECK(strcmp(message, "model is null") == 0 && error.stage == IPROP_STAGE_ARGUMENTS);
    CHECK(iprop3d_model_input_count(crate, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_model_input_find(NULL, text("wide"), &count, NULL) == IPROP_EX_E_ARGUMENT && count == 7);
    CHECK(iprop3d_model_parameters_create(NULL, &parameters, &error) == IPROP_EX_E_ARGUMENT && parameters == NULL);
    CHECK(iprop3d_model_parameters_create(crate, NULL, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_model_parameters_create(crate, &parameters, &error) == IPROP_EX_OK);
    CHECK(iprop3d_model_parameters_create(debug, &foreign, &error) == IPROP_EX_OK);

    CHECK(iprop3d_generate_options_init(&options, sizeof(options)) == IPROP_EX_OK);
    CHECK(iprop3d_generate(NULL, &options, NULL, NULL, &result, &error) == IPROP_EX_E_ARGUMENT && result == NULL);
    CHECK(strcmp(message, "model is null") == 0);
    CHECK(iprop3d_generate(crate, NULL, NULL, NULL, &result, &error) == IPROP_EX_E_ARGUMENT && result == NULL);
    CHECK(iprop3d_generate(crate, &options, NULL, NULL, NULL, &error) == IPROP_EX_E_ARGUMENT);
    /* A set of another load, even of the same source, is foreign. */
    CHECK(iprop3d_generate(crate, &options, foreign, NULL, &result, &error) == IPROP_EX_E_ARGUMENT && result == NULL);
    CHECK(error.reason == IPROP_REASON_FOREIGN_PARAMETERS && error.stage == IPROP_STAGE_ARGUMENTS);
    /* A wrong type and an unknown name are caught when the value is set. */
    CHECK(iprop_parameters_set_u32_named(parameters, text("wide"), 3, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(error.reason == IPROP_REASON_UNIFORM_TYPE);
    CHECK(iprop_parameters_set_f32_named(parameters, text("tall"), 3.0f, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(error.reason == IPROP_REASON_UNKNOWN_PARAMETER);

    /* Error storage may not overlap an output or an input of the call. */
    CHECK(iprop3d_generate(crate, &options, NULL, NULL, (iprop3d_result**)(void*)&error, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_generate(crate, (const iprop3d_generate_options*)(const void*)&error, NULL, NULL, &result, &error) ==
          IPROP_EX_E_ARGUMENT && result == NULL);
    CHECK(iprop3d_generate(crate, &options, NULL, NULL, &result, &error) == IPROP_EX_OK && result != NULL);
    CHECK(iprop3d_result_info_init(&info, sizeof(info)) == IPROP_EX_OK);
    /* Nor may its message buffer: the query is refused before any write. */
    error.message_buffer = (char*)&info; error.message_capacity = sizeof(info);
    CHECK(iprop3d_result_query(result, &info, &error) == IPROP_EX_E_ARGUMENT);
    CHECK(info.struct_size == sizeof(info) && info.vertex_count == 0);
    error.message_buffer = message; error.message_capacity = sizeof(message);
    iprop3d_result_free(result); result = NULL;

    /* ---- limits of the host: they cover the model and its textures ---- */
    CHECK(iprop_execution_limits_ex_init(&limits, sizeof(limits)) == IPROP_EX_OK);
    limits.work_limit = 1;
    CHECK(trapped(crate, NULL, &limits, IPROP_REASON_WORK_EXHAUSTED, &error) == 0);
    /* Enough for the model program, not for the first texture. */
    limits.work_limit = 200;
    CHECK(trapped(crate, NULL, &limits, IPROP_REASON_WORK_EXHAUSTED, &error) == 0);
    CHECK(strstr(message, "skin 'tile'") != NULL);
    limits.work_limit = 0;
    limits.timeout_ns = 1;
    CHECK(trapped(crate, NULL, &limits, IPROP_REASON_DEADLINE_EXCEEDED, &error) == 0);
    limits.timeout_ns = 0;
    CHECK(iprop_cancellation_create(&token, NULL) == IPROP_EX_OK);
    limits.cancellation = token;
    CHECK(iprop3d_generate(crate, &options, NULL, &limits, &result, &error) == IPROP_EX_OK);
    iprop3d_result_free(result); result = NULL;
    iprop_cancellation_cancel(token);
    CHECK(trapped(crate, NULL, &limits, IPROP_REASON_CANCELLED, &error) == 0);
    limits.cancellation = NULL;
    limits.poll = cancel_now; limits.poll_context = &polls;
    CHECK(trapped(crate, NULL, &limits, IPROP_REASON_CANCELLED, &error) == 0);
    CHECK(polls >= 1);
    polls = 0;
    limits.poll = never_cancel;
    CHECK(iprop3d_generate(crate, &options, NULL, &limits, &result, &error) == IPROP_EX_OK && polls >= 1);
    iprop3d_result_free(result); result = NULL;
    limits.poll = NULL; limits.flags = 1;
    CHECK(iprop3d_generate(crate, &options, NULL, &limits, &result, &error) == IPROP_EX_E_ARGUMENT && result == NULL);
    limits.flags = 0;

    /* ---- violations while generating ---- */
    /* In the model program: a box without width. With debug data the error
     * names the source line of the box. */
    CHECK(iprop_parameters_set_f32_named(parameters, text("wide"), 0.0f, NULL) == IPROP_EX_OK);
    CHECK(trapped(crate, parameters, NULL, IPROP_REASON_NUMERIC_RANGE, &error) == 0);
    CHECK(strstr(message, "box size must be positive") != NULL);
    CHECK((error.location_flags & (IPROP_LOCATION_FUNCTION | IPROP_LOCATION_PC)) ==
          (IPROP_LOCATION_FUNCTION | IPROP_LOCATION_PC));
    CHECK((error.location_flags & IPROP_LOCATION_DEBUG_LINE) == 0 && error.debug_line == 0);
    CHECK(iprop_parameters_set_f32_named(foreign, text("wide"), 0.0f, NULL) == IPROP_EX_OK);
    CHECK(trapped(debug, foreign, NULL, IPROP_REASON_NUMERIC_RANGE, &error) == 0);
    CHECK((error.location_flags & IPROP_LOCATION_DEBUG_LINE) != 0 && error.debug_line == 19);
    CHECK(iprop_parameters_set_f32_named(foreign, text("wide"), 8.0f, NULL) == IPROP_EX_OK);

    /* In a texture: the skin's own reason, its name in the message, and the
     * place in the model that asked for the texture. */
    CHECK(iprop_parameters_set_f32_named(foreign, text("rounding"), 100.0f, NULL) == IPROP_EX_OK);
    CHECK(trapped(debug, foreign, NULL, IPROP_REASON_NUMERIC_RANGE, &error) == 0);
    CHECK(strncmp(message, "skin 'tile': ", 13) == 0);
    CHECK((error.location_flags & IPROP_LOCATION_DEBUG_LINE) != 0 && error.debug_line == 16);

    /* A short message buffer truncates at a character and says so. */
    error.message_buffer = small; error.message_capacity = sizeof(small);
    CHECK(trapped(debug, foreign, NULL, IPROP_REASON_NUMERIC_RANGE, &error) == 0);
    CHECK(error.message_truncated == 1 && error.message_required_bytes > sizeof(small));
    CHECK(strcmp(small, "skin 't") == 0);
    /* The same failure without any error output. */
    CHECK(iprop3d_generate(debug, &options, foreign, NULL, &result, NULL) == IPROP_EX_E_TRAP && result == NULL);

    /* After every failure above the model still generates. */
    CHECK(iprop3d_generate(crate, &options, NULL, NULL, &result, NULL) == IPROP_EX_OK);
    CHECK(iprop3d_result_query(result, &info, NULL) == IPROP_EX_OK && info.vertex_count == 127);

    iprop3d_result_free(result);
    iprop_cancellation_free(token);
    iprop_parameters_free(parameters); iprop_parameters_free(foreign);
    iprop3d_model_free(crate); iprop3d_model_free(debug);
    free(crate_bytes); free(debug_bytes); free(tile_bytes);
    puts("capi3d errors: OK");
    return 0;
}
