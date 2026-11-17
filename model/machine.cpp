#include "iprop/model/Model.hpp"

#include "iprop/VmInternals.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <optional>

namespace iprop::model {
namespace {

using detail::Execution;

// One generated 2D program: what identifies it and what it produced.
struct Texture {
    u32 program = 0;
    u32 seed = 0;
    u32 width = 0, height = 0;
    std::vector<UniformValue> inputs;
    ModelTexture image;
};

// How a primitive uses a texture (spec models v1.2, Skins).
struct SkinRecord {
    u32 texture = 0;
    bool fit = false;
    f32 density = kSkinDensity;
    f32 shiftU = 0.f, shiftV = 0.f;
    f32 roughness = kSkinRoughness, metallic = kSkinMetallic, emission = kSkinEmission, cutoff = kSkinCutoff;
    SkinAlpha alpha = SkinAlpha::Opaque;
    bool twoSided = false, crisp = false;
};

bool sameValue(const UniformValue& a, const UniformValue& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
    case InputType::Color: return a.color == b.color;
    case InputType::F32: return std::bit_cast<u32>(a.f) == std::bit_cast<u32>(b.f);
    case InputType::U32: return a.u == b.u;
    }
    return false;
}

void appendLittleEndian(std::vector<u8>& out, u64 value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
}

u64 textureKey(const SkinProgram& program, u32 seed, u32 width, u32 height, std::span<const UniformValue> inputs) {
    std::vector<u8> bytes;
    bytes.reserve(20 + inputs.size() * 4);
    appendLittleEndian(bytes, program.hash, 8);
    appendLittleEndian(bytes, seed, 4);
    appendLittleEndian(bytes, width, 4);
    appendLittleEndian(bytes, height, 4);
    for (const UniformValue& value : inputs) {
        switch (value.type) {
        case InputType::Color:
            bytes.insert(bytes.end(), {value.color.r, value.color.g, value.color.b, value.color.a});
            break;
        case InputType::F32: appendLittleEndian(bytes, std::bit_cast<u32>(value.f), 4); break;
        case InputType::U32: appendLittleEndian(bytes, value.u, 4); break;
        }
    }
    return fnv1a64(bytes);
}

// The model page of one generation: scopes, skins and the growing mesh.
class Machine final : public detail::Extension {
public:
    Machine(const LoadedModel& model, const ExecutionLimits& limits) : model_(model), limits_(limits) {
        scopes_.push_back(Affine{});
    }

    bool step(Execution& ex, Instruction ins) override {
        if (ins.opcode != kModelPage) return false;
        try {
            run(ex, ins);
        } catch (const Violation& violation) {
            ex.trap(violation.what(), violation.reason);
        }
        return true;
    }

    bool opensScope(Instruction ins) const override {
        return ins.opcode == kModelPage && ins.aux == static_cast<u8>(Op::PlaceBegin);
    }

    void closeScope(Execution&) override { scopes_.pop_back(); }

    ModelResult pack(detail::WorkBudget& work);

private:
    const LoadedModel& model_;
    ExecutionLimits limits_;
    std::vector<Affine> scopes_;
    std::vector<Texture> textures_;
    std::vector<SkinRecord> skins_;
    u64 texels_ = 0;
    Mesh mesh_;

    Vec3 popVec3(Execution& ex) { return ex.popScalar<Vec3>("stack type mismatch: expected vec3"); }

    u32 popSkin(Execution& ex) {
        const i32 id = ex.popI32();
        if (id == 0) ex.trap("skin is unassigned", ErrorReason::RuntimeViolation);
        if (id < 0 || static_cast<std::size_t>(id) > skins_.size()) ex.trap("skin index out of range", ErrorReason::IndexRange);
        return static_cast<u32>(id);
    }

    std::vector<Vec2> popPoints(Execution& ex) {
        const auto list = ex.popHandle<ListValue>("list");
        if (list.type != ValueType::list(ValueType::Vec2))
            ex.trap("stack type mismatch: expected list<vec2>", ErrorReason::StackType);
        std::vector<Vec2> points;
        points.reserve(list.data->elements.size());
        for (const Value& element : list.data->elements) {
            const auto* point = std::get_if<Vec2>(&element);
            if (!point) ex.trap("stack type mismatch: expected list<vec2>", ErrorReason::StackType);
            points.push_back(*point);
        }
        return points;
    }

