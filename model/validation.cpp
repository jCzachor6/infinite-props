#include "iprop/model/Model.hpp"

namespace iprop::model {

// The 2D instructions of a model program (spec models v1.2).
bool admits(Opcode opcode) {
    switch (opcode) {
    case Opcode::PartCreateRegion: case Opcode::PartCreateMask: case Opcode::PartCreateLayer:
    case Opcode::PartsFromRegions: case Opcode::PartsCount: case Opcode::PartsGet:
    case Opcode::PartId: case Opcode::PartSourceId: case Opcode::PartCenter:
    case Opcode::PartBoundsOrigin: case Opcode::PartBoundsSize: case Opcode::PartPlace:
    case Opcode::PartFill: case Opcode::PartTexture: case Opcode::DynamicPartDraw: case Opcode::PartMask:
    case Opcode::Pixel: case Opcode::Rect: case Opcode::Line: case Opcode::Poly: case Opcode::Oval:
    case Opcode::Weather: case Opcode::Curve: case Opcode::CurveCubic: case Opcode::PolyList:
    case Opcode::OutPoint: case Opcode::OutRect: case Opcode::OutCircle:
    case Opcode::OutListPoint: case Opcode::OutListRect: case Opcode::OutListCircle: case Opcode::OutListPart:
    case Opcode::PartGetPoint: case Opcode::PartGetRect: case Opcode::PartGetCircle:
    case Opcode::SurfaceNormal:
    case Opcode::LayerBegin: case Opcode::SurfacePaint: case Opcode::MaskFrom: case Opcode::MaskUnion:
    case Opcode::MaskIntersect: case Opcode::MaskSubtract: case Opcode::MaskInvert: case Opcode::MaskExpand:
    case Opcode::MaskShrink: case Opcode::MaskOffset: case Opcode::SurfacePaintMask: case Opcode::ClipBegin:
    case Opcode::OutMask: case Opcode::OutLayer: case Opcode::PartGetMask: case Opcode::PartGetLayer:
    case Opcode::LayerBlur: case Opcode::MaskBlur:
    case Opcode::NoiseMask: case Opcode::NoiseMaskConfig:
    case Opcode::Voronoi: case Opcode::VoronoiConfig: case Opcode::VoronoiMetric: case Opcode::VoronoiRows:
    case Opcode::VoronoiSites: case Opcode::VoronoiSitesRelax:
    case Opcode::DiagramCount: case Opcode::DiagramSite: case Opcode::DiagramNeighbors:
    case Opcode::Contour: case Opcode::ContourConfig:
    case Opcode::RegionsDiagram: case Opcode::RegionsDiagramConfig: case Opcode::RegionsContours:
    case Opcode::RegionsContoursConfig: case Opcode::RegionsCount: case Opcode::RegionsId:
    case Opcode::RegionsArea: case Opcode::RegionsCenter: case Opcode::RegionsNeighbors:
    case Opcode::RegionsPoints: case Opcode::RegionsMask: case Opcode::RegionsMaskAll:
    case Opcode::RegionsGap: case Opcode::RegionsGapConfig: case Opcode::RegionsBevel: case Opcode::RegionsBevelConfig:
    case Opcode::PartDraw:
        return false;
    default:
        return true;
    }
}

namespace {

ValueType slotType(const InputDecl& input) {
    switch (input.type) {
    case InputType::Color: return ValueType::Color;
    case InputType::F32: return ValueType::F32;
    case InputType::U32: return ValueType::I32;
    }
    return ValueType::I32;
}

class ModelValidation final : public ValidationExtension {
public:
    explicit ModelValidation(const LoadedModel& model) : model_(model) {}

    const char* entryName() const override { return "render"; }
    bool admits(Opcode opcode) const override { return model::admits(opcode); }

