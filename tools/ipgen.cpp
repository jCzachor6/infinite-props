#include "ipgen.hpp"

#include "json.hpp"
#include "model_tools.hpp"
#include "texture_cache.hpp"
#include "png.hpp"

#include "iprop/VM.hpp"

#include <array>
#include <cmath>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace iprop {
namespace {

struct Options {
    std::string file;
    bool floatBits = false;
    bool omitPartOutputs = false;
    u32 seed = 0;
    u32 lod = 0; // a model's level of detail
    std::string textureCache; // a directory of stored textures
    u32 sizeX = 0; // 0 = use the meta size
    u32 sizeY = 0;
    u64 workLimit = kDefaultWorkLimit;
    u64 timeoutMs = 0;
    bool workStats = false;
    std::string output; // PNG path; empty = <stem>-<seed>.png
    // PNG path of each auxiliary image by kind (1 = normal, 2-5 the material
    // maps); empty = not written.
    std::array<std::string, kAuxImageKinds + 1> imageOutputs;
    bool deriveNormal = false;
    DerivedNormalRequest derived{};
    std::vector<std::pair<std::string, std::string>> uniforms;
    // Models (.iprop3d) only.
    std::string preview;  // PNG path of the preview image; empty = not written
    std::string skinsDir; // directory for the texture PNGs; empty = not written
    bool dump = false;    // the exact dump on stdout instead of the summary
    tools::PreviewOptions view{512, 512, 35.f, 20.f, tools::kPreviewBackground};
    std::string modelOnly; // the first option given that needs a model
};

// The kind of an `--<kind>-output` option (normal or a material map), or 0.
u32 imageOutputKind(const std::string& arg) {
    for (u32 kind = 1; kind <= kAuxImageKinds; ++kind)
        if (arg == std::string("--") + auxImageKindName(kind) + "-output") return kind;
    return 0;
}

void printUsage(std::FILE* out) {
    std::fprintf(out,
                 "ipgen - generate a seed-based procedural asset\n"
                 "\n"
                 "Usage: ipgen <file.iprop> [options]\n"
                 "\n"
                 "Options:\n"
                 "  --seed N            generation seed (default 0)\n"
                 "  --size WxH          requested output size (default: the meta size)\n"
                 "  --uniform name=val  uniform override (repeatable); colours are\n"
                 "                      #RRGGBB or #RRGGBBAA, numbers are decimal\n"
                 "  -o, --output PATH   PNG output path (default: <stem>-<seed>.png)\n"
                 "  --normal-output PATH  also write the normal image as a PNG\n"
                 "  --roughness-output PATH, --metallic-output PATH, --ao-output PATH,\n"
                 "  --emission-output PATH  also write that material map as a PNG\n"
                 "  --derive-normal     derive a normal image when the program publishes none\n"
                 "  --normal-depth F    derived depth in pixels (0 = size-relative default)\n"
                 "  --normal-rounding F derived Gaussian sigma in pixels, 0-64 (0 = default)\n"
                 "  --normal-detail F   derived share of brightness in the height, 0-1\n"
                 "  --float-bits        encode output coordinates as raw binary32 bits\n"
                 "  --omit-part-outputs preview PNG/legacy outputs without typed part records\n"
                 "  --work-limit N      independent native work budget (0 = finite default)\n"
                 "  --timeout-ms N      monotonic generation timeout (0 = none)\n"
                 "  --work-stats        include work counters in result JSON\n"
                 "  -h, --help          show this help\n"
                 "\n"
                 "The result JSON (outputs and fuel) is printed to stdout.\n"
                 "\n"
                 "A model (<file.iprop3d>) generates triangles and textures instead:\n"
                 "  -o, --output PATH   binary glTF (default: <stem>-<seed>.glb); a path\n"
                 "                      ending in .png writes the preview image instead\n"
                 "  --preview PATH      also write a preview image as a PNG\n"
                 "  --preview-size WxH  preview size in pixels (default 512x512)\n"
                 "  --lod N             level of detail the model reads with model.lod(),\n"
                 "                      0 (full, the default) to 7\n"
                 "  --texture-cache DIR keep generated textures in DIR and reuse those\n"
                 "                      stored there: same result, less time\n"
                 "  --view YAW,PITCH    where the preview camera stands, in degrees\n"
                 "                      (default 35,20)\n"
                 "  --background COLOR  preview background, #RRGGBB or #RRGGBBAA\n"
                 "  --skins-dir DIR     also write every texture and its normal image\n"
                 "  --dump              print every vertex, index and image hash, floats as\n"
                 "                      raw binary32 bits, instead of the summary\n"
                 "--seed, --uniform, --float-bits and the limits apply to a model too, and\n"
                 "--omit-part-outputs is accepted: a model has no part records. The preview\n"
                 "is a review aid and not part of any contract.\n");
}

int fail(const std::string& message) {
    std::cerr << "ipgen: " << message << "\n";
    return 1;
}

// Strict unsigned parse: all digits, within range.
bool parseU32(const std::string& text, u32& out) {
    if (text.empty()) {
        return false;
    }
    u64 value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<u64>(c - '0');
        if (value > 0xFFFFFFFFull) {
            return false;
        }
    }
    out = static_cast<u32>(value);
    return true;
}