    // Limits, fuel, memory and work of a primitive, before it emits.
    // `checks` is the fuel of argument checks that grow faster than the
    // primitive's vertices (an extrude outline).
    void reserve(Execution& ex, Counts counts, u64 checks) {
        if (mesh_.vertices.size() + counts.vertices > kMaxVertices)
            ex.trap("model exceeds 65536 vertices", ErrorReason::RuntimeViolation);
        if (mesh_.triangles.size() + counts.triangles > kMaxTriangles)
            ex.trap("model exceeds 131072 triangles", ErrorReason::RuntimeViolation);
        ex.spend(counts.vertices * kVertexFuel + counts.triangles * kTriangleFuel + checks);
        ex.allocateResource(counts.vertices * kVertexBytes + counts.triangles * kTriangleBytes);
        // Work stays one step per vertex and per triangle.
        ex.work(static_cast<u64>(counts.vertices) + counts.triangles);
    }

    template<class Shape> void build(Execution& ex, const Shape& shape, u32 skin, u64 checks = 0) {
        reserve(ex, count(shape), checks);
        const SkinRecord& record = skins_[skin - 1];
        const ModelTexture& texture = textures_[record.texture].image;
        const Mapping mapping{record.fit, record.density, record.shiftU, record.shiftV,
                              static_cast<double>(texture.assetWidth), static_cast<double>(texture.assetHeight)};
        emit(Target{mesh_, scopes_.back(), mapping, skin}, shape);
    }

    void pushRecord(Execution& ex, const SkinRecord& record) {
        if (skins_.size() >= kMaxSkinRecords) ex.trap("model exceeds 1024 skin records", ErrorReason::RuntimeViolation);
        ex.allocateResource(kSkinRecordBytes);
        skins_.push_back(record);
        ex.push(static_cast<i32>(skins_.size()));
    }

    void placeBegin(Execution& ex, u16 mask) {
        if (!validPlaceMask(mask)) ex.trap("invalid place.begin mask", ErrorReason::InvalidInstruction);
        Placement placement;
        placement.mask = mask;
        if (mask & kPlaceMirrorZ) placement.mirror[2] = ex.popI32();
        if (mask & kPlaceMirrorY) placement.mirror[1] = ex.popI32();
        if (mask & kPlaceMirrorX) placement.mirror[0] = ex.popI32();
        if (mask & kPlaceScale3) {
            const Vec3 scale = popVec3(ex);
            placement.scale[0] = scale.x;
            placement.scale[1] = scale.y;
            placement.scale[2] = scale.z;
        }
        if (mask & kPlaceScale) placement.scale[0] = placement.scale[1] = placement.scale[2] = ex.popF32();
        if (mask & kPlaceRoll) placement.roll = ex.popF32();
        if (mask & kPlacePitch) placement.pitch = ex.popF32();
        if (mask & kPlaceYaw) placement.yaw = ex.popF32();
        if (mask & kPlaceAt) placement.at = popVec3(ex);
        if (ex.layerScopes.size() >= kMaxSurfaceScopes) ex.trap("scope depth exceeds 32", ErrorReason::RuntimeViolation);
        const Affine child = place(scopes_.back(), placement);
        ex.spend(kPlaceFuel);

        // A label like `block`, and a scope the interpreter unwinds with it.
        auto& frame = ex.frame();
        ex.ensureControl(frame.functionIndex);
        const auto& info = ex.control[frame.functionIndex][frame.ip];
        detail::Label label;
        label.kind = 5u;
        label.cont = info.endIndex;
        label.end = info.endIndex;
        label.stackHeight = frame.stack.size();
        frame.labels.push_back(label);
        detail::LayerScope scope;
        scope.frameDepth = ex.frames.size();
        scope.labelDepth = frame.labels.size();
        scope.isExtension = true;
        ex.layerScopes.push_back(std::move(scope));
        scopes_.push_back(child);
    }