    bool check(Instruction ins, InstructionCheck& state) const override {
        if (ins.opcode != kModelPage) return false;
        const OpSpec* spec = findOp(ins.aux);
        if (!spec) state.fail("unknown model operation");
        if (!spec->operandIsImmediate && ins.operand != 0)
            state.fail(std::string(spec->mnemonic) + " operand must be zero");
        switch (spec->op) {
        case Op::PlaceBegin: {
            const u16 mask = ins.operand;
            if (!validPlaceMask(mask)) state.fail("invalid place.begin mask");
            // The arguments lie in bit order, so the top of the stack is the highest bit.
            if (mask & kPlaceMirrorZ) state.expect(ValueType::I32, "place mirror_z");
            if (mask & kPlaceMirrorY) state.expect(ValueType::I32, "place mirror_y");
            if (mask & kPlaceMirrorX) state.expect(ValueType::I32, "place mirror_x");
            if (mask & kPlaceScale3) state.expect(ValueType::Vec3, "place scale");
            if (mask & kPlaceScale) state.expect(ValueType::F32, "place scale");
            if (mask & kPlaceRoll) state.expect(ValueType::F32, "place roll");
            if (mask & kPlacePitch) state.expect(ValueType::F32, "place pitch");
            if (mask & kPlaceYaw) state.expect(ValueType::F32, "place yaw");
            if (mask & kPlaceAt) state.expect(ValueType::Vec3, "place at");
            state.openScope();
            return true;
        }
        case Op::SkinDraw: {
            if (ins.operand >= model_.skins.size()) state.fail("skin program index out of range");
            const auto& inputs = model_.skins[ins.operand].file.meta.inputs;
            state.expect(ValueType::I32, "skin height");
            state.expect(ValueType::I32, "skin width");
            state.expect(ValueType::I32, "skin seed");
            for (std::size_t i = inputs.size(); i > 0; --i) state.expect(slotType(inputs[i - 1]), "skin input");
            state.push(kSkinSlot);
            return true;
        }
        case Op::SmoothBegin: case Op::RefineBegin: case Op::BendBegin: case Op::TwistBegin: case Op::TaperBegin:
        case Op::DisplaceBegin:
            for (std::size_t i = spec->inputCount; i > 0; --i) state.expect(spec->inputs[i - 1], spec->mnemonic);
            state.openScope();
            return true;
        case Op::PartRender: {
            // The part's inputs, as its entry declares them, then the seed.
            if (ins.operand >= model_.parts.size()) state.fail("model part index out of range");
            const auto& entry = model_.file.code.functions[model_.parts[ins.operand].firstFunction];
            state.expect(ValueType::I32, "part seed");
            for (std::size_t i = entry.params.size(); i > 0; --i) state.expect(entry.params[i - 1], "part input");
            return true;
        }
        case Op::OutPoint3d: case Op::OutBox3d: case Op::OutListPoint3d: case Op::OutListBox3d:
        case Op::PartGetPoint3d: {
            if (ins.operand >= model_.outputs.size()) state.fail("model output slot out of range");
            const ModelOutputDecl& slot = model_.outputs[ins.operand];
            const bool box = spec->op == Op::OutBox3d || spec->op == Op::OutListBox3d;
            const bool list = spec->op == Op::OutListPoint3d || spec->op == Op::OutListBox3d;
            if ((slot.kind == ModelOutputKind::Box3d) != box || slot.list != list)
                state.fail(std::string(spec->mnemonic) + " does not match the kind of its slot");
            break;
        }
        default:
            break;
        }
        for (std::size_t i = spec->inputCount; i > 0; --i) state.expect(spec->inputs[i - 1], spec->mnemonic);
        if (spec->result) state.push(*spec->result);
        return true;
    }

private:
    const LoadedModel& model_;
};

// The asset a function belongs to: a part index, or kRootParent for the
// model's own functions.
u32 ownerOf(const LoadedModel& model, std::size_t function) {
    for (u32 part = 0; part < model.parts.size(); ++part) {
        const ModelPartDecl& decl = model.parts[part];
        if (function >= decl.firstFunction && function - decl.firstFunction < decl.functionCount) return part;
    }
    return kRootParent;
}

[[noreturn]] void misplaced(const std::string& message, std::size_t function, std::size_t pc) {
    FormatError error(message, ErrorReason::InvalidInstruction, ErrorStage::Validate);
    error.location.flags |= ErrorLocation::Function | ErrorLocation::PC;
    error.location.functionIndex = static_cast<u32>(function);
    error.location.pc = static_cast<u32>(pc);
    throw error;
}

// Each asset reaches only what is its own (M3): its helpers, its slots and
// its direct parts; a part's entry returns nothing.
void checkOwnership(const LoadedModel& model) {
    const auto& functions = model.file.code.functions;
    for (const ModelPartDecl& part : model.parts)
        if (!functions[part.firstFunction].results.empty())
            throw FormatError("model part '" + part.name + "' entry must return nothing", ErrorReason::InvalidSignature,
                              ErrorStage::Validate);
    for (std::size_t f = 0; f < functions.size(); ++f) {
        const u32 owner = ownerOf(model, f);
        const u16 slotOwner = owner == kRootParent ? kRootOwner : static_cast<u16>(owner);
        const auto code = functions[f].code;
        for (std::size_t pc = 0; pc * 4 + 4 <= code.size(); ++pc) {
            const Instruction ins = decodeInstruction(code.subspan(pc * 4, 4));
            if (ins.opcode == Opcode::Call) {
                if (ins.operand < functions.size() && ownerOf(model, ins.operand) != owner)
                    misplaced("a call reaches a function of another asset", f, pc);
                continue;
            }
            if (ins.opcode != kModelPage) continue;
            const OpSpec* spec = findOp(ins.aux);
            if (!spec) continue; // the validator reports it
            switch (spec->op) {
            case Op::OutPoint3d: case Op::OutBox3d: case Op::OutListPoint3d: case Op::OutListBox3d:
                if (ins.operand < model.outputs.size() && model.outputs[ins.operand].owner != slotOwner)
                    misplaced("an asset writes only its own outputs", f, pc);
                break;
            case Op::PartRender:
                if (ins.operand < model.parts.size() && model.parts[ins.operand].parent != owner)
                    misplaced("an asset builds only its own parts", f, pc);
                break;
            case Op::PartGetPoint3d:
                if (ins.operand < model.outputs.size()) {
                    const u16 reads = model.outputs[ins.operand].owner;
                    if (reads == kRootOwner || model.parts[reads].parent != owner)
                        misplaced("an asset reads the outputs of its own parts only", f, pc);
                }
                break;
            default:
                break;
            }
        }
    }
}

} // namespace

void validateModel(const LoadedModel& model) {
    const ModelValidation extension(model);
    validate(model.file, &extension);
    checkOwnership(model);
    for (const SkinProgram& program : model.skins) {
        try {
            validate(program.file);
        } catch (const FormatError& error) {
            throw FormatError("skin program '" + program.name + "': " + error.what(), ErrorReason::InvalidMetadata,
                              ErrorStage::Validate);
        }
    }
}

} // namespace iprop::model