bool parseSize(const std::string& text, u32& w, u32& h) {
    const auto x = text.find('x');
    if (x == std::string::npos || x == 0 || x + 1 >= text.size()) {
        return false;
    }
    return parseU32(text.substr(0, x), w) && parseU32(text.substr(x + 1), h) && w > 0 && h > 0;
}

bool parseU64(std::string_view text, u64& out) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool hexDigit(char c, u8& out) {
    if (c >= '0' && c <= '9') {
        out = static_cast<u8>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        out = static_cast<u8>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        out = static_cast<u8>(c - 'A' + 10);
        return true;
    }
    return false;
}

// Parses a colour as #RRGGBB or #RRGGBBAA. Throws std::invalid_argument.
Color parseColor(const std::string& text) {
    if (text.size() != 7 && text.size() != 9) {
        throw std::invalid_argument("colour must be #RRGGBB or #RRGGBBAA");
    }
    if (text[0] != '#') {
        throw std::invalid_argument("colour must start with '#'");
    }
    Color color;
    u8* channels[4] = {&color.r, &color.g, &color.b, &color.a};
    const int count = text.size() == 7 ? 3 : 4;
    for (int i = 0; i < count; ++i) {
        u8 hi = 0, lo = 0;
        if (!hexDigit(text[1 + i * 2], hi) || !hexDigit(text[2 + i * 2], lo)) {
            throw std::invalid_argument("invalid hex digit in colour");
        }
        *channels[i] = static_cast<u8>(hi * 16u + lo);
    }
    if (count == 3) {
        color.a = 255;
    }
    return color;
}

// Uniform overrides, each parsed per its declared input type. Returns the
// error message, empty when every override was accepted.
std::string readUniforms(const LoadedFile& loaded, const Options& options,
                         std::map<std::string, UniformValue>& uniforms) {
    for (const auto& [name, value] : options.uniforms) {
        const InputDecl* decl = loaded.meta.findInput(name, loaded.pool);
        if (decl == nullptr) {
            return "unknown uniform '" + name + "'";
        }
        try {
            UniformValue uniform;
            switch (decl->type) {
            case InputType::Color:
                uniform.type = InputType::Color;
                uniform.color = parseColor(value);
                break;
            case InputType::F32:
                uniform.type = InputType::F32;
                uniform.f = std::stof(value);
                break;
            case InputType::U32: {
                u32 parsed = 0;
                if (!parseU32(value, parsed)) {
                    throw std::invalid_argument("not an unsigned integer");
                }
                uniform.type = InputType::U32;
                uniform.u = parsed;
                break;
            }
            }
            uniforms[name] = uniform;
        } catch (const std::exception& error) {
            return "bad value for uniform '" + name + "': " + error.what();
        }
    }
    return {};
}

