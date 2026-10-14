#include "ipdis.hpp"

#include "iprop/Code.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Consts.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/Meta.hpp"
#include "iprop/Validator.hpp"

#include <cstdio>
#include <bit>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace iprop {
namespace {

const char* typeName(ValueType type) { return valueTypeName(type); }

std::string floatText(f32 value) {
    char buffer[32];
    const u32 bits = std::bit_cast<u32>(value);
    const u32 exponent = bits & 0x7f800000u;
    if (exponent == 0x7f800000u || (exponent == 0 && (bits & 0x7fffffu) != 0)) {
        // Decimal text cannot retain NaN payloads; source literal parsing
        // also rejects subnormals. Assembly is a lossless binary transport.
        std::snprintf(buffer, sizeof(buffer), "bits:0x%08x", bits);
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

std::string colorText(Color color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", color.r, color.g,
                  color.b, color.a);
    return buffer;
}

// Comma-separated for the fn signature (the assembler strips punctuation);
// space-separated for the locals line (parsed on whitespace).
std::string typeList(const std::vector<ValueType>& types, bool commas) {
    std::string result;
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (i != 0) {
            result += commas ? ", " : " ";
        }
        result += typeName(types[i]);
    }
    return result;
}

std::string inputEntryText(const InputDecl& input, const ConstPool& pool) {
    const std::string name = pool.strings.at(input.nameIndex);
    switch (input.type) {
    case InputType::Color:
        return "input: color " + name + " = " + colorText(input.colorDefault);
    case InputType::F32:
        return "input: f32 " + name + " = " + floatText(input.f32Default);
    case InputType::U32:
        return "input: u32 " + name + " = " + std::to_string(input.u32Default);
    }
    return {};
}

std::string outputEntryText(const OutputDecl& output, const ConstPool& pool) {
    std::string kind;
    switch (output.kind) {
    case OutputKind::Point: kind = "point"; break;
    case OutputKind::Rect: kind = "rect"; break;
    case OutputKind::Circle: kind = "circle"; break;
    }
    std::string result = "output: ";
    if (output.isList) {
        result += "list ";
    }
    result += kind + " " + pool.strings.at(output.nameIndex);
    return result;
}

// One instruction line, with the operand resolved against the pools and the
// name tables.
struct Names {
    const ConstPool* pool = nullptr;
    std::vector<std::string> functions; // index -> name
    std::vector<std::string> parts;     // index -> name
    std::vector<std::string> slots;     // global slot index -> name
};

std::string instructionText(const Function& function,
                            std::size_t instructionIndex, const Names& names) {
    const Instruction ins = decodeInstruction(
        function.code.subspan(instructionIndex * 4, 4));
    const char* mnemonic = opcodeMnemonic(ins.opcode);
    if (mnemonic == nullptr) {
        return "reserved." + std::to_string(static_cast<u32>(ins.opcode));
    }
    switch (operandKind(ins.opcode)) {
    case OperandKind::None:
        return mnemonic;
    case OperandKind::IntConst:
        return std::string(mnemonic) + " " +
               std::to_string(static_cast<i32>(names.pool->ints.at(ins.operand)));
    case OperandKind::FloatConst:
        // The pool stores f32 values, not bit patterns.
        return std::string(mnemonic) + " " +
               floatText(names.pool->floats.at(ins.operand));
    case OperandKind::ColorConst:
        return std::string(mnemonic) + " " +
               colorText(names.pool->colors.at(ins.operand));
    case OperandKind::Index:
    case OperandKind::Depth:
    case OperandKind::Count:
        return std::string(mnemonic) + " " + std::to_string(ins.operand) +
            (ins.opcode == Opcode::LayerBegin && ins.aux ? " " + std::to_string(ins.aux) : "");
    case OperandKind::Name:
        if (ins.opcode == Opcode::Call) {
            return std::string(mnemonic) + " " + names.functions.at(ins.operand);
        }
        if (ins.opcode == Opcode::PartDraw) {
            const std::string text = std::string(mnemonic) + " " + names.parts.at(ins.operand);
            return ins.aux == 0 ? text : text + " " + std::to_string(ins.aux);
        }
        return std::string(mnemonic) + " " + names.slots.at(ins.operand);
    }
    return mnemonic;
}

// The assembled name list for one function's locals (index -> name).
std::vector<const std::string*> localNames(const Function& function,
                                           const DebugFunction& debug,
                                           const ConstPool& pool) {
    std::vector<const std::string*> names;
    for (const u32 index : debug.localNames) {
        names.push_back(index == kAnonymousName ? nullptr : &pool.strings.at(index));
    }
    names.resize(function.params.size() + function.locals.size(), nullptr);
    return names;
}

void appendFunction(std::ostringstream& out, const std::string& name,
                    const Function& function, const Names& names,
                    bool indent, const DebugFunction* debug) {
    const auto parens = [](const std::vector<ValueType>& types) {
        return types.empty() ? std::string("( )")
                             : "( " + typeList(types, true) + " )";
    };
    const std::string pad = indent ? "  " : "";
    out << pad << "fn " << name << " " << parens(function.params) << " -> "
        << parens(function.results) << ":\n";
    if (!function.locals.empty()) {
        out << pad << "  locals: " << typeList(function.locals, false) << "\n";
        if (debug != nullptr) {
            const auto namesInOrder =
                localNames(function, *debug, *names.pool);
            out << pad << "  ; locals:";
            for (std::size_t i = function.params.size(); i < namesInOrder.size(); ++i) {
                const auto* local = namesInOrder[i];
                out << " " << (local != nullptr ? *local : "<scratch>");
            }
            out << "\n";
        }
    }
    const std::size_t count = function.code.size() / 4;
    for (std::size_t i = 0; i < count; ++i) {
        if (debug != nullptr && i < debug->lines.size() &&
            (i == 0 || debug->lines[i] != debug->lines[i - 1])) {
            out << pad << "  ; line " << debug->lines[i] << "\n";
        }
        out << pad << "  " << instructionText(function, i, names) << "\n";
    }
    out << pad << "endfn\n";
}

} // namespace



