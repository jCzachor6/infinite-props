#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>

struct Reader {
    std::vector<std::uint8_t> data;
    std::size_t at = 0;
    std::uint32_t word() {
        if (at > data.size() || data.size() - at < 4) throw std::runtime_error("truncated corpus");
        const auto* b = data.data() + at; at += 4;
        return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
    }
    std::uint64_t wide() {
        auto low = word();
        return low | (std::uint64_t(word()) << 32);
    }
    std::vector<std::uint8_t> blob() {
        const auto size = word();
        if (size > data.size() - at) throw std::runtime_error("truncated blob");
        std::vector<std::uint8_t> value(data.begin() + at, data.begin() + at + size); at += size; return value;
    }
};
