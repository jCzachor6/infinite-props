// Material maps (spec material maps v1.2) through the SFML host glue against
// the CLI-rendered shared fixture, including a linear texture upload of a
// scalar map.
//
// Usage: sfml_material_maps <binding-fixtures-dir>

#include "Host.hpp"
#include "Render.hpp"

#include <cstring>
#include <iostream>

namespace {
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
}

int main(int argc, char** argv) {
    using namespace iprop::sfml_host;
    try {
        require(argc == 2, "usage: sfml_material_maps binding-fixtures");
        const std::string fixtures = std::string(argv[1]) + "/";
        const auto load = [&](const std::string& name) { return readFile(fixtures + name); };
        const iprop_aux_image_kind kinds[] = {IPROP_AUX_IMAGE_NORMAL, IPROP_AUX_IMAGE_ROUGHNESS, IPROP_AUX_IMAGE_METALLIC,
                                              IPROP_AUX_IMAGE_AO, IPROP_AUX_IMAGE_EMISSION};
        const char* names[] = {"relief", "gloss", "metal", "shade", "glow"};
        const char* files[] = {"normal", "roughness", "metallic", "ao", "emission"};
        Program program(loadProgram(load("binding-maps.iprop")), iprop_program_free);
        // Declared out of order in the source; listed by kind.
        const auto declared = declaredImages(program.get());
        require(declared.size() == 5, "five declared images");
        for (std::uint32_t i = 0; i < 5; ++i)
            require(declared[i].index == i && declared[i].kind == kinds[i] && declared[i].name == names[i],
                    "declared image " + std::to_string(i));
        Result result(generate(program.get(), 5), iprop_result_free);
        const auto copy = copyResult(result.get());
        require(copy.pixels == load("binding-maps.rgba"), "native RGBA oracle");
        require(copy.images.size() == 5, "five published images");
        for (std::uint32_t i = 0; i < 5; ++i) {
            const auto& image = copy.images[i];
            require(image.info.image_index == i && image.info.kind == kinds[i] &&
                    image.info.source == IPROP_AUX_IMAGE_AUTHORED && image.name == names[i], std::string("tags of ") + files[i]);
            require(image.info.width == 8 && image.info.height == 4 && image.info.row_stride == 32 &&
                    image.info.alpha_mode == IPROP_IMAGE_ALPHA_OPAQUE, std::string("layout of ") + files[i]);
            require(image.pixels == load(std::string("binding-maps.") + files[i] + ".rgba"), std::string("native oracle of ") + files[i]);
            require(copy.image(kinds[i]) == &image, std::string("lookup of ") + files[i]);
        }
        require(copy.normal() == &copy.images[0], "the normal map comes first");
        // A scalar map is linear data: its bytes survive a linear SFML
        // texture upload unchanged.
        const auto& roughness = copy.image(IPROP_AUX_IMAGE_ROUGHNESS)->pixels;
        sf::Texture texture; sf::Sprite sprite(texture);
        uploadTexture(texture, sprite, {8, 4}, roughness.data());
        require(!texture.isSrgb(), "a roughness map texture is linear");
        const auto stored = texture.copyToImage();
        require(std::memcmp(stored.getPixelsPtr(), roughness.data(), roughness.size()) == 0, "SFML texture upload");
        // A program without maps publishes none.
        Program plainProgram(loadProgram(load("bindings.iprop")), iprop_program_free);
        Result plain(generate(plainProgram.get(), 5), iprop_result_free);
        require(copyResult(plain.get()).image(IPROP_AUX_IMAGE_ROUGHNESS) == nullptr, "no material map");
        std::cout << "SFML material maps: PASS every map against the CLI in kind order, declarations and texture upload\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