// The declared inputs, for uniform editors: the elements of a JSON array.
void writeInputsJson(std::ostream& json, const LoadedFile& loaded) {
    for (std::size_t i = 0; i < loaded.meta.inputs.size(); ++i) {
        if (i != 0) {
            json << ",";
        }
        const InputDecl& input = loaded.meta.inputs[i];
        json << "\n    {\"name\": \"" << loaded.pool.strings.at(input.nameIndex) << "\", \"type\": \"";
        switch (input.type) {
        case InputType::Color: {
            const Color& c = input.colorDefault;
            char hex[16];
            std::snprintf(hex, sizeof(hex), "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
            json << "color\", \"default\": \"" << hex << "\"";
            break;
        }
        case InputType::F32:
            json << "f32\", \"default\": " << input.f32Default;
            break;
        case InputType::U32:
            json << "u32\", \"default\": " << input.u32Default;
            break;
        }
        json << "}";
    }
}

bool endsWith(const std::string& text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char c = text[text.size() - suffix.size() + i];
        if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != suffix[i]) return false;
    }
    return true;
}

bool writeBytes(const std::string& path, const std::vector<u8>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return static_cast<bool>(out);
}

// A model (spec models v1.2): a binary glTF, an optional preview and texture
// images, and a summary or the exact dump on stdout.
int generateModelFile(const Options& options, const std::vector<u8>& bytes) {
    // --omit-part-outputs is accepted: a model has no part records to leave
    // out, and one command line then renders any compiled source.
    bool imageOutput = false;
    for (const auto& path : options.imageOutputs) imageOutput = imageOutput || !path.empty();
    if (options.sizeX != 0 || imageOutput || options.deriveNormal)
        return fail("--size, --normal-output, the material map outputs and --derive-normal do not apply to a model; "
                    "a texture takes its size from the model source");
    model::LoadedModel loaded;
    try {
        loaded = model::loadModel(bytes);
        model::validateModel(loaded);
    } catch (const FormatError& error) {
        return fail("invalid .iprop3d at byte offset " + std::to_string(error.offset) + ": " + error.what());
    }

    model::ModelInputs inputs;
    inputs.seed = options.seed;
    inputs.lod = options.lod;
    if (const auto problem = readUniforms(loaded.file, options, inputs.uniforms); !problem.empty()) return fail(problem);

    model::ModelResult result;
    try {
        inputs.limits.workLimit = options.workLimit;
        inputs.limits.deadline = ExecutionLimits::deadlineAfter(options.timeoutMs * 1'000'000);
        if (options.textureCache.empty()) {
            result = model::generateModel(loaded, inputs);
        } else {
            tools::TextureCache cache(options.textureCache);
            inputs.knownTextures = cache.known();
            result = model::generateModel(loaded, inputs);
            try {
                cache.complete(result);
            } catch (const std::runtime_error& error) {
                // A stored texture is unreadable: generate everything.
                std::cerr << "ipgen: " << error.what() << "; generating without the cache\n";
                inputs.knownTextures = {};
                result = model::generateModel(loaded, inputs);
            }
        }
    } catch (const FormatError& error) {
        return fail("generation failed at byte offset " + std::to_string(error.offset) + ": " + error.what());
    }

    // -o names the glTF, or the preview when it ends in .png.
    const std::string stem = options.file.substr(0, options.file.rfind('.')) + "-" + std::to_string(options.seed);
    std::string glbPath = options.output.empty() ? stem + ".glb" : options.output;
    std::string previewPath = options.preview;
    if (endsWith(glbPath, ".png")) {
        if (!previewPath.empty()) return fail("--output names a PNG and --preview another: give the preview once");
        previewPath = std::move(glbPath);
        glbPath.clear();
    }
    if (!glbPath.empty() && !writeBytes(glbPath, tools::encodeGlb(result))) return fail("cannot write '" + glbPath + "'");
    if (!previewPath.empty() && !tools::writePng(previewPath, tools::renderPreview(result, options.view)))
        return fail("cannot write PNG '" + previewPath + "'");
    std::vector<std::string> texturePaths;
    if (!options.skinsDir.empty()) {
        std::error_code error;
        std::filesystem::create_directories(options.skinsDir, error);
        for (std::size_t i = 0; i < result.textures.size(); ++i) {
            const auto& texture = result.textures[i];
            // The alias is an identifier, so it is safe in a file name.
            const std::string base = options.skinsDir + "/" + (i < 10 ? "0" : "") + std::to_string(i) + "-" + texture.name;
            if (!tools::writePng(base + ".png", tools::canvasImage(texture.color)))
                return fail("cannot write PNG '" + base + ".png'");
            for (const auto& image : texture.images) {
                const std::string path = base + "-" + auxImageKindName(static_cast<u32>(image.kind)) + ".png";
                if (!tools::writePng(path, tools::canvasImage(image.image))) return fail("cannot write PNG '" + path + "'");
            }
            texturePaths.push_back(base + ".png");
        }
    }

    if (options.dump) {
        tools::writeModelDump(std::cout, result, options.seed);
        return 0;
    }
    const auto number = [&](f32 value) { return tools::jsonNumber(value, options.floatBits); };
    const auto triple = [&](const f32 values[3]) {
        return "{\"x\": " + number(values[0]) + ", \"y\": " + number(values[1]) + ", \"z\": " + number(values[2]) + "}";
    };
    std::ostringstream json;
    json << "{\n  \"seed\": " << options.seed << ",\n  \"size\": " << triple(result.size)
         << ",\n  \"units_per_meter\": " << number(result.unitsPerMeter)
         << ",\n  \"bounds\": {\"min\": " << triple(result.boundsMin) << ", \"max\": " << triple(result.boundsMax)
         << "},\n  \"vertices\": " << result.vertexCount << ",\n  \"triangles\": " << result.triangleCount
         << ",\n  \"fuel\": {\"budget\": " << result.fuelBudget << ", \"spent\": " << result.fuelUsed << "}";
    if (options.workStats)
        json << ",\n  \"work\": {\"budget\": " << result.workBudget << ", \"spent\": " << result.workUsed << "}";
    json << ",\n  \"inputs\": [";
    writeInputsJson(json, loaded.file);
    json << "\n  ],\n  \"textures\": [";
    for (std::size_t i = 0; i < result.textures.size(); ++i) {
        const auto& texture = result.textures[i];
        char key[17];
        std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(texture.key));
        json << (i ? "," : "") << "\n    {\"name\": \"" << tools::jsonEscape(texture.name) << "\", \"key\": \"" << key
             << "\", \"size\": {\"x\": " << texture.color.width << ", \"y\": " << texture.color.height
             << "}, \"repeats\": " << (texture.repeats ? "true" : "false") << ", \"images\": [";
        for (std::size_t j = 0; j < texture.images.size(); ++j)
            json << (j ? ", " : "") << "{\"kind\": \"" << auxImageKindName(static_cast<u32>(texture.images[j].kind))
                 << "\", \"name\": \"" << tools::jsonEscape(texture.images[j].name) << "\"}";
        json << "]";
        if (i < texturePaths.size()) json << ", \"path\": \"" << tools::jsonEscape(texturePaths[i]) << "\"";
        json << "}";
    }
    json << "\n  ],\n  \"surfaces\": [";
    for (std::size_t i = 0; i < result.surfaces.size(); ++i) {
        const auto& surface = result.surfaces[i];
        json << (i ? "," : "") << "\n    {\"texture\": " << surface.texture << ", \"vertices\": "
             << surface.positions.size() / 3 << ", \"triangles\": " << surface.indices.size() / 3
             << ", \"roughness\": " << number(surface.roughness) << ", \"metallic\": " << number(surface.metallic)
             << ", \"emission\": " << number(surface.emission) << ", \"alpha\": \""
             << (surface.alpha == model::SkinAlpha::Cutout ? "cutout" : surface.alpha == model::SkinAlpha::Blend ? "blend" : "opaque")
             << "\"";
        if (surface.alpha == model::SkinAlpha::Cutout) json << ", \"cutoff\": " << number(surface.cutoff);
        json << ", \"two_sided\": " << (surface.twoSided ? "true" : "false") << ", \"crisp\": "
             << (surface.crisp ? "true" : "false") << "}";
    }
    json << "\n  ]";
    // The model's 3D outputs (M3), when it declares any.
    if (!result.outputs.empty()) {
        json << ",\n  \"outputs\": [";
        for (std::size_t i = 0; i < result.outputs.size(); ++i) {
            const auto& output = result.outputs[i];
            const bool box = output.kind == model::ModelOutputKind::Box3d;
            json << (i ? "," : "") << "\n    {\"name\": \"" << tools::jsonEscape(output.name) << "\", \"kind\": \""
                 << (box ? "box3d" : "point3d") << "\", \"list\": " << (output.list ? "true" : "false") << ", \"values\": [";
            const std::size_t count = box ? output.boxes.size() : output.points.size();
            for (std::size_t j = 0; j < count; ++j) {
                json << (j ? ", " : "");
                if (!box) {
                    const f32 point[3] = {output.points[j].x, output.points[j].y, output.points[j].z};
                    json << triple(point);
                    continue;
                }
                const auto& value = output.boxes[j];
                json << "{\"center\": " << triple(value.center) << ", \"size\": " << triple(value.size)
                     << ", \"axes\": [" << triple(value.axes[0]) << ", " << triple(value.axes[1]) << ", "
                     << triple(value.axes[2]) << "]}";
            }
            json << "]}";
        }
        json << "\n  ]";
    }
    if (!glbPath.empty()) json << ",\n  \"glb\": \"" << tools::jsonEscape(glbPath) << "\"";
    if (!previewPath.empty()) json << ",\n  \"preview\": \"" << tools::jsonEscape(previewPath) << "\"";
    json << "\n}\n";
    std::cout << json.str();
    return 0;
}

} // namespace

