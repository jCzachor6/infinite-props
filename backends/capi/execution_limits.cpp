#include "CheckedBoundary.hpp"
#include "Handles.hpp"

static_assert(IPROP_DEFAULT_WORK_LIMIT == iprop::kDefaultWorkLimit);
static_assert(IPROP_WORK_POLL_INTERVAL == iprop::kWorkPollInterval);

extern "C" {
iprop_status_ex iprop_execution_limits_ex_init(iprop_execution_limits_ex* out, uint32_t capacity) {
    using namespace iprop::capi;
    if (!out || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    const iprop_execution_limits_ex defaults{};
    writeField(out, capacity, 0, capacity);
#define ZERO(field) writeField(out, capacity, offsetof(iprop_execution_limits_ex, field), defaults.field)
    ZERO(flags); ZERO(work_limit); ZERO(timeout_ns); ZERO(cancellation); ZERO(poll); ZERO(poll_context);
#undef ZERO
    return IPROP_EX_OK;
}

iprop_status_ex iprop_work_stats_ex_init(iprop_work_stats_ex* out, uint32_t capacity) {
    using namespace iprop::capi;
    if (!out || capacity < sizeof(uint32_t)) return IPROP_EX_E_ARGUMENT;
    const iprop_work_stats_ex defaults{};
    writeField(out, capacity, 0, capacity);
#define ZERO(field) writeField(out, capacity, offsetof(iprop_work_stats_ex, field), defaults.field)
    ZERO(flags); ZERO(work_budget); ZERO(work_remaining); ZERO(work_used);
#undef ZERO
    return IPROP_EX_OK;
}

iprop_status_ex iprop_cancellation_create(iprop_cancellation** out, iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, sizeof(iprop_cancellation*), context, [&] {
        if (!out) return CallOutcome::argument("cancellation output is null");
        auto token = std::make_unique<iprop_cancellation>();
        *out = token.release();
        return CallOutcome{};
    });
}
void iprop_cancellation_cancel(iprop_cancellation* token) { if (token) token->token->cancel(); }
uint32_t iprop_cancellation_is_cancelled(const iprop_cancellation* token) { return token && token->token->cancelled() ? 1 : 0; }
void iprop_cancellation_free(iprop_cancellation* token) { delete token; }

iprop_status_ex iprop_result_query_work(const iprop_result* result, iprop_work_stats_ex* out, iprop_error_ex* error) {
    using namespace iprop::capi;
    ErrorContext context{iprop::ErrorStage::Arguments};
    return checkedBoundary(error, out, queryCapacity(out), context, [&] {
        if (!result) return CallOutcome::argument("result is null");
        uint32_t capacity = 0;
        if (!descriptorSize(out, capacity)) return CallOutcome::argument("work statistics are null or smaller than four bytes");
        if (readField<uint32_t>(out, capacity, offsetof(iprop_work_stats_ex, flags)))
            return CallOutcome::argument("work statistics flags are unsupported");
        const iprop_work_stats_ex stats{sizeof(iprop_work_stats_ex), 0, result->workBudget, result->workRemaining,
            result->workBudget - result->workRemaining};
#define WRITE(field) writeField(out, capacity, offsetof(iprop_work_stats_ex, field), stats.field)
        WRITE(work_budget); WRITE(work_remaining); WRITE(work_used);
#undef WRITE
        return CallOutcome{};
    });
}
} // extern "C"
