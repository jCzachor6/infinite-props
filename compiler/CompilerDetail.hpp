#pragma once

// Internal implementation of the .ips compiler, shared between
// Compiler.cpp (preparation and assembly), CompilerEmit.cpp (instruction
// emission) and CompilerFold.cpp (constant folding). Not part of the public
// API.

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "iprop/Binary.hpp"
#include "iprop/Code.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Value.hpp"
#include "ips/Ast.hpp"
#include "ips/Types.hpp"
#include "ips/Typing.hpp"

namespace iprop {
namespace detail {

// Language-AST names come in unqualified; ValueType/InputType/OutputKind
// clash with the runtime's, so those stay namespaced (ips::).
using ips::Arg;
using ips::Binary;
using ips::Call;
using ips::Expr;
using ips::ExprStmt;
using ips::Function;
using ips::Identifier;
using ips::IfStmt;
using ips::InputDecl;
using ips::Literal;
using ips::Member;
using ips::OutputDecl;
using ips::Program;
using ips::ReturnStmt;
using ips::SourceError;
using ips::SourceLocation;
using ips::Stmt;
using ips::Unary;
using ips::VarDecl;
using ips::VectorLit;
using ips::WhileStmt;
using ips::locationOf;

// ---- literal parsing (from the lexer's literal texts) ----------------------

inline bool parseIntBits(const std::string& text, u32& bits) {
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

inline bool parseFloatBits(const std::string& text, u32& bits) {
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

inline bool parseColor(const std::string& text, Color& out) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#') {
        return false;
    }
    const int count = text.size() == 7 ? 3 : 4;
    u8 channels[4] = {0, 0, 0, 255};
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < count; ++i) {
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

// ---- const pools (deduplicated, insertion-ordered) -------------------------

struct Pools {
    std::vector<std::string> strings;
    std::map<std::string, u32> stringIndex;
    std::vector<u32> ints;
    std::map<u32, u32> intIndex;
    std::vector<u32> floats;
    std::map<u32, u32> floatIndex;
    std::vector<Color> colors;
    std::map<u32, u32> colorIndex; // key: r | g<<8 | b<<16 | a<<24

    u32 addString(const std::string& text) {
        const auto found = stringIndex.find(text);
        if (found != stringIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(strings.size());
        strings.push_back(text);
        stringIndex.emplace(text, index);
        return index;
    }

    u32 addInt(u32 bits) {
        const auto found = intIndex.find(bits);
        if (found != intIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(ints.size());
        ints.push_back(bits);
        intIndex.emplace(bits, index);
        return index;
    }

    u32 addFloat(u32 bits) {
        const auto found = floatIndex.find(bits);
        if (found != floatIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(floats.size());
        floats.push_back(bits);
        floatIndex.emplace(bits, index);
        return index;
    }

    u32 addColor(Color color) {
        const u32 key = static_cast<u32>(color.r) |
                        (static_cast<u32>(color.g) << 8) |
                        (static_cast<u32>(color.b) << 16) |
                        (static_cast<u32>(color.a) << 24);
        const auto found = colorIndex.find(key);
        if (found != colorIndex.end()) {
            return found->second;
        }
        const u32 index = static_cast<u32>(colors.size());
        colors.push_back(color);
        colorIndex.emplace(key, index);
        return index;
    }
};

// ---- one compiled function --------------------------------------------------

struct FunctionInfo {
    std::string name;
    const Function* ast = nullptr;
    const Program* owner = nullptr;
    bool drawLike = false;
    std::vector<ValueType> params;  // runtime types
    std::vector<ValueType> results; // runtime types
    std::vector<ValueType> locals;  // extra locals beyond params
    std::optional<ips::ValueType> langResult;
    std::map<std::string, u32> slots;               // name -> local slot
    std::map<std::string, ips::ValueType> langTypes; // name -> language type
    std::vector<u8> code;
};

inline ips::ValueType inputValueType(ips::InputType type) {
    switch (type) {
    case ips::InputType::Color: return ips::ValueType::Color;
    case ips::InputType::F32: return ips::ValueType::F32;
    case ips::InputType::U32:
    case ips::InputType::I32: return ips::ValueType::I32;
    }
    return ips::ValueType::I32;
}

inline ValueType runtimeType(ips::ValueType type) {
    switch (type) {
    case ips::ValueType::I32: return ValueType::I32;
    case ips::ValueType::F32: return ValueType::F32;
    case ips::ValueType::Color: return ValueType::Color;
    default: return ValueType::I32; // compounds never enter the tables
    }
}

class Compiler {
public:
    Compiler(const Program& entry, const std::map<std::string, const Program*>& parts);

    std::vector<u8> run() {
        prepareAll();
        emitAll();
        return assemble();
    }

private:
    struct SlotInfo {
        ips::OutputKind kind;
        bool isList = false;
    };

    const Program& entry_;
    const std::map<std::string, const Program*>& parts_;

    Pools pools_;
    std::vector<const Program*> orderedParts_; // entry import order
    std::map<std::string, u32> partIndex_;
    std::vector<SlotInfo> rootSlots_;
    std::map<std::string, u32> rootSlotIndex_;
    std::vector<FunctionInfo> functions_;
    std::map<std::string, u32> functionIndex_;
    std::map<std::string, const Function*> registry_;
    std::map<std::string, bool> registryDrawLike_;

    // Part surfaces fed to the Typer (owned: the Typer keeps a reference).
    std::map<std::string, ips::PartInfo> surfaces_;
    // State of the function being prepared or emitted.
    ips::Typer typer_;
    const FunctionInfo* current_ = nullptr;
    std::set<std::string> preparing_;
    std::vector<u8> code_;
    bool scratchUsed_ = false;

    [[noreturn]] void error(const SourceLocation& at, std::string message) {
        throw SourceError{at, std::move(message)};
    }

    // ---- instruction emission (CompilerEmit.cpp) ---------------------------

    void ins(Opcode opcode, u8 aux = 0, u16 operand = 0);
    void emitConst(const Value& value);
    u32 scratch();

    void emitAll();
    bool isParam(const std::string& name) const;
    void emitStmt(const Stmt& stmt);
    u32 slotForOutput(const Program* owner, const OutputDecl& output) const;
    void emitOutputWrite(const Program* owner, const OutputDecl& output,
                         const Expr& value);

    ips::ValueType emitExpr(const Expr& expr);
    void emitPointValue(const Expr& expr);
    ips::ValueType emitPartRead(const Expr& expr);
    ips::ValueType emitCall(const Call& call);
    void emitArg(const Arg& arg, ips::ValueType expected);
    ips::ValueType emitBuiltin(const std::string& name, const Call& call);
    void emitPartCall(const Call& call);
    ips::ValueType emitBinary(const Binary& bin);
    ips::ValueType emitUnary(const Unary& un);

    struct PartRead {
        std::string alias;
        const OutputDecl* output = nullptr;
        ips::OutputKind kind = ips::OutputKind::Point;
    };
    PartRead partReadInfo(const Expr& expr) const;

    // ---- constant folding (CompilerFold.cpp) --------------------------------

    bool tryFold(const Expr& expr, Value& out) const;
    bool foldMixed(Binary::Op op, f32 a, f32 b, Value& out) const;

    // ---- preparation and assembly (Compiler.cpp) ----------------------------

    void prepareAll();
    void prepareFunction(const std::string& name);
    const InputDecl* ownerInput(const Program* owner,
                                const std::string& name) const;
    const OutputDecl* ownerOutput(const Program* owner,
                                  const std::string& name) const;
    f32 foldOrigin(const Expr& expr);
    void writeInputEntry(Writer& out, const InputDecl& input);
    std::vector<u8> assemble();
};

} // namespace detail
} // namespace iprop
