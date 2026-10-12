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
#include <utility>

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

void checkImage(const iprop_image_view_ex& image, const char* label,
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
    const auto count = static_cast<std::size_t>(image.byte_count);
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

std::vector<iprop::sfml_host::OutputSlot> checkOutputs(const iprop_result* result) {
    const auto slots = iprop::sfml_host::copyOutputs(result);
    iprop_image legacy{};
    iprop_result_image(result, &legacy); // Compatibility oracle, never the adapter read path.
    CHECK(slots.size() == legacy.output_count, "all legacy declarations must remain present");
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto& slot = slots[i];
        const auto& old = legacy.outputs[i];
        CHECK(slot.index == i && slot.owner == old.owner && slot.kind == static_cast<uint32_t>(old.kind) &&
              slot.isList == (old.is_list != 0), "slot %zu metadata", i);
        const auto count = static_cast<std::size_t>(old.count) * iprop::backend::outputComponents(slot.kind);
        CHECK(slot.values.size() == count && (count == 0 ||
              std::memcmp(slot.values.data(), old.values, count * sizeof(float)) == 0),
              "slot %zu typed float bits differ from legacy", i);
    }
    return slots;
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
        const iprop_program_info_ex meta = iprop::sfml_host::queryProgram(program);
        iprop_result* result =
            iprop::sfml_host::generate(program, entry.seed, 0, 0);
        const auto image = iprop::sfml_host::queryImage(result);
        checkOutputs(result);
        CHECK(image.width == meta.size_x && image.height == meta.size_y,
              "%s: queried metadata size differs from default generation", entry.program);

        // The host carries the pixels through sf::Image on the way to
        // the texture; the conformance does the same copy here.
        sf::Image sfmlImage;
        sfmlImage.resize({image.width, image.height}, image.pixels);
        CHECK(std::memcmp(sfmlImage.getPixelsPtr(), image.pixels,
                          static_cast<std::size_t>(image.byte_count)) == 0,
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
                                  static_cast<std::size_t>(image.byte_count)) == 0,
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
                const auto image = iprop::sfml_host::queryImage(result.get());
                const auto outputs = checkOutputs(result.get());
                iprop::sfml_host::uploadTexture(texture, sprite, {image.width, image.height}, image.pixels);
                target.clear(sf::Color::Transparent);
                target.draw(sprite);
                target.display();
                const sf::Image rendered = target.getTexture().copyToImage();
                CHECK(rendered.getPixel({0, 0}) == sf::Color(0x33,0x55,0xaa),
                      "language default input pixel");
                CHECK(rendered.getPixel({a + 1, 1}) == colors[v], "language rendered palette pixel");
                CHECK(outputs.size() == 4 && outputs[0].values[0] == static_cast<float>(v + 1) &&
                      outputs[0].values[1] == static_cast<float>(a) + .25f,
                      "language typed helper output");
            }
        }
        iprop::sfml_host::Program plant(
            iprop::sfml_host::loadProgram(iprop::sfml_host::readFile(programDir + "/plant.iprop")),
            &iprop_program_free);
        iprop::sfml_host::Result result(iprop::sfml_host::generate(plant.get(), 26, 0, 0), &iprop_result_free);
        const auto image = iprop::sfml_host::queryImage(result.get());
        iprop::sfml_host::uploadTexture(texture, sprite, {image.width, image.height}, image.pixels);
        CHECK(sprite.getLocalBounds().size == sf::Vector2f(64, 64), "compiled plant texture rect");
    }

    // Independent stage-3 ordinary-range pins through the existing host layouts.
    {
        iprop::sfml_host::Program program(iprop::sfml_host::loadProgram(
            iprop::sfml_host::readFile(programDir + "/samplers.iprop")), &iprop_program_free);
        iprop::sfml_host::Result result(iprop::sfml_host::generate(program.get(),26,0,0), &iprop_result_free);
        const auto image=iprop::sfml_host::queryImage(result.get());const auto slots=checkOutputs(result.get());
        const std::uint32_t expected[]={0x40400000,0x42200000,0x416ef976,0x401a259f,
            0x41500000,0x41500000,0x46c16e00,0x45b8a800};
        CHECK(image.width==32 && image.height==24 && slots.size()==4,"sampler image metadata");
        for(std::size_t i=0;i<slots.size() && i<4;++i){
            CHECK(slots[i].values.size()==2 && slots[i].owner==0 && !slots[i].isList,"sampler output metadata");
            if(slots[i].values.size()==2){std::uint32_t bits[2];std::memcpy(bits,slots[i].values.data(),sizeof(bits));
                CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"sampler output bits");}
        }
        if(image.width==32 && image.height==24)for(unsigned y=0;y<24;++y)for(unsigned x=0;x<32;++x){
            const std::uint8_t* actual=image.pixels+(y*32+x)*4;std::uint8_t color[4]={0,0,0,0};
            if(x>=2 && x<15 && ((y>=2 && y<6)||(y>=8 && y<12)||(y>=14 && y<18))){
                color[0]=134;color[1]=144;color[2]=156;color[3]=255;}
            CHECK(std::memcmp(actual,color,4)==0,"sampler pixel %u,%u",x,y);
        }
    }

    // Independent stage-2 polygon/RNG pins through the existing host layouts.
    {
        iprop::sfml_host::Program program(iprop::sfml_host::loadProgram(
            iprop::sfml_host::readFile(programDir + "/collections.iprop")), &iprop_program_free);
        iprop::sfml_host::Result result(iprop::sfml_host::generate(program.get(),26,0,0), &iprop_result_free);
        const auto image = iprop::sfml_host::queryImage(result.get());
        const auto slots = checkOutputs(result.get());
        const std::uint32_t expected[] = {0x40800000,0x41980000,0x41900000,0x40000000,
            0x41200000,0x41f00000,0x46da8a00,0x475aa600};
        const std::uint32_t rows[] = {0,0,0x3c000,0x7ff80,0x7fff8,0xffff8,0xffff0,0x1ffff0,
            0x1ffff0,0x3fffe0,0x7fffe0,0x7fffe0,0xffffc0,0xffffc0,0x1ffffc0,0x7fff80,
            0xfff80,0xff80,0x1f00,0x300,0,0,0,0};
        CHECK(image.width==32 && image.height==24 && slots.size()==4,"collection image metadata");
        for(std::size_t i=0;i<slots.size() && i<4;++i) {
            CHECK(slots[i].values.size()==2 && slots[i].owner==0 && !slots[i].isList,"collection output metadata");
            if(slots[i].values.size()==2) {
                std::uint32_t bits[2];std::memcpy(bits,slots[i].values.data(),sizeof(bits));
                CHECK(bits[0]==expected[i*2] && bits[1]==expected[i*2+1],"collection output bits");
            }
        }
        if(image.width==32 && image.height==24) for(unsigned y=0;y<24;++y) for(unsigned x=0;x<32;++x) {
            const std::uint8_t* actual=image.pixels+(y*32+x)*4;
            std::uint8_t color[4]={0,0,0,0};
            if((rows[y]>>x)&1) {color[0]=146;color[1]=156;color[2]=168;color[3]=255;}
            if(x==3 && y==4) {color[0]=52;color[1]=58;color[2]=66;color[3]=255;}
            CHECK(std::memcmp(actual,color,4)==0,"collection polygon pixel %u,%u",x,y);
        }
    }

    // Existing root/helper/part fixture: all D4 transforms, owner 2 has no
    // declarations, unset singulars and empty lists remain in their positions.
    {
        iprop::sfml_host::Program program(iprop::sfml_host::loadProgram(
            iprop::sfml_host::readFile(programDir + "/output-values.iprop")), &iprop_program_free);
        const float points[8][2] = {{1,2},{2,7},{7,6},{6,1},{7,2},{1,6},{2,1},{6,7}};
        const unsigned transforms[] = {0,1,2,3,4,7,5,6};
        for (uint32_t v = 0; v < 8; ++v) {
            iprop::sfml_host::Result result(iprop::sfml_host::generate(program.get(), 26, v, 0), &iprop_result_free);
            const auto slots = checkOutputs(result.get());
            CHECK(slots.size() == 18, "all root and part output slots");
            for (std::size_t i = 0; i < slots.size(); ++i) {
                CHECK(slots[i].owner == (i < 6 ? 0u : i < 12 ? 1u : 3u), "owner gap at slot %zu", i);
                CHECK(slots[i].name == (i % 6 == 0 ? "anchor" : i % 6 == 1 ? "collider" :
                    i % 6 == 2 ? "zone" : i % 6 == 3 ? "anchors" : i % 6 == 4 ? "colliders" : "zones"),
                    "exact output name at %zu", i);
            }
            CHECK(slots[0].values.empty() == (v == 0) && slots[9].values.empty() &&
                  slots[13].values.empty() && slots[12].isList && slots[12].values.size() == 4,
                  "unset singular, empty lists and populated part list");
            // Root coordinates: the D4 image plus the part's blit origin {3, 4}.
            CHECK(slots[6].values.size() == 2 && slots[6].values[0] == points[transforms[v]][0] + 3.f &&
                  slots[6].values[1] == points[transforms[v]][1] + 4.f,
                  "D4 output must already be transformed exactly once");
            if (v == 7) {
                const auto before = iprop::sfml_host::queryImage(result.get());
                iprop::sfml_host::Result later(iprop::sfml_host::generate(program.get(), 7, 0, 0), &iprop_result_free);
                program.reset();
                const auto after = iprop::sfml_host::queryImage(result.get());
                CHECK(before.pixels == after.pixels && before.byte_count == after.byte_count,
                      "borrowed image survives later generation and program_free");
                checkOutputs(result.get());
                result.reset();
                CHECK(slots[12].values == std::vector<float>({9,10,11,12}), "copied geometry survives result_free");
            }
        }
    }

    // Binary fixture supplies values source syntax cannot express: NaN payloads,
    // infinities, subnormals, signed zero, an empty name and UTF-8 with embedded NUL.
    {
        const uint32_t bits[] = {0x80000000,1,0x7fc12345,0xff800000,0x7f800000,
                                 0xffc12345,0x80000001,0x3f800000,0x80000000};
        const char name[] = "p\xc5\x82\0suffix";
        for (const char* fixture : {"backend-raw-values.iprop", "backend-empty-values.iprop"}) {
            iprop::sfml_host::Program program(iprop::sfml_host::loadProgram(
                iprop::sfml_host::readFile(programDir + "/" + fixture)), &iprop_program_free);
            iprop::sfml_host::Result result(iprop::sfml_host::generate(program.get(), 0, 0, 0), &iprop_result_free);
            program.reset();
            auto slots = checkOutputs(result.get());
            CHECK(slots.size() == 6 && slots[0].name == std::string(name, sizeof(name) - 1) &&
                  slots[1].name.empty() && slots[2].name == std::string("\0c\0\0", 4) && slots[3].owner == 2,
                  "byte-counted names and owner gap");
            std::size_t component = 0;
            for (std::size_t i = 0; i < slots.size(); ++i) {
                if (std::strcmp(fixture, "backend-raw-values.iprop") == 0 && i < 3) {
                    CHECK(std::memcmp(slots[i].values.data(), bits + component,
                          slots[i].values.size() * sizeof(float)) == 0, "raw bits for kind %zu", i);
                    component += slots[i].values.size();
                } else CHECK(slots[i].values.empty(), "unassigned raw fixture slot %zu", i);
            }
            const auto old_size = slots.size();
            try { slots = iprop::sfml_host::copyOutputs(nullptr); CHECK(false, "invalid result accepted"); }
            catch (const std::runtime_error&) { CHECK(slots.size() == old_size && slots[0].name.size() == sizeof(name)-1,
                                                     "failed copy must not replace published outputs"); }
            result.reset();
            CHECK(slots[0].name == std::string(name, sizeof(name)-1), "owned names survive result_free");
        }
    }

    // Inputs set by name through the checked parameter API render exactly as
    // the legacy named-uniform overrides; unknown names and bad text fail.
    {
        const iprop::sfml_host::Program program(iprop::sfml_host::loadProgram(
            iprop::sfml_host::readFile(programDir + "/uniforms.iprop")), &iprop_program_free);
        const auto parameters = iprop::sfml_host::createParameters(program.get());
        iprop::sfml_host::setParameter(parameters.get(), "base", "#ff000080");
        iprop::sfml_host::setParameter(parameters.get(), "fx", "5.5");
        iprop::sfml_host::setParameter(parameters.get(), "ux", "3");
        const iprop::sfml_host::Result checked(
            iprop::sfml_host::generate(program.get(), 0, 0, 0, parameters.get()), &iprop_result_free);
        iprop_uniform uniforms[3] = {};
        uniforms[0].name = "base"; uniforms[0].type = IPROP_UNIFORM_COLOR;
        uniforms[0].r = 255; uniforms[0].a = 128;
        uniforms[1].name = "fx"; uniforms[1].type = IPROP_UNIFORM_F32; uniforms[1].f = 5.5f;
        uniforms[2].name = "ux"; uniforms[2].type = IPROP_UNIFORM_U32; uniforms[2].u = 3;
        iprop_generate_options options{};
        options.uniforms = uniforms;
        options.uniform_count = 3;
        iprop_result* legacy = nullptr;
        char message[256] = {};
        CHECK(iprop_generate(program.get(), &options, &legacy, message, sizeof(message)) == IPROP_OK,
              "legacy uniforms: %s", message);
        const iprop::sfml_host::Result legacyResult(legacy, &iprop_result_free);
        if (legacy != nullptr) {
            const auto a = iprop::sfml_host::queryImage(checked.get());
            const auto b = iprop::sfml_host::queryImage(legacy);
            CHECK(a.byte_count == b.byte_count && std::memcmp(a.pixels, b.pixels, a.byte_count) == 0,
                  "parameters differ from the legacy uniforms");
        }
        for (const auto& [name, value] : {std::pair{"missing", "1"}, {"ux", "-"}, {"base", "#12"}, {"fx", "x"}}) {
            try {
                iprop::sfml_host::setParameter(parameters.get(), name, value);
                CHECK(false, "accepted %s=%s", name, value);
            } catch (const std::runtime_error&) {}
        }
    }

    {
        iprop::sfml_host::Program program(
            iprop::sfml_host::loadProgram(iprop::sfml_host::readFile(programDir + "/math-bounds.iprop")),
            &iprop_program_free);
        iprop::sfml_host::Result result(iprop::sfml_host::generate(program.get(), 26, 0, 0), &iprop_result_free);
        const auto image = iprop::sfml_host::queryImage(result.get());
        const auto slots = checkOutputs(result.get());
        const std::uint32_t expected[] = {0x3f800000, 0x3f800000, 0x3f19999a, 0x3f4ccccd,
                                        0, 0, 0x40000000, 0x40000000};
        CHECK(slots.size() == 4, "format 0.6 output slots");
        for (std::size_t i = 0; i < slots.size(); ++i) {
            CHECK(slots[i].values.size() == 2 && slots[i].owner == 0 && !slots[i].isList,
                  "format 0.6 output metadata");
            if (i < 4 && slots[i].values.size() == 2) {
                std::uint32_t bits[2];
                std::memcpy(bits, slots[i].values.data(), sizeof(bits));
                CHECK(bits[0] == expected[i * 2] && bits[1] == expected[i * 2 + 1],
                      "format 0.6 math output bits");
            }
        }
        sf::Image rendered;
        rendered.resize({image.width, image.height}, image.pixels);
        CHECK(rendered.getPixel({0, 0}) == sf::Color(0x20, 0xc0, 0x80), "format 0.6 math pixel");
    }

    {
        iprop::sfml_host::Program program(
            iprop::sfml_host::loadProgram(iprop::sfml_host::readFile(programDir + "/asset-constants.iprop")),
            &iprop_program_free);
        iprop::sfml_host::Result result(iprop::sfml_host::generate(program.get(), 26, 0, 0), &iprop_result_free);
        const auto image = iprop::sfml_host::queryImage(result.get());
        const auto slots = checkOutputs(result.get());
        // Preserved stage-1 constructor pins, including the final RNG state.
        const std::uint32_t expected[] = {0x3f800000, 0x3f800000, 0x3f19999a, 0x3f4ccccd,
            0, 0, 0x3f116e13, 0x3ee83a45, 0, 0x3efbda76, 0x4766fe00, 0x46f1ba00};
        CHECK(slots.size() == 6, "asset constant output slots");
        for (std::size_t i = 0; i < slots.size(); ++i) {
            CHECK(slots[i].values.size() == 2 && slots[i].owner == 0 && !slots[i].isList,
                  "asset constant output metadata");
            if (i < 6 && slots[i].values.size() == 2) {
                std::uint32_t bits[2];
                std::memcpy(bits, slots[i].values.data(), sizeof(bits));
                CHECK(bits[0] == expected[i * 2] && bits[1] == expected[i * 2 + 1],
                      "asset constant output bits");
            }
        }
        sf::Image rendered;
        rendered.resize({image.width, image.height}, image.pixels);
        CHECK(rendered.getSize() == sf::Vector2u(2, 2), "asset constant dimensions");
        CHECK(rendered.getPixel({0, 0}) == sf::Color(146,156,168), "asset constant base color");
        CHECK(rendered.getPixel({1, 0}) == sf::Color(139,149,161), "asset constant helper color");
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
