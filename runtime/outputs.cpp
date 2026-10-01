#include "iprop/VmInternals.hpp"

namespace iprop::detail {
namespace {

// Runtime defenses mirror the validator: wrong slots on unvalidated input
// trap instead of corrupting memory.
void checkSlot(Execution& ex, u16 index, OutputKind kind, bool list) {
    if (index >= ex.outputSlots.size()) {
        ex.trap("output slot index out of range");
    }
    const auto& slot = ex.outputSlots[index];
    if (slot.kind != kind) {
        ex.trap("output slot kind mismatch");
    }
    if (slot.isList != list) {
        ex.trap(list ? "expected a list slot" : "expected a singular slot");
    }
}

// The D4 propagation of a point in the part's asset-unit space (spec
// §10.5); applied to a part frame's own writes when the call carries a
// transform.
void transformPoint(u8 kind, f32 sizeX, f32 sizeY, f32& x, f32& y) {
    switch (kind) {
    case 0: break;
    case 1: { const f32 nx = y; y = sizeX - x; x = nx; break; }
    case 2: x = sizeX - x; y = sizeY - y; break;
    case 3: { const f32 nx = sizeY - y; y = x; x = nx; break; }
    case 4: x = sizeX - x; break;
    case 5: y = sizeY - y; break;
    case 6: { const f32 nx = y; y = x; x = nx; break; }
    case 7: { const f32 nx = sizeY - y; y = sizeX - x; x = nx; break; }
    default: break; // unreachable: the validator caps the kind
    }
}

bool transformOutput(Execution& ex, u16 index, u8& kind, f32& sizeX, f32& sizeY) {
    if (ex.partCalls.empty()) {
        return false;
    }
    const PartCallState& state = ex.partCalls.back();
    // Helpers share the active part's canvas and output coordinates. A
    // global slot owned by another asset must keep its own coordinates.
    if (ex.slotOwner[index] != state.partIndex) {
        return false;
    }
    kind = state.transform;
    if (kind == 0) {
        return false;
    }
    const Part& part = ex.file.code.parts[state.partIndex];
    sizeX = static_cast<f32>(part.sizeX);
    sizeY = static_cast<f32>(part.sizeY);
    return true;
}

void writePoint(Execution& ex, u16 index, f32 x, f32 y, bool list) {
    u8 kind = 0;
    f32 sizeX = 0.f;
    f32 sizeY = 0.f;
    if (transformOutput(ex, index, kind, sizeX, sizeY)) {
        transformPoint(kind, sizeX, sizeY, x, y);
    }
    OutputSlot& slot = ex.outputSlots[index];
    if (!list) {
        slot.points.clear(); // singular: a later write replaces the value
    }
    slot.points.push_back({x, y});
}

void writeRect(Execution& ex, u16 index, f32 x, f32 y, f32 w, f32 h, bool list) {
    u8 kind = 0;
    f32 sizeX = 0.f;
    f32 sizeY = 0.f;
    if (transformOutput(ex, index, kind, sizeX, sizeY)) {
        // The axis-aligned bounds of the two transformed corners.
        f32 x2 = x + w;
        f32 y2 = y + h;
        transformPoint(kind, sizeX, sizeY, x, y);
        transformPoint(kind, sizeX, sizeY, x2, y2);
        const f32 minX = f32MinMax(x, x2, false);
        const f32 minY = f32MinMax(y, y2, false);
        w = f32MinMax(x, x2, true) - minX;
        h = f32MinMax(y, y2, true) - minY;
        x = minX;
        y = minY;
    }
    OutputSlot& slot = ex.outputSlots[index];
    if (!list) {
        slot.rects.clear();
    }
    slot.rects.push_back({x, y, w, h});
}

void writeCircle(Execution& ex, u16 index, f32 x, f32 y, f32 r, bool list) {
    u8 kind = 0;
    f32 sizeX = 0.f;
    f32 sizeY = 0.f;
    if (transformOutput(ex, index, kind, sizeX, sizeY)) {
        transformPoint(kind, sizeX, sizeY, x, y);
    }
    OutputSlot& slot = ex.outputSlots[index];
    if (!list) {
        slot.circles.clear();
    }
    slot.circles.push_back({x, y, r});
}

} // namespace

// Structured outputs (spec §9.9): singular slots are write-last, list slots
// append in program order.
bool stepOutputs(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::OutPoint: {
        checkSlot(ex, ins.operand, OutputKind::Point, false);
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        writePoint(ex, ins.operand, x, y, false);
        ++ex.frame().ip;
        return true;
    }
    case Opcode::OutRect: {
        checkSlot(ex, ins.operand, OutputKind::Rect, false);
        const f32 h = ex.popF32();
        const f32 w = ex.popF32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        writeRect(ex, ins.operand, x, y, w, h, false);
        ++ex.frame().ip;
        return true;
    }
    case Opcode::OutCircle: {
        checkSlot(ex, ins.operand, OutputKind::Circle, false);
        const f32 r = ex.popF32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        writeCircle(ex, ins.operand, x, y, r, false);
        ++ex.frame().ip;
        return true;
    }
    case Opcode::OutListPoint: {
        checkSlot(ex, ins.operand, OutputKind::Point, true);
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        writePoint(ex, ins.operand, x, y, true);
        ++ex.frame().ip;
        return true;
    }
    case Opcode::OutListRect: {
        checkSlot(ex, ins.operand, OutputKind::Rect, true);
        const f32 h = ex.popF32();
        const f32 w = ex.popF32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        writeRect(ex, ins.operand, x, y, w, h, true);
        ++ex.frame().ip;
        return true;
    }
    case Opcode::OutListCircle: {
        checkSlot(ex, ins.operand, OutputKind::Circle, true);
        const f32 r = ex.popF32();
        const f32 y = ex.popF32();
        const f32 x = ex.popF32();
        writeCircle(ex, ins.operand, x, y, r, true);
        ++ex.frame().ip;
        return true;
    }
    default:
        return false;
    }
}

} // namespace iprop::detail
