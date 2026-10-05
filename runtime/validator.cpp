#include "iprop/Validator.hpp"
#include "iprop/Compression.hpp"

#include "iprop/Instruction.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "iprop/D4Transform.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace iprop {
namespace {

const char* typeName(ValueType type) {
    switch (type) {
    case ValueType::I32: return "i32";
    case ValueType::F32: return "f32";
    case ValueType::Color: return "color";
    case ValueType::Vec2: return "vec2";
    }
    return "?";
}

// Uniform params travel in i32 slots for u32 values (spec §3/§6.1).
ValueType typeOf(const InputDecl& decl) {
    switch (decl.type) {
    case InputType::Color: return ValueType::Color;
    case InputType::F32: return ValueType::F32;
    case InputType::U32: return ValueType::I32;
    }
    return ValueType::I32;
}

struct SlotInfo {
    OutputKind kind = OutputKind::Point;
    bool isList = false;
    bool rootOwned = false;
};

// Every edge preserves the target's complete entry type sequence. Reachability
// is tracked independently: a loop back-edge never reaches its following code.
struct ControlFrame {
    Opcode kind;
    std::vector<ValueType> entry;
    bool entryReachable;
    bool hasElse = false;
    bool thenReachable = false;
    bool exitReachable = false;
};

// Per-function stack/label type checking over structured control flow
// (spec §9/§14). Blocks are statement-level; no block results in v0.1.
class FunctionValidator {
public:
    FunctionValidator(const LoadedFile& file, const Function& function,
                      const std::vector<SlotInfo>& slots, u32 functionIndex)
        : file_(file), fn_(function), slots_(slots), functionIndex_(functionIndex) {}

    void run() {
        const auto hasVec2 = [](const std::vector<ValueType>& types) {
            return std::find(types.begin(), types.end(), ValueType::Vec2) != types.end();
        };
        if (file_.layout.header.versionMinor < 3 &&
            (hasVec2(fn_.params) || hasVec2(fn_.results) || hasVec2(fn_.locals))) {
            fail(0, "vec2 types require format 0.3");
        }
        const auto count = fn_.code.size() / 4;
        for (std::size_t i = 0; i < count; ++i) {
            const auto ins = decodeInstruction(fn_.code.subspan(i * 4, 4));
            const auto* spec = findOpcode(ins.opcode);
            if (!spec) fail(i, "reserved opcode");
            if (spec->aux == AuxPolicy::Zero && ins.aux != 0) {
                fail(i, "aux byte must be zero");
            }
            if (spec->aux == AuxPolicy::PartTransform) {
                if (!D4Transform::fromKind(ins.aux)) fail(i, "unknown part transform kind");
                if (ins.aux != 0 && file_.layout.header.versionMinor < 3)
                    fail(i, "part transforms require format 0.3");
            }
            if (file_.layout.header.versionMinor < spec->minimumMinor) {
                fail(i, std::string(spec->mnemonic) + " opcodes require format 0." +
                        std::to_string(spec->minimumMinor));
            }
            if (spec->stack) {
                applyStack(i, *spec);
                continue;
            }
            step(i, ins);
        }
        if (!labels_.empty()) {
            fail(count, "unbalanced structured control flow");
        }
        if (reachable_ && stack_.size() != fn_.results.size()) {
            fail(count, "function stack does not match declared results");
        }
        for (std::size_t r = 0; reachable_ && r < fn_.results.size(); ++r) {
            if (stack_[r] != fn_.results[r]) {
                fail(count, "function result type mismatch");
            }
        }
    }

private:
    const LoadedFile& file_;
    const Function& fn_;
    const std::vector<SlotInfo>& slots_;
    u32 functionIndex_;
    std::vector<ValueType> stack_;
    std::vector<ControlFrame> labels_;
    bool reachable_ = true;

    [[noreturn]] void fail(std::size_t instruction, std::string message) const {
        throw FormatError(std::move(message), ErrorReason::InvalidInstruction, ErrorStage::Validate,
            ErrorLocation::instruction(fn_.fileOffset + instruction * 4, functionIndex_,
                                       static_cast<u32>(instruction)));
    }