    void skinDraw(Execution& ex, u16 index) {
        if (index >= model_.skins.size()) ex.trap("skin program index out of range", ErrorReason::IndexRange);
        const SkinProgram& program = model_.skins[index];
        const auto& meta = program.file.meta;
        const i32 requestedHeight = ex.popI32();
        const i32 requestedWidth = ex.popI32();
        const u32 seed = static_cast<u32>(ex.popI32());
        std::vector<UniformValue> values(meta.inputs.size());
        for (std::size_t i = meta.inputs.size(); i > 0; --i) {
            UniformValue& value = values[i - 1];
            value.type = meta.inputs[i - 1].type;
            switch (value.type) {
            case InputType::Color: value.color = ex.popColor(); break;
            case InputType::F32: value.f = ex.popF32(); break;
            case InputType::U32: value.u = static_cast<u32>(ex.popI32()); break;
            }
        }
        const auto sized = [&](i32 requested, u32 declared) -> u32 {
            if (requested < 0 || requested > static_cast<i32>(kMaxTextureSide))
                ex.trap("skin size must be in 0..2048", ErrorReason::NumericRange);
            const u32 resolved = requested ? static_cast<u32>(requested) : declared;
            if (resolved > kMaxTextureSide) ex.trap("skin size must be in 0..2048", ErrorReason::NumericRange);
            return resolved;
        };
        const u32 width = sized(requestedWidth, meta.sizeX);
        const u32 height = sized(requestedHeight, meta.sizeY);
        ex.spend(kSkinDrawFuel);

        std::size_t texture = textures_.size();
        for (std::size_t i = 0; i < textures_.size(); ++i) {
            const Texture& known = textures_[i];
            if (known.program == index && known.seed == seed && known.width == width && known.height == height &&
                std::equal(values.begin(), values.end(), known.inputs.begin(), known.inputs.end(), sameValue)) {
                texture = i;
                break;
            }
        }
        if (texture == textures_.size()) {
            if (textures_.size() >= kMaxTextures) ex.trap("model exceeds 32 skin textures", ErrorReason::RuntimeViolation);
            if (texels_ + static_cast<u64>(width) * height > kMaxTexels)
                ex.trap("model exceeds 4194304 skin texels", ErrorReason::RuntimeViolation);
            textures_.push_back(generate(ex, index, seed, width, height, std::move(values)));
            texels_ += static_cast<u64>(width) * height;
        }
        SkinRecord record;
        record.texture = static_cast<u32>(texture);
        pushRecord(ex, record);
    }

    // A standard 2D generation of an embedded program, under what is left
    // of this generation's work and under its deadline and cancellation.
    Texture generate(Execution& ex, u16 index, u32 seed, u32 width, u32 height, std::vector<UniformValue> values) {
        const SkinProgram& program = model_.skins[index];
        if (ex.workBudget->remaining() == 0) ex.work(1); // traps where the 2D VM would
        GenerationInputs inputs;
        inputs.seed = seed;
        inputs.sizeX = width;
        inputs.sizeY = height;
        inputs.limits = limits_;
        inputs.limits.workLimit = ex.workBudget->remaining();
        GenerationResult result;
        u64 used = 0;
        try {
            VM vm(program.file, inputs, std::span<const UniformValue>(values));
            vm.run();
            result = vm.takeResult();
            used = vm.workBudget() - vm.work();
        } catch (const FormatError& error) {
            throw Trap("skin '" + program.name + "': " + error.what(), error.reason, ErrorStage::Execute,
                       ex.activeInstruction);
        }
        ex.work(used);

        Texture texture;
        texture.program = index;
        texture.seed = seed;
        texture.width = width;
        texture.height = height;
        texture.image.key = textureKey(program, seed, width, height, values);
        texture.image.name = program.name;
        texture.image.assetWidth = program.file.meta.sizeX;
        texture.image.assetHeight = program.file.meta.sizeY;
        texture.image.color = std::move(result.image);
        texture.image.images = std::move(result.images);
        texture.inputs = std::move(values);
        return texture;
    }

