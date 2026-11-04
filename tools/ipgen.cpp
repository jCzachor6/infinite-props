#include "ipgen.hpp"

#include "json.hpp"
#include "png.hpp"

#include "iprop/VM.hpp"

#include <cmath>
#include <charconv>
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
    bool floatBits = false;
    bool omitPartOutputs = false;
    u32 seed = 0;
    u32 sizeX = 0; // 0 = use the meta size
    u32 sizeY = 0;
    u64 workLimit = kDefaultWorkLimit;
    u64 timeoutMs = 0;
    bool workStats = false;
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
                 "  --size WxH          requested output size (default: the meta size)\n"
                 "  --uniform name=val  uniform override (repeatable); colours are\n"
                 "                      #RRGGBB or #RRGGBBAA, numbers are decimal\n"
                 "  -o, --output PATH   PNG output path (default: <stem>-<seed>.png)\n"
                 "  --float-bits        encode output coordinates as raw binary32 bits\n"
                 "  --omit-part-outputs preview PNG/legacy outputs without typed part records\n"
                 "  --work-limit N      independent native work budget (0 = finite default)\n"
                 "  --timeout-ms N      monotonic generation timeout (0 = none)\n"
                 "  --work-stats        include work counters in result JSON\n"
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
    if (hasPartOutputs(loaded) && !options.omitPartOutputs)
        return fail("part outputs are unsupported by the legacy JSON result; use the typed host API");
    inputs.seed = options.seed;
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

    GenerationResult result;
    u64 budget = 0;
    u64 spent = 0;
    u64 workBudget = 0, workUsed = 0;
    try {
        inputs.limits.workLimit = options.workLimit;
        inputs.limits.deadline = ExecutionLimits::deadlineAfter(options.timeoutMs * 1'000'000);
        VM vm(loaded, inputs); // setup traps (canvas
                               // size) throw here too
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
    if (!tools::writePng(pngPath, tools::canvasImage(canvas))) {
        return fail("cannot write PNG '" + pngPath + "'");
    }

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
    json << "\n}\n";
    std::cout << json.str();

    return 0;
}

} // namespace iprop
