#pragma once

#include "Error.hpp"

#include <cstring>
#include <string_view>

namespace iprop::capi {

struct MessageBuffer {
    char* data = nullptr;
    std::size_t capacity = 0;
};

// Legacy callers keep their historical byte-prefix truncation. New text can
// preserve a complete UTF-8 boundary without allocating or validating again.
inline std::size_t copyMessage(std::string_view message, MessageBuffer buffer,
                               bool utf8Boundary = false) noexcept {
    if (buffer.data == nullptr || buffer.capacity == 0) return 0;
    std::size_t count = message.size() < buffer.capacity - 1 ? message.size() : buffer.capacity - 1;
    if (utf8Boundary && count < message.size()) {
        while (count != 0 && (static_cast<unsigned char>(message[count]) & 0xc0u) == 0x80u) --count;
    }
    if (count != 0) std::memmove(buffer.data, message.data(), count);
    buffer.data[count] = '\0';
    return count;
}

enum class ErrorSinkIssue { None, DescriptorTooSmall, InvalidMessageBuffer };

// Call-local snapshot of caller storage, validated before an operation.
// NULL is the optional no-error-output case. A malformed message span still
// permits metadata/required-length reporting into a valid descriptor prefix,
// without dereferencing the invalid buffer. No global last_error is used.
class ErrorSink {
public:
    ErrorSinkIssue bind(iprop_error_ex* descriptor) noexcept {
        descriptor_ = nullptr;
        buffer_ = {};
        if (descriptor == nullptr) return ErrorSinkIssue::None;
        uint32_t size = 0;
        if (!descriptorSize(descriptor, size)) return ErrorSinkIssue::DescriptorTooSmall;
        descriptor_ = descriptor;
        buffer_.data = readField<char*>(descriptor, size, offsetof(iprop_error_ex, message_buffer));
        buffer_.capacity = readField<uint32_t>(descriptor, size, offsetof(iprop_error_ex, message_capacity));
        if (buffer_.data == nullptr && buffer_.capacity != 0) {
            buffer_ = {};
            return ErrorSinkIssue::InvalidMessageBuffer;
        }
        return ErrorSinkIssue::None;
    }

    void success() const noexcept { emit(iprop_error_ex{}, {}); }

    void report(iprop_status_ex status, ErrorReason reason, ErrorStage stage,
                std::string_view message) const noexcept {
        iprop_error_ex error{};
        error.status = status;
        error.reason = static_cast<uint32_t>(reason);
        error.stage = static_cast<uint32_t>(stage);
        emit(error, message);
    }

    void argument(std::string_view message = "invalid argument") const noexcept {
        report(IPROP_EX_E_ARGUMENT, ErrorReason::InvalidArgument, ErrorStage::Arguments, message);
    }

    void outOfMemory(ErrorStage stage) const noexcept {
        report(IPROP_EX_E_OOM, ErrorReason::OutOfMemory, stage, "out of memory");
    }

    void runtime(iprop_status_ex status, const FormatError& error,
                 const LoadedFile* file = nullptr) const noexcept {
        emit(describeError(status, error, file), error.what());
    }

private:
    iprop_error_ex* descriptor_ = nullptr;
    MessageBuffer buffer_;

    void emit(iprop_error_ex error, std::string_view message) const noexcept {
        const auto copied = copyMessage(message, buffer_, true);
        error.message_required_bytes = message.size();
        error.message_truncated = copied < message.size() ? 1 : 0;
        if (descriptor_ != nullptr) writeError(descriptor_, error);
    }
};

} // namespace iprop::capi
