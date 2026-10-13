#include "iprop/VmInternals.hpp"

namespace iprop::detail {
namespace {

// Shared branch mechanics: the target label stays active (its `end` pops
// it); inner labels unwind and the stack restores the target's entry height.
void branch(Execution& ex, Frame& frame, u16 depth) {
    if (depth >= frame.labels.size()) {
        ex.trap("branch depth out of range", ErrorReason::InvalidControl);
    }
    const Label target = frame.labels[frame.labels.size() - 1 - depth];
    ex.closeLayers(frame.labels.size() - depth);
    frame.labels.resize(frame.labels.size() - depth);
    frame.stack.resize(target.stackHeight);
    frame.ip = target.kind == 1 ? target.cont : target.end;
}

} // namespace

// Structured control flow (spec §9.1), with WASM branch semantics.
bool stepControl(Execution& ex, Instruction ins) {
    Frame& frame = ex.frames.back();
    switch (ins.opcode) {
    case Opcode::Block:
    case Opcode::Loop:
    case Opcode::LayerBegin:
    case Opcode::ClipBegin: {
        ex.ensureControl(frame.functionIndex);
        const auto& info = ex.control[frame.functionIndex][frame.ip];
        Label label;
        if (ins.opcode == Opcode::LayerBegin) ex.beginLayer(ins.operand, ins.aux);
        if (ins.opcode == Opcode::ClipBegin) ex.beginClip();
        label.kind = ins.opcode == Opcode::Loop ? 1u : ins.opcode == Opcode::LayerBegin ? 3u :
            ins.opcode == Opcode::ClipBegin ? 4u : 0u;
        label.cont = ins.opcode == Opcode::Loop ? frame.ip + 1 : info.endIndex;
        label.end = info.endIndex;
        label.stackHeight = frame.stack.size();
        frame.labels.push_back(label);
        ++frame.ip;
        return true;
    }

    case Opcode::If: {
        ex.ensureControl(frame.functionIndex);
        const auto& info = ex.control[frame.functionIndex][frame.ip];
        const i32 condition = ex.popI32();
        Label label;
        label.kind = 2u;
        label.cont = info.endIndex;
        label.end = info.endIndex;
        label.stackHeight = frame.stack.size(); // after popping the condition
        frame.labels.push_back(label);
        if (condition != 0) {
            ++frame.ip;
        } else if (info.elseIndex != ControlInfo::kNone) {
            frame.ip = info.elseIndex + 1;
        } else {
            frame.ip = info.endIndex; // the matching `end` pops the label
        }
        return true;
    }

    case Opcode::Else: {
        if (frame.labels.empty()) {
            ex.trap("else without if", ErrorReason::InvalidControl);
        }
        // Reached by falling through the then-branch: restore the entry
        // height and skip the else body; the `end` pops the label.
        const Label label = frame.labels.back();
        frame.stack.resize(label.stackHeight);
        frame.ip = label.end;
        return true;
    }

    case Opcode::End: {
        if (frame.labels.empty()) {
            ex.trap("end without block", ErrorReason::InvalidControl);
        }
        const Label label = frame.labels.back();
        frame.labels.pop_back();
        ex.closeLayers(frame.labels.size());
        frame.stack.resize(label.stackHeight);
        ++frame.ip;
        return true;
    }

    case Opcode::Br: {
        branch(ex, frame, ins.operand);
        return true;
    }

    case Opcode::BrIf: {
        const i32 condition = ex.popI32();
        if (condition != 0) {
            branch(ex, frame, ins.operand);
        } else {
            ++frame.ip;
        }
        return true;
    }

    default:
        return false;
    }
}

} // namespace iprop::detail
