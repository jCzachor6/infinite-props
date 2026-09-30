// The SFML host (tasks 7.1/7.3): a native window over the C API. Loads an
// .iprop, draws the generated pixels, cycles seeds/variants with the arrow
// keys, toggles a variant × seed atlas with A and exports the current
// image as a PNG with S. Escape closes the window.
//
// One-shot modes (no window) make the host's paths scriptable and
// verifiable against the reference generator:
//
//   iprop_sfml [file.iprop] [--seed N] [--variant V]
//              [--export out.png] [--atlas out.png] [--screenshot out.png]

#include "Host.hpp"
#include "Render.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWindowSize = 640;
constexpr std::uint32_t kAtlasSide = 4; // variants × seeds per atlas

struct RenderedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

RenderedImage generateSingle(iprop_program* program, std::uint32_t seed,
                             std::uint32_t variant) {
    iprop_result* result = iprop::sfml_host::generate(program, seed, variant, 0);
    iprop_image image;
    iprop_result_image(result, &image);
    RenderedImage out;
    out.width = image.width;
    out.height = image.height;
    out.pixels.assign(image.pixels,
                      image.pixels + static_cast<std::size_t>(image.width) *
                                         image.height * 4);
    iprop_result_free(result);
    return out;
}

// The variant × seed grid: rows are variants (wrapped at the meta count),
// columns are consecutive seeds.
RenderedImage generateAtlas(iprop_program* program, const iprop_meta& meta,
                            std::uint32_t seed, std::uint32_t variant) {
    const RenderedImage first = generateSingle(program, seed, variant);
    const std::uint32_t tileWidth = first.width;
    const std::uint32_t tileHeight = first.height;
    RenderedImage atlas;
    atlas.width = tileWidth * kAtlasSide;
    atlas.height = tileHeight * kAtlasSide;
    atlas.pixels.resize(static_cast<std::size_t>(atlas.width) * atlas.height *
                        4);
    for (std::uint32_t row = 0; row < kAtlasSide; ++row) {
        const std::uint32_t tileVariant =
            (variant + row) % (meta.variants != 0 ? meta.variants : 1);
        for (std::uint32_t col = 0; col < kAtlasSide; ++col) {
            const RenderedImage tile =
                generateSingle(program, seed + col, tileVariant);
            for (std::uint32_t y = 0; y < tile.height; ++y) {
                const std::uint8_t* src =
                    tile.pixels.data() + (static_cast<std::size_t>(y) *
                                          tile.width * 4);
                std::uint8_t* dst =
                    atlas.pixels.data() +
                    ((static_cast<std::size_t>(row) * tileHeight + y) *
                         kAtlasSide * tileWidth +
                     col * tileWidth) *
                        4;
                std::copy(src, src + tile.width * 4, dst);
            }
        }
    }
    return atlas;
}

bool savePng(const RenderedImage& image, const std::string& path) {
    sf::Image sfImage;
    sfImage.resize({image.width, image.height}, image.pixels.data());
    return sfImage.saveToFile(path);
}

std::string nextArg(int& index, int argc, char** argv, const char* option) {
    if (index + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", option);
        std::exit(2);
    }
    return argv[++index];
}

} // namespace

