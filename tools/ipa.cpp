#include "ipa.hpp"

#include "iprop/Code.hpp"
#include "iprop/Consts.hpp"
#include "iprop/Format.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/Meta.hpp"
#include "iprop/Validator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace iprop {
namespace {

// ---------------------------------------------------------------------------
// .ipa text format:
//
//   ; comments
//   meta: size=64x64 origin=32,60 variants=8 archetypes=1 fuel=1000000
//   input: color skin = #d9b58c          (type: color|f32|u32)
//   output: point pupilCenter            (kind: point|rect|circle, opt. "list")
//   output: list rect colliders
//   part hair:                           (a part: meta/input/output/fn inside)
//     ...
//   end
//   fn draw ( i32, i32, color ) -> ( ):  (types: i32|f32|color)
//     locals: i32 f32                    (optional extra locals)
//     const.i32 5
//     const.f32 2.5
//     const.color #RRGGBBAA
//     local.get 2
//     call helper
//     part.draw hair
//     out.point pupil                   (slot names resolve to global indices)
//     poly 3                            (vertex count operand)
//     br 1                              (depth operand)
//     return
//   end
// ---------------------------------------------------------------------------

struct SourceError : std::runtime_error {
    SourceError(int line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message) {}
};

struct OpDef {
    Opcode opcode;
    OperandKind operand = OperandKind::None;
};

const OpDef* lookup(const std::string& mnemonic) {
    static const std::map<std::string, OpDef> table = {
        {"nop", {Opcode::Nop}},
        {"block", {Opcode::Block}},
        {"loop", {Opcode::Loop}},
        {"if", {Opcode::If}},
        {"else", {Opcode::Else}},
        {"end", {Opcode::End}},
        {"br", {Opcode::Br, OperandKind::Depth}},
        {"br_if", {Opcode::BrIf, OperandKind::Depth}},
        {"call", {Opcode::Call, OperandKind::Name}},
        {"return", {Opcode::Return}},

        {"const.i32", {Opcode::ConstI32, OperandKind::IntConst}},
        {"const.f32", {Opcode::ConstF32, OperandKind::FloatConst}},
        {"const.color", {Opcode::ConstColor, OperandKind::ColorConst}},
        {"local.get", {Opcode::LocalGet, OperandKind::Index}},
        {"local.set", {Opcode::LocalSet, OperandKind::Index}},
        {"local.tee", {Opcode::LocalTee, OperandKind::Index}},
        {"drop", {Opcode::Drop}},
        {"dup", {Opcode::Dup}},

        {"i32.add", {Opcode::I32Add}}, {"i32.sub", {Opcode::I32Sub}},
        {"i32.mul", {Opcode::I32Mul}}, {"i32.div_s", {Opcode::I32DivS}},
        {"i32.div_u", {Opcode::I32DivU}}, {"i32.rem_s", {Opcode::I32RemS}},
        {"i32.rem_u", {Opcode::I32RemU}}, {"i32.and", {Opcode::I32And}},
        {"i32.or", {Opcode::I32Or}}, {"i32.xor", {Opcode::I32Xor}},
        {"i32.shl", {Opcode::I32Shl}}, {"i32.shr_s", {Opcode::I32ShrS}},
        {"i32.shr_u", {Opcode::I32ShrU}}, {"i32.neg", {Opcode::I32Neg}},
        {"i32.not", {Opcode::I32Not}},
        {"i32.eq", {Opcode::I32Eq}}, {"i32.ne", {Opcode::I32Ne}},
        {"i32.lt_s", {Opcode::I32LtS}}, {"i32.lt_u", {Opcode::I32LtU}},
        {"i32.gt_s", {Opcode::I32GtS}}, {"i32.gt_u", {Opcode::I32GtU}},
        {"i32.le_s", {Opcode::I32LeS}}, {"i32.le_u", {Opcode::I32LeU}},
        {"i32.ge_s", {Opcode::I32GeS}}, {"i32.ge_u", {Opcode::I32GeU}},

        {"f32.add", {Opcode::F32Add}}, {"f32.sub", {Opcode::F32Sub}},
        {"f32.mul", {Opcode::F32Mul}}, {"f32.div", {Opcode::F32Div}},
        {"f32.eq", {Opcode::F32Eq}}, {"f32.ne", {Opcode::F32Ne}},
        {"f32.lt", {Opcode::F32Lt}}, {"f32.gt", {Opcode::F32Gt}},
        {"f32.le", {Opcode::F32Le}}, {"f32.ge", {Opcode::F32Ge}},
        {"f32.neg", {Opcode::F32Neg}}, {"f32.abs", {Opcode::F32Abs}},
        {"f32.sqrt", {Opcode::F32Sqrt}},
        {"f32.sin", {Opcode::F32Sin}}, {"f32.cos", {Opcode::F32Cos}},
        {"f32.floor", {Opcode::F32Floor}}, {"f32.ceil", {Opcode::F32Ceil}},
        {"f32.min", {Opcode::F32Min}}, {"f32.max", {Opcode::F32Max}},
        {"f32.pow", {Opcode::F32Pow}}, {"f32.exp", {Opcode::F32Exp}},
        {"f32.atan2", {Opcode::F32Atan2}},
        {"vec2.make", {Opcode::Vec2Make}}, {"vec2.add", {Opcode::Vec2Add}},
        {"vec2.sub", {Opcode::Vec2Sub}}, {"vec2.mul", {Opcode::Vec2Mul}},
        {"vec2.div", {Opcode::Vec2Div}}, {"vec2.neg", {Opcode::Vec2Neg}},
        {"vec2.len", {Opcode::Vec2Len}}, {"vec2.dot", {Opcode::Vec2Dot}},
        {"vec2.rot", {Opcode::Vec2Rot}}, {"vec2.x", {Opcode::Vec2X}},
        {"vec2.y", {Opcode::Vec2Y}},
        {"dist.circle", {Opcode::DistCircle}},
        {"dist.rect", {Opcode::DistRect}},
        {"dist.roundrect", {Opcode::DistRoundRect}},
        {"dist.line", {Opcode::DistLine}},
        {"dist.bezier", {Opcode::DistBezier}},
        {"dist.bezier.cubic", {Opcode::DistBezierCubic}},
        {"fill", {Opcode::Fill}},
        {"i32.to.f32", {Opcode::I32ToF32}}, {"f32.to.i32", {Opcode::F32ToI32}},

        {"color.mix", {Opcode::ColorMix}}, {"color.shade", {Opcode::ColorShade}},

        {"stream.push", {Opcode::StreamPush}}, {"stream.pop", {Opcode::StreamPop}},
        {"rng.unit", {Opcode::RngUnit}}, {"rng.range", {Opcode::RngRange}},
        {"rng.index", {Opcode::RngIndex}}, {"hash", {Opcode::Hash}},
        {"stream.state", {Opcode::StreamState}},

        {"noise", {Opcode::Noise}}, {"fault", {Opcode::Fault}},
        {"smooth", {Opcode::Smooth}}, {"lerp", {Opcode::Lerp}},

        {"pixel", {Opcode::Pixel}}, {"rect", {Opcode::Rect}},
        {"line", {Opcode::Line}}, {"poly", {Opcode::Poly, OperandKind::Count}},
        {"curve", {Opcode::Curve}}, {"curve.cubic", {Opcode::CurveCubic}},
        {"oval", {Opcode::Oval}}, {"weather", {Opcode::Weather}},

        {"out.point", {Opcode::OutPoint, OperandKind::Name}},
        {"out.rect", {Opcode::OutRect, OperandKind::Name}},
        {"out.circle", {Opcode::OutCircle, OperandKind::Name}},
        {"out.list.point", {Opcode::OutListPoint, OperandKind::Name}},
        {"out.list.rect", {Opcode::OutListRect, OperandKind::Name}},
        {"out.list.circle", {Opcode::OutListCircle, OperandKind::Name}},
        {"part.get.point", {Opcode::PartGetPoint, OperandKind::Name}},
        {"part.get.rect", {Opcode::PartGetRect, OperandKind::Name}},
        {"part.get.circle", {Opcode::PartGetCircle, OperandKind::Name}},

        {"part.draw", {Opcode::PartDraw, OperandKind::Name}},
    };
    const auto found = table.find(mnemonic);
    return found == table.end() ? nullptr : &found->second;
}

struct AsmInstruction {
    Opcode opcode;
    OperandKind operand = OperandKind::None;
    std::string operandText;
    int line = 0;
};

struct AsmFunction {
    std::string name;
    std::vector<ValueType> params;
    std::vector<ValueType> results;
    std::vector<ValueType> locals;
    std::vector<AsmInstruction> instructions;
};

struct AsmInput {
    InputType type;
    std::string name;
    std::string defaultValue;
};

struct AsmOutput {
    OutputKind kind;
    bool isList = false;
    std::string name;
};

struct AsmPart {
    std::string name;
    u32 sizeX = 0, sizeY = 0;
    f32 originX = 0.f, originY = 0.f;
    std::vector<AsmInput> inputs;
    std::vector<AsmOutput> outputs;
    AsmFunction function;
};

struct Program {
    u32 sizeX = 0, sizeY = 0;
    f32 originX = 0.f, originY = 0.f;
    u32 variants = 1, archetypes = 1;
    u64 fuel = 1000000;
    std::vector<AsmInput> inputs;
    std::vector<AsmOutput> outputs;
    std::vector<AsmFunction> functions;
    std::vector<AsmPart> parts;
};

std::vector<std::string> tokens(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> result;
    std::string token;
    while (stream >> token) {
        result.push_back(token);
    }
    return result;
}

// Removes '(' ')' ',' ':' from a token so signatures parse as plain type
// lists.
std::string clean(const std::string& token) {
    std::string result;
    for (const char c : token) {
        if (c != '(' && c != ')' && c != ',' && c != ':') {
            result.push_back(c);
        }
    }
    return result;
}

bool parseValueType(const std::string& text, ValueType& out) {
    if (text == "i32") {
        out = ValueType::I32;
        return true;
    }
    if (text == "f32") {
        out = ValueType::F32;
        return true;
    }
    if (text == "color") {
        out = ValueType::Color;
        return true;
    }
    if (text == "vec2") {
        out = ValueType::Vec2;
        return true;
    }
    return false;
}

bool parseInputType(const std::string& text, InputType& out) {
    if (text == "color") {
        out = InputType::Color;
        return true;
    }
    if (text == "f32") {
        out = InputType::F32;
        return true;
    }
    if (text == "u32") {
        out = InputType::U32;
        return true;
    }
    return false;
}

bool parseOutputKind(const std::string& text, OutputKind& out) {
    if (text == "point") {
        out = OutputKind::Point;
        return true;
    }
    if (text == "rect") {
        out = OutputKind::Rect;
        return true;
    }
    if (text == "circle") {
        out = OutputKind::Circle;
        return true;
    }
    return false;
}

bool parseSize(const std::string& text, u32& w, u32& h) {
    const auto x = text.find('x');
    if (x == std::string::npos) {
        return false;
    }
    try {
        w = static_cast<u32>(std::stoul(text.substr(0, x)));
        h = static_cast<u32>(std::stoul(text.substr(x + 1)));
    } catch (...) {
        return false;
    }
    return w > 0 && h > 0;
}

bool parseU32Text(const std::string& text, u32& out) {
    try {
        std::size_t used = 0;
        const unsigned long long value = std::stoull(text, &used, 0);
        if (used != text.size() || value > 0xFFFFFFFFull) {
            return false;
        }
        out = static_cast<u32>(value);
        return true;
    } catch (...) {
        return false;
    }
}

// Integer literal (decimal or 0x hex, optionally negative) → 32-bit pattern.
bool parseIntBits(const std::string& text, u32& bits) {
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used, 0);
        if (used != text.size()) {
            return false;
        }
        bits = static_cast<u32>(static_cast<unsigned long long>(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool parseFloatBits(const std::string& text, u32& bits) {
    try {
        std::size_t used = 0;
        const f32 value = std::stof(text, &used);
        if (used != text.size()) {
            return false;
        }
        std::memcpy(&bits, &value, sizeof(bits));
        return true;
    } catch (...) {
        return false;
    }
}

bool parseColor(const std::string& text, Color& out) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') {
        return false;
    }
    const int count = text.size() == 7 ? 3 : 4;
    u8 channels[4] = {0, 0, 0, 255};
    for (int i = 0; i < count; ++i) {
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = hex(text[1 + i * 2]);
        const int lo = hex(text[2 + i * 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        channels[i] = static_cast<u8>(hi * 16 + lo);
    }
    out = {channels[0], channels[1], channels[2], channels[3]};
    return true;
}

} // namespace

std::vector<u8> assemble(const std::string& source) {
    Program program;
    AsmPart* part = nullptr;
    AsmFunction* function = nullptr;
    int lineNumber = 0;

    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        ++lineNumber;
        // Strip comments.
        if (const auto comment = line.find(';'); comment != std::string::npos) {
            line.resize(comment);
        }
        auto words = tokens(line);
        if (words.empty()) {
            continue;
        }

        const auto error = [&](const std::string& message) -> SourceError {
            return SourceError(lineNumber, message);
        };

        if (words[0] == "meta:") {
            if (function != nullptr) {
                throw error("meta inside a function");
            }
            for (std::size_t i = 1; i < words.size(); ++i) {
                const auto eq = words[i].find('=');
                if (eq == std::string::npos) {
                    throw error("bad meta pair '" + words[i] + "'");
                }
                const std::string key = words[i].substr(0, eq);
                const std::string value = words[i].substr(eq + 1);
                if (part == nullptr) {
                    if (key == "size" && !parseSize(value, program.sizeX, program.sizeY)) {
                        throw error("bad size");
                    } else if (key == "origin") {
                        const auto comma = value.find(',');
                        if (comma == std::string::npos) {
                            throw error("bad origin");
                        }
                        try {
                            program.originX = std::stof(value.substr(0, comma));
                            program.originY = std::stof(value.substr(comma + 1));
                        } catch (...) {
                            throw error("bad origin");
                        }
                    } else if (key == "variants" && !parseU32Text(value, program.variants)) {
                        throw error("bad variants");
                    } else if (key == "archetypes" && !parseU32Text(value, program.archetypes)) {
                        throw error("bad archetypes");
                    } else if (key == "fuel") {
                        try {
                            program.fuel = std::stoull(value);
                        } catch (...) {
                            throw error("bad fuel");
                        }
                    } else if (key != "size" && key != "origin" && key != "variants" &&
                               key != "archetypes" && key != "fuel") {
                        throw error("unknown meta key '" + key + "'");
                    }
                } else {
                    if (key == "size") {
                        if (!parseSize(value, part->sizeX, part->sizeY)) {
                            throw error("bad size");
                        }
                    } else if (key == "origin") {
                        const auto comma = value.find(',');
                        if (comma == std::string::npos) {
                            throw error("bad origin");
                        }
                        try {
                            part->originX = std::stof(value.substr(0, comma));
                            part->originY = std::stof(value.substr(comma + 1));
                        } catch (...) {
                            throw error("bad origin");
                        }
                    } else {
                        throw error("unknown part meta key '" + key + "'");
                    }
                }
            }
            continue;
        }

        if (words[0] == "input:") {
            if (function != nullptr || words.size() != 5 || words[3] != "=") {
                throw error("bad input declaration (expected: input: <type> <name> = <default>)");
            }
            AsmInput input;
            if (!parseInputType(words[1], input.type)) {
                throw error("unknown input type '" + words[1] + "'");
            }
            input.name = words[2];
            input.defaultValue = words[4];
            (part != nullptr ? part->inputs : program.inputs).push_back(input);
            continue;
        }

        if (words[0] == "output:") {
            if (function != nullptr || words.size() < 3) {
                throw error("bad output declaration");
            }
            AsmOutput output;
            std::size_t at = 1;
            if (words[at] == "list") {
                output.isList = true;
                ++at;
            }
            if (at + 1 >= words.size() || !parseOutputKind(words[at], output.kind)) {
                throw error("bad output declaration");
            }
            output.name = words[at + 1];
            (part != nullptr ? part->outputs : program.outputs).push_back(output);
            continue;
        }

        if (words[0] == "part") {
            if (words.size() != 2 || function != nullptr || part != nullptr) {
                throw error("bad part declaration");
            }
            program.parts.push_back({});
            part = &program.parts.back();
            part->name = clean(words[1]);
            continue;
        }

        if (words[0] == "endfn") {
            if (function == nullptr) {
                throw error("endfn without an open function");
            }
            function = nullptr;
            continue;
        }
        if (words[0] == "endpart") {
            if (function != nullptr) {
                throw error("endpart inside a function");
            }
            if (part == nullptr) {
                throw error("endpart without an open part");
            }
            part = nullptr;
            continue;
        }

        if (words[0] == "fn") {
            if (function != nullptr) {
                throw error("nested function");
            }
            // fn name ( types ) -> ( types )
            AsmFunction parsed;
            parsed.name = words[1];
            // Signature tokens have punctuation stripped; empty tokens from
            // the parens are dropped.
            std::vector<std::string> sig;
            for (std::size_t i = 2; i < words.size(); ++i) {
                const std::string cleaned = clean(words[i]);
                if (!cleaned.empty()) {
                    sig.push_back(cleaned);
                }
            }
            const auto arrow = std::find(sig.begin(), sig.end(), "->");
            if (arrow == sig.end()) {
                throw error("missing -> in signature");
            }
            for (auto it = sig.begin(); it != arrow; ++it) {
                ValueType type;
                if (!parseValueType(*it, type)) {
                    throw error("unknown type '" + *it + "'");
                }
                parsed.params.push_back(type);
            }
            for (auto it = arrow + 1; it != sig.end(); ++it) {
                ValueType type;
                if (!parseValueType(*it, type)) {
                    throw error("unknown type '" + *it + "'");
                }
                parsed.results.push_back(type);
            }
            if (part != nullptr) {
                part->function = std::move(parsed);
                function = &part->function;
            } else {
                program.functions.push_back(std::move(parsed));
                function = &program.functions.back();
            }
            continue;
        }

        if (words[0] == "locals:") {
            if (function == nullptr) {
                throw error("locals outside a function");
            }
            for (std::size_t i = 1; i < words.size(); ++i) {
                ValueType type;
                if (!parseValueType(words[i], type)) {
                    throw error("unknown local type '" + words[i] + "'");
                }
                function->locals.push_back(type);
            }
            continue;
        }

        // Otherwise: an instruction.
        if (function == nullptr) {
            throw error("instruction outside a function: '" + words[0] + "'");
        }
        const OpDef* def = lookup(words[0]);
        if (def == nullptr) {
            throw error("unknown instruction '" + words[0] + "'");
        }
        AsmInstruction instruction;
        instruction.opcode = def->opcode;
        instruction.operand = def->operand;
        instruction.line = lineNumber;
        if (def->operand != OperandKind::None) {
            if (words.size() != 2) {
                throw error("instruction '" + words[0] + "' expects one operand");
            }
            instruction.operandText = words[1];
        } else if (words.size() != 1) {
            throw error("instruction '" + words[0] + "' takes no operand");
        }
        function->instructions.push_back(std::move(instruction));
    }

    if (function != nullptr || part != nullptr) {
        throw SourceError(lineNumber, "unclosed function or part");
    }
    if (program.functions.empty()) {
        throw SourceError(0, "no draw function");
    }
    {
        const bool hasDraw = std::any_of(program.functions.begin(), program.functions.end(),
                                         [](const AsmFunction& fn) { return fn.name == "draw"; });
        if (!hasDraw) {
            throw SourceError(0, "no function named 'draw'");
        }
    }
    if (program.sizeX == 0 || program.sizeY == 0) {
        throw SourceError(0, "missing meta size");
    }

    // ---- Constant pools: dedupe strings, ints, floats, colours ----
    std::vector<std::string> strings;
    std::map<std::string, u32> stringIndex;
    const auto addString = [&](const std::string& text) -> u32 {
        const auto found = stringIndex.find(text);
        if (found != stringIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(strings.size());
        strings.push_back(text);
        stringIndex[text] = index;
        return index;
    };

    std::vector<u32> intBits;
    std::map<u32, u32> intIndex;
    const auto addInt = [&](u32 bits) -> u32 {
        const auto found = intIndex.find(bits);
        if (found != intIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(intBits.size());
        intBits.push_back(bits);
        intIndex[bits] = index;
        return index;
    };
    std::vector<u32> floatBits;
    std::map<u32, u32> floatIndex;
    const auto addFloat = [&](u32 bits) -> u32 {
        const auto found = floatIndex.find(bits);
        if (found != floatIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(floatBits.size());
        floatBits.push_back(bits);
        floatIndex[bits] = index;
        return index;
    };
    std::vector<Color> colors;
    std::map<std::array<u8, 4>, u32> colorIndex;
    const auto addColor = [&](const Color& color) -> u32 {
        const std::array<u8, 4> key = {color.r, color.g, color.b, color.a};
        const auto found = colorIndex.find(key);
        if (found != colorIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(colors.size());
        colors.push_back(color);
        colorIndex[key] = index;
        return index;
    };

    // Names that must exist in the string pool: function names, input names,
    // output names.
    for (const auto& fn : program.functions) {
        addString(fn.name);
    }
    std::map<std::string, u32> partIndex;
    for (std::size_t i = 0; i < program.parts.size(); ++i) {
        partIndex[program.parts[i].name] = static_cast<u32>(i);
        addString(program.parts[i].function.name);
    }

    // Global slot numbering (spec §4.3): root outputs first, then parts.
    std::map<std::string, u32> slotIndex;
    std::size_t slotCount = 0;
    for (const auto& output : program.outputs) {
        slotIndex[output.name] = static_cast<u32>(slotCount++);
        addString(output.name);
    }
    for (const auto& part : program.parts) {
        for (const auto& output : part.outputs) {
            slotIndex[output.name] = static_cast<u32>(slotCount++);
            addString(output.name);
        }
    }
    for (const auto& input : program.inputs) {
        addString(input.name);
    }
    for (const auto& part : program.parts) {
        for (const auto& input : part.inputs) {
            addString(input.name);
        }
    }

    // ---- Emit ----
    Writer meta;
    meta.writeU32(program.sizeX);
    meta.writeU32(program.sizeY);
    meta.writeF32(program.originX);
    meta.writeF32(program.originY);
    meta.writeU32(program.variants);
    meta.writeU32(program.archetypes);
    meta.writeU64(program.fuel);
    meta.writeU16(0); // no emission in the assembler v1
    for (int i = 0; i < 6; ++i) {
        meta.writeU8(0);
    }
    meta.writeU16(static_cast<u16>(program.inputs.size()));
    for (const auto& input : program.inputs) {
        meta.writeU8(static_cast<u8>(input.type));
        meta.writeU32(stringIndex[input.name]);
        switch (input.type) {
        case InputType::Color: {
            Color color;
            if (!parseColor(input.defaultValue, color)) {
                throw SourceError(0, "bad colour default for input '" + input.name + "'");
            }
            meta.writeColor(color);
            break;
        }
        case InputType::F32: {
            u32 bits = 0;
            if (!parseFloatBits(input.defaultValue, bits)) {
                throw SourceError(0, "bad f32 default for input '" + input.name + "'");
            }
            meta.writeU32(bits);
            break;
        }
        case InputType::U32: {
            u32 value = 0;
            if (!parseU32Text(input.defaultValue, value)) {
                throw SourceError(0, "bad u32 default for input '" + input.name + "'");
            }
            meta.writeU32(value);
            break;
        }
        }
    }
    meta.writeU16(static_cast<u16>(program.outputs.size()));
    for (const auto& output : program.outputs) {
        meta.writeU8(static_cast<u8>(output.kind));
        meta.writeU8(output.isList ? 1 : 0);
        meta.writeU8(0);
        meta.writeU8(0);
        meta.writeU32(stringIndex[output.name]);
    }

    // Resolve an instruction operand to a u16 index.
    std::map<std::string, u32> functionIndex; // populated once `ordered` exists
    const auto resolve = [&](const AsmInstruction& instruction) -> u16 {
        switch (instruction.operand) {
        case OperandKind::None: return 0;
        case OperandKind::IntConst: {
            u32 bits = 0;
            if (!parseIntBits(instruction.operandText, bits)) {
                throw SourceError(instruction.line, "bad integer literal");
            }
            return static_cast<u16>(addInt(bits));
        }
        case OperandKind::FloatConst: {
            u32 bits = 0;
            if (!parseFloatBits(instruction.operandText, bits)) {
                throw SourceError(instruction.line, "bad float literal");
            }
            return static_cast<u16>(addFloat(bits));
        }
        case OperandKind::ColorConst: {
            Color color;
            if (!parseColor(instruction.operandText, color)) {
                throw SourceError(instruction.line, "bad colour literal");
            }
            return static_cast<u16>(addColor(color));
        }
        case OperandKind::Index:
        case OperandKind::Depth:
        case OperandKind::Count: {
            u32 value = 0;
            if (!parseU32Text(instruction.operandText, value)) {
                throw SourceError(instruction.line, "bad index operand");
            }
            return static_cast<u16>(value);
        }
        case OperandKind::Name: {
            const auto& name = instruction.operandText;
            if (instruction.opcode == Opcode::Call) {
                const auto found = functionIndex.find(name);
                if (found == functionIndex.end()) {
                    throw SourceError(instruction.line, "unknown function '" + name + "'");
                }
                return static_cast<u16>(found->second);
            }
            if (instruction.opcode == Opcode::PartDraw) {
                const auto found = partIndex.find(name);
                if (found == partIndex.end()) {
                    throw SourceError(instruction.line, "unknown part '" + name + "'");
                }
                return static_cast<u16>(found->second);
            }
            const auto found = slotIndex.find(name);
            if (found == slotIndex.end()) {
                throw SourceError(instruction.line, "unknown output slot '" + name + "'");
            }
            return static_cast<u16>(found->second);
        }
        }
        return 0;
    };

    // Functions in index order: draw first, then the remaining root
    // functions, then the part functions (spec §6.1: function 0 is draw).
    std::vector<const AsmFunction*> ordered;
    for (const auto& fn : program.functions) {
        if (fn.name == "draw") {
            ordered.push_back(&fn);
        }
    }
    for (const auto& fn : program.functions) {
        if (fn.name != "draw") {
            ordered.push_back(&fn);
        }
    }
    for (const auto& part : program.parts) {
        ordered.push_back(&part.function);
    }
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        functionIndex[ordered[i]->name] = static_cast<u32>(i);
    }

    // Resolve every operand first: the const pools must be complete when
    // the const section is written.
    std::vector<std::vector<u16>> operands(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        for (const auto& instruction : ordered[i]->instructions) {
            operands[i].push_back(resolve(instruction));
        }
    }

    Writer consts;
    consts.writeU32(static_cast<u32>(strings.size()));
    for (const auto& text : strings) {
        consts.writeU32(static_cast<u32>(text.size()));
        consts.writeBytes({reinterpret_cast<const u8*>(text.data()), text.size()});
    }
    consts.writeU32(static_cast<u32>(intBits.size()));
    for (const u32 bits : intBits) {
        consts.writeU32(bits);
    }
    consts.writeU32(static_cast<u32>(floatBits.size()));
    for (const u32 bits : floatBits) {
        consts.writeU32(bits);
    }
    consts.writeU32(static_cast<u32>(colors.size()));
    for (const Color& color : colors) {
        consts.writeColor(color);
    }


    // Function table + streams.
    Writer table;
    Writer streams;
    const u32 functionCount = static_cast<u32>(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const AsmFunction& fn = *ordered[i];
        table.writeU32(stringIndex.at(fn.name));
        table.writeU32(streams.size());
        table.writeU32(static_cast<u32>(fn.instructions.size()) * 4);
        table.writeU16(static_cast<u16>(fn.params.size()));
        for (const ValueType type : fn.params) {
            table.writeU8(static_cast<u8>(type));
        }
        table.writeU16(static_cast<u16>(fn.results.size()));
        for (const ValueType type : fn.results) {
            table.writeU8(static_cast<u8>(type));
        }
        table.writeU16(static_cast<u16>(fn.locals.size()));
        for (const ValueType type : fn.locals) {
            table.writeU8(static_cast<u8>(type));
        }
        for (std::size_t j = 0; j < fn.instructions.size(); ++j) {
            streams.writeU8(static_cast<u8>(fn.instructions[j].opcode));
            streams.writeU8(0); // aux
            streams.writeU16(operands[i][j]);
        }
    }

    Writer code;
    code.writeU32(functionCount);
    code.writeBytes(table.data());
    code.writeBytes(streams.data());
    code.writeU32(static_cast<u32>(program.parts.size()));
    // Recompute part function indices: root functions first.
    for (std::size_t i = 0; i < program.parts.size(); ++i) {
        const u32 partFunctionIndex =
            static_cast<u32>(program.functions.size() + i);
        code.writeU32(partFunctionIndex);
        const AsmPart& part = program.parts[i];
        code.writeU32(part.sizeX);
        code.writeU32(part.sizeY);
        code.writeF32(part.originX);
        code.writeF32(part.originY);
        code.writeU16(static_cast<u16>(part.inputs.size()));
        for (const auto& input : part.inputs) {
            code.writeU8(static_cast<u8>(input.type));
            code.writeU32(stringIndex[input.name]);
            switch (input.type) {
            case InputType::Color: {
                Color color;
                if (!parseColor(input.defaultValue, color)) {
                    throw SourceError(0, "bad colour default for part input '" + input.name + "'");
                }
                code.writeColor(color);
                break;
            }
            case InputType::F32: {
                u32 bits = 0;
                if (!parseFloatBits(input.defaultValue, bits)) {
                    throw SourceError(0, "bad f32 default for part input '" + input.name + "'");
                }
                code.writeU32(bits);
                break;
            }
            case InputType::U32: {
                u32 value = 0;
                if (!parseU32Text(input.defaultValue, value)) {
                    throw SourceError(0, "bad u32 default for part input '" + input.name + "'");
                }
                code.writeU32(value);
                break;
            }
            }
        }
        code.writeU16(static_cast<u16>(part.outputs.size()));
        for (const auto& output : part.outputs) {
            code.writeU8(static_cast<u8>(output.kind));
            code.writeU8(output.isList ? 1 : 0);
            code.writeU8(0);
            code.writeU8(0);
            code.writeU32(stringIndex[output.name]);
        }
    }

    // ---- Assemble the file ----
    Writer file;
    for (const u8 magic : kMagic) {
        file.writeU8(magic);
    }
    file.writeU16(kVersionMajor);
    file.writeU16(kVersionMinor);
    file.writeU32(0);
    file.writeU32(3);
    file.writeU32(0);
    u32 offset = kHeaderSize + 3 * 12;
    const auto section = [&](u32 id, const std::vector<u8>& bytes) {
        file.writeU32(id);
        file.writeU32(offset);
        file.writeU32(static_cast<u32>(bytes.size()));
        offset += static_cast<u32>(bytes.size());
    };
    section(1, meta.data());
    section(2, consts.data());
    section(3, code.data());
    file.writeBytes(meta.data());
    file.writeBytes(consts.data());
    file.writeBytes(code.data());

    const auto bytes = file.data();

    // Dogfood: the assembled file must pass the runtime validator.
    try {
        const auto loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        throw SourceError(0, std::string("assembled program failed validation at byte ") +
                                 std::to_string(error.offset) + ": " + error.what());
    }
    return bytes;
}

int ipaMain(int argc, char* argv[]) {
    std::string input;
    std::string output;
    bool help = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            help = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "ipa: missing value for " << arg << "\n";
                return 1;
            }
            output = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "ipa: unknown option '" << arg << "'\n";
            return 1;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "ipa: multiple input files\n";
            return 1;
        }
    }
    if (help) {
        std::cout << "ipa - assemble .ipa text into .iprop binary\n\n"
                  << "Usage: ipa <file.ipa> -o <out.iprop>\n";
        return 0;
    }
    if (input.empty()) {
        std::cerr << "ipa: no input file\n";
        return 1;
    }
    if (output.empty()) {
        output = input.substr(0, input.rfind('.')) + ".iprop";
    }

    std::ifstream stream(input);
    if (!stream) {
        std::cerr << "ipa: cannot open '" << input << "'\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(stream)), {});
    try {
        const auto bytes = assemble(source);
        std::ofstream out(output, std::ios::binary);
        if (!out) {
            std::cerr << "ipa: cannot write '" << output << "'\n";
            return 1;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    } catch (const std::exception& error) {
        std::cerr << "ipa: " << error.what() << "\n";
        return 1;
    }
    std::cout << "assembled " << input << " -> " << output << "\n";
    return 0;
}

} // namespace iprop
