// The C boundary adds nothing and loses nothing: every number, array and
// image it hands out equals the C++ model runtime's, bit for bit, for a set
// of seeds and inputs; two results never share storage; a generation is
// repeatable.
#include "iprop3d.h"
#include "check.h"

#include "iprop/model/Model.hpp"

#include <cstring>
#include <string>
#include <vector>

// The header names the runtime's default.
static_assert(IPROP3D_DEFAULT_WORK_LIMIT == iprop::model::kDefaultModelWorkLimit);

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    uint8_t* bytes = nullptr;
    size_t length = 0;
    if (read_fixture(path.c_str(), &bytes, &length) != 0) return {};
    std::vector<uint8_t> out(bytes, bytes + length);
    free(bytes);
    return out;
}

template<class T> bool sameBytes(const T* actual, const std::vector<T>& expected) {
    if (expected.empty()) return actual == nullptr;
    return actual != nullptr && std::memcmp(actual, expected.data(), expected.size() * sizeof(T)) == 0;
}

bool sameName(iprop_utf8_view name, const std::string& expected) {
    return name.byte_count == expected.size() && std::memcmp(name.data, expected.data(), expected.size()) == 0;
}

struct Case {
    uint32_t seed;
    float wide;
    uint32_t extra;
    iprop_color_rgba8 stain;
    bool defaults; // generate without a parameter set
};

