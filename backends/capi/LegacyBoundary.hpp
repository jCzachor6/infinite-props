#pragma once

#include "ErrorBoundary.hpp"

namespace iprop::capi {

enum class LegacyOperation { Load, Generate };

// Frozen API 0.2 policy: success, expected arguments and OOM leave text alone;
// load maps all other exceptions to FORMAT, while generation keeps Trap/base
// FormatError/other classifications. Borrowed copying never allocates.
template<class Operation>
iprop_status legacyBoundary(LegacyOperation policy, char* message, std::size_t capacity,
                            Operation&& operation) noexcept {
    const auto copy = [&](const char* text) noexcept { copyMessage(text, {message, capacity}); };
    const auto unexpected = policy == LegacyOperation::Load ? IPROP_E_FORMAT : IPROP_E_TRAP;
    try {
        return std::forward<Operation>(operation)().succeeded ? IPROP_OK : IPROP_E_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return IPROP_E_OOM;
    } catch (const Trap& error) {
        copy(error.what());
        return unexpected;
    } catch (const FormatError& error) {
        copy(error.what());
        return IPROP_E_FORMAT;
    } catch (const std::exception& error) {
        copy(error.what());
        return unexpected;
    } catch (...) {
        copy(policy == LegacyOperation::Load ? "unexpected exception while loading program" :
            "unexpected exception during generation");
        return unexpected;
    }
}

} // namespace iprop::capi
