// The model corpus (models v1.2, a preview): models from the repository,
// several seeds and inputs each, compiled and generated in process. Every
// platform must give the bytes pinned in tests/fixtures/model-corpus-v1.2.json:
// one set of digests, never one per platform. A difference is a bug of the
// runtime, the compiler or the platform's arithmetic, not a second answer.
//
// The pins are the SHA-256 of each part of a result: the expanded compiled
// file, every array of every surface (little-endian binary32 or u32), the
// pixels of every texture and of its auxiliary images, plus the counters in
// clear text. On a difference the test prints the first lines that differ
// and, when IPROP_MODEL_CORPUS_REPORT names a directory, writes the whole
// report there with the exact dump (ipgen --dump) of every case that
// differs, for comparing with a dump made where the pins were recorded.
//
// This is a regression pin, not an independent reference: the digests are
// what this runtime computed when they were recorded. Record them again only
// for an intended change of a model's bytes, and say why:
//
//   IPROP_MODEL_CORPUS_REGENERATE=1 build/tests/iprop_model_tests "[corpus]"
#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "iprop/model/Model.hpp"
#include "ips/SourceText.hpp"
#include "model_tools.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace iprop;

namespace {

const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
const std::filesystem::path fixture = root / "tests/fixtures/model-corpus-v1.2.json";

struct Case {
    const char* source; // relative to the repository
    u32 seed = 0;
    std::vector<std::pair<const char*, const char*>> inputs; // as ipgen --uniform takes them
};

// The showcase props and the handbook's models, the models of the WASM and C
// API conformance, and a model whose arguments take the general paths of
// the kernels (odd sides, oblique turns, mirrored non-uniform scales,
// creases near a joint's bend, a concave bevelled outline, sizes that are no
// integers).
const std::vector<Case>& cases() {
    static const std::vector<Case> list = {
        {"examples/models/chair.ips", 1, {}},
        {"examples/models/chair.ips", 5, {}},
        {"examples/models/chair.ips", 13, {}},
        {"examples/models/chair.ips", 42, {}},
        {"examples/models/chair.ips", 7, {{"finish", "5"}, {"fabric", "2"}}},
        {"examples/models/crate.ips", 1, {}},
        {"examples/models/crate.ips", 4, {}},
        {"examples/models/crate.ips", 8, {}},
        {"examples/models/crate.ips", 11, {}},
        {"examples/models/crate.ips", 13, {}},
        {"examples/models/crate.ips", 7, {{"finish", "3"}}},
        {"examples/models/barrel.ips", 1, {}},
        {"examples/models/barrel.ips", 4, {}},
        {"examples/models/barrel.ips", 9, {}},
        {"examples/models/barrel.ips", 14, {}},
        {"examples/models/sword.ips", 1, {}},
        {"examples/models/sword.ips", 2, {}},
        {"examples/models/sword.ips", 7, {}},
        {"examples/models/sword.ips", 12, {}},
        {"examples/models/sword.ips", 13, {}},
        {"examples/models/sword.ips", 5, {{"metalwork", "2"}, {"wrap", "3"}}},
        {"examples/language/stool.ips", 1, {}},
        {"examples/language/stool.ips", 7, {}},
        {"examples/language/model-primitives.ips", 1, {}},
        {"examples/language/model-place.ips", 1, {}},
        {"examples/language/model-skins.ips", 3, {}},
        {"examples/language/model-vectors.ips", 1, {}},
        {"examples/language/model-vectors.ips", 2, {}},
        {"backends/html5/test/fixtures/model-primitives.ips", 0, {}},
        {"backends/html5/test/fixtures/model-primitives.ips", 7, {{"stretch", "1.75"}, {"sides", "5"}, {"tint", "#c0392b"}}},
        {"backends/html5/test/fixtures/model-primitives.ips", 2, {{"stretch", "0.625"}, {"sides", "31"}, {"rounding", "2.5"}}},
        {"backends/capi3d/test/crate.ips", 5, {}},
        {"backends/capi3d/test/crate.ips", 77, {{"wide", "11.75"}, {"stain", "#10a0f080"}}},
        {"tests/model/corpus/edges.ips", 1, {}},
        {"tests/model/corpus/edges.ips", 2, {}},
        {"tests/model/corpus/edges.ips", 3, {}},
        {"tests/model/corpus/edges.ips", 4, {}},
        {"tests/model/corpus/edges.ips", 5, {}},
        {"tests/model/corpus/edges.ips", 6, {}},
        {"tests/model/corpus/edges.ips", 7, {}},
        {"tests/model/corpus/edges.ips", 8, {}},
        // Material maps, glass, windows and shared constants (M2).
        {"tests/model/corpus/materials.ips", 1, {}},
        {"tests/model/corpus/materials.ips", 2, {}},
        {"tests/model/corpus/materials.ips", 3, {}},
        {"tests/model/corpus/materials.ips", 4, {}},
        {"tests/model/corpus/materials.ips", 5, {}},
        {"tests/model/corpus/materials.ips", 6, {}},
        {"examples/models/lantern.ips", 1, {}},
        {"examples/models/lantern.ips", 4, {}},
        {"examples/models/lantern.ips", 6, {}},
        {"examples/models/lantern.ips", 3, {{"rust", "0.9"}}},
        // Shapes along a path, shaping blocks, outputs and parts (M3).
        {"tests/model/corpus/shapes.ips", 1, {}},
        {"tests/model/corpus/shapes.ips", 2, {}},
        {"tests/model/corpus/shapes.ips", 3, {}},
        {"tests/model/corpus/shapes.ips", 4, {}},
        {"tests/model/corpus/shapes.ips", 5, {}},
        {"tests/model/corpus/shapes.ips", 6, {}},
        {"tests/model/corpus/shapes.ips", 7, {}},
        {"tests/model/corpus/shapes.ips", 8, {}},
        {"examples/language/model-paths.ips", 1, {}},
        {"examples/language/model-shaping.ips", 1, {}},
        {"examples/language/model-shaping.ips", 2, {}},
        {"examples/language/model-outputs.ips", 3, {}},
        {"examples/language/model-parts.ips", 1, {}},
        {"examples/language/model-parts.ips", 9, {}},
        {"examples/models/streetlamp.ips", 1, {}},
        {"examples/models/streetlamp.ips", 2, {}},
        {"examples/models/streetlamp.ips", 5, {{"paint", "#5a2a2a"}, {"rust", "0.8"}}},
        {"examples/models/boulder.ips", 1, {}},
        {"examples/models/boulder.ips", 6, {{"rough", "0.9"}}},
        {"examples/models/table.ips", 1, {}},
        {"examples/models/table.ips", 3, {{"stain", "#5c3a1e"}}},
        {"examples/models/railing.ips", 1, {}},
        {"examples/models/railing.ips", 2, {}},
        {"examples/models/railing.ips", 4, {{"rust", "0.9"}}},
        // Skins of the texture toolkit (M4), under the model's one fuel budget.
        {"examples/models/bench.ips", 1, {}},
        {"examples/models/bench.ips", 2, {}},
        {"examples/models/bench.ips", 5, {{"paint", "#7a3b2e"}, {"dye", "#6b2f3a"}}},
    };
    return list;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// A checkout with automatic CRLF conversion must compare equal too.
std::string withoutCarriageReturns(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
        if (c != '\r') out += c;
    return out;
}

std::string digest(const std::string& bytes) { return ips::sourceContentId(bytes); }

std::string littleEndian(const std::vector<f32>& values) {
    std::string bytes;
    bytes.reserve(values.size() * 4);
    for (const f32 value : values) {
        const u32 word = std::bit_cast<u32>(value);
        for (int shift = 0; shift < 32; shift += 8) bytes += static_cast<char>((word >> shift) & 0xff);
    }
    return bytes;
}

std::string littleEndian(const std::vector<u32>& values) {
    std::string bytes;
    bytes.reserve(values.size() * 4);
    for (const u32 word : values)
        for (int shift = 0; shift < 32; shift += 8) bytes += static_cast<char>((word >> shift) & 0xff);
    return bytes;
}

std::string pixels(const Canvas& canvas) {
    std::string bytes;
    bytes.reserve(canvas.pixels.size() * 4);
    for (const Color& pixel : canvas.pixels) {
        bytes += static_cast<char>(pixel.r);
        bytes += static_cast<char>(pixel.g);
        bytes += static_cast<char>(pixel.b);
        bytes += static_cast<char>(pixel.a);
    }
    return bytes;
}

std::string bits(f32 value) { return std::to_string(std::bit_cast<u32>(value)); }
std::string triple(const f32 values[3]) { return bits(values[0]) + "," + bits(values[1]) + "," + bits(values[2]); }

std::string hex64(u64 value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

UniformValue uniform(InputType type, const std::string& text) {
    UniformValue value;
    value.type = type;
    switch (type) {
    case InputType::Color: {
        if (text.size() != 7 && text.size() != 9) throw std::runtime_error("bad colour " + text);
        const auto channel = [&](std::size_t at) { return static_cast<u8>(std::stoul(text.substr(at, 2), nullptr, 16)); };
        value.color = {channel(1), channel(3), channel(5), text.size() == 9 ? channel(7) : u8{255}};
        break;
    }
    case InputType::F32: value.f = std::strtof(text.c_str(), nullptr); break;
    case InputType::U32: value.u = static_cast<u32>(std::stoul(text)); break;
    }
    return value;
}

struct Generated {
    std::string label;
    std::vector<std::string> lines; // the case's lines of the report
    std::string dump;               // ipgen --dump of the result
};

Generated generate(const Case& item) {
    SourceCompilationOptions options;
    options.compress = false; // pin the expanded file: deflate varies with the zlib build
    // Forward slashes on every host: imports resolve lexically by '/', and
    // `../` in an import must not stop at a Windows separator.
    const auto compiled = compileSource((root / item.source).generic_string(), [](std::string_view path) {
        return readText(std::filesystem::path(std::string(path)));
    }, options);
    if (!compiled.succeeded())
        throw std::runtime_error(std::string(item.source) + ": " +
                                 (compiled.diagnostics.empty() ? "compilation failed" : compiled.diagnostics.front().message));
    const auto loaded = model::loadModel(compiled.bytes);
    model::validateModel(loaded);
    model::ModelInputs inputs;
    inputs.seed = item.seed;
    std::string inputText;
    for (const auto& [name, text] : item.inputs) {
        const auto* declaration = loaded.file.meta.findInput(name, loaded.file.pool);
        if (!declaration) throw std::runtime_error(std::string(item.source) + ": no input " + name);
        inputs.uniforms[name] = uniform(declaration->type, text);
        inputText += (inputText.empty() ? "" : " ") + std::string(name) + "=" + text;
    }
    const auto result = model::generateModel(loaded, inputs);

    Generated out;
    out.label = std::string(item.source) + " seed " + std::to_string(item.seed) + (inputText.empty() ? "" : " " + inputText);
    auto& lines = out.lines;
    lines.push_back("program " + digest(std::string(compiled.bytes.begin(), compiled.bytes.end())));
    lines.push_back("vertices " + std::to_string(result.vertexCount) + " triangles " + std::to_string(result.triangleCount) +
                    " fuel " + std::to_string(result.fuelUsed) + "/" + std::to_string(result.fuelBudget) + " work " +
                    std::to_string(result.workUsed) + "/" + std::to_string(result.workBudget));
    lines.push_back("size " + triple(result.size) + " units " + bits(result.unitsPerMeter) + " bounds " +
                    triple(result.boundsMin) + " " + triple(result.boundsMax));
    for (std::size_t t = 0; t < result.textures.size(); ++t) {
        const auto& texture = result.textures[t];
        const auto name = "texture " + std::to_string(t);
        lines.push_back(name + " " + texture.name + " " + hex64(texture.key) + " " + std::to_string(texture.color.width) + "x" +
                        std::to_string(texture.color.height) + " of " + std::to_string(texture.assetWidth) + "x" +
                        std::to_string(texture.assetHeight) + (texture.repeats ? " repeats" : " clamps") + " " +
                        digest(pixels(texture.color)));
        for (std::size_t i = 0; i < texture.images.size(); ++i)
            lines.push_back(name + " image " + std::to_string(i) + " kind " + std::to_string(static_cast<u32>(texture.images[i].kind)) +
                            " " + texture.images[i].name + " " + digest(pixels(texture.images[i].image)));
    }
    for (std::size_t s = 0; s < result.surfaces.size(); ++s) {
        const auto& surface = result.surfaces[s];
        const auto name = "surface " + std::to_string(s);
        lines.push_back(name + " texture " + std::to_string(surface.texture) + " roughness " + bits(surface.roughness) +
                        " metallic " + bits(surface.metallic) + " emission " + bits(surface.emission) + " alpha " +
                        std::to_string(static_cast<u32>(surface.alpha)) + " cutoff " + bits(surface.cutoff) +
                        (surface.twoSided ? " two-sided" : " one-sided") + (surface.crisp ? " crisp" : " filtered") +
                        " vertices " + std::to_string(surface.positions.size() / 3) + " indices " +
                        std::to_string(surface.indices.size()));
        lines.push_back(name + " positions " + digest(littleEndian(surface.positions)));
        lines.push_back(name + " normals " + digest(littleEndian(surface.normals)));
        lines.push_back(name + " tangents " + digest(littleEndian(surface.tangents)));
        lines.push_back(name + " uvs " + digest(littleEndian(surface.uvs)));
        lines.push_back(name + " indices " + digest(littleEndian(surface.indices)));
    }
    // The model's outputs (M3), only for a model that declares some.
    for (std::size_t o = 0; o < result.outputs.size(); ++o) {
        const auto& output = result.outputs[o];
        const bool box = output.kind == model::ModelOutputKind::Box3d;
        std::vector<f32> values;
        for (const Vec3& point : output.points) values.insert(values.end(), {point.x, point.y, point.z});
        for (const auto& value : output.boxes) {
            values.insert(values.end(), value.center, value.center + 3);
            values.insert(values.end(), value.size, value.size + 3);
            for (const auto& axis : value.axes) values.insert(values.end(), axis, axis + 3);
        }
        lines.push_back("output " + std::to_string(o) + " " + output.name + (box ? " box3d" : " point3d") +
                        (output.list ? " list" : " single") + " values " +
                        std::to_string(box ? output.boxes.size() : output.points.size()) + " " + digest(littleEndian(values)));
    }
    std::ostringstream dump;
    tools::writeModelDump(dump, result, item.seed);
    out.dump = dump.str();
    return out;
}

// One JSON document, one line for each pinned part, so that two reports
// compare and differ line by line.
std::string report(const std::vector<Generated>& generated) {
    std::string text = "{\n  \"corpus\": \"models v1.2 (preview)\",\n"
                       "  \"regenerate\": \"IPROP_MODEL_CORPUS_REGENERATE=1 build/tests/iprop_model_tests [corpus]\",\n"
                       "  \"cases\": [\n";
    for (std::size_t c = 0; c < generated.size(); ++c) {
        text += "    {\"case\": \"" + generated[c].label + "\", \"pins\": [\n";
        const auto& lines = generated[c].lines;
        for (std::size_t i = 0; i < lines.size(); ++i)
            text += "      \"" + lines[i] + "\"" + (i + 1 < lines.size() ? ",\n" : "\n");
        text += std::string("    ]}") + (c + 1 < generated.size() ? ",\n" : "\n");
    }
    return text + "  ]\n}\n";
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

std::string fileName(std::string label) {
    for (char& c : label)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) c = '_';
    return label;
}

} // namespace

TEST_CASE("the model corpus gives the pinned bytes", "[corpus]") {
    std::vector<Generated> generated;
    for (const auto& item : cases()) generated.push_back(generate(item));
    const auto actual = report(generated);

    if (std::getenv("IPROP_MODEL_CORPUS_REGENERATE")) {
        std::ofstream(fixture, std::ios::binary) << actual;
        WARN("recorded " << generated.size() << " cases in " << fixture.string());
        return;
    }
    const auto expected = withoutCarriageReturns(readText(fixture));
    if (actual == expected) {
        SUCCEED(generated.size() << " cases equal to their pins");
        return;
    }

    // Name the cases that differ and their first differing lines.
    const auto want = split(expected), got = split(actual);
    std::string summary, current;
    std::size_t differences = 0;
    std::vector<std::size_t> differing;
    for (std::size_t i = 0; i < std::max(want.size(), got.size()); ++i) {
        const std::string& line = i < got.size() ? got[i] : std::string();
        if (line.find("\"case\": ") != std::string::npos) current = line;
        if (i < want.size() && i < got.size() && want[i] == got[i]) continue;
        if (++differences <= 12)
            summary += "\n  in " + current + "\n    pinned: " + (i < want.size() ? want[i] : "(none)") + "\n    actual: " + line;
    }
    for (std::size_t c = 0; c < generated.size(); ++c) {
        const auto label = "\"case\": \"" + generated[c].label + "\"";
        bool same = false;
        for (std::size_t i = 0; i < want.size() && !same; ++i) {
            if (want[i].find(label) == std::string::npos) continue;
            // The case's block in both reports, from its label to its end.
            std::size_t j = i, k = 0;
            while (k < got.size() && got[k].find(label) == std::string::npos) ++k;
            same = k < got.size();
            for (; same && j < want.size() && k < got.size(); ++j, ++k) {
                if (want[j] != got[k]) same = false;
                if (want[j].find("]}") != std::string::npos) break;
            }
        }
        if (!same) differing.push_back(c);
    }

    if (const char* directory = std::getenv("IPROP_MODEL_CORPUS_REPORT")) {
        const std::filesystem::path out(directory);
        std::filesystem::create_directories(out);
        std::ofstream(out / "model-corpus-actual.json", std::ios::binary) << actual;
        for (const auto c : differing)
            std::ofstream(out / (fileName(generated[c].label) + ".dump.json"), std::ios::binary) << generated[c].dump;
        summary += "\n  report and dumps: " + out.string();
    }
    FAIL(differences << " pinned lines differ in " << differing.size() << " of " << generated.size() << " cases:" << summary);
}
