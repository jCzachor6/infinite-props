#include "iprop/Validator.hpp"

#include "iprop/Instruction.hpp"

#include <string>
#include <vector>

namespace iprop {
namespace {

const char* typeName(ValueType type) {
    switch (type) {
    case ValueType::I32: return "i32";
    case ValueType::F32: return "f32";
    case ValueType::Color: return "color";
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

// Per-function stack/label type checking over structured control flow
// (spec §9/§14). Blocks are statement-level; no block results in v0.1.
class FunctionValidator {
public:
    FunctionValidator(const LoadedFile& file, const Function& function,
                      const std::vector<SlotInfo>& slots)
        : file_(file), fn_(function), slots_(slots) {}

    void run() {
        const auto count = fn_.code.size() / 4;
        for (std::size_t i = 0; i < count; ++i) {
            const auto ins = decodeInstruction(fn_.code.subspan(i * 4, 4));
            if (ins.aux != 0) {
                fail(i, "aux byte must be zero");
            }
            step(i, ins);
        }
        if (!labels_.empty()) {
            fail(count, "unbalanced structured control flow");
        }
        if (stack_.size() != fn_.results.size()) {
            fail(count, "function stack does not match declared results");
        }
        for (std::size_t r = 0; r < fn_.results.size(); ++r) {
            if (stack_[r] != fn_.results[fn_.results.size() - 1 - r]) {
                fail(count, "function result type mismatch");
            }
        }
    }

private:
    const LoadedFile& file_;
    const Function& fn_;
    const std::vector<SlotInfo>& slots_;
    std::vector<ValueType> stack_;
    std::vector<std::size_t> labels_; // operand stack height at label entry

    [[noreturn]] void fail(std::size_t instruction, std::string message) const {
        throw FormatError(std::move(message), fn_.fileOffset + instruction * 4);
    }

    void push(std::size_t at, ValueType type) {
        if (stack_.size() >= kMaxStackDepth) {
            fail(at, "operand stack depth exceeds limit");
        }
        stack_.push_back(type);
    }

    ValueType pop(std::size_t at, const char* what) {
        if (stack_.empty()) {
            fail(at, std::string("stack underflow (") + what + ")");
        }
        const ValueType type = stack_.back();
        stack_.pop_back();
        return type;
    }

    void expect(std::size_t at, ValueType expected, const char* what) {
        const ValueType found = pop(at, what);
        if (found != expected) {
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

    void step(std::size_t at, const Instruction ins) {
        const u16 op = ins.operand;
        switch (ins.opcode) {
        case Opcode::Nop: return;

        case Opcode::Block:
        case Opcode::Loop:
            labels_.push_back(stack_.size());
            return;
        case Opcode::If:
            expect(at, ValueType::I32, "if condition");
            labels_.push_back(stack_.size());
            return;
        case Opcode::Else:
            if (labels_.empty()) fail(at, "else without if");
            if (stack_.size() != labels_.back()) {
                fail(at, "then-branch leaves values on the stack");
            }
            return;
        case Opcode::End:
            if (labels_.empty()) fail(at, "end without block");
            if (stack_.size() != labels_.back()) {
                fail(at, "block leaves values on the stack");
            }
            labels_.pop_back();
            return;
        case Opcode::Br:
            branch(at, op);
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
            if (stack_.size() != fn_.results.size()) {
                fail(at, "return with wrong stack height");
            }
            for (std::size_t r = 0; r < fn_.results.size(); ++r) {
                if (stack_[r] != fn_.results[fn_.results.size() - 1 - r]) {
                    fail(at, "return type mismatch");
                }
            }
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
            if (stack_.empty()) fail(at, "stack underflow (dup)");
            push(at, stack_.back());
            return;
        }

        // Integer arithmetic and comparisons (§9.3).
        case Opcode::I32Add: case Opcode::I32Sub: case Opcode::I32Mul:
        case Opcode::I32DivS: case Opcode::I32DivU: case Opcode::I32RemS:
        case Opcode::I32RemU: case Opcode::I32And: case Opcode::I32Or:
        case Opcode::I32Xor: case Opcode::I32Shl: case Opcode::I32ShrS:
        case Opcode::I32ShrU:
        case Opcode::I32Eq: case Opcode::I32Ne: case Opcode::I32LtS:
        case Opcode::I32LtU: case Opcode::I32GtS: case Opcode::I32GtU:
        case Opcode::I32LeS: case Opcode::I32LeU: case Opcode::I32GeS:
        case Opcode::I32GeU:
            expect(at, ValueType::I32, "integer operand");
            expect(at, ValueType::I32, "integer operand");
            push(at, ValueType::I32);
            return;
        case Opcode::I32Neg: case Opcode::I32Not:
            expect(at, ValueType::I32, "integer operand");
            push(at, ValueType::I32);
            return;

        // Float arithmetic and comparisons (§9.4).
        case Opcode::F32Add: case Opcode::F32Sub: case Opcode::F32Mul:
        case Opcode::F32Div:
            expect(at, ValueType::F32, "float operand");
            expect(at, ValueType::F32, "float operand");
            push(at, ValueType::F32);
            return;
        case Opcode::F32Eq: case Opcode::F32Ne: case Opcode::F32Lt:
        case Opcode::F32Gt: case Opcode::F32Le: case Opcode::F32Ge:
            expect(at, ValueType::F32, "float operand");
            expect(at, ValueType::F32, "float operand");
            push(at, ValueType::I32);
            return;
        case Opcode::F32Neg: case Opcode::F32Abs: case Opcode::F32Sqrt:
            expect(at, ValueType::F32, "float operand");
            push(at, ValueType::F32);
            return;
        case Opcode::I32ToF32:
            expect(at, ValueType::I32, "conversion operand");
            push(at, ValueType::F32);
            return;
        case Opcode::F32ToI32:
            expect(at, ValueType::F32, "conversion operand");
            push(at, ValueType::I32);
            return;

        // Colour (§9.5).
        case Opcode::ColorMix:
            expect(at, ValueType::F32, "mix amount");
            expect(at, ValueType::Color, "mix colour");
            expect(at, ValueType::Color, "mix colour");
            push(at, ValueType::Color);
            return;
        case Opcode::ColorShade:
            expect(at, ValueType::I32, "shade amount");
            expect(at, ValueType::Color, "shade colour");
            push(at, ValueType::Color);
            return;
        case Opcode::ColorSetA: // reserved in v0.1
            fail(at, "reserved opcode");

        // Seeded primitives (§9.6).
        case Opcode::StreamPush:
            expect(at, ValueType::I32, "stream seed");
            return;
        case Opcode::StreamPop: return;
        case Opcode::RngUnit: push(at, ValueType::F32); return;
        case Opcode::RngRange:
            expect(at, ValueType::F32, "range bound");
            expect(at, ValueType::F32, "range bound");
            push(at, ValueType::F32);
            return;
        case Opcode::RngIndex:
            expect(at, ValueType::I32, "index bound");
            push(at, ValueType::I32);
            return;
        case Opcode::Hash:
            expect(at, ValueType::I32, "hash input");
            push(at, ValueType::I32);
            return;

        // Noise and helpers (§9.7).
        case Opcode::Noise:
        case Opcode::Fault:
            expect(at, ValueType::I32, "noise seed");
            expect(at, ValueType::F32, "noise coordinate");
            expect(at, ValueType::F32, "noise coordinate");
            push(at, ValueType::F32);
            return;
        case Opcode::Smooth:
            expect(at, ValueType::F32, "smooth input");
            push(at, ValueType::F32);
            return;
        case Opcode::Lerp:
            expect(at, ValueType::F32, "lerp amount");
            expect(at, ValueType::F32, "lerp operand");
            expect(at, ValueType::F32, "lerp operand");
            push(at, ValueType::F32);
            return;

        // Canvas (§9.8).
        case Opcode::Pixel:
            expect(at, ValueType::Color, "pixel colour");
            expect(at, ValueType::I32, "pixel y");
            expect(at, ValueType::I32, "pixel x");
            return;
        case Opcode::Rect:
            expect(at, ValueType::Color, "rect colour");
            for (int i = 0; i < 4; ++i) expect(at, ValueType::F32, "rect extent");
            return;
        case Opcode::Line:
            expect(at, ValueType::Color, "line colour");
            expect(at, ValueType::I32, "line thickness");
            for (int i = 0; i < 4; ++i) expect(at, ValueType::F32, "line endpoint");
            return;
        case Opcode::Poly: {
            if (op > kMaxStackDepth) fail(at, "poly vertex count exceeds limit");
            expect(at, ValueType::Color, "poly colour");
            for (u16 v = 0; v < op * 2; ++v) {
                expect(at, ValueType::F32, "poly vertex");
            }
            return;
        }
        case Opcode::Oval:
            expect(at, ValueType::Color, "oval colour");
            for (int i = 0; i < 4; ++i) expect(at, ValueType::F32, "oval extent");
            return;
        case Opcode::Weather:
            expect(at, ValueType::I32, "weather amount");
            expect(at, ValueType::I32, "weather seed");
            return;

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
        }

        fail(at, "reserved opcode");
    }

    void branch(std::size_t at, u16 depth) {
        if (depth >= labels_.size()) {
            fail(at, "branch depth out of range");
        }
        stack_.resize(labels_[labels_.size() - 1 - depth]);
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

void checkEntry(const LoadedFile& file) {
    const auto& entry = file.code.entry();
    if (entry.isAnonymous() || file.pool.strings[entry.nameIndex] != "draw") {
        throw FormatError("entry function must be named 'draw'", entry.fileOffset);
    }
    const std::size_t expected = 2 + file.meta.inputs.size();
    if (entry.params.size() != expected) {
        throw FormatError("draw signature mismatch: parameter count", entry.fileOffset);
    }
    for (std::size_t i = 0; i < expected; ++i) {
        const ValueType want = i < 2 ? ValueType::I32 : typeOf(file.meta.inputs[i - 2]);
        if (entry.params[i] != want) {
            throw FormatError("draw signature mismatch: parameter type", entry.fileOffset);
        }
    }
    if (!entry.results.empty()) {
        throw FormatError("draw must have no results", entry.fileOffset);
    }
}

void checkPartSignatures(const LoadedFile& file) {
    for (const auto& part : file.code.parts) {
        const auto& fn = file.code.functions[part.functionIndex];
        const std::size_t expected = 2 + part.inputs.size();
        if (fn.params.size() != expected) {
            throw FormatError("part function signature mismatch: parameter count", fn.fileOffset);
        }
        for (std::size_t i = 0; i < expected; ++i) {
            const ValueType want = i < 2 ? ValueType::I32 : typeOf(part.inputs[i - 2]);
            if (fn.params[i] != want) {
                throw FormatError("part function signature mismatch: parameter type", fn.fileOffset);
            }
        }
        if (!fn.results.empty()) {
            throw FormatError("part function must have no results", fn.fileOffset);
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
                        throw FormatError("part import cycle",
                                          file.code.functions[file.code.parts[p].functionIndex]
                                              .fileOffset);
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
    file.storage.assign(data.begin(), data.end());
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
    for (const auto& function : file.code.functions) {
        FunctionValidator(file, function, slots).run();
    }
    checkPartAcyclicity(file);
}

} // namespace iprop