    void push(std::size_t at, ValueType type) {
        if (!reachable_) return;
        if (stack_.size() >= kMaxStackDepth) {
            fail(at, "operand stack depth exceeds limit");
        }
        stack_.push_back(type);
    }

    ValueType pop(std::size_t at, const char* what) {
        if (!reachable_) return ValueType::I32; // polymorphic dead stack
        if (stack_.empty()) {
            fail(at, std::string("stack underflow (") + what + ")");
        }
        const ValueType type = stack_.back();
        stack_.pop_back();
        return type;
    }

    void expect(std::size_t at, ValueType expected, const char* what) {
        const ValueType found = pop(at, what);
        if (reachable_ && found != expected) {
            fail(at, std::string("type mismatch: expected ") + typeName(expected) +
                          ", found " + typeName(found) + " (" + what + ")");
        }
    }

    std::size_t localCount() const { return fn_.params.size() + fn_.locals.size(); }

    ValueType localType(std::size_t index) const {
        return index < fn_.params.size() ? fn_.params[index]
                                         : fn_.locals[index - fn_.params.size()];
    }

    void checkSlot(std::size_t at, u16 slot, OutputKind kind, bool wantList, bool partSlot) {
        if (slot >= slots_.size()) {
            fail(at, "output slot index out of range");
        }
        const auto& info = slots_[slot];
        if (info.kind != kind) {
            fail(at, "output slot kind mismatch");
        }
        if (info.isList != wantList) {
            fail(at, wantList ? "expected a list slot" : "expected a singular slot");
        }
        if (partSlot && info.rootOwned) {
            fail(at, "part.get reads a root-owned slot");
        }
    }

    void applyStack(std::size_t at, const OpcodeSpec& spec) {
        const auto& signature = *spec.stack;
        for (std::size_t i = signature.inputCount; i > 0; --i) {
            const auto expected = signature.inputs[i-1];
            const auto actual = pop(at, spec.mnemonic);
            if (reachable_ && !accepts(expected,actual)) {
                const char* name = expected == StackType::ScalarOrVec2 ? "f32 or vec2" :
                    expected == StackType::I32 ? "i32" : expected == StackType::F32 ? "f32" :
                    expected == StackType::Color ? "color" : "vec2";
                fail(at, std::string("type mismatch: expected ") + name + ", found " +
                         typeName(actual) + " (" + spec.mnemonic + ")");
            }
        }
        if (signature.result) push(at, *signature.result);
    }

