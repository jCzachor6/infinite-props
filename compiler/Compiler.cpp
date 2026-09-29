#include "Compiler.hpp"

#include <cstring>
#include <optional>
#include <set>
#include <utility>

#include "iprop/Binary.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Validator.hpp"
#include "iprop/Value.hpp"
#include "ips/Types.hpp"

namespace iprop {
namespace {

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
using ips::promote;

// ---- literal parsing (from the lexer's literal texts) ----------------------

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

// ---- const pools ------------------------------------------------------------

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

// ---- small type helpers -------------------------------------------------------

ValueType runtimeType(ips::ValueType type) {
    switch (type) {
    case ips::ValueType::I32: return ValueType::I32;
    case ips::ValueType::F32: return ValueType::F32;
    case ips::ValueType::Color: return ValueType::Color;
    default: return ValueType::I32; // compounds never enter the tables
    }
}

ips::ValueType inputValueType(ips::InputType type) {
    switch (type) {
    case ips::InputType::Color: return ips::ValueType::Color;
    case ips::InputType::F32: return ips::ValueType::F32;
    case ips::InputType::U32:
    case ips::InputType::I32: return ips::ValueType::I32;
    }
    return ips::ValueType::I32;
}

ips::ValueType outputValueType(ips::OutputKind kind) {
    switch (kind) {
    case ips::OutputKind::Point: return ips::ValueType::Point;
    case ips::OutputKind::Rect: return ips::ValueType::Rect;
    case ips::OutputKind::Circle: return ips::ValueType::Circle;
    }
    return ips::ValueType::Point;
}

const std::map<std::string, ips::ValueType>& builtinResults() {
    static const std::map<std::string, ips::ValueType> results = {
        {"unit", ips::ValueType::F32},    {"range", ips::ValueType::F32},
        {"index", ips::ValueType::I32},   {"hash", ips::ValueType::I32},
        {"noise", ips::ValueType::F32},   {"fault", ips::ValueType::F32},
        {"smooth", ips::ValueType::F32},  {"lerp", ips::ValueType::F32},
        {"mix", ips::ValueType::Color},   {"shade", ips::ValueType::Color},
        {"pixel", ips::ValueType::Void},  {"rect", ips::ValueType::Void},
        {"line", ips::ValueType::Void},   {"oval", ips::ValueType::Void},
        {"weather", ips::ValueType::Void}, {"poly", ips::ValueType::Void},
    };
    return results;
}

bool isBuiltin(const std::string& name) {
    return builtinResults().count(name) != 0;
}

// ---- the compiler ------------------------------------------------------------

struct FunctionInfo {
    std::string name;
    const Function* ast = nullptr;
    const Program* owner = nullptr;
    bool drawLike = false;
    std::vector<ValueType> params;  // runtime types
    std::vector<ValueType> results; // runtime types
    std::vector<ValueType> locals;  // extra locals beyond params
    std::optional<ips::ValueType> langResult;
    std::map<std::string, u32> slots;              // name -> local slot
    std::map<std::string, ips::ValueType> langTypes; // name -> language type
    std::vector<u8> code;
};

class Compiler {
public:
    Compiler(const Program& entry, const std::map<std::string, const Program*>& parts)
        : entry_(entry), parts_(parts) {}

    std::vector<u8> run() {
        prepareAll();
        emitAll();
        return assemble();
    }

private:
    const Program& entry_;
    const std::map<std::string, const Program*>& parts_;

    struct SlotInfo {
        ips::OutputKind kind;
        bool isList = false;
    };

    Pools pools_;
    std::vector<const Program*> orderedParts_; // entry import order
    std::map<std::string, u32> partIndex_;
    std::vector<SlotInfo> rootSlots_;
    std::map<std::string, u32> rootSlotIndex_;
    std::vector<FunctionInfo> functions_;
    std::map<std::string, u32> functionIndex_;
    std::map<std::string, const Function*> registry_;
    std::map<std::string, bool> registryDrawLike_;

    // State of the function being prepared or emitted.
    const FunctionInfo* current_ = nullptr;
    std::map<std::string, ips::ValueType> langTypes_;
    std::set<std::string> preparing_;
    std::vector<u8> code_;
    bool scratchUsed_ = false;

    [[noreturn]] void error(const SourceLocation& at, std::string message) {
        throw SourceError{at, std::move(message)};
    }

    // ---- instruction emission -------------------------------------------------

    void ins(Opcode opcode, u8 aux = 0, u16 operand = 0) {
        code_.push_back(static_cast<u8>(opcode));
        code_.push_back(aux);
        code_.push_back(static_cast<u8>(operand));
        code_.push_back(static_cast<u8>(operand >> 8));
    }

    void emitConst(const Value& value) {
        if (const auto* i = std::get_if<i32>(&value)) {
            ins(Opcode::ConstI32, 0,
                static_cast<u16>(pools_.addInt(static_cast<u32>(*i))));
        } else if (const auto* f = std::get_if<f32>(&value)) {
            u32 bits = 0;
            std::memcpy(&bits, &*f, sizeof(bits));
            ins(Opcode::ConstF32, 0, static_cast<u16>(pools_.addFloat(bits)));
        } else if (const auto* c = std::get_if<Color>(&value)) {
            ins(Opcode::ConstColor, 0, static_cast<u16>(pools_.addColor(*c)));
        }
    }

    // One extra i32 local reused by every && / || in the function. Appended
    // to the function's locals; the table is written after all emission.
    u32 scratch() {
        scratchUsed_ = true;
        return static_cast<u32>(current_->params.size() + current_->locals.size());
    }

    // ---- preparation ----------------------------------------------------------

    const InputDecl* ownerInput(const Program* owner, const std::string& name) const {
        for (const auto& input : owner->asset.inputs) {
            if (input.name == name) {
                return &input;
            }
        }
        return nullptr;
    }

    const OutputDecl* ownerOutput(const Program* owner, const std::string& name) const {
        for (const auto& output : owner->asset.outputs) {
            if (output.name == name) {
                return &output;
            }
        }
        return nullptr;
    }

