// The SFML-path conformance (task 7.2): the golden corpus rendered
// through the C API and carried by SFML's sf::Image, compared pixel-
// exactly against the committed golden PNGs — headless, no window.
// One entry additionally round-trips through saveToFile/loadFromFile to
// exercise the PNG export path.
//
// Usage: sfml_conformance <iprop-dir> <png-dir>

#include "Host.hpp"
#include "Render.hpp"

#include <SFML/Graphics.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            std::fprintf(stderr, __VA_ARGS__);                             \
            std::fprintf(stderr, "\n");                                    \
        }                                                                  \
    } while (0)

void checkImage(const iprop_image& image, const char* label,
                const char* pngPath) {
    sf::Image golden;
    CHECK(golden.loadFromFile(pngPath), "cannot load the golden %s",
          pngPath);
    if (golden.getSize().x == 0) {
        return;
    }
    CHECK(image.width == golden.getSize().x &&
              image.height == golden.getSize().y,
          "%s: size %ux%u != golden %ux%u", label, image.width, image.height,
          golden.getSize().x, golden.getSize().y);
    if (image.width != golden.getSize().x ||
        image.height != golden.getSize().y) {
        return;
    }
    const std::uint8_t* actual = image.pixels;
    const std::uint8_t* expected = golden.getPixelsPtr();
    const std::size_t count =
        static_cast<std::size_t>(image.width) * image.height * 4;
    for (std::size_t i = 0; i < count; ++i) {
        if (actual[i] != expected[i]) {
            const std::size_t pixel = i / 4;
            CHECK(0, "%s: pixel (%u,%u) channel %u: 0x%02x != 0x%02x",
                  label, static_cast<unsigned>(pixel % image.width),
                  static_cast<unsigned>(pixel / image.width),
                  static_cast<unsigned>(i % 4), actual[i], expected[i]);
            break;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: sfml_conformance <iprop-dir> <png-dir>\n");
        return 2;
    }
    const std::string programDir = argv[1];
    const std::string pngDir = argv[2];

    struct Case {
        const char* program;
        const char* png;
        std::uint32_t seed;
    };
    static const Case cases[] = {
        {"portrait.iprop", "portrait-seed0.png", 0},
        {"portrait.iprop", "portrait-seed1.png", 1},
        {"portrait.iprop", "portrait-seed2.png", 2},
        {"portrait.iprop", "portrait-seed7.png", 7},
        {"portrait.iprop", "portrait-seed26.png", 26},
        {"shapes.iprop", "shapes.png", 0},
        {"noise.iprop", "noise.png", 0},
        {"parts.iprop", "parts.png", 0},
    };

    for (const Case& entry : cases) {
        const std::string programPath =
            programDir + "/" + entry.program;
        const auto bytes = iprop::sfml_host::readFile(programPath);
        iprop_program* program = iprop::sfml_host::loadProgram(bytes);
        iprop_result* result =
            iprop::sfml_host::generate(program, entry.seed, 0, 0);
        iprop_image image;
        iprop_result_image(result, &image);

        // The host carries the pixels through sf::Image on the way to
        // the texture; the conformance does the same copy here.
        sf::Image sfmlImage;
        sfmlImage.resize({image.width, image.height}, image.pixels);
        CHECK(std::memcmp(sfmlImage.getPixelsPtr(), image.pixels,
                          static_cast<std::size_t>(image.width) *
                              image.height * 4) == 0,
              "%s: the sf::Image copy must be byte-identical",
              entry.program);

        checkImage(image, entry.program,
                   (pngDir + "/" + entry.png).c_str());

        // The PNG export round-trip on the shapes entry.
        if (std::strcmp(entry.program, "shapes.iprop") == 0) {
            const std::string tmpPath = "sfml-roundtrip.png";
            CHECK(sfmlImage.saveToFile(tmpPath), "the PNG export failed");
            sf::Image reloaded;
            CHECK(reloaded.loadFromFile(tmpPath), "the PNG reload failed");
            if (reloaded.getSize().x == image.width &&
                reloaded.getSize().y == image.height) {
                CHECK(std::memcmp(reloaded.getPixelsPtr(), image.pixels,
                                  static_cast<std::size_t>(image.width) *
                                      image.height * 4) == 0,
                      "the PNG round-trip must be lossless");
            }
            std::remove(tmpPath.c_str());
        }

        iprop_result_free(result);
        iprop_program_free(program);
    }

    // Current compiler source goes through the same GPU upload/draw path
    // as the host, for every variant/archetype combination.
    {
        iprop::sfml_host::Program program(
            iprop::sfml_host::loadProgram(iprop::sfml_host::readFile(programDir + "/language.iprop")),
            &iprop_program_free);
        sf::Texture texture;
        sf::Sprite sprite(texture);
        sf::RenderTexture target({8, 8});
        const sf::Color colors[] = {{0x11,0x22,0x33}, {0x44,0x55,0x66}, {0x77,0x88,0x99}};
        for (std::uint32_t v = 0; v < 3; ++v) {
            for (std::uint32_t a = 0; a < 2; ++a) {
                iprop::sfml_host::Result result(
                    iprop::sfml_host::generate(program.get(), 26, v, a), &iprop_result_free);
                iprop_image image{};
                iprop_result_image(result.get(), &image);
                iprop::sfml_host::uploadTexture(texture, sprite, {image.width, image.height}, image.pixels);
                target.clear(sf::Color::Transparent);
                target.draw(sprite);
                target.display();
                const sf::Image rendered = target.getTexture().copyToImage();
                CHECK(rendered.getPixel({0, 0}) == sf::Color(0x33,0x55,0xaa),
                      "language default input pixel");
                CHECK(rendered.getPixel({a + 1, 1}) == colors[v], "language rendered palette pixel");
                CHECK(image.output_count == 4 && image.outputs[0].values[0] == static_cast<float>(v + 1) &&
                      image.outputs[0].values[1] == static_cast<float>(a) + .25f,
                      "language typed helper output");
            }
        }
        iprop::sfml_host::Program plant(
            iprop::sfml_host::loadProgram(iprop::sfml_host::readFile(programDir + "/plant.iprop")),
            &iprop_program_free);
        iprop::sfml_host::Result result(iprop::sfml_host::generate(plant.get(), 26, 0, 0), &iprop_result_free);
        iprop_image image{};
        iprop_result_image(result.get(), &image);
        iprop::sfml_host::uploadTexture(texture, sprite, {image.width, image.height}, image.pixels);
        CHECK(sprite.getLocalBounds().size == sf::Vector2f(64, 64), "compiled plant texture rect");
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("sfml conformance: PASS (%zu corpus entries + the export "
                "round-trip)\n",
                sizeof(cases) / sizeof(cases[0]));
    return 0;
}