std::string disassembleImpl(std::span<const u8> bytes, bool annotated) {
    const auto loaded = loadFile(bytes);
    validate(loaded);

    const ConstPool& pool = loaded.pool;
    const AssetMeta& meta = loaded.meta;
    const CodeSection& code = loaded.code;

    // Function names (anonymous functions get stable synthetic names).
    Names names;
    names.pool = &pool;
    for (std::size_t i = 0; i < code.functions.size(); ++i) {
        if (code.functions[i].isAnonymous()) {
            names.functions.push_back("fn" + std::to_string(i));
        } else {
            names.functions.push_back(pool.strings.at(code.functions[i].nameIndex));
        }
    }

    // Part names: the v0.5 instance names, else the alias prefix of their
    // draw function's name (an alias path for nested parts).
    for (std::size_t i = 0; i < code.parts.size(); ++i) {
        if (code.parts[i].nameIndex) {
            names.parts.push_back(pool.strings.at(*code.parts[i].nameIndex));
            continue;
        }
        const std::string fnName = names.functions.at(code.parts[i].functionIndex);
        const auto dot = fnName.rfind('.');
        if (dot == std::string::npos) {
            names.parts.push_back("part" + std::to_string(i));
        } else {
            names.parts.push_back(fnName.substr(0, dot));
        }
    }

    // Global slot names: root outputs first, then part outputs (spec §4.3).
    // A part slot whose name is not unique is written `part.output`.
    std::map<std::string, int> uses;
    for (const auto& output : meta.outputs) uses[pool.strings.at(output.nameIndex)] += 1;
    for (const auto& part : code.parts)
        for (const auto& output : part.outputs) uses[pool.strings.at(output.nameIndex)] += 1;
    for (const auto& output : meta.outputs) {
        names.slots.push_back(pool.strings.at(output.nameIndex));
    }
    for (std::size_t p = 0; p < code.parts.size(); ++p) {
        for (const auto& output : code.parts[p].outputs) {
            const auto& name = pool.strings.at(output.nameIndex);
            names.slots.push_back(uses[name] > 1 ? names.parts[p] + "." + name : name);
        }
    }

    std::ostringstream out;
    out << "meta: size=" << meta.sizeX << "x" << meta.sizeY
        << " origin=" << floatText(meta.origin.x) << "," << floatText(meta.origin.y)
        << " fuel=" << meta.fuel << "\n";
    for (const auto& input : meta.inputs) {
        out << inputEntryText(input, pool) << "\n";
    }
    for (const auto& output : meta.outputs) {
        out << outputEntryText(output, pool) << "\n";
    }
    const auto ownerText = [](u32 owner) { return owner == kRootParent ? std::string("root") : std::to_string(owner); };
    for (const auto& output : loaded.resourceExports)
        out << "export: " << valueTypeName(output.type) << ' ' << pool.strings.at(output.nameIndex)
            << ' ' << ownerText(output.owner) << '\n';
    for (u32 f = 0; f < loaded.functionOwners.size(); ++f)
        out << "owner: " << names.functions[f] << ' ' << ownerText(loaded.functionOwners[f]) << '\n';

    if (!loaded.collections.types.empty()) {
        out << "types:";
        for (const auto type : loaded.collections.types) out << ' ' << valueTypeName(type);
        out << '\n';
    }
    for (const auto& d : loaded.collections.distributions) {
        Writer data; writePointDistribution(data, *d.points);
        out << "distribution: ";
        constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : data.data()) out << digits[byte >> 4] << digits[byte & 15];
        out << '\n';
    }
    for (const auto& list : loaded.collections.lists) {
        Writer data; writeListElements(data, *list.data, loaded.collections.samplers, loaded.collections.distributions);
        out << "collection: " << valueTypeName(list.type) << ' ';
        constexpr char digits[] = "0123456789abcdef";
        if (data.data().empty()) out << '-';
        for (const auto byte : data.data()) out << digits[byte >> 4] << digits[byte & 15];
        out << '\n';
    }
    for (const auto& sampler : loaded.collections.samplers) {
        Writer data; writeSamplerData(data, *sampler.data, loaded.collections.distributions);
        out << "sampler: " << valueTypeName(sampler.type) << ' ';
        constexpr char digits[] = "0123456789abcdef";
        for (const auto byte : data.data()) out << digits[byte >> 4] << digits[byte & 15];
        out << '\n';
    }

    // The debug source map, when requested (one entry per function).
    const DebugInfo* debugInfo = nullptr;
    DebugInfo debugStorage;
    if (annotated) {
        if (const Section* debugSection = loaded.layout.find(SectionId::Debug)) {
            debugStorage = parseDebug(loaded.bytes(), *debugSection);
            debugInfo = &debugStorage;
        }
    }

    // Part blocks first (the assembler grammar wants parts before functions).
    for (std::size_t i = 0; i < code.parts.size(); ++i) {
        const Part& part = code.parts[i];
        const u32 functionIndex = part.functionIndex;
        const DebugFunction* debug = nullptr;
        if (debugInfo != nullptr && debugInfo->functions.size() == code.functions.size()) {
            debug = &debugInfo->functions[functionIndex];
        }
        out << "part " << names.parts[i];
        if (part.nameIndex) {
            out << " instance";
            if (part.parent != kRootParent) out << " " << names.parts.at(part.parent);
        }
        out << ":\n";
        out << "  meta: size=" << part.sizeX << "x" << part.sizeY
            << " origin=" << floatText(part.origin.x) << ","
            << floatText(part.origin.y) << "\n";
        for (const auto& input : part.inputs) {
            out << "  " << inputEntryText(input, pool) << "\n";
        }
        for (const auto& output : part.outputs) {
            out << "  " << outputEntryText(output, pool) << "\n";
        }
        appendFunction(out, loaded.resourceExports.empty() ? "draw" : names.functions[functionIndex], code.functions[functionIndex], names, true,
                       debug);
        out << "endpart\n";
    }

    // Root functions (those the part table does not reference).
    std::set<u32> partFunctions;
    for (const auto& part : code.parts) {
        partFunctions.insert(part.functionIndex);
    }
    for (std::size_t i = 0; i < code.functions.size(); ++i) {
        if (partFunctions.count(static_cast<u32>(i)) == 0) {
            const DebugFunction* debug = nullptr;
            if (debugInfo != nullptr && debugInfo->functions.size() == code.functions.size()) {
                debug = &debugInfo->functions[i];
            }
            appendFunction(out, names.functions[i], code.functions[i], names,
                           false, debug);
        }
    }
    return out.str();
}

std::string disassembleWithSource(std::span<const u8> bytes) {
    return disassembleImpl(bytes, /*annotated=*/true);
}

std::string disassemble(std::span<const u8> bytes) {
    return disassembleImpl(bytes, /*annotated=*/false);
}

int ipdisMain(int argc, char* argv[]) {
    std::string input;
    bool source = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "ipdis - disassemble an .iprop binary into .ipa text\n\n"
                      << "Usage: ipdis [--source] <file.iprop>\n"
                      << "The output can be reassembled with ipa; --source\n"
                      << "annotates it with the debug section's source map.\n";
            return 0;
        }
        if (arg == "--source") {
            source = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "ipdis: unknown option '" << arg << "'\n";
            return 2;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "ipdis: multiple input files\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << "ipdis: no input file\n";
        return 2;
    }
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        std::cerr << "ipdis: cannot open '" << input << "'\n";
        return 2;
    }
    const std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), {});
    try {
        std::cout << (source ? disassembleWithSource(bytes) : disassemble(bytes));
    } catch (const FormatError& error) {
        std::cerr << "ipdis: " << input << ": byte " << error.offset << ": "
                  << error.what() << "\n";
        return 1;
    }
    return 0;
}

} // namespace iprop