    void prepareAll() {
        for (const auto& output : entry_.asset.outputs) {
            rootSlots_.push_back({output.kind, output.isList});
            rootSlotIndex_.emplace(output.name,
                                   static_cast<u32>(rootSlots_.size() - 1));
        }

        for (const auto& import : entry_.imports) {
            const auto found = parts_.find(import.alias);
            if (found == parts_.end()) {
                error(import.location,
                      "imported part '" + import.alias + "' was not provided");
            }
            const Program* part = found->second;
            if (!part->imports.empty()) {
                error(part->imports[0].location,
                      "nested part imports are not supported yet");
            }
            partIndex_.emplace(import.alias, static_cast<u32>(orderedParts_.size()));
            orderedParts_.push_back(part);
        }

        // Plan the function table: draw first, then the entry's other
        // functions in declaration order, then each part's functions.
        std::vector<std::string> planned;
        for (const auto& function : entry_.asset.functions) {
            if (function.name == "draw") {
                planned.push_back("draw");
                registry_.emplace("draw", &function);
                registryDrawLike_.emplace("draw", true);
            }
        }
        for (const auto& function : entry_.asset.functions) {
            if (function.name != "draw") {
                planned.push_back(function.name);
                registry_.emplace(function.name, &function);
                registryDrawLike_.emplace(function.name, false);
            }
        }
        for (const auto& import : entry_.imports) {
            const Program* part = parts_.at(import.alias);
            for (const auto& function : part->asset.functions) {
                const std::string name = import.alias + "." + function.name;
                planned.push_back(name);
                registry_.emplace(name, &function);
                registryDrawLike_.emplace(name, function.name == "draw");
            }
        }

        // Register names first (deterministic order): function names, slot
        // names, input names.
        for (const auto& name : planned) {
            pools_.addString(name);
        }
        for (const auto& output : entry_.asset.outputs) {
            pools_.addString(output.name);
        }
        for (const auto& import : entry_.imports) {
            for (const auto& output : parts_.at(import.alias)->asset.outputs) {
                pools_.addString(output.name);
            }
        }
        for (const auto& input : entry_.asset.inputs) {
            pools_.addString(input.name);
        }
        for (const auto& import : entry_.imports) {
            for (const auto& input : parts_.at(import.alias)->asset.inputs) {
                pools_.addString(input.name);
            }
        }

        for (const auto& name : planned) {
            prepareFunction(name);
        }
    }

    // Prepares one function: fills its params, discovers locals in
    // first-assignment order and computes the return type. Called on demand,
    // so calls between functions resolve naturally (the checker already
    // rejected recursion).
    void prepareFunction(const std::string& name) {
        if (functionIndex_.count(name) != 0) {
            return;
        }
        if (preparing_.count(name) != 0) {
            error(registry_.at(name)->location,
                  "recursive call to '" + name + "' is not supported");
        }
        preparing_.insert(name);

        const Function* ast = registry_.at(name);
        const bool drawLike = registryDrawLike_.at(name);
        const Program* owner = &entry_;
        const auto dot = name.find('.');
        if (dot != std::string::npos) {
            owner = parts_.at(name.substr(0, dot));
        }

        FunctionInfo info;
        info.name = name;
        info.ast = ast;
        info.owner = owner;
        info.drawLike = drawLike;

        u32 slot = 0;
        if (drawLike) {
            info.params = {ValueType::I32, ValueType::I32}; // variant, archetype
            for (int i = 0; i < 2; ++i) {
                const std::string& pname = ast->params[static_cast<std::size_t>(i)];
                info.slots.emplace(pname, slot);
                info.langTypes.emplace(pname, ips::ValueType::I32);
                ++slot;
            }
            for (const auto& input : owner->asset.inputs) {
                if (input.type == ips::InputType::I32) {
                    error(input.location,
                          "input i32 is not supported by the v0.1 format (use u32)");
                }
                info.slots.emplace(input.name, slot);
                info.langTypes.emplace(input.name, inputValueType(input.type));
                info.params.push_back(runtimeType(inputValueType(input.type)));
                ++slot;
            }
        } else {
            for (const auto& param : ast->params) {
                info.slots.emplace(param, slot);
                info.langTypes.emplace(param, ips::ValueType::I32);
                info.params.push_back(ValueType::I32);
                ++slot;
            }
        }

        current_ = &info;
        langTypes_ = info.langTypes;
        std::optional<ips::ValueType> returnType;

        const auto walk = [&](auto&& self, const std::vector<Stmt>& body) -> void {
            for (const auto& stmt : body) {
                if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
                    if (ownerOutput(owner, decl->name) != nullptr ||
                        ownerInput(owner, decl->name) != nullptr ||
                        info.slots.count(decl->name) != 0) {
                        continue; // output write, input or parameter
                    }
                    const ips::ValueType type = inferType(*decl->value);
                    info.slots.emplace(decl->name, slot);
                    info.langTypes.emplace(decl->name, type);
                    langTypes_.emplace(decl->name, type);
                    info.locals.push_back(runtimeType(type));
                    ++slot;
                } else if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
                    if (ret->value) {
                        const ips::ValueType type = inferType(*ret->value);
                        if (returnType.has_value() && *returnType != type) {
                            error(ret->location,
                                  "inconsistent return type in '" + name + "'");
                        }
                        returnType = type;
                    }
                } else if (const auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
                    self(self, ifStmt->thenBranch);
                    self(self, ifStmt->elseBranch);
                } else if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
                    self(self, whileStmt->body);
                }
            }
        };
        walk(walk, ast->body);

        info.langTypes = langTypes_;
        if (returnType.has_value()) {
            info.langResult = returnType;
            info.results.push_back(runtimeType(*returnType));
        }
        if (name == "draw" && returnType.has_value()) {
            error(ast->location, "'draw' cannot return a value");
        }