    void step(std::size_t at, const Instruction ins) {
        const u16 op = ins.operand;
        switch (ins.opcode) {

        case Opcode::Block:
        case Opcode::Loop:
            labels_.push_back({ins.opcode, stack_, reachable_});
            return;
        case Opcode::If:
            expect(at, ValueType::I32, "if condition");
            labels_.push_back({ins.opcode, stack_, reachable_});
            return;
        case Opcode::Else: {
            if (labels_.empty() || labels_.back().kind != Opcode::If) {
                fail(at, "else without if");
            }
            auto& label = labels_.back();
            if (label.hasElse) fail(at, "duplicate else");
            checkExit(at, label, "then-branch");
            label.hasElse = true;
            label.thenReachable = reachable_;
            stack_ = label.entry;
            reachable_ = label.entryReachable;
            return;
        }
        case Opcode::End: {
            if (labels_.empty()) fail(at, "end without block");
            const auto label = std::move(labels_.back());
            checkExit(at, label, "block");
            reachable_ = reachable_ || label.exitReachable ||
                (label.kind == Opcode::If &&
                 (label.hasElse ? label.thenReachable : label.entryReachable));
            stack_ = label.entry;
            labels_.pop_back();
            return;
        }
        case Opcode::Br:
            branch(at, op);
            makeUnreachable();
            return;
        case Opcode::BrIf:
            expect(at, ValueType::I32, "br_if condition");
            branch(at, op);
            return;

        case Opcode::Call: {
            if (op >= file_.code.functions.size()) {
                fail(at, "call index out of range");
            }
            const auto& callee = file_.code.functions[op];
            for (std::size_t p = callee.params.size(); p > 0; --p) {
                expect(at, callee.params[p - 1], "call argument");
            }
            for (const ValueType result : callee.results) {
                push(at, result);
            }
            return;
        }
        case Opcode::Return:
            if (reachable_ && stack_.size() != fn_.results.size()) {
                fail(at, "return with wrong stack height");
            }
            for (std::size_t r = 0; reachable_ && r < fn_.results.size(); ++r) {
                if (stack_[r] != fn_.results[r]) {
                    fail(at, "return type mismatch");
                }
            }
            makeUnreachable();
            return;

        case Opcode::ConstI32:
            if (op >= file_.pool.ints.size()) fail(at, "i32 constant index out of range");
            push(at, ValueType::I32);
            return;
        case Opcode::ConstF32:
            if (op >= file_.pool.floats.size()) fail(at, "f32 constant index out of range");
            push(at, ValueType::F32);
            return;
        case Opcode::ConstColor:
            if (op >= file_.pool.colors.size()) fail(at, "color constant index out of range");
            push(at, ValueType::Color);
            return;
        case Opcode::LocalGet:
            if (op >= localCount()) fail(at, "local index out of range");
            push(at, localType(op));
            return;
        case Opcode::LocalSet:
            if (op >= localCount()) fail(at, "local index out of range");
            expect(at, localType(op), "local.set");
            return;
        case Opcode::LocalTee:
            if (op >= localCount()) fail(at, "local index out of range");
            expect(at, localType(op), "local.tee");
            push(at, localType(op));
            return;
        case Opcode::Drop:
            (void)pop(at, "drop");
            return;
        case Opcode::Dup: {
            if (!reachable_) return;
            if (stack_.empty()) fail(at, "stack underflow (dup)");
            push(at, stack_.back());
            return;
        }

        case Opcode::Poly: {
            if (op > kMaxStackDepth) fail(at, "poly vertex count exceeds limit");
            expect(at, ValueType::Color, "poly colour");
            for (u16 v = 0; v < op * 2; ++v) {
                expect(at, ValueType::F32, "poly vertex");
            }
            return;
        }

        // Structured outputs (§9.9).
        case Opcode::OutPoint:
            checkSlot(at, op, OutputKind::Point, false, false);
            expect(at, ValueType::F32, "point y");
            expect(at, ValueType::F32, "point x");
            return;
        case Opcode::OutRect:
            checkSlot(at, op, OutputKind::Rect, false, false);
            for (int i = 0; i < 4; ++i) expect(at, ValueType::F32, "rect value");
            return;
        case Opcode::OutCircle:
            checkSlot(at, op, OutputKind::Circle, false, false);
            for (int i = 0; i < 3; ++i) expect(at, ValueType::F32, "circle value");
            return;
        case Opcode::OutListPoint:
            checkSlot(at, op, OutputKind::Point, true, false);
            expect(at, ValueType::F32, "point y");
            expect(at, ValueType::F32, "point x");
            return;
        case Opcode::OutListRect:
            checkSlot(at, op, OutputKind::Rect, true, false);
            for (int i = 0; i < 4; ++i) expect(at, ValueType::F32, "rect value");
            return;
        case Opcode::OutListCircle:
            checkSlot(at, op, OutputKind::Circle, true, false);
            for (int i = 0; i < 3; ++i) expect(at, ValueType::F32, "circle value");
            return;
        case Opcode::PartGetPoint:
            checkSlot(at, op, OutputKind::Point, false, true);
            push(at, ValueType::F32);
            push(at, ValueType::F32);
            return;
        case Opcode::PartGetRect:
            checkSlot(at, op, OutputKind::Rect, false, true);
            for (int i = 0; i < 4; ++i) push(at, ValueType::F32);
            return;
        case Opcode::PartGetCircle:
            checkSlot(at, op, OutputKind::Circle, false, true);
            for (int i = 0; i < 3; ++i) push(at, ValueType::F32);
            return;

        // Parts (§9.10).
        case Opcode::PartDraw: {
            if (op >= file_.code.parts.size()) {
                fail(at, "part index out of range");
            }
            const auto& part = file_.code.parts[op];
            expect(at, ValueType::F32, "part offset y");
            expect(at, ValueType::F32, "part offset x");
            expect(at, ValueType::I32, "part archetype");
            expect(at, ValueType::I32, "part variant");
            expect(at, ValueType::I32, "part seed");
            for (std::size_t p = part.inputs.size(); p > 0; --p) {
                expect(at, typeOf(part.inputs[p - 1]), "part uniform");
            }
            return;
        }
        default: break;
        }

        fail(at, "missing dynamic opcode validation");
    }

