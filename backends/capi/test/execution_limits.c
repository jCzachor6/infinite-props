#include "iprop.h"
#include "check.h"

static uint32_t cancel_after(void* value) {
    uint32_t* remaining = (uint32_t*)value;
    if (*remaining) --*remaining;
    return *remaining == 0;
}

static int rejected(const iprop_program* program, const iprop_generate_options_ex* options,
                    const iprop_execution_limits_ex* limits, uint32_t reason) {
    iprop_error_ex error;
    iprop_result* previous = (iprop_result*)(void*)&previous;
    iprop_result* out = previous;
    CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
    CHECK(iprop_generate_controlled(program, options, NULL, limits, &out, &error) == IPROP_EX_E_TRAP);
    CHECK(out == previous && error.reason == reason && error.stage == IPROP_STAGE_EXECUTE);
    return 0;
}

static int same(const iprop_result* a, const iprop_result* b) {
    iprop_image x, y;
    iprop_result_stats_ex fx, fy;
    iprop_result_image(a, &x); iprop_result_image(b, &y);
    CHECK(x.width == y.width && x.height == y.height && x.output_count == y.output_count);
    CHECK(memcmp(x.pixels, y.pixels, (size_t)x.width * x.height * 4) == 0);
    CHECK(iprop_result_stats_ex_init(&fx, sizeof(fx)) == IPROP_EX_OK);
    CHECK(iprop_result_stats_ex_init(&fy, sizeof(fy)) == IPROP_EX_OK);
    CHECK(iprop_result_query_stats(a, &fx, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_query_stats(b, &fy, NULL) == IPROP_EX_OK);
    CHECK(fx.fuel_used == fy.fuel_used && fx.fuel_budget == fy.fuel_budget);
    return 0;
}

int main(int argc, char** argv) {
    uint8_t* bytes; size_t length, i;
    uint32_t capacity;
    iprop_program* program = NULL;
    iprop_result *reference = NULL, *actual = NULL;
    iprop_cancellation* token = NULL;
    iprop_generate_options_ex options;
    iprop_execution_limits_ex limits;
    iprop_work_stats_ex stats, other;
    union { iprop_execution_limits_ex align; unsigned char bytes[sizeof(iprop_execution_limits_ex) + 16]; } storage;
    CHECK(argc == 2 && read_fixture(argv[1], &bytes, &length) == 0);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &program, NULL) == IPROP_EX_OK); free(bytes);
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 26;
    CHECK(iprop_generate_ex(program, &options, NULL, &reference, NULL) == IPROP_EX_OK);
    CHECK(iprop_work_stats_ex_init(&stats, sizeof(stats)) == IPROP_EX_OK);
    CHECK(iprop_result_query_work(reference, &stats, NULL) == IPROP_EX_OK);
    CHECK(stats.work_budget == IPROP_DEFAULT_WORK_LIMIT && stats.work_used > 0);
    CHECK(stats.work_used + stats.work_remaining == stats.work_budget);
    CHECK(iprop_execution_limits_ex_init(&limits, sizeof(limits)) == IPROP_EX_OK);
    limits.work_limit = stats.work_used;
    limits.timeout_ns = UINT64_C(60000000000);
    CHECK(iprop_generate_controlled(program, &options, NULL, &limits, &actual, NULL) == IPROP_EX_OK);
    CHECK(same(reference, actual) == 0);
    CHECK(iprop_work_stats_ex_init(&other, sizeof(other)) == IPROP_EX_OK);
    CHECK(iprop_result_query_work(actual, &other, NULL) == IPROP_EX_OK);
    CHECK(other.work_remaining == 0 && other.work_used == stats.work_used);
    iprop_result_free(actual); actual = NULL;
    limits.work_limit--;
    CHECK(rejected(program, &options, &limits, IPROP_REASON_WORK_EXHAUSTED) == 0);
    limits.work_limit = 0;
    limits.timeout_ns = 1;
    CHECK(rejected(program, &options, &limits, IPROP_REASON_DEADLINE_EXCEEDED) == 0);
    limits.timeout_ns = 0;
    CHECK(iprop_cancellation_create(&token, NULL) == IPROP_EX_OK);
    CHECK(iprop_cancellation_is_cancelled(token) == 0);
    iprop_cancellation_cancel(token); iprop_cancellation_cancel(token);
    CHECK(iprop_cancellation_is_cancelled(token) == 1);
    limits.cancellation = token;
    for (i = 0; i < 2048; ++i) CHECK(rejected(program, &options, &limits, IPROP_REASON_CANCELLED) == 0);
    iprop_cancellation_free(token); limits.cancellation = NULL;
    iprop_cancellation_cancel(NULL); iprop_cancellation_free(NULL);
    CHECK(iprop_cancellation_is_cancelled(NULL) == 0);
    {
        uint32_t countdown = 4;
        limits.poll = cancel_after; limits.poll_context = &countdown;
        CHECK(rejected(program, &options, &limits, IPROP_REASON_CANCELLED) == 0);
        CHECK(countdown == 0);
        limits.poll = NULL; limits.poll_context = NULL;
    }
    /* All prior cancellation/error paths leave the next generation usable. */
    CHECK(iprop_generate_controlled(program, &options, NULL, NULL, &actual, NULL) == IPROP_EX_OK);
    CHECK(same(reference, actual) == 0); iprop_result_free(actual); actual = NULL;
    /* Four-byte prefixes ignore poisoned absent controls. */
    memset(&limits, 0xa5, sizeof(limits)); limits.struct_size = 4;
    CHECK(iprop_generate_controlled(program, &options, NULL, &limits, &actual, NULL) == IPROP_EX_OK);
    CHECK(same(reference, actual) == 0); iprop_result_free(actual); actual = NULL;
    for (capacity = 0; capacity <= sizeof(storage.bytes); ++capacity) {
        memset(storage.bytes, 0xa5, sizeof(storage.bytes));
        CHECK(iprop_execution_limits_ex_init((iprop_execution_limits_ex*)storage.bytes, capacity) ==
              (capacity < 4 ? IPROP_EX_E_ARGUMENT : IPROP_EX_OK));
        for (i = capacity < 4 ? 0 : capacity; i < sizeof(storage.bytes); ++i) CHECK(storage.bytes[i] == 0xa5);
        if (capacity >= sizeof(limits)) {
            const iprop_execution_limits_ex* initialized = (const iprop_execution_limits_ex*)storage.bytes;
            CHECK(initialized->struct_size == capacity && initialized->flags == 0 && initialized->work_limit == 0);
            CHECK(initialized->timeout_ns == 0 && initialized->cancellation == NULL && initialized->poll == NULL && initialized->poll_context == NULL);
        }
    }
    CHECK(iprop_execution_limits_ex_init(&limits, sizeof(limits)) == IPROP_EX_OK);
    limits.flags = 1;
    { iprop_result* previous = reference; iprop_error_ex error;
      CHECK(iprop_error_ex_init(&error, sizeof(error)) == IPROP_EX_OK);
      CHECK(iprop_generate_controlled(program, &options, NULL, &limits, &previous, &error) == IPROP_EX_E_ARGUMENT);
      CHECK(previous == reference && error.reason == IPROP_REASON_INVALID_ARGUMENT); }
    CHECK(iprop_work_stats_ex_init(&other, sizeof(other)) == IPROP_EX_OK);
    other.flags = 1;
    { iprop_work_stats_ex before = other;
      CHECK(iprop_result_query_work(reference, &other, NULL) == IPROP_EX_E_ARGUMENT);
      CHECK(memcmp(&before, &other, sizeof(other)) == 0); }
    iprop_result_free(reference); iprop_program_free(program);
    puts("C execution limits: PASS (exact work, deadline, 2048 cancellations, callback, prefixes and recovery)");
    return 0;
}
