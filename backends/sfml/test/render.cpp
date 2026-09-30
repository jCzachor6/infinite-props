#include "Render.hpp"

#include <cstdio>
#include <vector>

int main() {
    sf::Texture texture;
    sf::Texture atlasTexture;
    sf::Sprite sprite(texture);
    sf::Sprite atlasSprite(atlasTexture);
    sf::RenderTexture target({32, 32});
    for (const sf::Vector2u size : {sf::Vector2u{8, 4}, {4, 8}, {16, 16}}) {
        const std::vector<std::uint8_t> pixels(size.x * size.y * 4, 255);
        for (const bool atlas : {false, true}) {
            auto& tex = atlas ? atlasTexture : texture;
            auto& spr = atlas ? atlasSprite : sprite;
            iprop::sfml_host::uploadTexture(tex, spr, size, pixels.data());
            if (spr.getLocalBounds().size != sf::Vector2f(size)) {
                std::fprintf(stderr, "sprite bounds do not match uploaded texture\n");
                return 1;
            }
            target.clear(sf::Color::Black);
            target.draw(spr);
            target.display();
            const auto image = target.getTexture().copyToImage();
            if (image.getPixel({0, 0}) != sf::Color::White ||
                image.getPixel({size.x - 1, size.y - 1}) != sf::Color::White ||
                image.getPixel({size.x, size.y}) != sf::Color::Black) {
                std::fprintf(stderr, "sprite did not render the uploaded pixels\n");
                return 1;
            }
        }
    }
    std::puts("SFML sprite rendering: PASS");
}
