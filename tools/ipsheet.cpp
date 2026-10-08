#include "ipsheet.hpp"
#include "png.hpp"

#include "iprop/VM.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace iprop {
namespace {

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

} // namespace

int ipsheetMain(int argc, char* argv[]) {
    std::string input;
    std::string output = "sheet.png";
    u32 variants = 0; // 0 = all declared variants
    u32 seeds = 4;
    u32 columns = 0; // 0 = variants count

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "ipsheet: missing value for " << what << "\n";
                return "";
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            std::cout << "ipsheet - render a variant × seed grid PNG\n\n"
                      << "Usage: ipsheet <file.iprop> [options]\n"
                      << "  --variants N   variant count (default: all declared)\n"
                      << "  --seeds N      seeds per variant (default 4)\n"
                      << "  --columns N    grid columns (default: the seed count)\n"
                      << "  -o PATH        output PNG (default: sheet.png)\n";
            return 0;
        } else if (arg == "--variants") {
            if (!parseU32(next("--variants"), variants)) {
                std::cerr << "ipsheet: bad --variants\n";
                return 1;
            }
        } else if (arg == "--seeds") {
            if (!parseU32(next("--seeds"), seeds) || seeds == 0) {
                std::cerr << "ipsheet: bad --seeds\n";
                return 1;
            }
        } else if (arg == "--columns") {
            if (!parseU32(next("--columns"), columns)) {
                std::cerr << "ipsheet: bad --columns\n";
                return 1;
            }
        } else if (arg == "-o" || arg == "--output") {
            output = next("--output");
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "ipsheet: unknown option '" << arg << "'\n";
            return 1;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "ipsheet: multiple input files\n";
            return 1;
        }
    }
    if (input.empty()) {
        std::cerr << "ipsheet: no input file\n";
        return 1;
    }

    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        std::cerr << "ipsheet: cannot open '" << input << "'\n";
        return 1;
    }
    std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), {});
    LoadedFile loaded;
    try {
        loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        std::cerr << "ipsheet: invalid .iprop at byte offset " << error.offset << ": "
                  << error.what() << "\n";
        return 1;
    }

    if (variants == 0) {
        variants = loaded.meta.variants;
    }
    if (columns == 0) {
        columns = seeds;
    }
    const u32 cellW = loaded.meta.sizeX;
    const u32 cellH = loaded.meta.sizeY;
    // Layout: row-major over (variant, seed) pairs — each variant gets a
    // row of its seeds, wrapped to the requested column count.
    const u32 cells = variants * seeds;
    const u32 rows = (cells + columns - 1) / columns;
    std::vector<u8> rgba(
        static_cast<std::size_t>(columns) * cellW * rows * cellH * 4, 0);

    try {
        for (u32 v = 0; v < variants; ++v) {
            for (u32 s = 0; s < seeds; ++s) {
                GenerationInputs inputs;
                inputs.seed = s;
                inputs.variant = v;
                VM vm(loaded, inputs);
                vm.run();
                const Canvas& canvas = vm.canvas();
                const u32 cell = v * seeds + s;
                const u32 cellX = cell % columns;
                const u32 cellY = cell / columns;
                for (u32 y = 0; y < canvas.height; ++y) {
                    for (u32 x = 0; x < canvas.width; ++x) {
                        const Color& pixel = canvas.at(x, y);
                        const std::size_t at =
                            ((static_cast<std::size_t>(cellY) * cellH + y) * columns * cellW +
                             static_cast<std::size_t>(cellX) * cellW + x) *
                            4;
                        rgba[at] = pixel.r;
                        rgba[at + 1] = pixel.g;
                        rgba[at + 2] = pixel.b;
                        rgba[at + 3] = pixel.a;
                    }
                }
            }
        }
    } catch (const Trap& error) {
        std::cerr << "ipsheet: generation failed at byte offset " << error.offset << ": "
                  << error.what() << "\n";
        return 1;
    }

    if (!tools::writePng(output, {columns * cellW, rows * cellH, std::move(rgba)})) {
        std::cerr << "ipsheet: cannot write '" << output << "'\n";
        return 1;
    }
    std::cout << "wrote " << output << " (" << variants << " variants × " << seeds
              << " seeds, " << columns * cellW << "×" << rows * cellH << ")\n";
    return 0;
}

} // namespace iprop
