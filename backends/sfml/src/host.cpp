// The SFML host (tasks 7.1/7.3): a native window over the C API. Loads an
// .iprop, draws the generated pixels, cycles seeds with the arrow
// keys, toggles a seed atlas with A and exports the current
// image as a PNG with S. Escape closes the window.
//
// One-shot modes (no window) make the host's paths scriptable and
// verifiable against the reference generator:
//
//   iprop_sfml [file.iprop] [--seed N] [--set input=value]...
//              [--export out.png] [--atlas out.png] [--screenshot out.png]
//
// --set overrides one declared input: #RRGGBB[AA] colours, decimal f32 and
// u32 (a leading minus sets an i32 input).

#include "Host.hpp"
#include "Render.hpp"

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kWindowSize = 640;
constexpr std::uint32_t kAtlasSide = 4; // consecutive seeds per atlas

struct RenderedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

RenderedImage generateSingle(const iprop_program* program, const iprop_parameters* parameters,
                             std::uint32_t seed) {
    const iprop::sfml_host::Result result(
        iprop::sfml_host::generate(program, seed, parameters), iprop_result_free);
    const auto image = iprop::sfml_host::queryImage(result.get());
    RenderedImage out;
    out.width = image.width;
    out.height = image.height;
    if (image.byte_count != 0) {
        out.pixels.assign(image.pixels, image.pixels + static_cast<std::size_t>(image.byte_count));
    }
    return out;
}

// A 4 by 4 grid of consecutive seeds with fixed input values.
RenderedImage generateAtlas(const iprop_program* program, const iprop_parameters* parameters,
                            std::uint32_t seed) {
    const RenderedImage first = generateSingle(program, parameters, seed);
    const std::uint32_t tileWidth = first.width;
    const std::uint32_t tileHeight = first.height;
    RenderedImage atlas;
    atlas.width = tileWidth * kAtlasSide;
    atlas.height = tileHeight * kAtlasSide;
    atlas.pixels.resize(static_cast<std::size_t>(atlas.width) * atlas.height *
                        4);
    for (std::uint32_t row = 0; row < kAtlasSide; ++row) {
        for (std::uint32_t col = 0; col < kAtlasSide; ++col) {
            const RenderedImage tile =
                generateSingle(program, parameters, seed + row * kAtlasSide + col);
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
        throw std::runtime_error(std::string(option) + " requires a value");
    }
    return argv[++index];
}

std::uint32_t parseU32(const std::string& token, const char* option) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
        throw std::runtime_error(std::string(option) + " requires a decimal u32: " + token);
    }
    return value;
}

} // namespace

int run(int argc, char** argv) {
    std::string path = "examples/plant.iprop";
    std::uint32_t seed = 0;
    std::string exportPath;
    std::string atlasPath;
    std::string screenshotPath;
    std::vector<std::pair<std::string, std::string>> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed") {
            seed = parseU32(nextArg(i, argc, argv, "--seed"), "--seed");
        } else if (arg == "--export") {
            exportPath = nextArg(i, argc, argv, "--export");
        } else if (arg == "--atlas") {
            atlasPath = nextArg(i, argc, argv, "--atlas");
        } else if (arg == "--screenshot") {
            screenshotPath = nextArg(i, argc, argv, "--screenshot");
        } else if (arg == "--set") {
            const std::string assignment = nextArg(i, argc, argv, "--set");
            const auto equals = assignment.find('=');
            if (equals == std::string::npos || equals == 0) {
                throw std::runtime_error("--set requires input=value: " + assignment);
            }
            inputs.emplace_back(assignment.substr(0, equals), assignment.substr(equals + 1));
        } else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return 2;
        } else {
            path = arg;
        }
    }

    const auto bytes = iprop::sfml_host::readFile(path);
    const iprop::sfml_host::Program program(
        iprop::sfml_host::loadProgram(bytes), iprop_program_free);
    const iprop::sfml_host::Parameters parameters = iprop::sfml_host::createParameters(program.get());
    for (const auto& [name, value] : inputs) {
        iprop::sfml_host::setParameter(parameters.get(), name, value);
    }

    // The one-shot modes — the same code paths the interactive keys use,
    // exercised without a window.
    if (!exportPath.empty()) {
        const RenderedImage image = generateSingle(program.get(), parameters.get(), seed);
        if (!savePng(image, exportPath)) {
            std::fprintf(stderr, "export to %s failed\n", exportPath.c_str());
            return 1;
        }
        std::printf("exported %s\n", exportPath.c_str());
        return 0;
    }
    if (!atlasPath.empty()) {
        const RenderedImage atlas =
            generateAtlas(program.get(), parameters.get(), seed);
        if (!savePng(atlas, atlasPath)) {
            std::fprintf(stderr, "atlas export to %s failed\n",
                         atlasPath.c_str());
            return 1;
        }
        std::printf("atlas exported %s\n", atlasPath.c_str());
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
        iprop::sfml_host::fitSprite(sprite, window.getSize());
    };

    const auto uploadAtlas = [&](const RenderedImage& atlasImage) {
        iprop::sfml_host::uploadTexture(atlasTexture, atlasSprite,
            {atlasImage.width, atlasImage.height}, atlasImage.pixels.data());
        iprop::sfml_host::fitSprite(atlasSprite, window.getSize());
    };

    if (!screenshotPath.empty()) {
        uploadSingle(generateSingle(program.get(), parameters.get(), seed));
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
            return 1;
        }
        std::printf("screenshot saved %s\n", screenshotPath.c_str());
        return 0;
    }

    // The interactive loop.
    const auto refresh = [&]() {
        if (atlas) {
            uploadAtlas(generateAtlas(program.get(), parameters.get(), seed));
        } else {
            uploadSingle(generateSingle(program.get(), parameters.get(), seed));
        }
        window.setTitle("iprop - " + path + " - seed " +
                        std::to_string(seed) +
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
                case sf::Keyboard::Key::A:
                    atlas = !atlas;
                    refresh();
                    break;
                case sf::Keyboard::Key::S: {
                    const RenderedImage image =
                        generateSingle(program.get(), parameters.get(), seed);
                    const std::string outPath =
                        "iprop-export-seed" + std::to_string(seed) +
                        ".png";
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

    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "iprop_sfml: %s\n", error.what());
        return 1;
    }
}
