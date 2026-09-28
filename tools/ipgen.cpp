#include "ipgen.hpp"

#include "iprop/VM.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
    u32 seed = 0;
    u32 variant = 0;
    u32 archetype = 0;
    u32 sizeX = 0; // 0 = use the meta size
    u32 sizeY = 0;
    std::string output; // PNG path; empty = <stem>-<seed>.png
    std::vector<std::pair<std::string, std::string>> uniforms;
};

void printUsage(std::FILE* out) {
    std::fprintf(out,
                 "ipgen - generate a seed-based procedural asset\n"
                 "\n"
                 "Usage: ipgen <file.iprop> [options]\n"
                 "\n"
                 "Options:\n"
                 "  --seed N            generation seed (default 0)\n"
                 "  --variant V         variant index (default 0)\n"
                 "  --archetype A       archetype index (default 0)\n"
                 "  --size WxH          requested output size (default: the meta size)\n"
                 "  --uniform name=val  uniform override (repeatable); colours are\n"
                 "                      #RRGGBB or #RRGGBBAA, numbers are decimal\n"
                 "  -o, --output PATH   PNG output path (default: <stem>-<seed>.png)\n"
                 "  -h, --help          show this help\n"
                 "\n"
                 "The result JSON (outputs and fuel) is printed to stdout.\n");
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

// Formats an f32 for JSON with round-trip precision; non-finite values are
// null (JSON has no infinities).
std::string jsonNumber(f32 value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream stream;
    for (const char c : text) {
        switch (c) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\n': stream << "\\n"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned>(static_cast<unsigned char>(c)) << std::dec;
            } else {
                stream << c;
            }
        }
    }
    return stream.str();
}

void writeOutputJson(std::ostream& out, const NamedOutput& output, bool& firstValue) {
    out << "{\"name\":\"" << jsonEscape(output.name) << "\",\"kind\":\"";
    switch (output.kind) {
    case OutputKind::Point: out << "point"; break;
    case OutputKind::Rect: out << "rect"; break;
    case OutputKind::Circle: out << "circle"; break;
    }
    out << "\",\"list\":" << (output.isList ? "true" : "false") << ",\"values\":[";
    bool first = true;
    for (const auto& point : output.points) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(point.x) << ",\"y\":" << jsonNumber(point.y) << "}";
    }
    for (const auto& rect : output.rects) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(rect.x) << ",\"y\":" << jsonNumber(rect.y)
            << ",\"w\":" << jsonNumber(rect.w) << ",\"h\":" << jsonNumber(rect.h) << "}";
    }
    for (const auto& circle : output.circles) {
        if (!first) out << ",";
        first = false;
        out << "{\"x\":" << jsonNumber(circle.x) << ",\"y\":" << jsonNumber(circle.y)
            << ",\"r\":" << jsonNumber(circle.r) << "}";
    }
    out << "]}";
    (void)firstValue;
}

} // namespace

int ipgenMain(int argc, char* argv[]) {
    Options options;
    bool wantHelp = false;
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
        } else if (arg == "--seed") {
            if (!parseU32(next("--seed"), options.seed)) invalid = "bad --seed value";
        } else if (arg == "--variant") {
            if (!parseU32(next("--variant"), options.variant)) invalid = "bad --variant value";
        } else if (arg == "--archetype") {
            if (!parseU32(next("--archetype"), options.archetype)) invalid = "bad --archetype value";
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

    // Read the file; the buffer must outlive the LoadedFile (zero-copy).
    std::ifstream stream(options.file, std::ios::binary);
    if (!stream) {
        return fail("cannot open '" + options.file + "'");
    }
    std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), {});
    if (bytes.empty()) {
        return fail("empty file '" + options.file + "'");
    }

    LoadedFile loaded;
    try {
        loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        return fail("invalid .iprop at byte offset " + std::to_string(error.offset) + ": " +
                    error.what());
    }

    GenerationInputs inputs;
    inputs.seed = options.seed;
    inputs.variant = options.variant;
    inputs.archetype = options.archetype;
    inputs.sizeX = options.sizeX;
    inputs.sizeY = options.sizeY;

    // Uniform overrides: parse per the declared input type.
    for (const auto& [name, value] : options.uniforms) {
        const InputDecl* decl = loaded.meta.findInput(name, loaded.pool);
        if (decl == nullptr) {
            return fail("unknown uniform '" + name + "'");
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
            inputs.uniforms[name] = uniform;
        } catch (const std::exception& error) {
            return fail("bad value for uniform '" + name + "': " + error.what());
        }
    }

    VM vm(loaded, inputs);
    try {
        vm.run();
    } catch (const Trap& error) {
        return fail("generation failed at byte offset " + std::to_string(error.offset) + ": " +
                    error.what());
    }
    const GenerationResult result = vm.result();

    // PNG (deterministic encoder: pinned stb_image_write revision).
    const std::string pngPath = !options.output.empty()
                                    ? options.output
                                    : options.file.substr(0, options.file.rfind('.')) + "-" +
                                          std::to_string(options.seed) + ".png";
    const Canvas& canvas = result.image;
    std::vector<u8> rgba(static_cast<std::size_t>(canvas.width) * canvas.height * 4);
    for (u32 y = 0; y < canvas.height; ++y) {
        for (u32 x = 0; x < canvas.width; ++x) {
            const Color& pixel = canvas.at(x, y);
            const std::size_t at = (static_cast<std::size_t>(y) * canvas.width + x) * 4;
            rgba[at] = pixel.r;
            rgba[at + 1] = pixel.g;
            rgba[at + 2] = pixel.b;
            rgba[at + 3] = pixel.a;
        }
    }
    if (stbi_write_png(pngPath.c_str(), static_cast<int>(canvas.width),
                       static_cast<int>(canvas.height), 4, rgba.data(),
                       static_cast<int>(canvas.width) * 4) == 0) {
        return fail("cannot write PNG '" + pngPath + "'");
    }

    // Result JSON to stdout.
    std::ostringstream json;
    json << "{\n  \"seed\": " << options.seed << ",\n  \"variant\": " << options.variant
         << ",\n  \"archetype\": " << options.archetype << ",\n  \"size\": {\"x\": "
         << canvas.width << ", \"y\": " << canvas.height
         << "},\n  \"fuel\": {\"budget\": " << vm.fuelBudget()
         << ", \"spent\": " << (vm.fuelBudget() - vm.fuel()) << "},\n  \"outputs\": [";
    for (std::size_t i = 0; i < result.outputs.size(); ++i) {
        if (i != 0) {
            json << ",";
        }
        json << "\n    ";
        bool firstValue = true;
        writeOutputJson(json, result.outputs[i], firstValue);
    }
    json << "\n  ]\n}\n";
    std::cout << json.str();

    return 0;
}

} // namespace iprop
