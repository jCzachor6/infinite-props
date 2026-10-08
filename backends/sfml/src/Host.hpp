// The shared glue both the windowed SFML host and the headless SFML
// conformance use: file loading and generation through the C API.
#pragma once

#include "iprop.h"
#include "../../ResultQueries.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace iprop::sfml_host {

using Program = std::unique_ptr<iprop_program, decltype(&iprop_program_free)>;
using Result = std::unique_ptr<iprop_result, decltype(&iprop_result_free)>;
using Parameters = std::unique_ptr<iprop_parameters, decltype(&iprop_parameters_free)>;

// Caller-owned structured error storage for one C API call. The descriptor
// points at this object's message buffer, so it is neither copied nor moved.
class Error {
public:
    Error() {
        if (iprop_error_ex_init(&value_, sizeof(value_)) != IPROP_EX_OK) {
            throw std::runtime_error("iprop error initialization failed");
        }
        value_.message_buffer = message_;
        value_.message_capacity = sizeof(message_);
    }
    Error(const Error&) = delete;
    Error& operator=(const Error&) = delete;

    iprop_error_ex* get() { return &value_; }

    // Throws the operation's message and its stable cause on failure.
    void check(iprop_status_ex status, const char* operation) const {
        if (status == IPROP_EX_OK) return;
        throw std::runtime_error(std::string(operation) + " failed: " +
                                 (message_[0] != '\0' ? message_ : "no message") +
                                 " (reason " + std::to_string(value_.reason) + ")");
    }

private:
    iprop_error_ex value_{};
    char message_[512] = {};
};

// This borrowed view is valid until result_free, including after program_free
// and later generations. The host copies byte_count bytes before releasing it.
inline iprop_image_view_ex queryImage(const iprop_result* result) {
    iprop_image_view_ex image;
    if (backend::queryImage(result, image) != IPROP_EX_OK || !backend::rgba8Image(image) ||
        image.byte_count > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("unsupported or invalid generated image");
    }
    return image;
}

struct OutputSlot {
    std::uint32_t index, owner;
    iprop_output_kind_ex kind;
    bool isList;
    std::string name; // Exact UTF-8 bytes, including embedded NUL.
    std::vector<float> values; // Owned leaf copies; empty declarations remain.
};

inline std::vector<OutputSlot> copyOutputs(const iprop_result* result) {
    std::uint32_t count;
    if (iprop_result_output_count(result, &count, nullptr) != IPROP_EX_OK) {
        throw std::runtime_error("iprop_result_output_count failed");
    }
    std::vector<OutputSlot> outputs;
    outputs.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        iprop_result_output_view_ex view;
        if (backend::queryOutput(result, i, view) != IPROP_EX_OK) {
            throw std::runtime_error("iprop_result_output_get failed");
        }
        const auto components = backend::outputComponents(view.kind);
        if (components == 0 || view.element_count > std::numeric_limits<std::size_t>::max() / components) {
            throw std::runtime_error("unsupported or oversized output");
        }
        OutputSlot slot{view.output_index, view.owner, view.kind, view.is_list != 0, {}, {}};
        if (view.name.byte_count != 0) slot.name.assign(view.name.data, view.name.byte_count);
        slot.values.resize(static_cast<std::size_t>(view.element_count) * components);
        if (backend::copyOutputValues(result, view, slot.values.data()) != IPROP_EX_OK) {
            throw std::runtime_error("typed result output read failed");
        }
        outputs.push_back(std::move(slot));
    }
    return outputs;
}

// A callable avoids carrying glibc's function attributes into a template
// argument, which GCC rejects under -Werror=ignored-attributes.
struct FileCloser {
    void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

// Reads a whole file; throws std::runtime_error on failure.
inline std::vector<std::uint8_t> readFile(const std::string& path) {
    const std::unique_ptr<std::FILE, FileCloser> file(std::fopen(path.c_str(), "rb"));
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        throw std::runtime_error("cannot seek " + path);
    }
    const long size = std::ftell(file.get());
    if (size < 0 || std::fseek(file.get(), 0, SEEK_SET) != 0) {
        throw std::runtime_error("cannot size " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), file.get());
    if (got != bytes.size()) {
        throw std::runtime_error("short read of " + path);
    }
    return bytes;
}

