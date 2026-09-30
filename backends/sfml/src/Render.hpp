#pragma once

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <algorithm>
#include <stdexcept>

namespace iprop::sfml_host {

inline void fitSprite(sf::Sprite& sprite, sf::Vector2u viewport) {
    const auto size = sprite.getLocalBounds().size;
    if (size.x <= 0.f || size.y <= 0.f) {
        throw std::runtime_error("cannot fit an empty sprite");
    }
    const float scale = std::min(viewport.x / size.x, viewport.y / size.y);
    sprite.setScale({scale, scale});
    sprite.setPosition({(viewport.x - scale * size.x) / 2.f,
                        (viewport.y - scale * size.y) / 2.f});
}

inline void uploadTexture(sf::Texture& texture, sf::Sprite& sprite,
                          sf::Vector2u size, const std::uint8_t* pixels) {
    if (texture.getSize() != size && !texture.resize(size)) {
        throw std::runtime_error("texture resize failed");
    }
    texture.update(pixels);
    // A sprite created with an empty texture retains a 0x0 rectangle until
    // reset explicitly. Also reset it when switching to a different size.
    sprite.setTexture(texture, true);
}

} // namespace iprop::sfml_host
