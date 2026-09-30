#pragma once

#include <SFML/Graphics.hpp>

#include <cstdint>
#include <stdexcept>

namespace iprop::sfml_host {

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
