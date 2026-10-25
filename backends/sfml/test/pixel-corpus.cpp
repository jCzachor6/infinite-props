#include "Host.hpp"
#include "Render.hpp"
#include <bit>
#include <cstring>
#include <iostream>

namespace {
struct Reader {
    std::vector<std::uint8_t> data;
    std::size_t at = 0;
    std::uint32_t word() {
        if (at > data.size() || data.size() - at < 4) throw std::runtime_error("truncated corpus");
        const auto* b = data.data() + at; at += 4;
        return std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
    }
    std::vector<std::uint8_t> blob() {
        const auto size = word();
        if (size > data.size() - at) throw std::runtime_error("truncated blob");
        std::vector<std::uint8_t> value(data.begin() + at, data.begin() + at + size); at += size; return value;
    }
};
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    using namespace iprop::sfml_host;
    try {
        require(argc == 2, "usage: sfml_pixel_corpus pixel-corpus.bin");
        Reader r{readFile(argv[1])};
        require(r.data.size() >= 8 && std::memcmp(r.data.data(), "IPXCRP01", 8) == 0, "corpus version"); r.at = 8;
        std::vector<std::vector<std::uint8_t>> programs(r.word());
        for (auto& bytes : programs) bytes = r.blob();
        const auto count = r.word(); require(count > 0, "empty corpus");
        sf::Texture texture; sf::Sprite sprite(texture);
        for (std::uint32_t i = 0; i < count; ++i) {
            Program program(loadProgram(programs.at(r.word())), iprop_program_free);
            const auto seed = r.word(), width = r.word(), height = r.word();
            auto parameters = createParameters(program.get()); Error error;
            const auto inputCount = r.word();
            for (std::uint32_t j = 0; j < inputCount; ++j) {
                const auto name = r.blob(); const auto tag = r.word(), bits = r.word(), integer = r.word();
                const iprop_utf8_view view{reinterpret_cast<const char*>(name.data()), static_cast<std::uint32_t>(name.size())};
                iprop_parameter_value_ex current;
                error.check(iprop_parameter_value_ex_init(&current, sizeof(current)), "init");
                error.check(iprop_parameters_get_named(parameters.get(), view, &current, error.get()), "input query");
                if (tag == 0) error.check(iprop_parameters_set_color_named(parameters.get(), view,
                    {static_cast<std::uint8_t>(bits), static_cast<std::uint8_t>(bits >> 8), static_cast<std::uint8_t>(bits >> 16), static_cast<std::uint8_t>(bits >> 24)}, error.get()), "color");
                else if (current.type == IPROP_INPUT_F32) error.check(iprop_parameters_set_f32_named(parameters.get(), view, std::bit_cast<float>(bits), error.get()), "float");
                else error.check(iprop_parameters_set_u32_named(parameters.get(), view, integer, error.get()), "uint");
            }
            const auto expectedWidth = r.word(), expectedHeight = r.word(); const auto expected = r.blob();
            Result result(generate(program.get(), seed, parameters.get(), width, height), iprop_result_free);
            const auto image = queryImage(result.get()); const std::string label = "case " + std::to_string(i);
            require(image.width == expectedWidth && image.height == expectedHeight, label + ": size");
            require(image.byte_count == expected.size() && std::memcmp(image.pixels, expected.data(), expected.size()) == 0, label + ": RGBA");
            const auto outputs = copyOutputs(result.get()); require(outputs.size() == r.word(), label + ": outputs");
            for (const auto& output : outputs) {
                const auto name = r.blob(); r.word(); const auto kind = r.word(), list = r.word(), elements = r.word(), leaves = r.word();
                require(output.name.size() == name.size() && std::memcmp(output.name.data(), name.data(), name.size()) == 0, label + ": name");
                const auto components = iprop::backend::outputComponents(output.kind);
                require(static_cast<std::uint32_t>(output.kind) == kind && output.isList == (list != 0) && output.values.size() == leaves && output.values.size() == elements * components, label + ": slot");
                for (const auto value : output.values) require(std::bit_cast<std::uint32_t>(value) == r.word(), label + ": bits");
            }
            uploadTexture(texture, sprite, {image.width, image.height}, image.pixels);
            const auto readback = texture.copyToImage();
            require(std::memcmp(readback.getPixelsPtr(), expected.data(), expected.size()) == 0, label + ": SFML texture upload");
            sf::RenderTexture target({image.width, image.height});
            target.clear(sf::Color::Transparent);
            target.draw(sprite, sf::BlendNone); target.display();
            const auto rendered = target.getTexture().copyToImage();
            require(std::memcmp(rendered.getPixelsPtr(), expected.data(), expected.size()) == 0, label + ": SFML GPU render");
        }
        require(r.at == r.data.size(), "unread corpus cases");
        std::cout << "SFML + texture + GPU render: " << count << " pixel-exact cases\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
