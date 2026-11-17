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
        default:
            for (std::size_t i = spec->inputCount; i > 0; --i) state.expect(spec->inputs[i - 1], spec->mnemonic);
            if (spec->result) state.push(*spec->result);
            return true;
        }
    }

private:
    const LoadedModel& model_;
};

} // namespace

void validateModel(const LoadedModel& model) {
    const ModelValidation extension(model);
    validate(model.file, &extension);
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