int ipgenMain(int argc, char* argv[]) {
    Options options;
    bool wantHelp = false;
    bool derivedParameters = false;
    std::string invalid;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                invalid = std::string("missing value for ") + what;
                return "";
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            wantHelp = true;
        } else if (arg == "--float-bits") {
            options.floatBits = true;
        } else if (arg == "--omit-part-outputs") {
            options.omitPartOutputs = true;
        } else if (arg == "--work-stats") {
            options.workStats = true;
        } else if (arg == "--work-limit") {
            if (!parseU64(next("--work-limit"), options.workLimit)) invalid = "bad --work-limit value";
        } else if (arg == "--timeout-ms") {
            if (!parseU64(next("--timeout-ms"), options.timeoutMs) || options.timeoutMs > UINT64_MAX / 1'000'000)
                invalid = "bad --timeout-ms value";
        } else if (arg == "--seed") {
            if (!parseU32(next("--seed"), options.seed)) invalid = "bad --seed value";
        } else if (arg == "--size") {
            if (!parseSize(next("--size"), options.sizeX, options.sizeY)) invalid = "bad --size value";
        } else if (arg == "--uniform") {
            const std::string pair = next("--uniform");
            const auto eq = pair.find('=');
            if (eq == std::string::npos || eq == 0) {
                invalid = "bad --uniform value (expected name=value)";
            } else {
                options.uniforms.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
            }
        } else if (arg == "-o" || arg == "--output") {
            options.output = next("--output");
        } else if (const u32 kind = imageOutputKind(arg); kind != 0) {
            options.imageOutputs[kind] = next(arg.c_str());
            if (invalid.empty() && options.imageOutputs[kind].empty()) invalid = "empty " + arg + " path";
        } else if (arg == "--derive-normal") {
            options.deriveNormal = true;
        } else if (arg == "--normal-depth" || arg == "--normal-rounding" || arg == "--normal-detail") {
            const std::string text = next(arg.c_str());
            f32& field = arg == "--normal-depth" ? options.derived.depth
                : arg == "--normal-rounding" ? options.derived.rounding : options.derived.detail;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), field);
            if (invalid.empty() && (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()))
                invalid = "bad " + arg + " value";
            derivedParameters = true;
        } else if (arg == "--preview" || arg == "--skins-dir") {
            std::string& path = arg == "--preview" ? options.preview : options.skinsDir;
            path = next(arg.c_str());
            if (invalid.empty() && path.empty()) invalid = "empty " + arg + " path";
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (arg == "--dump") {
            options.dump = true;
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (arg == "--preview-size") {
            if (!parseSize(next("--preview-size"), options.view.width, options.view.height) ||
                options.view.width > 4096 || options.view.height > 4096)
                invalid = "bad --preview-size value (at most 4096x4096)";
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (arg == "--lod") {
            if (!parseU32(next("--lod"), options.lod) || options.lod > model::kMaxLod) invalid = "bad --lod value (0 to 7)";
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (arg == "--texture-cache") {
            options.textureCache = next("--texture-cache");
            if (options.textureCache.empty()) invalid = "bad --texture-cache value";
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (arg == "--view") {
            if (!tools::parseView(next("--view"), options.view)) invalid = "bad --view value (expected YAW,PITCH in degrees)";
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (arg == "--background") {
            try {
                options.view.background = parseColor(next("--background"));
            } catch (const std::invalid_argument& error) {
                invalid = std::string("bad --background value: ") + error.what();
            }
            if (options.modelOnly.empty()) options.modelOnly = arg;
        } else if (!arg.empty() && arg[0] == '-') {
            invalid = "unknown option '" + arg + "'";
        } else if (options.file.empty()) {
            options.file = arg;
        } else {
            invalid = "multiple input files";
        }
        if (!invalid.empty()) {
            return fail(invalid);
        }
    }

    if (wantHelp) {
        printUsage(stdout);
        return 0;
    }
    if (options.file.empty()) {
        printUsage(stderr);
        return fail("no input file");
    }
    if (derivedParameters && !options.deriveNormal)
        return fail("--normal-depth, --normal-rounding and --normal-detail need --derive-normal");
    if (options.deriveNormal && !validDerivedNormalRequest(options.derived))
        return fail("derived normal parameters out of range (depth >= 0, rounding 0-64, detail 0-1)");

    // Read the file; the buffer must outlive the LoadedFile (zero-copy).
    std::ifstream stream(options.file, std::ios::binary);
    if (!stream) {
        return fail("cannot open '" + options.file + "'");
    }
    std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), {});
    if (bytes.empty()) {
        return fail("empty file '" + options.file + "'");
    }

    if (tools::isModelFile(bytes)) return generateModelFile(options, bytes);
    if (!options.modelOnly.empty()) return fail(options.modelOnly + " needs a model (.iprop3d)");

    LoadedFile loaded;
    try {
        loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        return fail("invalid .iprop at byte offset " + std::to_string(error.offset) + ": " +
                    error.what());
    }

    GenerationInputs inputs;
    if (hasPartOutputs(loaded) && !options.omitPartOutputs)
        return fail("part outputs are unsupported by the legacy JSON result; use the typed host API");
    inputs.seed = options.seed;
    inputs.sizeX = options.sizeX;
    inputs.sizeY = options.sizeY;

    if (const auto problem = readUniforms(loaded, options, inputs.uniforms); !problem.empty()) return fail(problem);

    GenerationResult result;
    u64 budget = 0;
    u64 spent = 0;
    u64 workBudget = 0, workUsed = 0;
    try {
        inputs.limits.workLimit = options.workLimit;
        inputs.limits.deadline = ExecutionLimits::deadlineAfter(options.timeoutMs * 1'000'000);
        VM vm(loaded, inputs); // setup traps (canvas
                               // size) throw here too
        if (options.deriveNormal) vm.deriveNormal(options.derived);
        vm.run();
        result = vm.result();
        budget = vm.fuelBudget();
        spent = budget - vm.fuel();
        workBudget = vm.workBudget(); workUsed = workBudget - vm.work();
    } catch (const Trap& error) {
        return fail("generation failed at byte offset " + std::to_string(error.offset) + ": " +
                    error.what());
    }

    const std::string pngPath = !options.output.empty()
                                    ? options.output
                                    : options.file.substr(0, options.file.rfind('.')) + "-" +
                                          std::to_string(options.seed) + ".png";
    const Canvas& canvas = result.image;
    std::array<const AuxImage*, kAuxImageKinds + 1> published{};
    for (const auto& image : result.images) {
        const u32 kind = static_cast<u32>(image.kind);
        if (kind <= kAuxImageKinds && !published[kind]) published[kind] = &image;
    }
    // Checked before anything is written: a missing image leaves no file.
    for (u32 kind = 1; kind <= kAuxImageKinds; ++kind)
        if (!options.imageOutputs[kind].empty() && !published[kind])
            return fail(kind == static_cast<u32>(AuxImageKind::Normal)
                            ? std::string("no normal image: the program publishes none; --derive-normal derives one")
                            : std::string("no ") + auxImageKindName(kind) + " image: the program publishes none");
    if (!tools::writePng(pngPath, tools::canvasImage(canvas))) {
        return fail("cannot write PNG '" + pngPath + "'");
    }
    for (u32 kind = 1; kind <= kAuxImageKinds; ++kind)
        if (!options.imageOutputs[kind].empty() &&
            !tools::writePng(options.imageOutputs[kind], tools::canvasImage(published[kind]->image)))
            return fail("cannot write PNG '" + options.imageOutputs[kind] + "'");

    // Result JSON to stdout: the declared inputs (for uniform editors), the
    // fuel readout and the structured outputs.
    std::ostringstream json;
    json << "{\n  \"seed\": " << options.seed << ",\n  \"size\": {\"x\": "
         << canvas.width << ", \"y\": " << canvas.height
         << "},\n  \"fuel\": {\"budget\": " << budget
         << ", \"spent\": " << spent
         << "}";
    if (options.workStats) json << ",\n  \"work\": {\"budget\": " << workBudget << ", \"spent\": " << workUsed << "}";
    json << ",\n  \"inputs\": [";
    writeInputsJson(json, loaded);
    json << "\n  ],\n  \"outputs\": [";
    bool firstOutput = true;
    for (std::size_t i = 0; i < result.outputs.size(); ++i) {
        if (options.omitPartOutputs && result.outputs[i].kind == OutputKind::Part) continue;
        if (!firstOutput) {
            json << ",";
        }
        json << "\n    ";
        firstOutput = false;
        tools::writeOutputJson(json, result.outputs[i], options.floatBits);
    }
    json << "\n  ]";
    if (options.omitPartOutputs && hasPartOutputs(loaded)) json << ",\n  \"part_outputs_omitted\": true";
    // Present only when the generation published an image beside the main one.
    if (!result.images.empty()) {
        json << ",\n  \"images\": [";
        for (std::size_t i = 0; i < result.images.size(); ++i) {
            const auto& image = result.images[i];
            const u32 kind = static_cast<u32>(image.kind);
            json << (i ? "," : "") << "\n    {\"kind\": \"" << auxImageKindName(kind) << "\", \"source\": \""
                 << (image.source == AuxImageSource::Authored ? "authored" : "derived") << "\", \"name\": ";
            json << '"' << tools::jsonEscape(image.name) << '"';
            if (&image == published[kind] && !options.imageOutputs[kind].empty()) {
                json << ", \"path\": ";
                json << '"' << tools::jsonEscape(options.imageOutputs[kind]) << '"';
            }
            json << "}";
        }
        json << "\n  ]";
    }
    json << "\n}\n";
    std::cout << json.str();

    return 0;
}

} // namespace iprop
