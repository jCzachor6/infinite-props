#pragma once

#include "ErrorSink.hpp"

#include <exception>
#include <new>
#include <string_view>
#include <utility>

namespace iprop::capi {

// Stage identifies the originating check, rather than the class name or cause
// number alone. VM setup raises Trap for legacy compatibility, but it is an
// argument error in the new policy. Only an Execute-stage Trap is E_TRAP.
inline iprop_status_ex classifyError(const FormatError& error) noexcept {
    switch (error.stage) {
    case ErrorStage::Arguments: return IPROP_EX_E_ARGUMENT;
    case ErrorStage::Load:
    case ErrorStage::Validate: return IPROP_EX_E_FORMAT;
    default: return IPROP_EX_E_INTERNAL;
    }
}

inline iprop_status_ex classifyError(const Trap& error) noexcept {
    return error.stage == ErrorStage::Execute ? IPROP_EX_E_TRAP :
        classifyError(static_cast<const FormatError&>(error));
}

// The caller updates the active phase before each operation, so untyped OOM
// and internal failures identify the phase that failed. Typed errors retain
// their originating stage. An optional diagnostic file is borrowed until the
// boundary returns, including exception unwinding: its owner must live outside
// the callable, rather than being a local destroyed before a catch handler.
struct ErrorContext {
    ErrorStage stage = ErrorStage::None;
    const LoadedFile* file = nullptr;
};

// Expected argument failures use a borrowed message, without constructing or
// throwing an allocating runtime exception. Successful operations return {}.
// Parameter validators may supply a more specific typed argument cause.
struct CallOutcome {
    bool succeeded = true;
    ErrorReason argumentReason = ErrorReason::InvalidArgument;
    std::string_view message;

    static CallOutcome argument(std::string_view message = "invalid argument",
                                ErrorReason reason = ErrorReason::InvalidArgument) noexcept {
        return {false, reason, message};
    }
};

// Common foundation for future structured load/query/generation operations.
// It owns no output handle and does not validate or repeat an operation. The
// caller retains ownership until its final publication, inside the callable.
// Binding, classification and reporting do not allocate or throw. No legacy
// entry point uses this policy; their existing catch/status rules stay frozen.
template<class Operation>
iprop_status_ex errorBoundary(iprop_error_ex* output, ErrorContext& context,
                              Operation&& operation) noexcept {
    ErrorSink sink;
    const auto issue = sink.bind(output);
    if (issue != ErrorSinkIssue::None) {
        sink.argument(issue == ErrorSinkIssue::DescriptorTooSmall ?
            "error descriptor capacity is smaller than four bytes" :
            "message_buffer is null with nonzero capacity");
        return IPROP_EX_E_ARGUMENT;
    }
    const auto typed = [&](const auto& error) noexcept {
        const auto status = classifyError(error);
        if (status == IPROP_EX_E_INTERNAL)
            sink.report(status, ErrorReason::Internal, context.stage, error.what());
        else
            sink.runtime(status, error, context.file);
        return status;
    };
    try {
        const CallOutcome outcome = std::forward<Operation>(operation)();
        if (!outcome.succeeded) {
            sink.report(IPROP_EX_E_ARGUMENT, outcome.argumentReason, ErrorStage::Arguments, outcome.message);
            return IPROP_EX_E_ARGUMENT;
        }
        sink.success();
        return IPROP_EX_OK;
    } catch (const std::bad_alloc&) {
        sink.outOfMemory(context.stage);
        return IPROP_EX_E_OOM;
    } catch (const Trap& error) {
        return typed(error);
    } catch (const FormatError& error) {
        return typed(error);
    } catch (const std::exception& error) {
        sink.report(IPROP_EX_E_INTERNAL, ErrorReason::Internal, context.stage, error.what());
        return IPROP_EX_E_INTERNAL;
    } catch (...) {
        sink.report(IPROP_EX_E_INTERNAL, ErrorReason::Internal, context.stage, "unexpected internal exception");
        return IPROP_EX_E_INTERNAL;
    }
}

} // namespace iprop::capi
