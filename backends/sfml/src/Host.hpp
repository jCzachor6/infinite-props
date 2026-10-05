// The shared glue both the windowed SFML host and the headless SFML
// conformance use: file loading and generation through the C API.
#pragma once

#include "iprop.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace iprop::sfml_host {

using Program = std::unique_ptr<iprop_program, decltype(&iprop_program_free)>;
using Result = std::unique_ptr<iprop_result, decltype(&iprop_result_free)>;

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
