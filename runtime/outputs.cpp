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

// A write by the active part (also from its helpers) maps its local value to
// the caller: the D4 transform, plus the blit origin in files with a parts
// section (spec v0.5 §6). Each enclosing part then maps the published copy
// up to the root, once, at write time. Writes to foreign slots are not mapped.
struct OutputMapping {
    const Execution& ex;
    bool active = false;
    bool placed = false;

    OutputMapping(const Execution& execution, u16 index)
        : ex(execution), active(!ex.partCalls.empty() && ex.slotOwner[index] == ex.partCalls.back().partIndex),
          placed(!ex.file.code.parts.empty() && ex.file.code.parts.front().nameIndex.has_value()) {}

    template <class T> T read(T value) const {
        if (!active) return value;
        const auto& render = ex.partCalls.back().render;
        return placed ? render.toCaller(value) : local(render, value);
    }
    template <class T> T published(T value) const {
        if (!active || !placed) return value;
        for (std::size_t i = ex.partCalls.size() - 1; i-- > 0;) value = ex.partCalls[i].render.toCaller(value);
        return value;
    }

private:
    static Vec2 local(const PartRenderPlan& render, Vec2 value) { return render.transform.mapPoint(value, render.assetExtent); }
    static OutputRect local(const PartRenderPlan& render, OutputRect value) { return render.transform.mapRect(value, render.assetExtent); }
};

void writePoint(Execution& ex, u16 index, f32 x, f32 y, bool list) {
    const OutputMapping mapping(ex, index);
    const Vec2 read = mapping.read(Vec2{x, y});
    const Vec2 published = mapping.published(read);
    OutputSlot& slot = ex.outputSlots[index];
    slot.readPoint = {read.x, read.y};
    if (!list) {
        slot.points.clear(); // singular: a later write replaces the value
    }
    slot.points.push_back({published.x, published.y});
}

void writeRect(Execution& ex, u16 index, f32 x, f32 y, f32 w, f32 h, bool list) {
    const OutputMapping mapping(ex, index);
    const OutputRect read = mapping.read(OutputRect{x, y, w, h});
    const OutputRect published = mapping.published(read);
    OutputSlot& slot = ex.outputSlots[index];
    slot.readRect = read;
    if (!list) {
        slot.rects.clear();
    }
    slot.rects.push_back(published);
}

void writeCircle(Execution& ex, u16 index, f32 x, f32 y, f32 r, bool list) {
    const OutputMapping mapping(ex, index);
    const Vec2 read = mapping.read(Vec2{x, y});
    const Vec2 published = mapping.published(read);
    OutputSlot& slot = ex.outputSlots[index];
    slot.readCircle = {read.x, read.y, r};
    if (!list) {
        slot.circles.clear();
    }
    slot.circles.push_back({published.x, published.y, r});
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
