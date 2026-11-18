#include "ipsheet.hpp"
#include "model_tools.hpp"
#include "png.hpp"

#include "iprop/VM.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iprop {
namespace {

using Overrides = std::vector<std::pair<std::string, std::string>>;

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

// Fixed input overrides, each parsed per its declared type. Throws
// std::invalid_argument (std::stof and std::stoull throw their own).
std::map<std::string, UniformValue> readOverrides(const LoadedFile& loaded, const Overrides& overrides) {
    std::map<std::string, UniformValue> uniforms;
    for (const auto& [name, text] : overrides) {
        const auto* declaration = loaded.meta.findInput(name, loaded.pool);
        if (!declaration) throw std::invalid_argument("unknown uniform '" + name + "'");
        UniformValue value;
        value.type = declaration->type;
        std::size_t end = 0;
        if (value.type == InputType::Color) {
            if ((text.size() != 7 && text.size() != 9) || text[0] != '#')
                throw std::invalid_argument("colour must be #RRGGBB or #RRGGBBAA");
            const auto number = std::stoull(text.substr(1), &end, 16);
            if (end != text.size() - 1) throw std::invalid_argument("invalid colour");
            const u32 rgba = static_cast<u32>(text.size() == 7 ? number << 8 | 255u : number);
            value.color = {static_cast<u8>(rgba >> 24), static_cast<u8>(rgba >> 16),
                           static_cast<u8>(rgba >> 8), static_cast<u8>(rgba)};
        } else if (value.type == InputType::F32) {
            value.f = std::stof(text, &end);
            if (end != text.size()) throw std::invalid_argument("invalid f32");
        } else if (!parseU32(text, value.u)) {
            throw std::invalid_argument("invalid u32");
        }
        uniforms[name] = value;
    }
    return uniforms;
}

// The seed of a cell in its upper left corner: 3x5 digits, scaled.
void drawSeed(std::vector<u8>& rgba, u32 sheetWidth, u32 left, u32 top, u32 seed, u32 scale) {
    static constexpr u16 kDigits[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
    const std::string text = std::to_string(seed);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const u16 glyph = kDigits[text[i] - '0'];
        for (u32 row = 0; row < 5; ++row)
            for (u32 column = 0; column < 3; ++column) {
                if (!((glyph >> (14 - (row * 3 + column))) & 1)) continue;
                for (u32 dy = 0; dy < scale; ++dy)
                    for (u32 dx = 0; dx < scale; ++dx) {
                        const std::size_t x = left + (static_cast<u32>(i) * 4 + column) * scale + dx;
                        const std::size_t y = top + row * scale + dy;
                        const std::size_t at = (y * sheetWidth + x) * 4;
                        rgba[at] = rgba[at + 1] = rgba[at + 2] = 0xd8;
                        rgba[at + 3] = 0xff;
                    }
            }
    }
}

// A sheet of previews: one model per seed, each in a square cell.
int modelSheet(const std::vector<u8>& bytes, const std::string& output, u32 seeds, u32 columns, u32 firstSeed,
               const Overrides& overrides, const tools::PreviewOptions& view) {
    model::LoadedModel loaded;
    try {
        loaded = model::loadModel(bytes);
        model::validateModel(loaded);
    } catch (const FormatError& error) {
        std::cerr << "ipsheet: invalid .iprop3d at byte offset " << error.offset << ": " << error.what() << "\n";
        return 1;
    }
    model::ModelInputs inputs;
    try {
        inputs.uniforms = readOverrides(loaded.file, overrides);
    } catch (const std::exception& error) {
        std::cerr << "ipsheet: " << error.what() << "\n";
        return 1;
    }
    if (columns == 0) columns = seeds;
    const u32 rows = (seeds + columns - 1) / columns;
    const u32 cellW = view.width, cellH = view.height;
    std::vector<u8> rgba(static_cast<std::size_t>(columns) * cellW * rows * cellH * 4, 0);
    for (u32 s = 0; s < seeds; ++s) {
        inputs.seed = firstSeed + s;
        tools::Image cell;
        try {
            cell = tools::renderPreview(model::generateModel(loaded, inputs), view);
        } catch (const FormatError& error) {
            std::cerr << "ipsheet: generation of seed " << inputs.seed << " failed at byte offset " << error.offset
                      << ": " << error.what() << "\n";
            return 1;
        }
        const u32 cellX = s % columns, cellY = s / columns;
        for (u32 y = 0; y < cellH; ++y)
            std::copy_n(cell.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * cellW * 4), cellW * 4,
                        rgba.begin() + static_cast<std::ptrdiff_t>(
                            ((static_cast<std::size_t>(cellY) * cellH + y) * columns * cellW + static_cast<std::size_t>(cellX) * cellW) * 4));
        if (cellW >= 96 && cellH >= 32) drawSeed(rgba, columns * cellW, cellX * cellW + 6, cellY * cellH + 6, inputs.seed, 2);
    }
    if (!tools::writePng(output, {columns * cellW, rows * cellH, std::move(rgba)})) {
        std::cerr << "ipsheet: cannot write '" << output << "'\n";
        return 1;
    }
    std::cout << "wrote " << output << " (" << seeds << " seeds, " << columns * cellW << "×" << rows * cellH << ")\n";
    return 0;
}

} // namespace

