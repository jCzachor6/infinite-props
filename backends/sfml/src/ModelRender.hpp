#pragma once
#include "ModelHost.hpp"
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <filesystem>

namespace iprop::sfml_host {
inline void uploadModelTexture(sf::Texture& texture, const ModelTexture& source, bool crisp) {
    if (!source.data || source.data->rgba.empty()) throw std::runtime_error("Model texture pixels are missing");
    sf::Image image({source.width, source.height}, source.data->rgba.data());
    if (!texture.loadFromImage(image)) throw std::runtime_error("Cannot upload model texture");
    texture.setSmooth(!crisp);
    texture.setRepeated(source.repeats);
}

inline void exportModelTextures(const GeneratedModel& model, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for (const auto& t : model.textures) {
        const auto hex = t.keyHex();
        auto save = [&](std::string suffix, const std::vector<std::uint8_t>& pixels) {
            if (pixels.empty()) throw std::runtime_error("Missing model texture");
            sf::Image image({t.width, t.height}, pixels.data());
            if (!image.saveToFile(directory / (hex + suffix + ".png"))) throw std::runtime_error("Cannot export model texture");
        };
        save("", t.data->rgba);
        for (const auto& a : t.data->images) save("-" + std::to_string(a.info.kind), a.pixels);
    }
}
} // namespace iprop::sfml_host