    void run(Execution& ex, Instruction ins) {
        const OpSpec* spec = findOp(ins.aux);
        if (!spec) ex.trap("unknown model operation", ErrorReason::InvalidInstruction);
        const auto range = [&](bool valid, const char* message) {
            if (!valid) ex.trap(message, ErrorReason::NumericRange);
        };
        switch (spec->op) {
        case Op::Vec3Make: {
            const f32 z = ex.popF32(), y = ex.popF32(), x = ex.popF32();
            ex.push(Vec3{x, y, z});
            break;
        }
        case Op::Vec3Add: case Op::Vec3Sub: case Op::Vec3Mul: case Op::Vec3Div: case Op::Vec3Cross: {
            const Vec3 b = popVec3(ex), a = popVec3(ex);
            ex.push(spec->op == Op::Vec3Add ? vec3Add(a, b) : spec->op == Op::Vec3Sub ? vec3Sub(a, b)
                    : spec->op == Op::Vec3Mul ? vec3Mul(a, b) : spec->op == Op::Vec3Div ? vec3Div(a, b) : vec3Cross(a, b));
            break;
        }
        case Op::Vec3Scale: {
            const f32 k = ex.popF32();
            ex.push(vec3Scale(popVec3(ex), k));
            break;
        }
        case Op::Vec3Neg: ex.push(vec3Neg(popVec3(ex))); break;
        case Op::Vec3X: ex.push(popVec3(ex).x); break;
        case Op::Vec3Y: ex.push(popVec3(ex).y); break;
        case Op::Vec3Z: ex.push(popVec3(ex).z); break;
        case Op::Vec3Len: ex.push(vec3Len(popVec3(ex))); break;
        case Op::Vec3Dot: {
            const Vec3 b = popVec3(ex), a = popVec3(ex);
            ex.push(vec3Dot(a, b));
            break;
        }
        case Op::Vec3Normalize: ex.push(vec3Normalize(popVec3(ex))); break;
        case Op::Vec3Mix: {
            const f32 t = ex.popF32();
            const Vec3 b = popVec3(ex), a = popVec3(ex);
            ex.push(vec3Mix(a, b, t));
            break;
        }

        case Op::PlaceBegin: placeBegin(ex, ins.operand); break;

        case Op::Box: {
            const u32 skin = popSkin(ex);
            const Vec3 size = popVec3(ex), at = popVec3(ex);
            build(ex, checkBox(at, size), skin);
            break;
        }
        case Op::BoxBevel: {
            const u32 skin = popSkin(ex);
            const f32 bevel = ex.popF32();
            const Vec3 size = popVec3(ex), at = popVec3(ex);
            build(ex, checkBox(at, size, bevel), skin);
            break;
        }
        case Op::Cylinder: {
            const u32 skin = popSkin(ex);
            const i32 sides = ex.popI32();
            const f32 height = ex.popF32(), radius = ex.popF32();
            const Vec3 at = popVec3(ex);
            build(ex, checkCone(at, radius, radius, height, sides, true), skin);
            break;
        }
        case Op::Cone: case Op::Band: {
            const u32 skin = popSkin(ex);
            const i32 sides = ex.popI32();
            const f32 height = ex.popF32(), r1 = ex.popF32(), r0 = ex.popF32();
            const Vec3 at = popVec3(ex);
            build(ex, checkCone(at, r0, r1, height, sides, spec->op == Op::Cone), skin);
            break;
        }
        case Op::Sphere: {
            const u32 skin = popSkin(ex);
            const i32 rings = ex.popI32(), sides = ex.popI32();
            const f32 radius = ex.popF32();
            const Vec3 at = popVec3(ex);
            build(ex, checkSphere(at, radius, sides, rings), skin);
            break;
        }
        case Op::Lathe: case Op::LatheCrease: {
            const u32 skin = popSkin(ex);
            const bool creased = spec->op == Op::LatheCrease;
            const f32 crease = creased ? ex.popF32() : 0.f;
            const i32 sides = ex.popI32();
            const auto profile = popPoints(ex);
            const Vec3 at = popVec3(ex);
            build(ex, checkLathe(at, profile, sides, creased ? &crease : nullptr), skin);
            break;
        }
        case Op::Extrude: case Op::ExtrudeBevel: {
            const u32 skin = popSkin(ex);
            const bool bevelled = spec->op == Op::ExtrudeBevel;
            const f32 bevel = bevelled ? ex.popF32() : 0.f;
            const f32 depth = ex.popF32();
            const auto outline = popPoints(ex);
            const Vec3 at = popVec3(ex);
            build(ex, checkExtrude(at, outline, depth, bevelled ? &bevel : nullptr), skin,
                  outlineFuel(outline.size(), bevelled));
            break;
        }
        case Op::Quad: {
            const u32 skin = popSkin(ex);
            const Vec3 d = popVec3(ex), c = popVec3(ex), b = popVec3(ex), a = popVec3(ex);
            build(ex, checkQuad(a, b, c, d), skin);
            break;
        }
        case Op::Tri: {
            const u32 skin = popSkin(ex);
            const Vec3 c = popVec3(ex), b = popVec3(ex), a = popVec3(ex);
            build(ex, checkTri(a, b, c), skin);
            break;
        }

        case Op::SkinDraw: skinDraw(ex, ins.operand); break;
        case Op::SkinDensity: case Op::SkinRoughness: case Op::SkinMetallic: case Op::SkinEmission: case Op::SkinCutout: {
            const f32 value = ex.popF32();
            SkinRecord record = skins_[popSkin(ex) - 1];
            switch (spec->op) {
            case Op::SkinDensity:
                range(std::isfinite(value) && value > 0.f, "skin density must be finite and positive");
                record.density = value;
                break;
            case Op::SkinRoughness:
                range(value >= 0.f && value <= 1.f, "skin roughness must be in 0..1");
                record.roughness = value;
                break;
            case Op::SkinMetallic:
                range(value >= 0.f && value <= 1.f, "skin metallic must be in 0..1");
                record.metallic = value;
                break;
            case Op::SkinEmission:
                range(std::isfinite(value) && value >= 0.f, "skin emission must be finite and not negative");
                record.emission = value;
                break;
            default:
                range(value >= 0.f && value <= 1.f, "skin cutout must be in 0..1");
                record.alpha = SkinAlpha::Cutout;
                record.cutoff = value;
                break;
            }
            pushRecord(ex, record);
            break;
        }
        case Op::SkinShift: {
            const Vec2 shift = ex.popVec2();
            SkinRecord record = skins_[popSkin(ex) - 1];
            range(std::isfinite(shift.x) && std::isfinite(shift.y), "skin shift must be finite");
            record.shiftU = shift.x;
            record.shiftV = shift.y;
            pushRecord(ex, record);
            break;
        }
        case Op::SkinFit: case Op::SkinTwoSided: case Op::SkinCrisp: {
            SkinRecord record = skins_[popSkin(ex) - 1];
            if (spec->op == Op::SkinFit) record.fit = true;
            else if (spec->op == Op::SkinTwoSided) record.twoSided = true;
            else record.crisp = true;
            pushRecord(ex, record);
            break;
        }
        }
        // place.begin opens a label at this instruction, like every opener.
        ++ex.frame().ip;
    }
};

// Groups the triangles into surfaces by the material of their records, in
// order of first use, and gives the result its own copies.
ModelResult Machine::pack(detail::WorkBudget& work) {
    ModelResult result;
    const auto sameMaterial = [](const ModelSurface& surface, const SkinRecord& record) {
        return surface.texture == record.texture &&
               std::bit_cast<u32>(surface.roughness) == std::bit_cast<u32>(record.roughness) &&
               std::bit_cast<u32>(surface.metallic) == std::bit_cast<u32>(record.metallic) &&
               std::bit_cast<u32>(surface.emission) == std::bit_cast<u32>(record.emission) &&
               std::bit_cast<u32>(surface.cutoff) == std::bit_cast<u32>(record.cutoff) && surface.alpha == record.alpha &&
               surface.twoSided == record.twoSided && surface.crisp == record.crisp;
    };
    // The surface of each record, found when a triangle first uses it.
    constexpr u32 kNone = 0xFFFFFFFFu;
    std::vector<u32> surfaceOf(skins_.size(), kNone);
    for (const Triangle& triangle : mesh_.triangles) {
        work.spend();
        u32& surface = surfaceOf[triangle.skin - 1];
        if (surface != kNone) continue;
        const SkinRecord& record = skins_[triangle.skin - 1];
        for (u32 i = 0; i < result.surfaces.size() && surface == kNone; ++i)
            if (sameMaterial(result.surfaces[i], record)) surface = i;
        if (surface == kNone) {
            surface = static_cast<u32>(result.surfaces.size());
            ModelSurface made;
            made.texture = record.texture;
            made.roughness = record.roughness;
            made.metallic = record.metallic;
            made.emission = record.emission;
            made.cutoff = record.cutoff;
            made.alpha = record.alpha;
            made.twoSided = record.twoSided;
            made.crisp = record.crisp;
            result.surfaces.push_back(std::move(made));
        }
        if (!record.fit) textures_[record.texture].image.repeats = true;
    }

    std::vector<u32> local(mesh_.vertices.size(), 0);
    for (std::size_t i = 0; i < mesh_.vertices.size(); ++i) {
        work.spend();
        const u32 surfaceIndex = surfaceOf[mesh_.vertexSkins[i] - 1];
        if (surfaceIndex == kNone) continue; // a vertex no triangle uses
        ModelSurface& surface = result.surfaces[surfaceIndex];
        const Vertex& vertex = mesh_.vertices[i];
        local[i] = static_cast<u32>(surface.positions.size() / 3);
        surface.positions.insert(surface.positions.end(), vertex.position, vertex.position + 3);
        surface.normals.insert(surface.normals.end(), vertex.normal, vertex.normal + 3);
        surface.tangents.insert(surface.tangents.end(), vertex.tangent, vertex.tangent + 4);
        surface.uvs.insert(surface.uvs.end(), vertex.uv, vertex.uv + 2);
        for (int axis = 0; axis < 3; ++axis) {
            const f32 value = vertex.position[axis];
            if (result.vertexCount == 0 || value < result.boundsMin[axis]) result.boundsMin[axis] = value;
            if (result.vertexCount == 0 || value > result.boundsMax[axis]) result.boundsMax[axis] = value;
        }
        ++result.vertexCount;
    }
    for (const Triangle& triangle : mesh_.triangles) {
        work.spend();
        auto& indices = result.surfaces[surfaceOf[triangle.skin - 1]].indices;
        indices.insert(indices.end(), {local[triangle.a], local[triangle.b], local[triangle.c]});
    }
    result.triangleCount = static_cast<u32>(mesh_.triangles.size());

    result.textures.reserve(textures_.size());
    for (Texture& texture : textures_) {
        work.spend();
        result.textures.push_back(std::move(texture.image));
    }
    return result;
}

ModelResult generateWith(const LoadedModel& model, const ModelInputs& inputs,
                         const std::span<const UniformValue>* rootValues) {
    GenerationInputs vmInputs;
    vmInputs.seed = inputs.seed;
    vmInputs.uniforms = inputs.uniforms;
    vmInputs.limits = inputs.limits;
    Machine machine(model, inputs.limits);
    std::optional<VM> vm;
    if (rootValues) vm.emplace(model.file, vmInputs, *rootValues);
    else vm.emplace(model.file, vmInputs);
    vm->extend(machine);
    vm->run();
    const auto budget = vm->executionBudget();
    budget->stage = ErrorStage::Execute;
    ModelResult result = machine.pack(*budget);
    for (int axis = 0; axis < 3; ++axis) result.size[axis] = model.size[axis];
    result.unitsPerMeter = model.unitsPerMeter;
    result.fuelBudget = vm->fuelBudget();
    result.fuelUsed = result.fuelBudget - vm->fuel();
    result.workBudget = vm->workBudget();
    result.workUsed = result.workBudget - vm->work();
    return result;
}

} // namespace

ModelResult generateModel(const LoadedModel& model, const ModelInputs& inputs) {
    return generateWith(model, inputs, nullptr);
}

ModelResult generateModel(const LoadedModel& model, const ModelInputs& inputs,
                          std::span<const UniformValue> rootValues) {
    return generateWith(model, inputs, &rootValues);
}

} // namespace iprop::model