        functions_.push_back(std::move(info));
        functionIndex_.emplace(name, static_cast<u32>(functions_.size() - 1));
        preparing_.erase(name);
        current_ = nullptr;
    }

    // ---- the pure typer (mirrors the semantic checker) -------------------------

    ips::ValueType inferType(const Expr& expr) {
        if (const auto* lit = std::get_if<Literal>(&expr)) {
            switch (lit->kind) {
            case Literal::Kind::Int:
            case Literal::Kind::True:
            case Literal::Kind::False:
                return ips::ValueType::I32;
            case Literal::Kind::Float:
                return ips::ValueType::F32;
            case Literal::Kind::Color:
                return ips::ValueType::Color;
            case Literal::Kind::String:
                break;
            }
        }
        if (const auto* id = std::get_if<Identifier>(&expr)) {
            const auto local = langTypes_.find(id->name);
            if (local != langTypes_.end()) {
                return local->second;
            }
            if (current_->drawLike) {
                if (const InputDecl* input = ownerInput(current_->owner, id->name)) {
                    return inputValueType(input->type);
                }
            }
            if (id->name == "seed") {
                return ips::ValueType::I32;
            }
            error(id->location, "undefined name '" + id->name + "'");
        }
        if (std::holds_alternative<Member>(expr)) {
            return outputValueType(partReadInfo(expr).kind);
        }
        if (const auto* call = std::get_if<Call>(&expr)) {
            return callType(*call);
        }
        if (const auto* vector = std::get_if<VectorLit>(&expr)) {
            switch (vector->elements.size()) {
            case 2: return ips::ValueType::Point;
            case 3: return ips::ValueType::Circle;
            case 4: return ips::ValueType::Rect;
            default: break;
            }
        }
        if (const auto* bin = std::get_if<Binary>(&expr)) {
            const ips::ValueType left = inferType(*bin->left);
            const ips::ValueType right = inferType(*bin->right);
            switch (bin->op) {
            case Binary::Op::Add:
            case Binary::Op::Sub:
            case Binary::Op::Mul:
            case Binary::Op::Div:
            case Binary::Op::Mod:
                return promote(left, right);
            case Binary::Op::Eq:
            case Binary::Op::Ne:
            case Binary::Op::Lt:
            case Binary::Op::Le:
            case Binary::Op::Gt:
            case Binary::Op::Ge:
            case Binary::Op::And:
            case Binary::Op::Or:
            case Binary::Op::Xor:
            case Binary::Op::BitAnd:
            case Binary::Op::BitOr:
                return ips::ValueType::I32;
            }
        }
        if (const auto* un = std::get_if<Unary>(&expr)) {
            const ips::ValueType operand = inferType(*un->operand);
            if (un->op == Unary::Op::Neg) {
                return operand;
            }
            return ips::ValueType::I32;
        }
        error(locationOf(expr), "unsupported expression");
    }

    ips::ValueType callType(const Call& call) {
        if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
            if (isBuiltin(name->name)) {
                return builtinResults().at(name->name);
            }
            if (functionIndex_.count(name->name) == 0) {
                prepareFunction(name->name); // on demand
            }
            const FunctionInfo& info = functions_[functionIndex_.at(name->name)];
            return info.langResult.value_or(ips::ValueType::Void);
        }
        if (std::holds_alternative<Member>(*call.callee)) {
            return ips::ValueType::Void; // part call
        }
        error(call.location, "cannot call this expression");
    }

    struct PartRead {
        std::string alias;
        const OutputDecl* output = nullptr;
        ips::OutputKind kind = ips::OutputKind::Point;
    };

    PartRead partReadInfo(const Expr& expr) const {
        PartRead info;
        std::vector<std::string> reversed;
        const Expr* current = &expr;
        while (const auto* member = std::get_if<Member>(current)) {
            reversed.push_back(member->name);
            current = member->object.get();
        }
        const auto* base = std::get_if<Identifier>(current);
        if (base == nullptr) {
            throw SourceError{locationOf(expr), "unsupported member access"};
        }
        info.alias = base->name;
        const auto part = parts_.find(base->name);
        if (part == parts_.end()) {
            throw SourceError{locationOf(expr),
                              "'" + base->name + "' is not an imported part"};
        }
        if (reversed.size() != 2 || reversed[1] != "outputs") {
            throw SourceError{locationOf(expr), "expected 'part.outputs.<name>'"};
        }
        for (const auto& output : part->second->asset.outputs) {
            if (output.name == reversed[0]) {
                info.output = &output;
                info.kind = output.kind;
                return info;
            }
        }
        throw SourceError{locationOf(expr), "part '" + base->name +
                                                "' has no output '" + reversed[0] + "'"};
    }

    // ---- emission ---------------------------------------------------------------

    void emitAll() {
        for (auto& info : functions_) {
            current_ = &info;
            langTypes_ = info.langTypes;
            code_.clear();
            scratchUsed_ = false;
            for (const auto& stmt : info.ast->body) {
                emitStmt(stmt);
            }
            info.code = std::move(code_);
            if (scratchUsed_) {
                info.locals.push_back(ValueType::I32);
            }
            current_ = nullptr;
        }
    }

    bool isParam(const std::string& name) const {
        const auto& params = current_->ast->params;
        return std::find(params.begin(), params.end(), name) != params.end();
    }

    void emitStmt(const Stmt& stmt) {
        if (const auto* decl = std::get_if<VarDecl>(&stmt)) {
            if (const OutputDecl* output = ownerOutput(current_->owner, decl->name)) {
                emitOutputWrite(current_->owner, *output, *decl->value);
                return;
            }
            if (ownerInput(current_->owner, decl->name) != nullptr ||
                isParam(decl->name)) {
                return; // unreachable: the checker rejects these
            }
            emitExpr(*decl->value);
            ins(Opcode::LocalSet, 0,
                static_cast<u16>(current_->slots.at(decl->name)));
        } else if (const auto* exprStmt = std::get_if<ExprStmt>(&stmt)) {
            const ips::ValueType type = emitExpr(*exprStmt->expr);
            if (type != ips::ValueType::Void) {
                ins(Opcode::Drop);
            }
        } else if (const auto* ifStmt = std::get_if<IfStmt>(&stmt)) {
            emitExpr(*ifStmt->condition);
            ins(Opcode::If);
            for (const auto& s : ifStmt->thenBranch) {
                emitStmt(s);
            }
            if (!ifStmt->elseBranch.empty()) {
                ins(Opcode::Else);
                for (const auto& s : ifStmt->elseBranch) {
                    emitStmt(s);
                }
            }
            ins(Opcode::End);
        } else if (const auto* whileStmt = std::get_if<WhileStmt>(&stmt)) {
            ins(Opcode::Block);
            ins(Opcode::Loop);
            emitExpr(*whileStmt->condition);
            ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
            ins(Opcode::I32Eq);
            ins(Opcode::BrIf, 0, 1); // exit the enclosing block
            for (const auto& s : whileStmt->body) {
                emitStmt(s);
            }
            ins(Opcode::Br, 0, 0); // back to the loop start
            ins(Opcode::End);
            ins(Opcode::End);
        } else if (const auto* ret = std::get_if<ReturnStmt>(&stmt)) {
            if (ret->value) {
                emitExpr(*ret->value);
            }
            ins(Opcode::Return);
        }
    }

    // The global output slot for a root output or a part output (spec §4.3:
    // root slots first, then part slots in part order).
    u32 slotForOutput(const Program* owner, const OutputDecl& output) const {
        if (owner == &entry_) {
            return rootSlotIndex_.at(output.name);
        }
        u32 part = 0;
        while (orderedParts_[part] != owner) {
            ++part;
        }
        u32 slot = static_cast<u32>(rootSlots_.size());
        for (u32 i = 0; i < part; ++i) {
            slot += static_cast<u32>(orderedParts_[i]->asset.outputs.size());
        }
        for (const auto& candidate : owner->asset.outputs) {
            if (candidate.name == output.name) {
                return slot;
            }
            ++slot;
        }
        return slot;
    }

    void emitOutputWrite(const Program* owner, const OutputDecl& output,
                         const Expr& value) {
        const u32 slot = slotForOutput(owner, output);
        if (const auto* vector = std::get_if<VectorLit>(&value)) {
            for (const auto& element : vector->elements) {
                emitExpr(*element);
                if (inferType(*element) == ips::ValueType::I32) {
                    ins(Opcode::I32ToF32);
                }
            }
        } else {
            emitExpr(value); // part.get.* pushes the right shape
        }
        switch (output.kind) {
        case ips::OutputKind::Point:
            ins(output.isList ? Opcode::OutListPoint : Opcode::OutPoint, 0,
                static_cast<u16>(slot));
            break;
        case ips::OutputKind::Rect:
            ins(output.isList ? Opcode::OutListRect : Opcode::OutRect, 0,
                static_cast<u16>(slot));
            break;
        case ips::OutputKind::Circle:
            ins(output.isList ? Opcode::OutListCircle : Opcode::OutCircle, 0,
                static_cast<u16>(slot));
            break;
        }
    }

    // ---- expressions -----------------------------------------------------------

    ips::ValueType emitExpr(const Expr& expr) {
        if (Value folded; tryFold(expr, folded)) {
            emitConst(folded);
            if (std::holds_alternative<i32>(folded)) {
                return ips::ValueType::I32;
            }
            if (std::holds_alternative<f32>(folded)) {
                return ips::ValueType::F32;
            }
            return ips::ValueType::Color;
        }
        if (const auto* id = std::get_if<Identifier>(&expr)) {
            if (id->name == "seed") {
                ins(Opcode::StreamState);
                return ips::ValueType::I32;
            }
            const u32 slot = current_->slots.at(id->name);
            ins(Opcode::LocalGet, 0, static_cast<u16>(slot));
            return current_->langTypes.at(id->name);
        }
        if (std::holds_alternative<Member>(expr)) {
            return emitPartRead(expr);
        }
        if (const auto* call = std::get_if<Call>(&expr)) {
            return emitCall(*call);
        }
        if (const auto* bin = std::get_if<Binary>(&expr)) {
            return emitBinary(*bin);
        }
        if (const auto* un = std::get_if<Unary>(&expr)) {
            return emitUnary(*un);
        }
        error(locationOf(expr), "unsupported expression");
    }

    // A point value used by output writes and part placement: a vector
    // literal {x, y} or a part read of a point.
    void emitPointValue(const Expr& expr) {
        if (const auto* vector = std::get_if<VectorLit>(&expr)) {
            for (const auto& element : vector->elements) {
                emitExpr(*element);
                if (inferType(*element) == ips::ValueType::I32) {
                    ins(Opcode::I32ToF32);
                }
            }
            return;
        }
        emitExpr(expr); // part.get.point
    }

    ips::ValueType emitPartRead(const Expr& expr) {
        const PartRead read = partReadInfo(expr);
        const u32 part = partIndex_.at(read.alias);
        u32 slot = static_cast<u32>(rootSlots_.size());
        for (u32 i = 0; i < part; ++i) {
            slot += static_cast<u32>(orderedParts_[i]->asset.outputs.size());
        }
        for (const auto& output : orderedParts_[part]->asset.outputs) {
            if (output.name == read.output->name) {
                break;
            }
            ++slot;
        }
        switch (read.kind) {
        case ips::OutputKind::Point:
            ins(Opcode::PartGetPoint, 0, static_cast<u16>(slot));
            return ips::ValueType::Point;
        case ips::OutputKind::Rect:
            ins(Opcode::PartGetRect, 0, static_cast<u16>(slot));
            return ips::ValueType::Rect;
        case ips::OutputKind::Circle:
            ins(Opcode::PartGetCircle, 0, static_cast<u16>(slot));
            return ips::ValueType::Circle;
        }
        error(locationOf(expr), "unsupported part read");
    }

    ips::ValueType emitCall(const Call& call) {
        if (const auto* name = std::get_if<Identifier>(call.callee.get())) {
            if (isBuiltin(name->name)) {
                return emitBuiltin(name->name, call);
            }
            if (functionIndex_.count(name->name) == 0) {
                prepareFunction(name->name);
            }
            const u32 index = functionIndex_.at(name->name);
            for (const auto& arg : call.args) {
                emitExpr(*arg.value);
            }
            ins(Opcode::Call, 0, static_cast<u16>(index));
            return functions_[index].langResult.value_or(ips::ValueType::Void);
        }
        if (std::holds_alternative<Member>(*call.callee)) {
            emitPartCall(call);
            return ips::ValueType::Void;
        }
        error(call.location, "cannot call this expression");
    }

    // Emits one argument and widens i32 to f32 when the builtin expects f32.
    void emitArg(const Arg& arg, ips::ValueType expected) {
        emitExpr(*arg.value);
        if (expected == ips::ValueType::F32 &&
            inferType(*arg.value) == ips::ValueType::I32) {
            ins(Opcode::I32ToF32);
        }
    }

    ips::ValueType emitBuiltin(const std::string& name, const Call& call) {
        const auto emitFixed = [&](std::initializer_list<ips::ValueType> params,
                                   Opcode opcode) -> ips::ValueType {
            for (std::size_t i = 0; i < call.args.size(); ++i) {
                emitArg(call.args[i], params.begin()[i]);
            }
            ins(opcode);
            return builtinResults().at(name);
        };

        if (name == "unit") {
            ins(Opcode::RngUnit);
            return ips::ValueType::F32;
        }
        if (name == "range") {
            emitArg(call.args[0], ips::ValueType::F32);
            emitArg(call.args[1], ips::ValueType::F32);
            ins(Opcode::RngRange);
            return ips::ValueType::F32;
        }
        if (name == "index") return emitFixed({ips::ValueType::I32}, Opcode::RngIndex);
        if (name == "hash") return emitFixed({ips::ValueType::I32}, Opcode::Hash);
        if (name == "noise") {
            return emitFixed({ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::I32}, Opcode::Noise);
        }
        if (name == "fault") {
            return emitFixed({ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::I32}, Opcode::Fault);
        }
        if (name == "smooth") return emitFixed({ips::ValueType::F32}, Opcode::Smooth);
        if (name == "lerp") {
            return emitFixed({ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::F32}, Opcode::Lerp);
        }
        if (name == "mix") {
            return emitFixed({ips::ValueType::Color, ips::ValueType::Color,
                              ips::ValueType::F32}, Opcode::ColorMix);
        }
        if (name == "shade") {
            return emitFixed({ips::ValueType::Color, ips::ValueType::I32},
                             Opcode::ColorShade);
        }
        if (name == "pixel") {
            return emitFixed({ips::ValueType::I32, ips::ValueType::I32,
                              ips::ValueType::Color}, Opcode::Pixel);
        }
        if (name == "rect") {
            return emitFixed({ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::Color}, Opcode::Rect);
        }
        if (name == "line") {
            return emitFixed({ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::I32, ips::ValueType::Color},
                             Opcode::Line);
        }
        if (name == "oval") {
            return emitFixed({ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::F32, ips::ValueType::F32,
                              ips::ValueType::Color}, Opcode::Oval);
        }
        if (name == "weather") {
            return emitFixed({ips::ValueType::I32, ips::ValueType::I32},
                             Opcode::Weather);
        }
        if (name == "poly") {
            emitArg(call.args[0], ips::ValueType::Color);
            for (std::size_t i = 1; i < call.args.size(); ++i) {
                emitArg(call.args[i], ips::ValueType::F32);
            }
            const u16 vertices = static_cast<u16>((call.args.size() - 1) / 2);
            ins(Opcode::Poly, 0, vertices);
            return ips::ValueType::Void;
        }
        error(call.location, "undefined function '" + name + "'");
    }

    // `part.draw(seed, variant, archetype, at = {x, y}, uniforms...)`
    void emitPartCall(const Call& call) {
        std::vector<std::string> reversed;
        const Expr* current = call.callee.get();
        while (const auto* member = std::get_if<Member>(current)) {
            reversed.push_back(member->name);
            current = member->object.get();
        }
        const auto* base = std::get_if<Identifier>(current);
        const std::string alias = base->name;
        const Program* part = parts_.at(alias);
        const u32 partIndex = partIndex_.at(alias);

        // Uniforms in declaration order (bottom of the stack): override with
        // the named argument, else push the declared default.
        for (const auto& input : part->asset.inputs) {
            const Arg* overrideArg = nullptr;
            for (const auto& arg : call.args) {
                if (arg.name.has_value() && *arg.name == input.name) {
                    overrideArg = &arg;
                    break;
                }
            }
            if (overrideArg != nullptr) {
                emitExpr(*overrideArg->value);
                continue;
            }
            const auto* value = std::get_if<Literal>(input.defaultValue.get());
            Value folded;
            if (value == nullptr || !tryFold(*value, folded)) {
                error(input.location,
                      "part input '" + input.name + "' has no constant default");
            }
            emitConst(folded);
        }

        // seed, variant, archetype (positional, in order).
        for (const auto& arg : call.args) {
            if (!arg.name.has_value()) {
                emitExpr(*arg.value);
            }
        }

        // at = {x, y}
        for (const auto& arg : call.args) {
            if (arg.name.has_value() && *arg.name == "at") {
                emitPointValue(*arg.value);
                break;
            }
        }
        ins(Opcode::PartDraw, 0, static_cast<u16>(partIndex));
    }

    ips::ValueType emitBinary(const Binary& bin) {
        const ips::ValueType leftType = inferType(*bin.left);
        const ips::ValueType rightType = inferType(*bin.right);
        const bool f32op = leftType == ips::ValueType::F32 ||
                           rightType == ips::ValueType::F32;

        switch (bin.op) {
        case Binary::Op::Add:
        case Binary::Op::Sub:
        case Binary::Op::Mul:
        case Binary::Op::Div:
        case Binary::Op::Mod: {
            emitExpr(*bin.left);
            if (f32op && leftType == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
            emitExpr(*bin.right);
            if (f32op && rightType == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
            if (f32op) {
                switch (bin.op) {
                case Binary::Op::Add: ins(Opcode::F32Add); break;
                case Binary::Op::Sub: ins(Opcode::F32Sub); break;
                case Binary::Op::Mul: ins(Opcode::F32Mul); break;
                case Binary::Op::Div: ins(Opcode::F32Div); break;
                default: ins(Opcode::F32Sub); break; // unreachable
                }
                return ips::ValueType::F32;
            }
            switch (bin.op) {
            case Binary::Op::Add: ins(Opcode::I32Add); break;
            case Binary::Op::Sub: ins(Opcode::I32Sub); break;
            case Binary::Op::Mul: ins(Opcode::I32Mul); break;
            case Binary::Op::Div: ins(Opcode::I32DivS); break;
            case Binary::Op::Mod: ins(Opcode::I32RemS); break;
            default: break;
            }
            return ips::ValueType::I32;
        }
        case Binary::Op::Eq:
        case Binary::Op::Ne:
        case Binary::Op::Lt:
        case Binary::Op::Le:
        case Binary::Op::Gt:
        case Binary::Op::Ge: {
            emitExpr(*bin.left);
            if (f32op && leftType == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
            emitExpr(*bin.right);
            if (f32op && rightType == ips::ValueType::I32) {
                ins(Opcode::I32ToF32);
            }
            if (f32op) {
                switch (bin.op) {
                case Binary::Op::Eq: ins(Opcode::F32Eq); break;
                case Binary::Op::Ne: ins(Opcode::F32Ne); break;
                case Binary::Op::Lt: ins(Opcode::F32Lt); break;
                case Binary::Op::Le: ins(Opcode::F32Le); break;
                case Binary::Op::Gt: ins(Opcode::F32Gt); break;
                case Binary::Op::Ge: ins(Opcode::F32Ge); break;
                default: break;
                }
                return ips::ValueType::I32;
            }
            switch (bin.op) {
            case Binary::Op::Eq: ins(Opcode::I32Eq); break;
            case Binary::Op::Ne: ins(Opcode::I32Ne); break;
            case Binary::Op::Lt: ins(Opcode::I32LtS); break;
            case Binary::Op::Le: ins(Opcode::I32LeS); break;
            case Binary::Op::Gt: ins(Opcode::I32GtS); break;
            case Binary::Op::Ge: ins(Opcode::I32GeS); break;
            default: break;
            }
            return ips::ValueType::I32;
        }
        case Binary::Op::Xor:
        case Binary::Op::BitAnd:
        case Binary::Op::BitOr: {
            emitExpr(*bin.left);
            emitExpr(*bin.right);
            switch (bin.op) {
            case Binary::Op::Xor: ins(Opcode::I32Xor); break;
            case Binary::Op::BitAnd: ins(Opcode::I32And); break;
            case Binary::Op::BitOr: ins(Opcode::I32Or); break;
            default: break;
            }
            return ips::ValueType::I32;
        }
        case Binary::Op::And:
        case Binary::Op::Or: {
            // Short-circuit with 0/1 normalisation; the scratch local is
            // reused for every && / || in the function.
            emitExpr(*bin.left);
            ins(Opcode::If);
            if (bin.op == Binary::Op::And) {
                emitExpr(*bin.right);
                ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
                ins(Opcode::I32Ne);
            } else {
                ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(1)));
            }
            ins(Opcode::LocalSet, 0, static_cast<u16>(scratch()));
            ins(Opcode::Else);
            if (bin.op == Binary::Op::And) {
                ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
            } else {
                emitExpr(*bin.right);
                ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
                ins(Opcode::I32Ne);
            }
            ins(Opcode::LocalSet, 0, static_cast<u16>(scratch()));
            ins(Opcode::End);
            ins(Opcode::LocalGet, 0, static_cast<u16>(scratch()));
            return ips::ValueType::I32;
        }
        }
        error(bin.location, "unsupported binary operator");
    }

    ips::ValueType emitUnary(const Unary& un) {
        const ips::ValueType operand = inferType(*un.operand);
        emitExpr(*un.operand);
        switch (un.op) {
        case Unary::Op::Neg:
            ins(operand == ips::ValueType::F32 ? Opcode::F32Neg : Opcode::I32Neg);
            return operand;
        case Unary::Op::Not:
            ins(Opcode::ConstI32, 0, static_cast<u16>(pools_.addInt(0)));
            ins(Opcode::I32Eq);
            return ips::ValueType::I32;
        case Unary::Op::BitNot:
            ins(Opcode::I32Not);
            return ips::ValueType::I32;
        }
        error(un.location, "unsupported unary operator");
    }

    // ---- constant folding -------------------------------------------------------

    bool tryFold(const Expr& expr, Value& out) const {
        if (const auto* lit = std::get_if<Literal>(&expr)) {
            switch (lit->kind) {
            case Literal::Kind::Int: {
                u32 bits = 0;
                if (!parseIntBits(lit->text, bits)) {
                    return false;
                }
                out = static_cast<i32>(bits);
                return true;
            }
            case Literal::Kind::True:
                out = static_cast<i32>(1);
                return true;
            case Literal::Kind::False:
                out = static_cast<i32>(0);
                return true;
            case Literal::Kind::Float: {
                u32 bits = 0;
                if (!parseFloatBits(lit->text, bits)) {
                    return false;
                }
                f32 value = 0.f;
                std::memcpy(&value, &bits, sizeof(value));
                out = value;
                return true;
            }
            case Literal::Kind::Color: {
                Color color;
                if (!parseColor(lit->text, color)) {
                    return false;
                }
                out = color;
                return true;
            }
            case Literal::Kind::String:
                return false;
            }
        }
        if (const auto* un = std::get_if<Unary>(&expr)) {
            Value operand;
            if (!tryFold(*un->operand, operand)) {
                return false;
            }
            if (const auto* i = std::get_if<i32>(&operand)) {
                switch (un->op) {
                case Unary::Op::Neg:
                    out = static_cast<i32>(0u - static_cast<u32>(*i));
                    return true;
                case Unary::Op::Not:
                    out = static_cast<i32>(*i == 0 ? 1 : 0);
                    return true;
                case Unary::Op::BitNot:
                    out = static_cast<i32>(~static_cast<u32>(*i));
                    return true;
                }
            }
            if (const auto* f = std::get_if<f32>(&operand)) {
                if (un->op == Unary::Op::Neg) {
                    out = -*f;
                    return true;
                }
            }
            return false;
        }
        if (const auto* bin = std::get_if<Binary>(&expr)) {
            Value left;
            Value right;
            if (!tryFold(*bin->left, left) || !tryFold(*bin->right, right)) {
                return false;
            }
            const auto* li = std::get_if<i32>(&left);
            const auto* ri = std::get_if<i32>(&right);
            const auto* lf = std::get_if<f32>(&left);
            const auto* rf = std::get_if<f32>(&right);

            if (li != nullptr && ri != nullptr) {
                const u32 a = static_cast<u32>(*li);
                const u32 b = static_cast<u32>(*ri);
                switch (bin->op) {
                case Binary::Op::Add: out = static_cast<i32>(a + b); return true;
                case Binary::Op::Sub: out = static_cast<i32>(a - b); return true;
                case Binary::Op::Mul: out = static_cast<i32>(a * b); return true;
                case Binary::Op::Eq: out = static_cast<i32>(*li == *ri ? 1 : 0); return true;
                case Binary::Op::Ne: out = static_cast<i32>(*li != *ri ? 1 : 0); return true;
                case Binary::Op::Lt: out = static_cast<i32>(*li < *ri ? 1 : 0); return true;
                case Binary::Op::Le: out = static_cast<i32>(*li <= *ri ? 1 : 0); return true;
                case Binary::Op::Gt: out = static_cast<i32>(*li > *ri ? 1 : 0); return true;
                case Binary::Op::Ge: out = static_cast<i32>(*li >= *ri ? 1 : 0); return true;
                case Binary::Op::And:
                    out = static_cast<i32>((*li != 0 && *ri != 0) ? 1 : 0);
                    return true;
                case Binary::Op::Or:
                    out = static_cast<i32>((*li != 0 || *ri != 0) ? 1 : 0);
                    return true;
                case Binary::Op::Xor: out = static_cast<i32>(a ^ b); return true;
                case Binary::Op::BitAnd: out = static_cast<i32>(a & b); return true;
                case Binary::Op::BitOr: out = static_cast<i32>(a | b); return true;
                default: return false; // div/mod: keep the runtime trap
                }
            }
            if (lf != nullptr && rf != nullptr) {
                switch (bin->op) {
                case Binary::Op::Add: out = *lf + *rf; return true;
                case Binary::Op::Sub: out = *lf - *rf; return true;
                case Binary::Op::Mul: out = *lf * *rf; return true;
                case Binary::Op::Div: out = *lf / *rf; return true;
                case Binary::Op::Eq: out = static_cast<i32>(*lf == *rf ? 1 : 0); return true;
                case Binary::Op::Ne: out = static_cast<i32>(*lf != *rf ? 1 : 0); return true;
                case Binary::Op::Lt: out = static_cast<i32>(*lf < *rf ? 1 : 0); return true;
                case Binary::Op::Le: out = static_cast<i32>(*lf <= *rf ? 1 : 0); return true;
                case Binary::Op::Gt: out = static_cast<i32>(*lf > *rf ? 1 : 0); return true;
                case Binary::Op::Ge: out = static_cast<i32>(*lf >= *rf ? 1 : 0); return true;
                default: return false;
                }
            }
            if (lf != nullptr && ri != nullptr) {
                return foldMixed(bin->op, *lf, static_cast<f32>(*ri), out);
            }
            if (li != nullptr && rf != nullptr) {
                return foldMixed(bin->op, static_cast<f32>(*li), *rf, out);
            }
            return false;
        }
        return false;
    }

    bool foldMixed(Binary::Op op, f32 a, f32 b, Value& out) const {
        switch (op) {
        case Binary::Op::Add: out = a + b; return true;
        case Binary::Op::Sub: out = a - b; return true;
        case Binary::Op::Mul: out = a * b; return true;
        case Binary::Op::Div: out = a / b; return true;
        case Binary::Op::Eq: out = static_cast<i32>(a == b ? 1 : 0); return true;
        case Binary::Op::Ne: out = static_cast<i32>(a != b ? 1 : 0); return true;
        case Binary::Op::Lt: out = static_cast<i32>(a < b ? 1 : 0); return true;
        case Binary::Op::Le: out = static_cast<i32>(a <= b ? 1 : 0); return true;
        case Binary::Op::Gt: out = static_cast<i32>(a > b ? 1 : 0); return true;
        case Binary::Op::Ge: out = static_cast<i32>(a >= b ? 1 : 0); return true;
        default: return false;
        }
    }

    // ---- meta / sections --------------------------------------------------------

    f32 foldOrigin(const Expr& expr) {
        Value value;
        if (!tryFold(expr, value)) {
            error(locationOf(expr), "origin must be a constant expression");
        }
        if (const auto* i = std::get_if<i32>(&value)) {
            return static_cast<f32>(*i);
        }
        if (const auto* f = std::get_if<f32>(&value)) {
            return *f;
        }
        error(locationOf(expr), "origin must be a numeric constant");
    }

    void writeInputEntry(Writer& out, const InputDecl& input) {
        switch (input.type) {
        case ips::InputType::Color: {
            out.writeU8(static_cast<u8>(iprop::InputType::Color));
            out.writeU32(pools_.stringIndex.at(input.name));
            const auto* lit = std::get_if<Literal>(input.defaultValue.get());
            Color color;
            parseColor(lit->text, color);
            out.writeColor(color);
            break;
        }
        case ips::InputType::F32: {
            out.writeU8(static_cast<u8>(iprop::InputType::F32));
            out.writeU32(pools_.stringIndex.at(input.name));
            const auto* lit = std::get_if<Literal>(input.defaultValue.get());
            u32 bits = 0;
            parseFloatBits(lit->text, bits);
            out.writeU32(bits);
            break;
        }
        case ips::InputType::U32:
        case ips::InputType::I32: {
            out.writeU8(static_cast<u8>(iprop::InputType::U32));
            out.writeU32(pools_.stringIndex.at(input.name));
            const auto* lit = std::get_if<Literal>(input.defaultValue.get());
            u32 bits = 0;
            parseIntBits(lit->text, bits);
            out.writeU32(bits);
            break;
        }
        }
    }

    std::vector<u8> assemble() {
        Writer meta;
        meta.writeU32(*entry_.asset.sizeX);
        meta.writeU32(*entry_.asset.sizeY);
        meta.writeF32(entry_.asset.originX ? foldOrigin(*entry_.asset.originX) : 0.f);
        meta.writeF32(entry_.asset.originY ? foldOrigin(*entry_.asset.originY) : 0.f);
        meta.writeU32(entry_.asset.variants.value_or(1));
        meta.writeU32(entry_.asset.archetypes.value_or(1));
        meta.writeU64(1000000); // fuel
        meta.writeU16(0);       // emission: no offset/radius/colour
        for (int i = 0; i < 6; ++i) {
            meta.writeU8(0);
        }
        meta.writeU16(static_cast<u16>(entry_.asset.inputs.size()));
        for (const auto& input : entry_.asset.inputs) {
            writeInputEntry(meta, input);
        }
        meta.writeU16(static_cast<u16>(entry_.asset.outputs.size()));
        for (const auto& output : entry_.asset.outputs) {
            meta.writeU8(static_cast<u8>(output.kind));
            meta.writeU8(output.isList ? 1 : 0);
            meta.writeU8(0);
            meta.writeU8(0);
            meta.writeU32(pools_.stringIndex.at(output.name));
        }

        Writer consts;
        consts.writeU32(static_cast<u32>(pools_.strings.size()));
        for (const auto& text : pools_.strings) {
            consts.writeU32(static_cast<u32>(text.size()));
            consts.writeBytes({reinterpret_cast<const u8*>(text.data()),
                               text.size()});
        }
        consts.writeU32(static_cast<u32>(pools_.ints.size()));
        for (const u32 bits : pools_.ints) {
            consts.writeU32(bits);
        }
        consts.writeU32(static_cast<u32>(pools_.floats.size()));
        for (const u32 bits : pools_.floats) {
            consts.writeU32(bits);
        }
        consts.writeU32(static_cast<u32>(pools_.colors.size()));
        for (const Color& color : pools_.colors) {
            consts.writeColor(color);
        }

        Writer table;
        Writer streams;
        // The table and the instruction streams are written interleaved: each
        // function's offset is the running size of the streams writer.
        for (const auto& info : functions_) {
            table.writeU32(pools_.stringIndex.at(info.name));
            table.writeU32(static_cast<u32>(streams.size()));
            table.writeU32(static_cast<u32>(info.code.size()));
            table.writeU16(static_cast<u16>(info.params.size()));
            for (const ValueType type : info.params) {
                table.writeU8(static_cast<u8>(type));
            }
            table.writeU16(static_cast<u16>(info.results.size()));
            for (const ValueType type : info.results) {
                table.writeU8(static_cast<u8>(type));
            }
            table.writeU16(static_cast<u16>(info.locals.size()));
            for (const ValueType type : info.locals) {
                table.writeU8(static_cast<u8>(type));
            }
            streams.writeBytes(info.code);
        }

        Writer code;
        code.writeU32(static_cast<u32>(functions_.size()));
        code.writeBytes(table.data());
        code.writeBytes(streams.data());
        code.writeU32(static_cast<u32>(orderedParts_.size()));
        for (std::size_t i = 0; i < orderedParts_.size(); ++i) {
            const Program* part = orderedParts_[i];
            code.writeU32(functionIndex_.at(entry_.imports[i].alias + ".draw"));
            code.writeU32(*part->asset.sizeX);
            code.writeU32(*part->asset.sizeY);
            code.writeF32(part->asset.originX ? foldOrigin(*part->asset.originX) : 0.f);
            code.writeF32(part->asset.originY ? foldOrigin(*part->asset.originY) : 0.f);
            code.writeU16(static_cast<u16>(part->asset.inputs.size()));
            for (const auto& input : part->asset.inputs) {
                writeInputEntry(code, input);
            }
            code.writeU16(static_cast<u16>(part->asset.outputs.size()));
            for (const auto& output : part->asset.outputs) {
                code.writeU8(static_cast<u8>(output.kind));
                code.writeU8(output.isList ? 1 : 0);
                code.writeU8(0);
                code.writeU8(0);
                code.writeU32(pools_.stringIndex.at(output.name));
            }
        }

        Writer file;
        for (const u8 magic : kMagic) {
            file.writeU8(magic);
        }
        file.writeU16(kVersionMajor);
        file.writeU16(kVersionMinor);
        file.writeU32(0); // flags
        file.writeU32(3); // section count
        file.writeU32(0); // reserved
        u32 offset = kHeaderSize + 3 * 12;
        const auto section = [&](u32 id, const std::vector<u8>& bytes) {
            file.writeU32(id);
            file.writeU32(offset);
            file.writeU32(static_cast<u32>(bytes.size()));
            offset += static_cast<u32>(bytes.size());
        };
        section(static_cast<u32>(SectionId::Meta), meta.data());
        section(static_cast<u32>(SectionId::Const), consts.data());
        section(static_cast<u32>(SectionId::Code), code.data());
        file.writeBytes(meta.data());
        file.writeBytes(consts.data());
        file.writeBytes(code.data());
        return file.data();
    }
};

} // namespace

std::vector<u8> compileProgram(
    const Program& entry,
    const std::map<std::string, const Program*>& parts) {
    Compiler compiler(entry, parts);
    const std::vector<u8> bytes = compiler.run();

    // Dogfood: the compiled file must pass the runtime validator.
    try {
        const auto loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        throw SourceError{{1, 1},
                          std::string("compiled program failed validation: ") +
                              error.what()};
    }
    return bytes;
}

} // namespace iprop
