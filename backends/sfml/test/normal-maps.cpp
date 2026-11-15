// Auxiliary images through the SFML host glue against the CLI-rendered
// shared fixtures, including an SFML texture upload of the normal image.
//
// Usage: sfml_normal_maps <binding-fixtures-dir>

#include "Host.hpp"
#include "Render.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
}

int main(int argc, char** argv) {
    using namespace iprop::sfml_host;
    try {
        require(argc == 2, "usage: sfml_normal_maps binding-fixtures");
        const std::string fixtures = std::string(argv[1]) + "/";
        const auto load = [&](const std::string& name) { return readFile(fixtures + name); };
        {
            Program program(loadProgram(load("binding-normal.iprop")), iprop_program_free);
            const auto declared = declaredImages(program.get());
            require(declared.size() == 1 && declared[0].index == 0 && declared[0].kind == IPROP_AUX_IMAGE_NORMAL &&
                    declared[0].name == "relief", "declared normal output");
            Result result(generate(program.get(), 5), iprop_result_free);
            const auto copy = copyResult(result.get());
            require(copy.pixels == load("binding-normal.rgba"), "native RGBA oracle with a normal output");
            const auto* normal = copy.normal();
            require(copy.images.size() == 1 && normal == &copy.images[0], "one auxiliary image");
            require(normal->info.image_index == 0 && normal->info.kind == IPROP_AUX_IMAGE_NORMAL &&
                    normal->info.source == IPROP_AUX_IMAGE_AUTHORED && normal->name == "relief", "authored tags");
            require(normal->info.width == 8 && normal->info.height == 4 && normal->info.row_stride == 32 &&
                    normal->info.alpha_mode == IPROP_IMAGE_ALPHA_OPAQUE, "normal image layout");
            require(normal->info.pixels == nullptr && normal->info.name.data == nullptr, "copies hold no borrowed pointer");
            const auto expected = load("binding-normal.normal.rgba");
            require(normal->pixels == expected, "native normal map oracle");
            // The copies outlive the result and the program.
            result.reset(); program.reset();
            require(copy.normal()->pixels == expected, "owned copies");
            // The bytes survive an SFML texture upload unchanged.
            sf::Texture texture; sf::Sprite sprite(texture);
            uploadTexture(texture, sprite, {8, 4}, copy.normal()->pixels.data());
            require(!texture.isSrgb(), "a normal map texture is linear");
            const auto stored = texture.copyToImage();
            require(std::memcmp(stored.getPixelsPtr(), expected.data(), expected.size()) == 0, "SFML texture upload");
        }
        {
            Program program(loadProgram(load("binding-normal.iprop")), iprop_program_free);
            // A size override scales the image; an authored image wins over a request.
            Result scaled(generate(program.get(), 5, nullptr, 16, 8), iprop_result_free);
            const auto images = copyImages(scaled.get());
            require(images.size() == 1 && images[0].info.width == 16 && images[0].info.height == 8 && images[0].pixels.size() == 512, "scaled normal image");
            const DerivedNormal request{9.f, 0.f, 0.f};
            Result requested(generate(program.get(), 5, nullptr, 0, 0, {}, &request), iprop_result_free);
            const auto kept = copyImages(requested.get());
            require(kept.size() == 1 && kept[0].info.source == IPROP_AUX_IMAGE_AUTHORED &&
                    kept[0].pixels == load("binding-normal.normal.rgba"), "authored wins");
        }
        {
            Program program(loadProgram(load("bindings.iprop")), iprop_program_free);
            require(declaredImages(program.get()).empty(), "no declared image");
            Result plainResult(generate(program.get(), 5), iprop_result_free);
            const auto plain = copyResult(plainResult.get());
            require(plain.images.empty() && plain.normal() == nullptr, "derived images are opt-in");
            const DerivedNormal defaults{}, custom{2.f, 0.5f, 0.25f};
            for (const auto& [request, name] : {std::pair<const DerivedNormal*, const char*>{&defaults, "bindings-derived"},
                                                {&custom, "bindings-derived-custom"}}) {
                Result result(generate(program.get(), 5, nullptr, 0, 0, {}, request), iprop_result_free);
                const auto copy = copyResult(result.get());
                const auto* normal = copy.normal();
                require(normal != nullptr && normal->info.source == IPROP_AUX_IMAGE_DERIVED && normal->name == "normal", "derived tags");
                require(normal->pixels == load(std::string(name) + ".normal.rgba"), std::string("native derived normal oracle: ") + name);
                // The request leaves the main image and fuel alone and spends work.
                require(copy.pixels == plain.pixels && copy.statistics.fuel_used == plain.statistics.fuel_used, "main image unchanged");
                require(copy.work.work_used > plain.work.work_used, "derivation spends work");
            }
            Result measured(generate(program.get(), 5, nullptr, 0, 0, {}, &defaults), iprop_result_free);
            const auto used = copyResult(measured.get()).work.work_used;
            Result exact(generate(program.get(), 5, nullptr, 0, 0, {used, 0, {}}, &defaults), iprop_result_free);
            require(copyResult(exact.get()).work.work_remaining == 0, "exact work");
            try { generate(program.get(), 5, nullptr, 0, 0, {used - 1, 0, {}}, &defaults); require(false, "work limit was ignored"); }
            catch (const Failure& failure) { require(failure.reason == 3015 && failure.stage == 4, "work limit during derivation"); }
            const float infinity = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
            for (const DerivedNormal bad : {DerivedNormal{-1.f, 0.f, 0.f}, DerivedNormal{infinity, 0.f, 0.f}, DerivedNormal{0.f, 64.5f, 0.f},
                                            DerivedNormal{0.f, -0.5f, 0.f}, DerivedNormal{0.f, 0.f, 1.5f}, DerivedNormal{0.f, 0.f, nan}}) {
                try { generate(program.get(), 5, nullptr, 0, 0, {}, &bad); require(false, "expected an argument error"); }
                catch (const Failure& failure) {
                    require(failure.status == IPROP_EX_E_ARGUMENT && failure.stage == 3 &&
                            std::string(failure.what()).find("out of range") != std::string::npos, "invalid derive request");
                }
            }
        }
        std::cout << "SFML normal maps: PASS authored and derived images against the CLI, declarations, texture upload, work limits and argument errors\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