int compare(const iprop3d_model* model, const iprop::model::LoadedModel& loaded, const Case& test) {
    iprop3d_generate_options options;
    CHECK(iprop3d_generate_options_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = test.seed;
    iprop_parameters* parameters = nullptr;
    iprop::model::ModelInputs inputs;
    inputs.seed = test.seed;
    if (!test.defaults) {
        CHECK(iprop3d_model_parameters_create(model, &parameters, nullptr) == IPROP_EX_OK);
        CHECK(iprop_parameters_set_f32(parameters, 0, test.wide, nullptr) == IPROP_EX_OK);
        CHECK(iprop_parameters_set_color(parameters, 1, test.stain, nullptr) == IPROP_EX_OK);
        CHECK(iprop_parameters_set_u32(parameters, 2, test.extra, nullptr) == IPROP_EX_OK);
        iprop::UniformValue wide, stain, extra;
        wide.type = iprop::InputType::F32; wide.f = test.wide;
        stain.type = iprop::InputType::Color;
        stain.color = iprop::Color{test.stain.r, test.stain.g, test.stain.b, test.stain.a};
        extra.type = iprop::InputType::U32; extra.u = test.extra;
        inputs.uniforms = {{"wide", wide}, {"stain", stain}, {"extra", extra}};
    }
    const auto expected = iprop::model::generateModel(loaded, inputs);

    iprop3d_result *result = nullptr, *again = nullptr;
    CHECK(iprop3d_generate(model, &options, parameters, nullptr, &result, nullptr) == IPROP_EX_OK);
    CHECK(iprop3d_generate(model, &options, parameters, nullptr, &again, nullptr) == IPROP_EX_OK);
    CHECK(result != again);

    iprop3d_result_info info;
    CHECK(iprop3d_result_info_init(&info, sizeof(info)) == IPROP_EX_OK);
    CHECK(iprop3d_result_query(result, &info, nullptr) == IPROP_EX_OK);
    CHECK(std::memcmp(info.bounds_min, expected.boundsMin, sizeof(info.bounds_min)) == 0);
    CHECK(std::memcmp(info.bounds_max, expected.boundsMax, sizeof(info.bounds_max)) == 0);
    CHECK(std::memcmp(info.size, expected.size, sizeof(info.size)) == 0);
    CHECK(float_bits(&info.units_per_meter) == float_bits(&expected.unitsPerMeter));
    CHECK(info.vertex_count == expected.vertexCount && info.triangle_count == expected.triangleCount);
    CHECK(info.surface_count == expected.surfaces.size() && info.texture_count == expected.textures.size());
    CHECK(info.fuel_budget == expected.fuelBudget && info.fuel_used == expected.fuelUsed);
    CHECK(info.work_budget == expected.workBudget && info.work_used == expected.workUsed);

    uint32_t vertices = 0, triangles = 0;
    for (uint32_t s = 0; s < info.surface_count; ++s) {
        const auto& surface = expected.surfaces[s];
        iprop3d_surface_view view, repeated;
        CHECK(iprop3d_surface_view_init(&view, sizeof(view)) == IPROP_EX_OK);
        CHECK(iprop3d_surface_view_init(&repeated, sizeof(repeated)) == IPROP_EX_OK);
        CHECK(iprop3d_result_surface(result, s, &view, nullptr) == IPROP_EX_OK);
        CHECK(iprop3d_result_surface(again, s, &repeated, nullptr) == IPROP_EX_OK);
        CHECK(view.surface_index == s && view.texture_index == surface.texture);
        CHECK(float_bits(&view.roughness) == float_bits(&surface.roughness));
        CHECK(float_bits(&view.metallic) == float_bits(&surface.metallic));
        CHECK(float_bits(&view.emission) == float_bits(&surface.emission));
        CHECK(float_bits(&view.alpha_cutoff) == float_bits(&surface.cutoff));
        CHECK(view.alpha_mode == static_cast<uint32_t>(surface.alpha));
        CHECK(view.two_sided == (surface.twoSided ? 1u : 0u) && view.crisp == (surface.crisp ? 1u : 0u));
        CHECK(view.vertex_count == surface.positions.size() / 3 && view.index_count == surface.indices.size());
        CHECK(sameBytes(view.positions, surface.positions) && sameBytes(view.normals, surface.normals));
        CHECK(sameBytes(view.tangents, surface.tangents) && sameBytes(view.uvs, surface.uvs));
        CHECK(sameBytes(view.indices, surface.indices));
        // The repeated generation: equal bytes in storage of its own.
        CHECK(repeated.positions != view.positions && sameBytes(repeated.positions, surface.positions));
        CHECK(sameBytes(repeated.indices, surface.indices) && sameBytes(repeated.uvs, surface.uvs));
        vertices += view.vertex_count;
        triangles += view.index_count / 3;
    }
    CHECK(vertices == info.vertex_count && triangles == info.triangle_count);

    for (uint32_t t = 0; t < info.texture_count; ++t) {
        const auto& texture = expected.textures[t];
        iprop3d_texture_view view;
        CHECK(iprop3d_texture_view_init(&view, sizeof(view)) == IPROP_EX_OK);
        CHECK(iprop3d_result_texture(result, t, &view, nullptr) == IPROP_EX_OK);
        CHECK(view.texture_index == t && view.key == texture.key && sameName(view.name, texture.name));
        CHECK(view.repeats == (texture.repeats ? 1u : 0u));
        CHECK(view.asset_width == texture.assetWidth && view.asset_height == texture.assetHeight);
        CHECK(view.width == texture.color.width && view.height == texture.color.height);
        CHECK(view.byte_count == texture.color.pixels.size() * 4 && view.row_stride == uint64_t{view.width} * 4);
        CHECK(std::memcmp(view.pixels, texture.color.pixels.data(), static_cast<size_t>(view.byte_count)) == 0);
        CHECK(view.image_count == texture.images.size());
        for (uint32_t i = 0; i < view.image_count; ++i) {
            const auto& image = texture.images[i];
            iprop_aux_image_view_ex aux;
            CHECK(iprop_aux_image_view_ex_init(&aux, sizeof(aux)) == IPROP_EX_OK);
            CHECK(iprop3d_result_texture_image(result, t, i, &aux, nullptr) == IPROP_EX_OK);
            CHECK(aux.image_index == i && aux.kind == static_cast<uint32_t>(image.kind));
            CHECK(aux.source == static_cast<uint32_t>(image.source) && sameName(aux.name, image.name));
            CHECK(aux.width == image.image.width && aux.height == image.image.height);
            CHECK(aux.byte_count == image.image.pixels.size() * 4);
            CHECK(std::memcmp(aux.pixels, image.image.pixels.data(), static_cast<size_t>(aux.byte_count)) == 0);
        }
    }
    iprop3d_result_free(result);
    iprop3d_result_free(again);
    iprop_parameters_free(parameters);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string directory = argv[1];
    for (const char* name : {"crate.iprop3d", "crate-debug.iprop3d", "empty.iprop3d", "materials.iprop3d"}) {
        const auto bytes = readFile(directory + "/" + name);
        CHECK(!bytes.empty());
        iprop3d_model* model = nullptr;
        CHECK(iprop3d_model_load(bytes.data(), bytes.size(), &model, nullptr) == IPROP_EX_OK);
        const auto loaded = iprop::model::loadModel(bytes);
        iprop::model::validateModel(loaded);

        iprop3d_model_info info;
        CHECK(iprop3d_model_info_init(&info, sizeof(info)) == IPROP_EX_OK);
        CHECK(iprop3d_model_query(model, &info, nullptr) == IPROP_EX_OK);
        CHECK(float_bits(&info.size_x) == float_bits(&loaded.size[0]) && float_bits(&info.size_z) == float_bits(&loaded.size[2]));
        CHECK(info.fuel == loaded.file.meta.fuel && info.input_count == loaded.file.meta.inputs.size());
        CHECK(info.skin_program_count == loaded.skins.size());
        CHECK(info.container_flags == loaded.file.containerHeader.flags);

        const bool crate = info.input_count != 0;
        const Case cases[] = {
            {0, 0, 0, {0, 0, 0, 0}, true},
            {1, 0, 0, {0, 0, 0, 0}, true},
            {4294967295u, 0, 0, {0, 0, 0, 0}, true},
            {7, 3.5f, 1, {0x10, 0x80, 0xf0, 0xff}, false},
            {7, 11.25f, 0, {0xff, 0xff, 0xff, 0x80}, false},
            {123456789u, 2.0f / 3.0f, 2, {0, 0, 0, 0xff}, false},
        };
        for (const Case& test : cases) {
            if (!crate && !test.defaults) continue;
            CHECK(compare(model, loaded, test) == 0);
        }
        iprop3d_model_free(model);
    }
    puts("capi3d conformance: OK");
    return 0;
}
