#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace iprop::capi {

// The caller promises that struct_size is the actual allocation capacity.
// memcpy permits prefix-only storage without accessing absent typed fields.
inline bool descriptorSize(const void* descriptor, std::uint32_t& size) noexcept {
    if (descriptor == nullptr) return false;
    std::memcpy(&size, descriptor, sizeof(size));
    return size >= sizeof(size);
}

template<class T>
void writeField(void* descriptor, std::uint32_t size, std::size_t offset,
                const T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset <= size && sizeof(T) <= size - offset) {
        std::memcpy(static_cast<unsigned char*>(descriptor) + offset,
                    &value, sizeof(T));
    }
}

} // namespace iprop::capi
