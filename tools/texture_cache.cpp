#include "texture_cache.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace iprop::tools {
namespace {

std::vector<u8> readFile(const std::filesystem::path& path, std::size_t limit = SIZE_MAX) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<u8> bytes;
    char chunk[4096];
    while (stream && bytes.size() < limit) {
        stream.read(chunk, static_cast<std::streamsize>(std::min(sizeof(chunk), limit - bytes.size())));
        bytes.insert(bytes.end(), chunk, chunk + stream.gcount());
    }
    return bytes;
}

} // namespace

TextureCache::TextureCache(std::filesystem::path directory) : directory_(std::move(directory)) {
    if (directory_.empty()) return;
    std::error_code error;
    for (std::filesystem::directory_iterator at(directory_, error), end; !error && at != end; at.increment(error)) {
        if (at->path().extension() != ".iptx") continue;
        const auto record = model::peekTexture(readFile(at->path(), model::kTextureHeaderBytes));
        // A file of another stamp, or under another name than its key, is not ours.
        if (!record || at->path().filename() != model::textureFileName(record->key)) continue;
        if (keys_.insert(record->key).second) {
            known_.push_back(*record);
            stored_.insert(record->key);
        }
    }
}

void TextureCache::complete(model::ModelResult& result) {
    for (const model::ModelTexture& texture : result.textures) {
        if (!texture.reused) {
            ++generated_;
            continue;
        }
        ++reused_;
        if (store_.find(texture.key)) continue;
        const auto path = directory_ / model::textureFileName(texture.key);
        std::optional<model::ModelTexture> stored;
        try {
            stored = model::unpackTexture(readFile(path));
        } catch (const FormatError& error) {
            throw std::runtime_error("stored texture '" + path.string() + "' is damaged: " + error.what());
        }
        if (!stored || stored->key != texture.key)
            throw std::runtime_error("stored texture '" + path.string() + "' cannot be read");
        store_.add(std::move(*stored));
    }
    store_.complete(result);
    for (const model::ModelTexture& texture : result.textures) {
        if (keys_.insert(texture.key).second) known_.push_back(model::knownTexture(texture));
        if (directory_.empty() || !stored_.insert(texture.key).second) continue;
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        const auto bytes = model::packTexture(texture);
        // Written under another name first: a reader never sees half a file.
        const auto path = directory_ / model::textureFileName(texture.key);
        auto partial = path;
        partial += ".part";
        std::ofstream stream(partial, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) throw std::runtime_error("cannot write '" + partial.string() + "'");
        std::filesystem::rename(partial, path, error);
        if (error) throw std::runtime_error("cannot write '" + path.string() + "'");
    }
}

} // namespace iprop::tools
