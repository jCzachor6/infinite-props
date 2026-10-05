#include "iprop/VmInternals.hpp"

namespace iprop::detail {
namespace {

// Runtime defenses mirror the validator: wrong slots on unvalidated input
// trap instead of corrupting memory.
void checkSlot(Execution& ex, u16 index, OutputKind kind, bool list) {
    if (index >= ex.outputSlots.size()) {
        ex.trap("output slot index out of range", ErrorReason::IndexRange);
    }
    const auto& slot = ex.outputSlots[index];
    if (slot.kind != kind) {
        ex.trap("output slot kind mismatch", ErrorReason::InvalidOutput);
    }
    if (slot.isList != list) {
        ex.trap(list ? "expected a list slot" : "expected a singular slot", ErrorReason::InvalidOutput);
    }
}

// Helpers share the active part's geometry; writes to foreign slots do not.
const PartRenderPlan* outputTransform(const Execution& ex, u16 index) {
    if (ex.partCalls.empty()) return nullptr;
    const PartCallState& state = ex.partCalls.back();
    return ex.slotOwner[index] == state.partIndex ? &state.render : nullptr;
}

void writePoint(Execution& ex, u16 index, f32 x, f32 y, bool list) {
    if (const auto* render = outputTransform(ex, index)) {
        const auto point = render->transform.mapPoint({x, y}, render->assetExtent);
        x = point.x;
        y = point.y;
    }
    OutputSlot& slot = ex.outputSlots[index];
    if (!list) {
        slot.points.clear(); // singular: a later write replaces the value
    }
    slot.points.push_back({x, y});
}

void writeRect(Execution& ex, u16 index, f32 x, f32 y, f32 w, f32 h, bool list) {
    if (const auto* render = outputTransform(ex, index)) {
        const auto rect = render->transform.mapRect({x, y, w, h}, render->assetExtent);
        x = rect.x; y = rect.y; w = rect.w; h = rect.h;
    }
    OutputSlot& slot = ex.outputSlots[index];
    if (!list) {
        slot.rects.clear();
    }
    slot.rects.push_back({x, y, w, h});
}

void writeCircle(Execution& ex, u16 index, f32 x, f32 y, f32 r, bool list) {
    if (const auto* render = outputTransform(ex, index)) {
        const auto point = render->transform.mapPoint({x, y}, render->assetExtent);
        x = point.x;
        y = point.y;
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