// Loads and validates the file; throws with the located message on
// failure. The returned handle must be freed by the caller.
inline iprop_program* loadProgram(const std::vector<std::uint8_t>& bytes) {
    Error error;
    iprop_program* program = nullptr;
    error.check(iprop_program_load_ex(bytes.data(), bytes.size(), &program, error.get()),
                "iprop_program_load_ex");
    return program;
}

// Copies metadata through the sized contract; no borrowed views escape.
inline iprop_program_info_ex queryProgram(const iprop_program* program) {
    iprop_program_info_ex info;
    Error error;
    error.check(iprop_program_info_ex_init(&info, sizeof(info)), "iprop_program_info_ex_init");
    error.check(iprop_program_query(program, &info, error.get()), "iprop_program_query");
    return info;
}

// A parameter set holding the program's declared defaults.
inline Parameters createParameters(const iprop_program* program) {
    Error error;
    iprop_parameters* parameters = nullptr;
    error.check(iprop_parameters_create(program, &parameters, error.get()), "iprop_parameters_create");
    return Parameters(parameters, iprop_parameters_free);
}

// Sets one input from text, typed by its declaration: #RRGGBB or #RRGGBBAA
// colours, decimal f32 and decimal u32 (a leading minus stores i32 bits).
inline void setParameter(iprop_parameters* parameters, std::string_view name, std::string_view text) {
    const iprop_utf8_view view{name.data(), static_cast<std::uint32_t>(name.size())};
    Error error;
    iprop_parameter_value_ex current;
    error.check(iprop_parameter_value_ex_init(&current, sizeof(current)), "iprop_parameter_value_ex_init");
    error.check(iprop_parameters_get_named(parameters, view, &current, error.get()), "iprop_parameters_get_named");
    const auto invalid = [&] { return std::runtime_error("invalid value '" + std::string(text) + "' for input " + std::string(name)); };
    const char* end = text.data() + text.size();
    switch (current.type) {
    case IPROP_INPUT_COLOR: {
        if ((text.size() != 7 && text.size() != 9) || text[0] != '#') throw invalid();
        std::uint8_t channels[4] = {0, 0, 0, 255};
        for (std::size_t i = 0; i < (text.size() - 1) / 2; ++i) {
            const char* at = text.data() + 1 + i * 2;
            if (std::from_chars(at, at + 2, channels[i], 16).ptr != at + 2) throw invalid();
        }
        error.check(iprop_parameters_set_color_named(parameters, view,
                    {channels[0], channels[1], channels[2], channels[3]}, error.get()), "iprop_parameters_set_color_named");
        return;
    }
    case IPROP_INPUT_F32: {
        const std::string copy(text);
        char* parsed = nullptr;
        const float value = std::strtof(copy.c_str(), &parsed);
        if (copy.empty() || parsed != copy.c_str() + copy.size()) throw invalid();
        error.check(iprop_parameters_set_f32_named(parameters, view, value, error.get()), "iprop_parameters_set_f32_named");
        return;
    }
    case IPROP_INPUT_U32:
        if (!text.empty() && text[0] == '-') {
            std::int32_t value = 0;
            if (std::from_chars(text.data(), end, value).ptr != end) throw invalid();
            error.check(iprop_parameters_set_i32_named(parameters, view, value, error.get()), "iprop_parameters_set_i32_named");
        } else {
            std::uint32_t value = 0;
            if (std::from_chars(text.data(), end, value).ptr != end) throw invalid();
            error.check(iprop_parameters_set_u32_named(parameters, view, value, error.get()), "iprop_parameters_set_u32_named");
        }
        return;
    default:
        throw std::runtime_error("unsupported input type for " + std::string(name));
    }
}

// Generates one (seed, variant, archetype) tuple from the given parameters,
// or the stored defaults when none are given; throws on failure. The
// returned handle must be freed by the caller.
inline iprop_result* generate(const iprop_program* program, std::uint32_t seed,
                              std::uint32_t variant, std::uint32_t archetype,
                              const iprop_parameters* parameters = nullptr) {
    Error error;
    iprop_generate_options_ex options;
    error.check(iprop_generate_options_ex_init(&options, sizeof(options)), "iprop_generate_options_ex_init");
    options.seed = seed;
    options.variant = variant;
    options.archetype = archetype;
    iprop_result* result = nullptr;
    error.check(iprop_generate_ex(program, &options, parameters, &result, error.get()), "iprop_generate_ex");
    return result;
}

} // namespace iprop::sfml_host