int ipsheetMain(int argc, char* argv[]) {
    std::string input;
    std::string output = "sheet.png";
    u32 seeds = 4, repeat = 1;
    bool explicitSeeds = false;
    u32 columns = 0; // 0 = seed count
    u32 firstSeed = 0;
    std::vector<std::pair<std::string, std::string>> overrides;
    // What each cell shows: the image, its normal image, or the image lit
    // from the upper left through the normal image (a review preview).
    // A material map (roughness, metallic, ao, emission) shows that image.
    enum class Shown { Colour, Normal, Lit, Map } shown = Shown::Colour;
    u32 mapKind = 0;
    bool deriveNormal = false;
    // Models (.iprop3d): every cell is a preview from a fixed camera.
    tools::PreviewOptions view;
    std::string modelOnly, imageOnly; // the first option given that fits one kind of file

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
            std::cout << "ipsheet - render a seed grid PNG\n\n"
                      << "Usage: ipsheet <file.iprop> [options]\n"
                      << "  --seed N       first seed (default 0)\n"
                      << "  --seeds N      seed count (default 4)\n"
                      << "  --repeat N     repeat each texture N by N (default one seed with --repeat)\n"
                      << "  --columns N    grid columns (default: the seed count)\n"
                      << "  --uniform NAME=VALUE  fixed input override (repeatable)\n"
                      << "  --image KIND   color (default), normal or lit: the image, its normal\n"
                      << "                 image, or the image lit from the upper left (a preview);\n"
                      << "                 roughness, metallic, ao or emission: that material map\n"
                      << "  --derive-normal  derive a normal image when the program publishes none\n"
                      << "  -o PATH        output PNG (default: sheet.png)\n"
                      << "A model (<file.iprop3d>) fills each cell with a preview and its seed:\n"
                      << "  --cell N       cell size in pixels (default 256)\n"
                      << "  --view YAW,PITCH  where the camera stands, in degrees (default 35,20)\n"
                      << "  --background COLOR  cell background, #RRGGBB or #RRGGBBAA\n";
            return 0;
        } else if (arg == "--seed") {
            if (!parseU32(next("--seed"), firstSeed)) {
                std::cerr << "ipsheet: bad --seed\n";
                return 1;
            }
        } else if (arg == "--uniform") {
            const std::string pair = next("--uniform");
            const auto equal = pair.find('=');
            if (equal == std::string::npos || equal == 0 || equal + 1 == pair.size()) {
                std::cerr << "ipsheet: bad --uniform (expected name=value)\n";
                return 1;
            }
            overrides.emplace_back(pair.substr(0, equal), pair.substr(equal + 1));
        } else if (arg == "--repeat") {
            if (!parseU32(next("--repeat"), repeat) || !repeat) {
                std::cerr << "ipsheet: --repeat needs a positive integer\n";
                return 1;
            }
            if (!explicitSeeds) seeds = 1;
            if (imageOnly.empty()) imageOnly = arg;
        } else if (arg == "--seeds") {
            explicitSeeds = true;
            if (!parseU32(next("--seeds"), seeds) || seeds == 0) {
                std::cerr << "ipsheet: bad --seeds\n";
                return 1;
            }
        } else if (arg == "--columns") {
            if (!parseU32(next("--columns"), columns)) {
                std::cerr << "ipsheet: bad --columns\n";
                return 1;
            }
        } else if (arg == "--cell") {
            u32 cell = 0;
            if (!parseU32(next("--cell"), cell) || cell < 16 || cell > 2048) {
                std::cerr << "ipsheet: bad --cell (expected 16..2048)\n";
                return 1;
            }
            view.width = view.height = cell;
            if (modelOnly.empty()) modelOnly = arg;
        } else if (arg == "--view") {
            if (!tools::parseView(next("--view"), view)) {
                std::cerr << "ipsheet: bad --view (expected YAW,PITCH in degrees)\n";
                return 1;
            }
            if (modelOnly.empty()) modelOnly = arg;
        } else if (arg == "--background") {
            const std::string text = next("--background");
            std::size_t end = 0;
            unsigned long long number = 0;
            try {
                if ((text.size() != 7 && text.size() != 9) || text[0] != '#') throw std::invalid_argument("colour");
                number = std::stoull(text.substr(1), &end, 16);
                if (end != text.size() - 1) throw std::invalid_argument("colour");
            } catch (const std::exception&) {
                std::cerr << "ipsheet: bad --background (expected #RRGGBB or #RRGGBBAA)\n";
                return 1;
            }
            const u32 rgba = static_cast<u32>(text.size() == 7 ? number << 8 | 255u : number);
            view.background = {static_cast<u8>(rgba >> 24), static_cast<u8>(rgba >> 16), static_cast<u8>(rgba >> 8), static_cast<u8>(rgba)};
            if (modelOnly.empty()) modelOnly = arg;
        } else if (arg == "--image") {
            if (imageOnly.empty()) imageOnly = arg;
            const std::string kind = next("--image");
            if (kind == "color") shown = Shown::Colour;
            else if (kind == "normal") shown = Shown::Normal;
            else if (kind == "lit") shown = Shown::Lit;
            else if (kind == "roughness" || kind == "metallic" || kind == "ao" || kind == "emission") {
                shown = Shown::Map;
                for (u32 k = 2; k <= kAuxImageKinds; ++k) if (kind == auxImageKindName(k)) mapKind = k;
            } else {
                std::cerr << "ipsheet: bad --image (expected color, normal, lit, roughness, metallic, ao or emission)\n";
                return 1;
            }
        } else if (arg == "--derive-normal") {
            deriveNormal = true;
            if (imageOnly.empty()) imageOnly = arg;
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
    if (tools::isModelFile(bytes)) {
        if (!imageOnly.empty()) {
            std::cerr << "ipsheet: " << imageOnly << " does not apply to a model\n";
            return 1;
        }
        return modelSheet(bytes, output, seeds, columns, firstSeed, overrides, view);
    }
    if (!modelOnly.empty()) {
        std::cerr << "ipsheet: " << modelOnly << " needs a model (.iprop3d)\n";
        return 1;
    }
    LoadedFile loaded;
    try {
        loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        std::cerr << "ipsheet: invalid .iprop at byte offset " << error.offset << ": "
                  << error.what() << "\n";
        return 1;
    }

    if (columns == 0) {
        columns = seeds;
    }
    const u32 rows = 1 + (seeds-1)/columns;
    const u64 tileWidth = static_cast<u64>(loaded.meta.sizeX)*repeat;
    const u64 tileHeight = static_cast<u64>(loaded.meta.sizeY)*repeat;
    // PNG dimensions have 31 usable bits. Guard each multiplication before
    // narrowing the dimensions or reserving the complete RGBA buffer.
    constexpr u32 maxSide = std::numeric_limits<i32>::max();
    if (!tileWidth || !tileHeight || tileWidth > maxSide / columns || tileHeight > maxSide / rows) {
        std::cerr << "ipsheet: sheet dimensions exceed the PNG format\n";
        return 1;
    }
    const u64 width = tileWidth * columns, height = tileHeight * rows;
    const u64 pixels = width * height;
    if (pixels > std::vector<u8>().max_size() / 4 || (repeat > 1 && pixels > 64u * 1024u * 1024u)) {
        std::cerr << "ipsheet: texture sheet is too large (repeat previews allow 64 megapixels)\n";
        return 1;
    }
    const u32 cellW = static_cast<u32>(tileWidth), cellH = static_cast<u32>(tileHeight);
    GenerationInputs inputs;
    try {
        inputs.uniforms = readOverrides(loaded, overrides);
    } catch (const std::exception& error) {
        std::cerr << "ipsheet: " << error.what() << "\n";
        return 1;
    }
    // Consecutive seeds in row-major order, with fixed declared inputs.
    std::vector<u8> rgba(
        static_cast<std::size_t>(columns) * cellW * rows * cellH * 4, 0);

    try {
        for (u32 s = 0; s < seeds; ++s) {
            inputs.seed = firstSeed + s;
            VM vm(loaded, inputs);
            if (deriveNormal) vm.deriveNormal({});
            vm.run();
            const GenerationResult generated = shown == Shown::Colour ? GenerationResult{} : vm.takeResult();
            const Canvas& canvas = shown == Shown::Colour ? vm.canvas() : generated.image;
            const Canvas* normal = nullptr;
            const Canvas* map = nullptr;
            for (const auto& image : generated.images) {
                if (image.kind == AuxImageKind::Normal && !normal) normal = &image.image;
                if (static_cast<u32>(image.kind) == mapKind && !map) map = &image.image;
            }
            if (shown == Shown::Map && !map) {
                std::cerr << "ipsheet: no " << auxImageKindName(mapKind) << " image: the program publishes none\n";
                return 1;
            }
            if ((shown == Shown::Normal || shown == Shown::Lit) && !normal) {
                std::cerr << "ipsheet: no normal image: the program publishes none; --derive-normal derives one\n";
                return 1;
            }
            const u32 cell = s;
            const u32 cellX = cell % columns;
            const u32 cellY = cell / columns;
            for (u32 y = 0; y < canvas.height; ++y) {
                for (u32 x = 0; x < canvas.width; ++x) {
                    Color pixel = canvas.at(x, y);
                    if (shown == Shown::Normal) pixel = normal->at(x, y);
                    else if (shown == Shown::Map) pixel = map->at(x, y);
                    else if (shown == Shown::Lit) {
                        // A fixed light from the upper left: ambient plus a
                        // Lambert term. A preview, not part of any contract.
                        const Color n = normal->at(x, y);
                        const double nx = n.r / 127.5 - 1.0, ny = n.g / 127.5 - 1.0, nz = n.b / 127.5 - 1.0;
                        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                        const double facing = length > 0 ? std::max(0.0, (nx * -0.55 + ny * 0.55 + nz * 0.63) / length) : 0.0;
                        const double light = 0.35 + 1.05 * facing;
                        const auto lit = [&](u8 channel) { return static_cast<u8>(std::min(255.0, channel * light + 0.5)); };
                        pixel = {lit(pixel.r), lit(pixel.g), lit(pixel.b), pixel.a};
                    }
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
            // Duplicate complete rows of the rendered tile, including alpha and
            // any selected material map. Only one VM runs per seed.
            const auto stride = static_cast<std::size_t>(columns) * cellW * 4;
            const auto rowBytes = static_cast<std::size_t>(canvas.width) * 4;
            const auto origin = static_cast<std::size_t>(cellY) * cellH * stride +
                                static_cast<std::size_t>(cellX) * cellW * 4;
            for (u32 y = 0; repeat > 1 && y < cellH; ++y) {
                const auto* source = rgba.data() + origin + (y % canvas.height)*stride;
                auto* row = rgba.data() + origin + y*stride;
                for (u32 col = y < canvas.height ? 1u : 0u; col < repeat; ++col)
                    std::copy_n(source, rowBytes, row + col*rowBytes);
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
    std::cout << "wrote " << output << " (" << seeds
              << " seeds, " << columns * cellW << "×" << rows * cellH << ")\n";
    return 0;
}

} // namespace iprop
