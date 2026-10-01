#include "iprop/VmInternals.hpp"

namespace iprop::detail {

// Parts and composition (spec §9.10/§10): part.draw renders a part into its
// own sub-canvas and blits it; part.get.* reads the last-called part's
// structured outputs from the global slot table.
bool stepParts(Execution& ex, Instruction ins) {
    switch (ins.opcode) {
    case Opcode::PartDraw:
        ex.callPart(ins.operand, ins.aux);
        return true;

    case Opcode::PartGetPoint:
    case Opcode::PartGetRect:
    case Opcode::PartGetCircle: {
        const u16 slot = ins.operand;
        if (slot >= ex.outputSlots.size()) {
            ex.trap("output slot index out of range");
        }
        if (ex.slotOwner[slot] == kNoPartIndex) {
            ex.trap("part.get reads a root-owned slot");
        }
        const auto& output = ex.outputSlots[slot];
        const OutputKind want = ins.opcode == Opcode::PartGetPoint
                                    ? OutputKind::Point
                                    : ins.opcode == Opcode::PartGetRect ? OutputKind::Rect
                                                                        : OutputKind::Circle;
        if (output.kind != want) {
            ex.trap("output slot kind mismatch");
        }
        if (output.isList) {
            ex.trap("expected a singular slot");
        }
        if (ex.slotOwner[slot] != ex.frame().lastPart) {
            ex.trap("slot does not belong to the last called part");
        }
        switch (want) {
        case OutputKind::Point:
            if (output.points.empty()) {
                ex.trap("part output not written");
            }
            ex.push(Value(output.points.back().x));
            ex.push(Value(output.points.back().y));
            break;
        case OutputKind::Rect:
            if (output.rects.empty()) {
                ex.trap("part output not written");
            }
            ex.push(Value(output.rects.back().x));
            ex.push(Value(output.rects.back().y));
            ex.push(Value(output.rects.back().w));
            ex.push(Value(output.rects.back().h));
            break;
        case OutputKind::Circle:
            if (output.circles.empty()) {
                ex.trap("part output not written");
            }
            ex.push(Value(output.circles.back().x));
            ex.push(Value(output.circles.back().y));
            ex.push(Value(output.circles.back().r));
            break;
        }
        ++ex.frame().ip;
        return true;
    }

    default:
        return false;
    }
}

} // namespace iprop::detail
