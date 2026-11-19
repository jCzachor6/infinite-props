#include "ModelCorpus.hpp"
#include <future>

using namespace iprop::sfml_host;

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    checkModelCorpus(argv[1], [](const ModelTexture&) {});
    Model model(loadModel(readFile(std::string(argv[1]) + "/model-crate.iprop3d")), iprop3d_model_free);
    auto info = queryModel(model.get());
    if (info.size_x != 12 || info.input_count != 4 || info.skin_program_count != 1) return 1;
    auto lib = modelLibrary();
    if (lib.max_vertices != 65536 || lib.max_triangles != 131072 || lib.max_textures != 32 ||
        lib.max_texture_side != 2048 || lib.max_texels != 4194304 || !textureStamp()) return 1;
    auto inputs = modelInputs(model.get());
    auto skins = modelSkinPrograms(model.get());
    if (inputs.at(0).name != "wide" || inputs[0].floatDefault != 8 || inputs.at(2).uintDefault != 1 ||
        inputs.at(3).floatDefault != 0.5f || skins.at(0).name != "tile" ||
        skins[0].width != 16 || skins[0].height != 8) return 1;
    Program tile(loadProgram(readFile(std::string(argv[1]) + "/model-tile.iprop")), iprop_program_free);
    auto tileParams = createParameters(tile.get());
    setParameter(tileParams.get(), "tint", "#336699");
    Result tileResult(generate(tile.get(), 77, tileParams.get(), 32, 16), iprop_result_free);
    auto image = copyResult(tileResult.get());
    TextureCache cache;
    ModelRequest seeded{};
    seeded.seed = 5;
    auto skin = cache.generate(model.get(), seeded).textures.at(0);
    if (skin.data->rgba != image.pixels || skin.data->images.at(0).pixels != image.images.at(0).pixels) return 1;
    cache.clear();
    auto fails = [&](const ModelRequest& request, unsigned status, unsigned reason) {
        try { ModelResult unexpected(generateModel(model.get(), request), iprop3d_result_free); }
        catch (const Failure& failure) {
            if (failure.status != status || (reason && failure.reason != reason)) throw;
            ModelResult recovered(generateModel(model.get(), {}), iprop3d_result_free);
            return true;
        }
        return false;
    };
    ModelRequest invalid{};
    invalid.lod = 8;
    if (!fails(invalid, 4, 0)) return 1;
    ModelRequest limited{};
    limited.limits.workLimit = 200;
    if (!fails(limited, 3, 3015)) return 1;
    limited.limits = {};
    limited.limits.cancellation = createCancellation();
    iprop_cancellation_cancel(limited.limits.cancellation.get());
    if (!fails(limited, 3, 3016)) return 1;
    auto original = cache.generate(model.get(), {});
    auto warm = cache.generate(model.get(), {});
    if (original.textures.empty() || !warm.textures[0].reused || warm.textures[0].data != original.textures[0].data)
        return 1;
    auto known = cache.known();
    ModelRequest reuse{};
    reuse.known = known;
    ModelResult raw(generateModel(model.get(), reuse), iprop3d_result_free);
    auto low = copyModel(raw.get());
    raw.reset();
    if (!low.textures[0].reused || !low.textures[0].data->rgba.empty()) return 1;
    try { TextureCache{}.complete(low); return 1; }
    catch (const std::out_of_range&) {}
    cache.complete(low);
    if (low.textures[0].data != original.textures[0].data) return 1;
    std::vector<std::future<bool>> workers;
    for (unsigned i = 0; i < 8; ++i) {
        workers.push_back(std::async(std::launch::async, [&, i] {
            ModelRequest request{};
            request.seed = 100 + i;
            request.lod = i % 3;
            auto parameters = createModelParameters(model.get());
            setParameter(parameters.get(), "wide", std::to_string(4 + i));
            ModelResult raw(generateModel(model.get(), request, parameters.get()), iprop3d_result_free);
            auto expected = copyModel(raw.get());
            raw.reset();
            for (int j = 0; j < 6; ++j) {
                auto result = cache.generate(model.get(), request, parameters.get());
                if (expected.surfaces[0].positions != result.surfaces[0].positions ||
                    expected.textures[0].data->rgba != result.textures[0].data->rgba) return false;
            }
            return true;
        }));
    }
    for (auto& worker : workers) if (!worker.get()) return 1;
    model.reset();
    return original.textures[0].data->rgba.empty();
}
