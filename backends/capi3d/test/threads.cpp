// Independent parameter sets may be prepared and generated on one shared model.
#include "iprop3d.h"
#include "check.h"

#include <array>
#include <future>
#include <string>
#include <vector>

namespace {
struct Sample {
    std::vector<float> positions;
    std::vector<uint8_t> pixels;
    uint64_t work = 0;
    bool operator==(const Sample&) const = default;
};

int sample(const iprop3d_model* model, uint32_t i, Sample& out) {
    iprop_parameters* parameters = nullptr;
    CHECK(iprop3d_model_parameters_create(model, &parameters, nullptr) == IPROP_EX_OK);
    CHECK(iprop_parameters_set_f32_named(parameters, {"wide", 4}, 4.f + static_cast<float>(i), nullptr) == IPROP_EX_OK);
    iprop3d_generate_options options{};
    options.struct_size = sizeof(options);
    options.seed = 100 + i;
    options.lod = i % 3;
    iprop3d_result* result = nullptr;
    CHECK(iprop3d_generate(model, &options, parameters, nullptr, &result, nullptr) == IPROP_EX_OK);
    iprop_parameters_free(parameters);
    iprop3d_surface_view surface{};
    surface.struct_size = sizeof(surface);
    CHECK(iprop3d_result_surface(result, 0, &surface, nullptr) == IPROP_EX_OK);
    out.positions.assign(surface.positions, surface.positions + 3 * surface.vertex_count);
    iprop3d_texture_view tex{};
    tex.struct_size = sizeof(tex);
    CHECK(iprop3d_result_texture(result, 0, &tex, nullptr) == IPROP_EX_OK);
    out.pixels.assign(tex.pixels, tex.pixels + tex.byte_count);
    out.work = tex.work;
    iprop3d_result_free(result);
    return 0;
}
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    uint8_t* data = nullptr;
    size_t size = 0;
    CHECK(read_fixture((std::string(argv[1]) + "/crate.iprop3d").c_str(), &data, &size) == 0);
    iprop3d_model* model = nullptr;
    CHECK(iprop3d_model_load(data, size, &model, nullptr) == IPROP_EX_OK);
    free(data);
    std::array<Sample, 8> expected;
    for (uint32_t i = 0; i < expected.size(); ++i) CHECK(sample(model, i, expected[i]) == 0);
    std::array<std::future<int>, 8> workers;
    for (uint32_t i = 0; i < workers.size(); ++i) {
        workers[i] = std::async(std::launch::async, [&, i] {
            for (int j = 0; j < 6; ++j) {
                Sample result;
                CHECK(sample(model, i, result) == 0 && result == expected[i]);
            }
            return 0;
        });
    }
    for (auto& worker : workers) CHECK(worker.get() == 0);
    iprop3d_model_free(model);
}
