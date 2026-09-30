// The shared glue both the windowed SFML host and the headless SFML
// conformance use: file loading and generation through the C API.
#pragma once

#include "iprop.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace iprop::sfml_host {

// Reads a whole file; throws std::runtime_error on failure.
inline std::vector<std::uint8_t> readFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open " + path);
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(file);
        throw std::runtime_error("cannot size " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
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
