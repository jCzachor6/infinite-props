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
            ex.trap("output slot index out of range", ErrorReason::IndexRange);
        }
        if (ex.slotOwner[slot] == kNoPartIndex) {
            ex.trap("part.get reads a root-owned slot", ErrorReason::InvalidOutput);
        }
        const auto& output = ex.outputSlots[slot];
        const OutputKind want = ins.opcode == Opcode::PartGetPoint
                                    ? OutputKind::Point
                                    : ins.opcode == Opcode::PartGetRect ? OutputKind::Rect
                                                                        : OutputKind::Circle;
        if (output.kind != want) {
            ex.trap("output slot kind mismatch", ErrorReason::InvalidOutput);
        }
        if (output.isList) {
            ex.trap("expected a singular slot", ErrorReason::InvalidOutput);
        }
        if (ex.slotOwner[slot] != ex.frame().lastPart) {
            ex.trap("slot does not belong to the last called part", ErrorReason::InvalidOutput);
        }
        // The read copy is in the caller's coordinates with a parts section
        // (spec v0.5 §6); otherwise it equals the published value.
        switch (want) {
        case OutputKind::Point:
            if (output.points.empty()) {
                ex.trap("part output not written", ErrorReason::PartOutputMissing);
            }
            ex.push(Value(output.readPoint.x));
            ex.push(Value(output.readPoint.y));
            break;
        case OutputKind::Rect:
            if (output.rects.empty()) {
                ex.trap("part output not written", ErrorReason::PartOutputMissing);
            }
            ex.push(Value(output.readRect.x));
            ex.push(Value(output.readRect.y));
            ex.push(Value(output.readRect.w));
            ex.push(Value(output.readRect.h));
            break;
        case OutputKind::Circle:
            if (output.circles.empty()) {
                ex.trap("part output not written", ErrorReason::PartOutputMissing);
            }
            ex.push(Value(output.readCircle.x));
            ex.push(Value(output.readCircle.y));
            ex.push(Value(output.readCircle.r));
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