    void checkExit(std::size_t at, const ControlFrame& label, const char* what) {
        if (reachable_ && stack_ != label.entry) {
            fail(at, std::string(what) + " stack types differ from entry");
        }
    }

    void makeUnreachable() {
        reachable_ = false;
        stack_.clear();
    }

    void branch(std::size_t at, u16 depth) {
        if (depth >= labels_.size()) {
            fail(at, "branch depth out of range");
        }
        auto& target = labels_[labels_.size() - 1 - depth];
        if (reachable_) {
            if (stack_.size() < target.entry.size() ||
                !std::equal(target.entry.begin(), target.entry.end(), stack_.begin())) {
                fail(at, "branch stack types differ from target entry");
            }
            if (target.kind != Opcode::Loop) target.exitReachable = true;
        }
        // br_if must leave its untaken edge untouched. br makes the current
        // path unreachable; the target restores its own snapshot at its end.
    }
};

// Global slot descriptors: root outputs first, then parts (spec §4.3).
std::vector<SlotInfo> buildSlots(const LoadedFile& file) {
    std::vector<SlotInfo> slots;
    for (const auto& output : file.meta.outputs) {
        slots.push_back({output.kind, output.isList, true});
    }
    for (const auto& part : file.code.parts) {
        for (const auto& output : part.outputs) {
            slots.push_back({output.kind, output.isList, false});
        }
    }
    return slots;
}

ErrorLocation functionLocation(const LoadedFile& file, const Function& function) {
    auto location = ErrorLocation::at(function.fileOffset);
    location.flags |= ErrorLocation::Function;
    location.functionIndex = static_cast<u32>(&function - file.code.functions.data());
    return location;
}

void checkEntry(const LoadedFile& file) {
    const auto& entry = file.code.entry();
    if (entry.isAnonymous() || file.pool.strings[entry.nameIndex] != "draw") {
        throw FormatError("entry function must be named 'draw'", ErrorReason::InvalidSignature, ErrorStage::Validate,
            functionLocation(file, entry));
    }
    const std::size_t expected = 2 + file.meta.inputs.size();
    if (entry.params.size() != expected) {
        throw FormatError("draw signature mismatch: parameter count", ErrorReason::InvalidSignature, ErrorStage::Validate,
            functionLocation(file, entry));
    }
    for (std::size_t i = 0; i < expected; ++i) {
        const ValueType want = i < 2 ? ValueType::I32 : typeOf(file.meta.inputs[i - 2]);
        if (entry.params[i] != want) {
            throw FormatError("draw signature mismatch: parameter type", ErrorReason::InvalidSignature, ErrorStage::Validate,
                functionLocation(file, entry));
        }
    }
    if (!entry.results.empty()) {
        throw FormatError("draw must have no results", ErrorReason::InvalidSignature, ErrorStage::Validate,
            functionLocation(file, entry));
    }
}

void checkPartSignatures(const LoadedFile& file) {
    for (const auto& part : file.code.parts) {
        const auto& fn = file.code.functions[part.functionIndex];
        const std::size_t expected = 2 + part.inputs.size();
        if (fn.params.size() != expected) {
            throw FormatError("part function signature mismatch: parameter count", ErrorReason::InvalidSignature, ErrorStage::Validate,
                functionLocation(file, fn));
        }
        for (std::size_t i = 0; i < expected; ++i) {
            const ValueType want = i < 2 ? ValueType::I32 : typeOf(part.inputs[i - 2]);
            if (fn.params[i] != want) {
                throw FormatError("part function signature mismatch: parameter type", ErrorReason::InvalidSignature, ErrorStage::Validate,
                    functionLocation(file, fn));
            }
        }
        if (!fn.results.empty()) {
            throw FormatError("part function must have no results", ErrorReason::InvalidSignature, ErrorStage::Validate,
                functionLocation(file, fn));
        }
    }
}

// Part import graph: A -> B when some function reachable from A's function
// contains `part.draw B`. Cycles are rejected (spec §6.3/§14).
void checkPartAcyclicity(const LoadedFile& file) {
    const auto functionCount = file.code.functions.size();
    const auto partCount = file.code.parts.size();

    // Call graph from `call` opcodes.
    std::vector<std::vector<u16>> calls(functionCount);
    for (std::size_t f = 0; f < functionCount; ++f) {
        const auto& fn = file.code.functions[f];
        const auto count = fn.code.size() / 4;
        for (std::size_t i = 0; i < count; ++i) {
            const auto ins = decodeInstruction(fn.code.subspan(i * 4, 4));
            if (ins.opcode == Opcode::Call) {
                calls[f].push_back(ins.operand);
            }
        }
    }

    // For each part's function, walk reachable functions and collect direct
    // part references.
    std::vector<std::vector<u16>> partEdges(partCount);
    for (std::size_t p = 0; p < partCount; ++p) {
        std::vector<u8> visited(functionCount, 0);
        std::vector<u16> work;
        work.push_back(static_cast<u16>(file.code.parts[p].functionIndex));
        while (!work.empty()) {
            const u16 f = work.back();
            work.pop_back();
            if (visited[f]) continue;
            visited[f] = 1;
            const auto& fn = file.code.functions[f];
            const auto count = fn.code.size() / 4;
            for (std::size_t i = 0; i < count; ++i) {
                const auto ins = decodeInstruction(fn.code.subspan(i * 4, 4));
                if (ins.opcode == Opcode::PartDraw) {
                    partEdges[p].push_back(ins.operand);
                }
            }
            for (const u16 callee : calls[f]) {
                work.push_back(callee);
            }
        }
    }

    // DFS over parts; a back edge means a cycle.
    std::vector<u8> state(partCount, 0); // 0 = unvisited, 1 = in progress, 2 = done
    std::vector<u16> stack;
    for (std::size_t start = 0; start < partCount; ++start) {
        if (state[start] != 0) continue;
        stack.push_back(static_cast<u16>(start));
        while (!stack.empty()) {
            const u16 p = stack.back();
            if (state[p] == 0) {
                state[p] = 1;
                for (const u16 next : partEdges[p]) {
                    if (state[next] == 1) {
                        throw FormatError("part import cycle", ErrorReason::PartCycle, ErrorStage::Validate,
                            functionLocation(file, file.code.functions[file.code.parts[p].functionIndex]));
                    }
                    if (state[next] == 0) {
                        stack.push_back(next);
                    }
                }
            } else {
                stack.pop_back();
                state[p] = 2;
            }
        }
    }
}

} // namespace

LoadedFile loadFile(std::span<const u8> data) {
    LoadedFile file;
    // The file owns its buffer: spans stay valid for the LoadedFile's whole
    // lifetime (and after moves), independent of the caller's buffer.
    file.containerHeader = parseHeader(data);
    file.storage = decompressFile(data);
    const auto bytes = std::span<const u8>(file.storage);
    file.layout = parseLayout(bytes);
    file.pool = parseConsts(bytes, *file.layout.find(SectionId::Const));
    file.meta = parseMeta(bytes, *file.layout.find(SectionId::Meta), file.pool);
    file.code = parseCode(bytes, *file.layout.find(SectionId::Code), file.pool);
    return file;
}

void validate(const LoadedFile& file) {
    checkEntry(file);
    checkPartSignatures(file);
    const auto slots = buildSlots(file);
    for (std::size_t index = 0; index < file.code.functions.size(); ++index) {
        FunctionValidator(file, file.code.functions[index], slots, static_cast<u32>(index)).run();
    }
    checkPartAcyclicity(file);
}

} // namespace iprop
