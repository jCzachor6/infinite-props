#pragma once
#include "CorpusReader.hpp"
#include "ModelHost.hpp"
#include <bit>
#include <cstring>

namespace iprop::sfml_host {
template<class Transport> void checkModelCorpus(const std::string& directory, Transport transport) {
    Reader r{readFile(directory + "/model-corpus.bin")};
    if (r.data.size() < 8 || std::memcmp(r.data.data(), "IPMCRP01", 8)) throw std::runtime_error("Invalid model corpus");
    r.at = 8;
    std::vector<Model> programs;
    const auto models = r.word();
    for (unsigned i = 0; i < models; ++i) programs.emplace_back(loadModel(r.blob()), iprop3d_model_free);
    auto text = [&] { auto b = r.blob(); return std::string(b.begin(), b.end()); };
    auto hash = [](const std::vector<uint8_t>& bytes) {
        uint64_t fnv = 0xcbf29ce484222325ULL;
        for (auto byte : bytes) fnv = (fnv ^ byte) * 0x100000001b3ULL;
        return fnv;
    };
    auto cases = r.word();
    for (unsigned i = 0; i < cases; ++i) {
        auto label = text();
        auto check = [&](bool equal) { if (!equal) throw std::runtime_error(label + " at byte " + std::to_string(r.at)); };
        auto& program = programs.at(r.word());
        ModelRequest req{};
        req.seed = r.word();
        req.lod = r.word();
        auto params = createModelParameters(program.get());
        Error error;
        auto inputs = r.word();
        for (unsigned j = 0; j < inputs; ++j) {
            auto name = text();
            iprop_utf8_view view{name.data(), static_cast<uint32_t>(name.size())};
            auto kind = r.word(), value = r.word();
            iprop_parameter_value_ex v{};
            v.struct_size = sizeof(v);
            v.type = kind;
            if (kind == 0) v.color = {uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
            else if (kind == 1) v.f32 = std::bit_cast<float>(value);
            else v.u32 = value;
            error.check(iprop_parameters_set_named(params.get(), view, &v, error.get()), "model corpus input");
        }
        ModelResult raw(generateModel(program.get(), req, params.get()), iprop3d_result_free);
        auto out = copyModel(raw.get());
        raw.reset();
        auto& s = out.statistics;
        auto word = [&](uint32_t v) { check(v == r.word()); };
        auto number = [&](float f) { word(std::bit_cast<uint32_t>(f)); };
        for (float f : s.size) number(f);
        number(s.units_per_meter);
        for (float f : s.bounds_min) number(f);
        for (float f : s.bounds_max) number(f);
        word(s.vertex_count);
        word(s.triangle_count);
        for (auto value : {s.fuel_budget, s.fuel_used, s.work_budget, s.work_used}) check(value == r.wide());
        check(out.textures.size() == r.word());
        for (const auto& t : out.textures) {
            check(t.known.key == r.wide() && t.name == text());
            for (auto v : {t.width,t.height,t.assetWidth,t.assetHeight}) word(v);
            word(t.repeats);
            check(hash(t.data->rgba) == r.wide());
            check(t.data->images.size() == r.word());
            for (const auto& a : t.data->images) {
                word(a.info.kind);
                check(a.name == text() && hash(a.pixels) == r.wide());
            }
            transport(t);
        }
        auto array = [&](const auto& values) {
            auto span = std::as_bytes(std::span(values));
            auto expected = r.blob();
            check(expected.size() == span.size() && (expected.empty() || !std::memcmp(expected.data(), span.data(), span.size())));
        };
        check(out.surfaces.size() == r.word());
        for (const auto& v : out.surfaces) {
            word(v.textureIndex);
            for (float f : {v.roughness, v.metallic, v.emission}) number(f);
            word(v.alphaMode);
            number(v.alphaCutoff);
            word(v.twoSided);
            word(v.crisp);
            for (auto a : {&v.positions,&v.normals,&v.tangents,&v.uvs}) array(*a);
            array(v.indices);
        }
        check(out.outputs.size() == r.word());
        for (const auto& o : out.outputs) {
            check(o.name == text());
            word(o.kind);
            word(o.isList);
            word(o.valueCount);
            array(o.values);
        }
    }
    if (r.at != r.data.size()) throw std::runtime_error("Unread model corpus");
}
}
