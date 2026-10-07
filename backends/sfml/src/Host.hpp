// The shared glue both the windowed SFML host and the headless SFML
// conformance use: file loading and generation through the C API.
#pragma once

#include "iprop.h"
#include "../../ResultQueries.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iprop::sfml_host {

using Program = std::unique_ptr<iprop_program, decltype(&iprop_program_free)>;
using Result = std::unique_ptr<iprop_result, decltype(&iprop_result_free)>;

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
    char error[512] = {};
    iprop_program* program = nullptr;
    const iprop_status status = iprop_program_load(bytes.data(), bytes.size(),
                                                   &program, error,
                                                   sizeof(error));
    if (status != IPROP_OK) {
        throw std::runtime_error(error[0] != '\0' ? error
                                                  : "iprop_program_load failed");
    }
    return program;
}

// Copies metadata through the sized contract; no borrowed views escape.
inline iprop_program_info_ex queryProgram(const iprop_program* program) {
    iprop_program_info_ex info;
    char message[512] = {};
    iprop_error_ex error;
    if (iprop_program_info_ex_init(&info, sizeof(info)) != IPROP_EX_OK ||
        iprop_error_ex_init(&error, sizeof(error)) != IPROP_EX_OK) {
        throw std::runtime_error("iprop metadata initialization failed");
    }
    error.message_buffer = message;
    error.message_capacity = sizeof(message);
    if (iprop_program_query(program, &info, &error) != IPROP_EX_OK) {
        throw std::runtime_error(message[0] != '\0' ? message
                                                   : "iprop_program_query failed");
    }
    return info;
}

// Generates one (seed, variant, archetype) tuple; throws on failure. The
// returned handle must be freed by the caller.
inline iprop_result* generate(iprop_program* program, std::uint32_t seed,
                              std::uint32_t variant,
                              std::uint32_t archetype) {
    iprop_generate_options options{};
    options.seed = seed;
    options.variant = variant;
    options.archetype = archetype;
    char error[512] = {};
    iprop_result* result = nullptr;
    const iprop_status status = iprop_generate(program, &options, &result,
                                               error, sizeof(error));
    if (status != IPROP_OK) {
        throw std::runtime_error(error[0] != '\0' ? error
                                                  : "iprop_generate failed");
    }
    return result;
}

} // namespace iprop::sfml_host
