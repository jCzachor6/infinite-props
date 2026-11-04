#pragma once

#include "ErrorBoundary.hpp"
#include "Handles.hpp"
#include "Sized.hpp"

namespace iprop::capi {

struct ExecutionRequest {
    ExecutionLimits limits;
    iprop_cancel_poll poll = nullptr;
    void* pollContext = nullptr;
};

// The request stays in the checked call's stack until VM and packing finish.
// Retain the atomic flag, never the caller's mutable options descriptor.
inline CallOutcome readExecutionRequest(const iprop_execution_limits_ex* descriptor, ExecutionRequest& out) noexcept {
    if (!descriptor) return {};
    uint32_t size = 0;
    if (!descriptorSize(descriptor, size)) return CallOutcome::argument("execution limits are smaller than four bytes");
    if (readField<uint32_t>(descriptor, size, offsetof(iprop_execution_limits_ex, flags)))
        return CallOutcome::argument("execution limit flags are unsupported");
    out.limits.workLimit = readField<uint64_t>(descriptor, size, offsetof(iprop_execution_limits_ex, work_limit));
    out.limits.deadline = ExecutionLimits::deadlineAfter(
        readField<uint64_t>(descriptor, size, offsetof(iprop_execution_limits_ex, timeout_ns)));
    const auto cancellation = readField<const iprop_cancellation*>(descriptor, size, offsetof(iprop_execution_limits_ex, cancellation));
    if (cancellation) out.limits.cancellation = cancellation->token;
    out.poll = readField<iprop_cancel_poll>(descriptor, size, offsetof(iprop_execution_limits_ex, poll));
    out.pollContext = readField<void*>(descriptor, size, offsetof(iprop_execution_limits_ex, poll_context));
    if (out.poll) {
        out.limits.context = &out;
        out.limits.cancelled = [](void* value) {
            const auto& request = *static_cast<ExecutionRequest*>(value);
            return request.poll(request.pollContext) != 0;
        };
    }
    return {};
}

} // namespace iprop::capi
