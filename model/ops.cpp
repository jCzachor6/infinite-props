#include "iprop/model/Ops.hpp"

#include <initializer_list>

namespace iprop::model {
namespace {

using T = TypeDescriptor;

constexpr OpSpec fixed(Op op, const char* mnemonic, std::initializer_list<TypeDescriptor> inputs,
                       std::optional<TypeDescriptor> result = {}) {
    OpSpec spec{op, mnemonic, {}, 0, {}, false};
    for (const auto input : inputs) spec.inputs[spec.inputCount++] = input;
    spec.result = result;
    return spec;
}

constexpr OpSpec dynamic(Op op, const char* mnemonic) {
    OpSpec spec{op, mnemonic, {}, 0, {}, false};
    spec.operandIsImmediate = true;
    return spec;
}

// A fixed signature whose operand names a slot or a part.
constexpr OpSpec indexed(Op op, const char* mnemonic, std::initializer_list<TypeDescriptor> inputs,
                         std::optional<TypeDescriptor> result = {}) {
    OpSpec spec = fixed(op, mnemonic, inputs, result);
    spec.operandIsImmediate = true;
    return spec;
}

constexpr TypeDescriptor kProfile = TypeDescriptor::list(TypeDescriptor::Vec2);
constexpr TypeDescriptor kPath = TypeDescriptor::list(TypeDescriptor::Vec3);
constexpr TypeDescriptor kSkin = kSkinSlot;

constexpr OpSpec kSpecs[] = {
    fixed(Op::Vec3Make, "vec3.make", {T::F32, T::F32, T::F32}, T::Vec3),
    fixed(Op::Vec3Add, "vec3.add", {T::Vec3, T::Vec3}, T::Vec3),
    fixed(Op::Vec3Sub, "vec3.sub", {T::Vec3, T::Vec3}, T::Vec3),
    fixed(Op::Vec3Mul, "vec3.mul", {T::Vec3, T::Vec3}, T::Vec3),
    fixed(Op::Vec3Div, "vec3.div", {T::Vec3, T::Vec3}, T::Vec3),
    fixed(Op::Vec3Scale, "vec3.scale", {T::Vec3, T::F32}, T::Vec3),
    fixed(Op::Vec3Neg, "vec3.neg", {T::Vec3}, T::Vec3),
    fixed(Op::Vec3X, "vec3.x", {T::Vec3}, T::F32),
    fixed(Op::Vec3Y, "vec3.y", {T::Vec3}, T::F32),
    fixed(Op::Vec3Z, "vec3.z", {T::Vec3}, T::F32),
    fixed(Op::Vec3Len, "vec3.len", {T::Vec3}, T::F32),
    fixed(Op::Vec3Dot, "vec3.dot", {T::Vec3, T::Vec3}, T::F32),
    fixed(Op::Vec3Cross, "vec3.cross", {T::Vec3, T::Vec3}, T::Vec3),
    fixed(Op::Vec3Normalize, "vec3.normalize", {T::Vec3}, T::Vec3),
    fixed(Op::Vec3Mix, "vec3.mix", {T::Vec3, T::Vec3, T::F32}, T::Vec3),

    dynamic(Op::PlaceBegin, "place.begin"),
    fixed(Op::SmoothBegin, "smooth.begin", {T::F32}),
    fixed(Op::RefineBegin, "refine.begin", {T::I32}),
    fixed(Op::BendBegin, "bend.begin", {T::F32}),
    fixed(Op::TwistBegin, "twist.begin", {T::F32}),
    fixed(Op::TaperBegin, "taper.begin", {T::F32}),
    fixed(Op::DisplaceBegin, "displace.begin", {T::F32, T::F32, T::I32}),
    fixed(Op::SculptBegin, "sculpt.begin", {T::F32, T::F32, kSkin}),
    fixed(Op::CarveBegin, "carve.begin", {}),

    fixed(Op::Box, "box", {T::Vec3, T::Vec3, kSkin}),
    fixed(Op::BoxBevel, "box.bevel", {T::Vec3, T::Vec3, T::F32, kSkin}),
    fixed(Op::Cylinder, "cylinder", {T::Vec3, T::F32, T::F32, T::I32, kSkin}),
    fixed(Op::Cone, "cone", {T::Vec3, T::F32, T::F32, T::F32, T::I32, kSkin}),
    fixed(Op::Band, "band", {T::Vec3, T::F32, T::F32, T::F32, T::I32, kSkin}),
    fixed(Op::Sphere, "sphere", {T::Vec3, T::F32, T::I32, T::I32, kSkin}),
    fixed(Op::Lathe, "lathe", {T::Vec3, kProfile, T::I32, kSkin}),
    fixed(Op::LatheCrease, "lathe.crease", {T::Vec3, kProfile, T::I32, T::F32, kSkin}),
    fixed(Op::Extrude, "extrude", {T::Vec3, kProfile, T::F32, kSkin}),
    fixed(Op::ExtrudeBevel, "extrude.bevel", {T::Vec3, kProfile, T::F32, T::F32, kSkin}),
    fixed(Op::Quad, "quad", {T::Vec3, T::Vec3, T::Vec3, T::Vec3, kSkin}),
    fixed(Op::Tri, "tri", {T::Vec3, T::Vec3, T::Vec3, kSkin}),
    fixed(Op::Tube, "tube", {T::Vec3, T::Vec3, T::Vec3, T::Vec3, T::F32, T::F32, T::I32, T::I32, kSkin}),
    fixed(Op::Sweep, "sweep", {kPath, kProfile, kSkin}),

    dynamic(Op::SkinDraw, "skin.draw"),
    fixed(Op::SkinDensity, "skin.density", {kSkin, T::F32}, kSkin),
    fixed(Op::SkinFit, "skin.fit", {kSkin}, kSkin),
    fixed(Op::SkinShift, "skin.shift", {kSkin, T::Vec2}, kSkin),
    fixed(Op::SkinRoughness, "skin.roughness", {kSkin, T::F32}, kSkin),
    fixed(Op::SkinMetallic, "skin.metallic", {kSkin, T::F32}, kSkin),
    fixed(Op::SkinEmission, "skin.emission", {kSkin, T::F32}, kSkin),
    fixed(Op::SkinCutout, "skin.cutout", {kSkin, T::F32}, kSkin),
    fixed(Op::SkinTwoSided, "skin.two_sided", {kSkin}, kSkin),
    fixed(Op::SkinCrisp, "skin.crisp", {kSkin}, kSkin),
    fixed(Op::SkinBlend, "skin.blend", {kSkin}, kSkin),
    fixed(Op::SkinRegion, "skin.region", {kSkin, T::F32, T::F32, T::F32, T::F32}, kSkin),

    fixed(Op::ModelLod, "model.lod", {}, T::I32),

    indexed(Op::OutPoint3d, "out.point3d", {T::Vec3}),
    indexed(Op::OutBox3d, "out.box3d", {T::Vec3, T::Vec3}),
    indexed(Op::OutListPoint3d, "out.list.point3d", {T::Vec3}),
    indexed(Op::OutListBox3d, "out.list.box3d", {T::Vec3, T::Vec3}),
    indexed(Op::PartGetPoint3d, "part.get.point3d", {}, T::Vec3),
    dynamic(Op::PartRender, "part.render"),

    fixed(Op::LumpBall, "lump.ball", {T::Vec3, T::F32}),
    fixed(Op::LumpBall3, "lump.ball3", {T::Vec3, T::Vec3}),
    fixed(Op::LumpCapsule, "lump.capsule", {T::Vec3, T::Vec3, T::F32}),
    fixed(Op::LumpCapsule2, "lump.capsule2", {T::Vec3, T::Vec3, T::F32, T::F32}),
    fixed(Op::LumpSlab, "lump.slab", {T::Vec3, T::Vec3, T::F32}),
};

} // namespace

std::span<const OpSpec> opSpecs() { return kSpecs; }

const OpSpec* findOp(u8 aux) {
    for (const auto& spec : kSpecs)
        if (static_cast<u8>(spec.op) == aux) return &spec;
    return nullptr;
}

const OpSpec* findOp(std::string_view mnemonic) {
    for (const auto& spec : kSpecs)
        if (mnemonic == spec.mnemonic) return &spec;
    return nullptr;
}

} // namespace iprop::model
