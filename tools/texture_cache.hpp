#pragma once

// Textures kept between the generations of one tool run and, with a
// directory, between runs (models v1.2, M5): one IPTX file a texture, named
// by its key.

#include "iprop/model/Model.hpp"

#include <filesystem>
#include <set>
#include <span>
#include <vector>

namespace iprop::tools {

class TextureCache {
public:
    // Reads the record of every texture stored in `directory` under the
    // current stamp; their pixels are read when a result reuses them. An
    // empty path keeps textures in memory only.
    explicit TextureCache(std::filesystem::path directory = {});

    // For ModelInputs::knownTextures; valid until complete() is called.
    std::span<const model::KnownTexture> known() const { return known_; }
    // Completes a result: every reused texture gets its pixels, from memory
    // or from its file, and every new one is kept and, with a directory,
    // written. Throws std::runtime_error when a stored texture cannot be
    // read; the caller then generates again without known textures.
    void complete(model::ModelResult& result);

    u32 reused() const { return reused_; }
    u32 generated() const { return generated_; }

private:
    std::filesystem::path directory_;
    model::TextureStore store_;
    std::vector<model::KnownTexture> known_;
    std::set<u64> keys_, stored_;
    u32 reused_ = 0, generated_ = 0;
};

} // namespace iprop::tools
