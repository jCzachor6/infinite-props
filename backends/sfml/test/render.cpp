#include "Render.hpp"

#include <cstdio>
#include <vector>

int main() {
    sf::Texture texture;
    sf::Texture atlasTexture;
    sf::Sprite sprite(texture);
    sf::Sprite atlasSprite(atlasTexture);
    sf::RenderTexture target({32, 32});
    for (const sf::Vector2u size : {sf::Vector2u{8, 4}, {4, 8}, {16, 16}, {4, 32}, {32, 4}}) {
        const std::vector<std::uint8_t> pixels(size.x * size.y * 4, 255);
        for (const bool atlas : {false, true}) {
            auto& tex = atlas ? atlasTexture : texture;
            auto& spr = atlas ? atlasSprite : sprite;
            spr.setScale({1.f, 1.f});
            spr.setPosition({0.f, 0.f});
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
                (size.x < 32 && size.y < 32 && image.getPixel({size.x, size.y}) != sf::Color::Black)) {
                std::fprintf(stderr, "sprite did not render the uploaded pixels\n");
                return 1;
            }
            iprop::sfml_host::fitSprite(spr, {32, 32});
            const auto bounds = spr.getGlobalBounds();
            if (bounds.position.x < 0 || bounds.position.y < 0 ||
                bounds.position.x + bounds.size.x > 32.f ||
                bounds.position.y + bounds.size.y > 32.f ||
                (bounds.size.x != 32.f && bounds.size.y != 32.f)) {
                std::fprintf(stderr, "sprite is cropped instead of fitted\n");
                return 1;
            }
            target.clear(sf::Color::Black);
            target.draw(spr);
            target.display();
            const auto fitted = target.getTexture().copyToImage();
            const sf::Vector2u first{static_cast<unsigned>(bounds.position.x),
                                     static_cast<unsigned>(bounds.position.y)};
            const sf::Vector2u last{first.x + static_cast<unsigned>(bounds.size.x) - 1,
                                    first.y + static_cast<unsigned>(bounds.size.y) - 1};
            if (fitted.getPixel(first) != sf::Color::White || fitted.getPixel(last) != sf::Color::White) {
                std::fprintf(stderr, "fitted sprite corners are missing\n");
                return 1;
            }
        }
    }
    std::puts("SFML sprite rendering: PASS");
}