int main(int argc, char** argv) {
    std::string path = "examples/plant.iprop";
    std::uint32_t seed = 0;
    std::uint32_t variant = 0;
    std::string exportPath;
    std::string atlasPath;
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed") {
            seed = static_cast<std::uint32_t>(
                std::stoul(nextArg(i, argc, argv, "--seed")));
        } else if (arg == "--variant") {
            variant = static_cast<std::uint32_t>(
                std::stoul(nextArg(i, argc, argv, "--variant")));
        } else if (arg == "--export") {
            exportPath = nextArg(i, argc, argv, "--export");
        } else if (arg == "--atlas") {
            atlasPath = nextArg(i, argc, argv, "--atlas");
        } else if (arg == "--screenshot") {
            screenshotPath = nextArg(i, argc, argv, "--screenshot");
        } else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return 2;
        } else {
            path = arg;
        }
    }

    const auto bytes = iprop::sfml_host::readFile(path);
    iprop_program* program = iprop::sfml_host::loadProgram(bytes);
    iprop_meta meta;
    iprop_program_meta(program, &meta);

    // The one-shot modes — the same code paths the interactive keys use,
    // exercised without a window.
    if (!exportPath.empty()) {
        const RenderedImage image = generateSingle(program, seed, variant);
        if (!savePng(image, exportPath)) {
            std::fprintf(stderr, "export to %s failed\n", exportPath.c_str());
            iprop_program_free(program);
            return 1;
        }
        std::printf("exported %s\n", exportPath.c_str());
        iprop_program_free(program);
        return 0;
    }
    if (!atlasPath.empty()) {
        const RenderedImage atlas =
            generateAtlas(program, meta, seed, variant);
        if (!savePng(atlas, atlasPath)) {
            std::fprintf(stderr, "atlas export to %s failed\n",
                         atlasPath.c_str());
            iprop_program_free(program);
            return 1;
        }
        std::printf("atlas exported %s\n", atlasPath.c_str());
        iprop_program_free(program);
        return 0;
    }

    // The windowed path (the screenshot mode renders one frame and reads
    // the texture back; the default is the interactive loop).
    sf::RenderWindow window(sf::VideoMode({kWindowSize, kWindowSize}),
                            "iprop");
    sf::Texture texture;
    texture.setSmooth(false);
    sf::Texture atlasTexture;
    atlasTexture.setSmooth(false);
    sf::Sprite sprite(texture);
    sf::Sprite atlasSprite(atlasTexture);

    bool atlas = false;

    const auto uploadSingle = [&](const RenderedImage& image) {
        iprop::sfml_host::uploadTexture(texture, sprite,
            {image.width, image.height}, image.pixels.data());
        const float scale = static_cast<float>(kWindowSize) /
                            static_cast<float>(image.width > image.height
                                                   ? image.width
                                                   : image.height);
        sprite.setScale({scale, scale});
        sprite.setPosition(
            {(static_cast<float>(kWindowSize) - scale * image.width) / 2.f,
             (static_cast<float>(kWindowSize) - scale * image.height) / 2.f});
    };

    const auto uploadAtlas = [&](const RenderedImage& atlasImage) {
        iprop::sfml_host::uploadTexture(atlasTexture, atlasSprite,
            {atlasImage.width, atlasImage.height}, atlasImage.pixels.data());
        const float scale = static_cast<float>(kWindowSize) /
                            static_cast<float>(atlasImage.width);
        atlasSprite.setScale({scale, scale});
        atlasSprite.setPosition({0.f, 0.f});
    };

    if (!screenshotPath.empty()) {
        uploadSingle(generateSingle(program, seed, variant));
        window.clear(sf::Color(24, 24, 28));
        window.draw(sprite);
        window.display();
        sf::Texture capture;
        if (!capture.resize(window.getSize())) {
            throw std::runtime_error("screenshot allocation failed");
        }
        capture.update(window);
        const sf::Image readback = capture.copyToImage();
        if (!readback.saveToFile(screenshotPath)) {
            std::fprintf(stderr, "screenshot to %s failed\n",
                         screenshotPath.c_str());
            iprop_program_free(program);
            return 1;
        }
        std::printf("screenshot saved %s\n", screenshotPath.c_str());
        iprop_program_free(program);
        return 0;
    }

    // The interactive loop.
    const auto refresh = [&]() {
        if (atlas) {
            uploadAtlas(generateAtlas(program, meta, seed, variant));
        } else {
            uploadSingle(generateSingle(program, seed, variant));
        }
        window.setTitle("iprop - " + path + " - seed " +
                        std::to_string(seed) + " variant " +
                        std::to_string(variant) +
                        (atlas ? " [atlas]" : ""));
    };
    refresh();

    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto* key =
                           event->getIf<sf::Event::KeyPressed>()) {
                switch (key->code) {
                case sf::Keyboard::Key::Escape:
                    window.close();
                    break;
                case sf::Keyboard::Key::Left:
                    --seed;
                    refresh();
                    break;
                case sf::Keyboard::Key::Right:
                    ++seed;
                    refresh();
                    break;
                case sf::Keyboard::Key::Up:
                    variant = meta.variants == 0
                                  ? 0
                                  : (variant + meta.variants - 1) %
                                        meta.variants;
                    refresh();
                    break;
                case sf::Keyboard::Key::Down:
                    variant = meta.variants == 0
                                  ? 0
                                  : (variant + 1) % meta.variants;
                    refresh();
                    break;
                case sf::Keyboard::Key::A:
                    atlas = !atlas;
                    refresh();
                    break;
                case sf::Keyboard::Key::S: {
                    const RenderedImage image =
                        generateSingle(program, seed, variant);
                    const std::string outPath =
                        "iprop-export-seed" + std::to_string(seed) +
                        "-variant" + std::to_string(variant) + ".png";
                    if (savePng(image, outPath)) {
                        std::printf("exported %s\n", outPath.c_str());
                    } else {
                        std::fprintf(stderr, "export to %s failed\n",
                                     outPath.c_str());
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        window.clear(sf::Color(24, 24, 28));
        window.draw(atlas ? atlasSprite : sprite);
        window.display();
    }

    iprop_program_free(program);
    return 0;
}
