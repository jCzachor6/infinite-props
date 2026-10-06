#pragma once

#include "ErrorBoundary.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace iprop::capi {

struct ProtectedSpan {
    const void* data;
    std::size_t size;
};

inline bool storageOverlaps(const void* a, std::size_t aSize,
                            const void* b, std::size_t bSize) noexcept {
    if (a == nullptr || b == nullptr || aSize == 0 || bSize == 0) return false;
    const auto first = reinterpret_cast<std::uintptr_t>(a);
    const auto second = reinterpret_cast<std::uintptr_t>(b);
    // Differences avoid overflow and ordering unrelated C++ object pointers.
    return first <= second ? second - first < aSize : first - second < bSize;
}

inline bool errorStorageAliases(const iprop_error_ex* error, const void* output,
                                std::size_t outputSize) noexcept {
    if (error == nullptr) return false;
    uint32_t size = 0;
    const bool valid = descriptorSize(error, size);
    const auto readable = std::max<std::size_t>(size, sizeof(uint32_t));
    if (storageOverlaps(error, readable, output, outputSize)) return true;
    if (!valid) return false;
    const auto* buffer = readField<char*>(error, size, offsetof(iprop_error_ex, message_buffer));
    const auto capacity = readField<uint32_t>(error, size, offsetof(iprop_error_ex, message_capacity));
    return storageOverlaps(buffer, capacity, error, readable) ||
        storageOverlaps(buffer, capacity, output, outputSize);
}

// Overlapping outputs cannot report an error without changing the protected
// query/handle storage. Reject them before binding, leaving both outputs alone.
template<class Operation>
iprop_status_ex checkedBoundary(iprop_error_ex* error, const void* output,
                                std::size_t outputSize, ErrorContext& context,
                                Operation&& operation,
                                std::span<const ProtectedSpan> protectedSpans = {}) noexcept {
    // Reject an error descriptor inside protected input storage before reading
    // any of its optional fields. A handle/value may only be four bytes long.
    uint32_t errorSize = 0;
    if (error != nullptr) (void)descriptorSize(error, errorSize);
    const auto errorReadable = error == nullptr ? 0 :
        std::max<std::size_t>(errorSize, sizeof(uint32_t));
    for (const auto& input : protectedSpans) {
        if (storageOverlaps(error, errorReadable, input.data, input.size) ||
            storageOverlaps(output, outputSize, input.data, input.size))
            return IPROP_EX_E_ARGUMENT;
    }
    if (errorStorageAliases(error, output, outputSize)) return IPROP_EX_E_ARGUMENT;
    for (const auto& input : protectedSpans) {
        if (errorStorageAliases(error, input.data, input.size))
            return IPROP_EX_E_ARGUMENT;
    }
    return errorBoundary(error, context, std::forward<Operation>(operation));
}

template<class T>
std::size_t queryCapacity(const T* output) noexcept {
    if (output == nullptr) return 0;
    uint32_t size = 0;
    (void)descriptorSize(output, size);
    return std::max<std::size_t>(size, sizeof(uint32_t));
}

} // namespace iprop::capi
