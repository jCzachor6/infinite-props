#include "iprop/model/Model.hpp"

#include "iprop/VmInternals.hpp"
#include "iprop/model/Blocks.hpp"
#include "iprop/model/Sculpt.hpp"

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
    bool region = false;
    f32 regionX = 0.f, regionY = 0.f, regionW = 0.f, regionH = 0.f;
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

bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// What a scope is (spec models v1.2, Placement scopes; M3 blocks).
enum class ScopeKind : u8 { Root, Place, Smooth, Refine, Bend, Twist, Taper, Displace, Part, Sculpt, Carve };

// Blocks that move vertices by a map of their own: a point follows them,
// a box cannot.
constexpr bool deforms(ScopeKind kind) {
    return kind == ScopeKind::Bend || kind == ScopeKind::Twist || kind == ScopeKind::Taper;
}

// An open scope. Its map is relative to the frame of the buffer it writes
// to: the model's mesh, or the buffer of the innermost open block.
struct Scope {
    ScopeKind kind = ScopeKind::Root;
    Affine affine;
    int block = -1; // -1: the model's mesh; otherwise an index into blocks_
    u32 serial = 0; // unique in a generation; the root scope is 0
    // From the frame of the asset that runs here (the model or a part): an
    // unequal scale lies on the way, and a turn that is not a multiple of
    // 90 degrees lies inside one, so a box is no longer a box.
    bool unequal = false, sheared = false;
};

// An output value on its way to its slot (M3): a point, or a box as its
// centre and its three edges, in the frame of the scope it is in.
struct Pending {
    u16 slot = 0;
    bool box = false;
    Point3 point;
    Point3 edges[3];
    bool flipped = false;
};

// A block that keeps its own buffer until it closes (M3).
struct Block {
    ScopeKind kind = ScopeKind::Root;
    LocalMesh mesh;
    std::vector<Pending> outputs;
    f32 number = 0.f, frequency = 0.f; // the angle, scale or amount; displace's frequency
    i32 count = 0;                     // refine
    u32 seed = 0;                      // displace
    u16 part = 0;                      // a part call: which part
    u32 callScope = 0;                 // a part call: the serial of the scope it was called in
    // A sculpt (M6): its cell is `number`, its blend `frequency`; the lumps
    // in its frame, in the order written, and the skin it wears.
    std::vector<Lump> lumps;
    u32 skin = 0;
};

// The values of one output slot, rounded.
struct SlotValues {
    std::vector<Vec3> points;
    std::vector<ModelBox> boxes;
};

// The model page of one generation: scopes, skins and the growing mesh.
class Machine final : public detail::Extension {
public:
    Machine(const LoadedModel& model, const ModelInputs& inputs)
        : model_(model), limits_(inputs.limits), lod_(inputs.lod), known_(inputs.knownTextures),
          slots_(model.outputs.size()) {
        scopes_.push_back(Scope{});
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
        return ins.opcode == kModelPage && model::opensScope(static_cast<Op>(ins.aux));
    }

    void closeScope(Execution& ex) override {
        // Called while unwinding, outside step(): a broken rule of a block's
        // processing becomes a trap here too.
        try {
            close(ex);
        } catch (const Violation& violation) {
            ex.trap(violation.what(), violation.reason);
        }
    }

    // A part's entry has returned and closed its own scopes: its block is
    // the innermost one.
    void frameLeft(Execution& ex) override {
        try {
            close(ex);
            ex.frame().lastPart = blocksClosedPart_;
            const std::size_t depth = ex.frames.size() - 1;
            if (partReads_.size() <= depth) partReads_.resize(depth + 1, 0);
            partReads_[depth] = scopes_.back().serial;
        } catch (const Violation& violation) {
            ex.trap(violation.what(), violation.reason);
        }
    }

    ModelResult pack(detail::WorkBudget& work);

private:
    const LoadedModel& model_;
    ExecutionLimits limits_;
    u32 lod_ = 0;
    std::span<const KnownTexture> known_;
    std::vector<Scope> scopes_;
    std::vector<Block> blocks_;
    std::vector<Texture> textures_;
    std::vector<SkinRecord> skins_;
    u64 texels_ = 0;
    Mesh mesh_;
    // Every vertex and triangle created so far, in the mesh and in open
    // blocks: each ends up in the model, so the limits count them all.
    u64 vertexTotal_ = 0, triangleTotal_ = 0;
    std::vector<SlotValues> slots_;
    u32 serials_ = 0;
    // Per frame depth, the scope in which that frame last called a part:
    // its outputs are read there (M3).
    std::vector<u32> partReads_;
    u16 blocksClosedPart_ = 0;

    // Moves an output value through the innermost scope's map, into its
    // slot when that scope writes to the frame of the slot's asset (the
    // model's mesh, or the buffer of a part call), otherwise into the
    // pending values of the block it writes to.
    void deliver(Execution& ex, Pending value) {
        const Scope& scope = scopes_.back();
        value.point = mapPoint(scope.affine, value.point);
        if (value.box)
            for (Point3& edge : value.edges) edge = mapVector(scope.affine, edge);
        value.flipped = value.flipped != scope.affine.flipped;
        if (scope.block >= 0) {
            Block& block = blocks_[static_cast<std::size_t>(scope.block)];
            if (block.kind != ScopeKind::Part) {
                block.outputs.push_back(value);
                return;
            }
        }
        commit(ex, value);
    }

    // Rounds a value into its slot: a later value replaces a singular one.
    void commit(Execution& ex, const Pending& value) {
        const ModelOutputDecl& decl = model_.outputs[value.slot];
        SlotValues& slot = slots_[value.slot];
        const auto rounded = [](double component) {
            const f32 out = static_cast<f32>(component);
            if (!std::isfinite(out)) throw Violation("model coordinate out of range");
            return out;
        };
        ex.work(1);
        if (!value.box) {
            const Vec3 point{rounded(value.point.x), rounded(value.point.y), rounded(value.point.z)};
            if (!decl.list) slot.points.clear();
            slot.points.push_back(point);
            return;
        }
        ModelBox box;
        box.center[0] = rounded(value.point.x);
        box.center[1] = rounded(value.point.y);
        box.center[2] = rounded(value.point.z);
        for (int i = 0; i < 3; ++i) {
            const Point3& edge = value.edges[i];
            const double length = std::sqrt((edge.x * edge.x + edge.y * edge.y) + edge.z * edge.z);
            if (!(length > 0.0) || !std::isfinite(length)) throw Violation("degenerate direction", ErrorReason::RuntimeViolation);
            box.size[i] = rounded(length);
            // A mirrored box is the same box: its third axis turns round so
            // that the axes form a rotation.
            const double sign = i == 2 && value.flipped ? -1.0 : 1.0;
            box.axes[i][0] = static_cast<f32>(sign * (edge.x / length));
            box.axes[i][1] = static_cast<f32>(sign * (edge.y / length));
            box.axes[i][2] = static_cast<f32>(sign * (edge.z / length));
        }
        if (!decl.list) slot.boxes.clear();
        slot.boxes.push_back(box);
    }

    void writePoint(Execution& ex, u16 slot) {
        const Vec3 p = popVec3(ex);
        ex.spend(kPointOutputFuel);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            ex.trap("a point3d output needs finite components", ErrorReason::NumericRange);
        Pending value;
        value.slot = slot;
        value.point = {p.x, p.y, p.z};
        deliver(ex, value);
    }

    void writeBox(Execution& ex, u16 slot) {
        const Vec3 size = popVec3(ex), at = popVec3(ex);
        ex.spend(kBoxOutputFuel);
        if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z) || !std::isfinite(size.x) ||
            !std::isfinite(size.y) || !std::isfinite(size.z) || !(size.x > 0.f) || !(size.y > 0.f) || !(size.z > 0.f))
            ex.trap("a box3d output needs finite values and a positive size", ErrorReason::NumericRange);
        if (scopes_.back().sheared)
            ex.trap("a box3d output cannot be turned inside an unequal scale", ErrorReason::RuntimeViolation);
        for (auto block = blocks_.rbegin(); block != blocks_.rend() && block->kind != ScopeKind::Part; ++block)
            if (deforms(block->kind))
                ex.trap("a box3d output cannot be written inside bend, twist or taper", ErrorReason::RuntimeViolation);
        Pending value;
        value.slot = slot;
        value.box = true;
        // The centre of the box whose base centre is `at`, as for box().
        value.point = {at.x, static_cast<double>(at.y) + static_cast<double>(size.y) * 0.5, at.z};
        value.edges[0] = {static_cast<double>(size.x), 0.0, 0.0};
        value.edges[1] = {0.0, static_cast<double>(size.y), 0.0};
        value.edges[2] = {0.0, 0.0, static_cast<double>(size.z)};
        deliver(ex, value);
    }

    // The buffer the innermost scope writes to, through that scope's map.
    template<class... Rest> Target target(Rest&&... rest) {
        const Scope& scope = scopes_.back();
        if (scope.block < 0) return Target{mesh_, scope.affine, std::forward<Rest>(rest)...};
        return Target{blocks_[static_cast<std::size_t>(scope.block)].mesh, scope.affine, std::forward<Rest>(rest)...};
    }

    void close(Execution& ex) {
        const Scope scope = scopes_.back();
        scopes_.pop_back();
        if (scope.kind == ScopeKind::Place || scope.kind == ScopeKind::Carve) return;
        // A block: its buffer and its pending outputs go to the scope it was
        // opened in, which is the innermost one again.
        Block block = std::move(blocks_.back());
        blocks_.pop_back();
        if (block.kind == ScopeKind::Part) blocksClosedPart_ = block.part;
        process(ex, block);
        const auto vertices = static_cast<u64>(block.mesh.vertices.size());
        const auto triangles = static_cast<u64>(block.mesh.triangles.size());
        ex.spend(vertices * kPassVertexFuel + triangles * kPassTriangleFuel);
        ex.work(vertices + triangles);
        pass(target(Mapping{}, 0u), block.mesh);
        for (const Pending& value : block.outputs) deliver(ex, value);
    }

    // What a closing block does to its buffer and its pending points. Every
    // limit, fuel, memory and work is charged before the processing.
    void process(Execution& ex, Block& block) {
        LocalMesh& mesh = block.mesh;
        const auto vertices = static_cast<u64>(mesh.vertices.size());
        const auto triangles = static_cast<u64>(mesh.triangles.size());
        if (block.kind != ScopeKind::Part) checkFinite(mesh);
        switch (block.kind) {
        case ScopeKind::Smooth:
            ex.spend(vertices * kSmoothFuel);
            ex.work(vertices);
            smooth(mesh, block.number);
            break;
        case ScopeKind::Refine: {
            const u32 n = static_cast<u32>(block.count);
            const Counts counts = refineCounts(mesh, n);
            const u64 added = counts.triangles - triangles;
            if (vertexTotal_ + counts.vertices > kMaxVertices)
                ex.trap("model exceeds 65536 vertices", ErrorReason::RuntimeViolation);
            if (triangleTotal_ + added > kMaxTriangles)
                ex.trap("model exceeds 131072 triangles", ErrorReason::RuntimeViolation);
            ex.spend(counts.vertices * kRefineVertexFuel + added * kRefineTriangleFuel);
            ex.allocateResource(counts.vertices * kVertexBytes + added * kTriangleBytes);
            ex.work(static_cast<u64>(counts.vertices) + counts.triangles);
            refine(mesh, n);
            vertexTotal_ += counts.vertices;
            triangleTotal_ += added;
            break;
        }
        case ScopeKind::Bend: case ScopeKind::Twist: case ScopeKind::Taper: {
            ex.spend(vertices * kDeformFuel);
            ex.work(vertices);
            const DeformKind kind = block.kind == ScopeKind::Bend    ? DeformKind::Bend
                                    : block.kind == ScopeKind::Twist ? DeformKind::Twist
                                                                     : DeformKind::Taper;
            const Deformation deformation = model::deformation(kind, block.number, mesh);
            deform(mesh, deformation);
            for (Pending& value : block.outputs) value.point = deformPoint(deformation, value.point);
            break;
        }
        case ScopeKind::Displace:
            ex.spend(vertices * kDisplaceFuel + triangles * kDisplaceTriangleFuel);
            ex.work(vertices + triangles);
            displace(mesh, block.number, block.frequency, block.seed);
            break;
        case ScopeKind::Sculpt:
            sculpt(ex, block);
            break;
        default:
            break;
        }
    }

    // A closing sculpt (M6): the field is sampled on its grid, the surface
    // is found and counted, and only then built, each step paid before it
    // allocates.
    void sculpt(Execution& ex, Block& block) {
        const double cell = block.number, blend = block.frequency;
        const SculptGrid grid = sculptGrid(block.lumps, cell, blend);
        const u64 samples = grid.samples();
        if (samples == 0) return; // nothing was added
        const u64 lumps = block.lumps.size();
        // What the kernel works in is charged before it is allocated and
        // given back when the block is built.
        struct Scratch {
            Execution& ex;
            u64 bytes = 0;
            void take(u64 size) {
                ex.allocateResource(size);
                bytes += size;
            }
            ~Scratch() { ex.resources.release(bytes); }
        } scratch{ex};
        ex.spend(samples * lumps * kSculptSampleFuel);
        scratch.take(samples * kSculptSampleBytes);
        ex.work(samples * lumps);
        const std::vector<double> field = sculptSamples(block.lumps, blend, cell, grid);
        // The surface is counted before it is built: a point is one vertex
        // at least.
        ex.work(samples);
        const SculptCounts counts = sculptCount(grid, field);
        if (vertexTotal_ + counts.points > kMaxVertices) ex.trap("model exceeds 65536 vertices", ErrorReason::RuntimeViolation);
        if (triangleTotal_ + counts.triangles > kMaxTriangles)
            ex.trap("model exceeds 131072 triangles", ErrorReason::RuntimeViolation);
        scratch.take(samples * kSculptCellBytes + counts.points * kSculptPointBytes + counts.triangles * kSculptTriangleBytes);
        const SculptNet net = sculptNet(grid, field, cell);
        const u64 triangles = net.triangles.size();
        if (vertexTotal_ + net.vertices > kMaxVertices) ex.trap("model exceeds 65536 vertices", ErrorReason::RuntimeViolation);
        ex.spend(net.vertices * kVertexFuel + triangles * kTriangleFuel +
                 net.points.size() * kSculptNormalSamples * lumps * kSculptSampleFuel);
        ex.allocateResource(net.vertices * kVertexBytes + triangles * kTriangleBytes);
        ex.work(static_cast<u64>(net.vertices) + triangles + net.points.size() * kSculptNormalSamples * lumps);
        const SkinRecord& record = skins_[block.skin - 1];
        const ModelTexture& texture = textures_[record.texture].image;
        Mapping mapping;
        mapping.density = record.density;
        mapping.shiftU = record.shiftU;
        mapping.shiftV = record.shiftV;
        mapping.assetWidth = static_cast<double>(texture.assetWidth);
        mapping.assetHeight = static_cast<double>(texture.assetHeight);
        sculptMesh(block.mesh, net, block.lumps, blend, cell, mapping, block.skin);
        vertexTotal_ += net.vertices;
        triangleTotal_ += static_cast<u32>(triangles);
    }

    // The sculpt block whose buffer the innermost scope writes to, or null.
    Block* sculpting() {
        const int index = scopes_.back().block;
        if (index < 0) return nullptr;
        Block& block = blocks_[static_cast<std::size_t>(index)];
        return block.kind == ScopeKind::Sculpt ? &block : nullptr;
    }
    // Geometry of any other kind has no place among lumps.
    void notSculpting(Execution& ex) {
        if (sculpting()) ex.trap("a sculpt block takes lumps: ball, capsule and slab", ErrorReason::RuntimeViolation);
    }

    void sculptBegin(Execution& ex) {
        const u32 skin = popSkin(ex);
        const f32 blend = ex.popF32(), cell = ex.popF32();
        if (!(std::isfinite(cell) && cell > 0.f)) ex.trap("sculpt cell must be finite and positive", ErrorReason::NumericRange);
        if (!(std::isfinite(blend) && blend >= 0.f)) ex.trap("sculpt blend must be finite and not negative", ErrorReason::NumericRange);
        const SkinRecord& record = skins_[skin - 1];
        if (record.fit || record.region)
            ex.trap("sculpt takes a skin that repeats, not skin.fit or skin.region", ErrorReason::RuntimeViolation);
        notSculpting(ex);
        if (ex.layerScopes.size() >= kMaxSurfaceScopes) ex.trap("scope depth exceeds 32", ErrorReason::RuntimeViolation);
        ex.spend(kBlockFuel);
        openLabel(ex);
        Block block;
        block.kind = ScopeKind::Sculpt;
        block.number = cell;
        block.frequency = blend;
        block.skin = skin;
        const Scope& parent = scopes_.back();
        Scope scope{ScopeKind::Sculpt, Affine{}, static_cast<int>(blocks_.size()), ++serials_};
        scope.unequal = parent.unequal;
        scope.sheared = parent.sheared;
        blocks_.push_back(std::move(block));
        scopes_.push_back(scope);
    }

    // carve: a scope of its sculpt block; lumps written in it are cut out.
    void carveBegin(Execution& ex) {
        if (!sculpting()) ex.trap("carve belongs in a sculpt block", ErrorReason::RuntimeViolation);
        if (carving()) ex.trap("carve cannot be nested", ErrorReason::RuntimeViolation);
        if (ex.layerScopes.size() >= kMaxSurfaceScopes) ex.trap("scope depth exceeds 32", ErrorReason::RuntimeViolation);
        openLabel(ex);
        const Scope& parent = scopes_.back();
        Scope scope{ScopeKind::Carve, parent.affine, parent.block, ++serials_};
        scope.unequal = parent.unequal;
        scope.sheared = parent.sheared;
        scopes_.push_back(scope);
    }

    // Whether a carve scope lies between the innermost scope and its sculpt.
    bool carving() const {
        for (std::size_t i = scopes_.size(); i > 0; --i) {
            const Scope& scope = scopes_[i - 1];
            if (scope.kind == ScopeKind::Carve) return true;
            if (scope.kind != ScopeKind::Place) return false;
        }
        return false;
    }

    // A lump joins its sculpt block in the block's frame. The scopes on the
    // way may move, turn, mirror and scale it equally: a field stretched
    // unequally is no distance any more.
    void lump(Execution& ex, const char* name, Lump shape) {
        Block* block = sculpting();
        if (!block) ex.trap((std::string(name) + " belongs in a sculpt block").c_str(), ErrorReason::RuntimeViolation);
        if (block->lumps.size() >= kMaxLumps) ex.trap("sculpt exceeds 64 lumps", ErrorReason::RuntimeViolation);
        const Affine& affine = scopes_.back().affine;
        Point3 column[3];
        double size[3];
        for (int i = 0; i < 3; ++i) {
            column[i] = {affine.a[0][i], affine.a[1][i], affine.a[2][i]};
            size[i] = std::sqrt((column[i].x * column[i].x + column[i].y * column[i].y) + column[i].z * column[i].z);
        }
        const double scale = size[0], slack = scale * 1.0e-9;
        const auto across = [&](int i, int j) {
            return (column[i].x * column[j].x + column[i].y * column[j].y) + column[i].z * column[j].z;
        };
        if (!(scale > 0.0) || std::fabs(size[1] - scale) > slack || std::fabs(size[2] - scale) > slack ||
            std::fabs(across(0, 1)) > slack * scale || std::fabs(across(0, 2)) > slack * scale ||
            std::fabs(across(1, 2)) > slack * scale)
            ex.trap("a lump takes an equal scale on every axis", ErrorReason::RuntimeViolation);
        shape.carve = carving();
        shape.a = mapPoint(affine, shape.a);
        shape.b = mapPoint(affine, shape.b);
        for (int i = 0; i < 3; ++i) {
            shape.axes[i] = {column[i].x / size[i], column[i].y / size[i], column[i].z / size[i]};
            shape.radii[i] = shape.radii[i] * scale;
        }
        shape.round = shape.round * scale;
        ex.allocateResource(kLumpBytes);
        block->lumps.push_back(shape);
    }

    // part.render: the part's entry runs in a frame of its own, with its own
    // stream, inside a block whose frame is the scope it is called in.
    void partRender(Execution& ex, u16 index) {
        if (index >= model_.parts.size()) ex.trap("model part index out of range", ErrorReason::IndexRange);
        notSculpting(ex);
        if (ex.frames.size() >= kMaxCallDepth) ex.trap("call depth exceeds limit", ErrorReason::CallDepth);
        const ModelPartDecl& part = model_.parts[index];
        const auto& entry = model_.file.code.functions[part.firstFunction];
        const u32 seed = static_cast<u32>(ex.popI32());
        ex.spend(kBlockFuel);
        detail::Frame next = ex.spareFrame();
        ex.popArguments(next, entry);
        ex.zeroLocals(next, entry);
        next.functionIndex = static_cast<u16>(part.firstFunction);
        next.stream = seed;
        next.initialSeed = seed;
        next.extensionFrame = true;
        // A part's outputs belong to its latest call.
        for (std::size_t i = 0; i < model_.outputs.size(); ++i)
            if (model_.outputs[i].owner == index) slots_[i] = SlotValues{};
        Block block;
        block.kind = ScopeKind::Part;
        block.part = index;
        block.callScope = scopes_.back().serial;
        blocks_.push_back(std::move(block));
        scopes_.push_back(Scope{ScopeKind::Part, Affine{}, static_cast<int>(blocks_.size() - 1), ++serials_});
        ++ex.frame().ip; // the caller resumes after part.render
        ex.frames.push_back(std::move(next));
    }

    void partGetPoint(Execution& ex, u16 slot) {
        const ModelOutputDecl& decl = model_.outputs[slot];
        if (decl.owner != ex.frame().lastPart)
            ex.trap("slot does not belong to the last called part", ErrorReason::InvalidOutput);
        const std::size_t depth = ex.frames.size() - 1;
        if (depth >= partReads_.size() || partReads_[depth] != scopes_.back().serial)
            ex.trap("a part's outputs are read in the block that called it", ErrorReason::InvalidOutput);
        if (slots_[slot].points.empty()) ex.trap("part output not written", ErrorReason::PartOutputMissing);
        ex.push(slots_[slot].points.back());
    }

    // A label like `block`, and a scope the interpreter unwinds with it.
    void openLabel(Execution& ex) {
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
    }

    // smooth, refine, bend, twist, taper and displace: the arguments are
    // checked, then the block opens with a buffer and an identity frame.
    void blockBegin(Execution& ex, Op op) {
        const auto range = [&](bool valid, const char* message) {
            if (!valid) ex.trap(message, ErrorReason::NumericRange);
        };
        Block block;
        switch (op) {
        case Op::SmoothBegin:
            block.kind = ScopeKind::Smooth;
            block.number = ex.popF32();
            range(block.number >= 0.f && block.number <= 180.f, "smooth angle must be in 0..180");
            break;
        case Op::RefineBegin:
            block.kind = ScopeKind::Refine;
            block.count = ex.popI32();
            range(block.count >= 1 && block.count <= static_cast<i32>(kMaxRefine), "refine count must be in 1..16");
            break;
        case Op::BendBegin:
            block.kind = ScopeKind::Bend;
            block.number = ex.popF32();
            range(block.number >= -360.f && block.number <= 360.f, "bend angle must be in -360..360");
            break;
        case Op::TwistBegin:
            block.kind = ScopeKind::Twist;
            block.number = ex.popF32();
            range(std::isfinite(block.number), "twist angle must be finite");
            break;
        case Op::TaperBegin:
            block.kind = ScopeKind::Taper;
            block.number = ex.popF32();
            range(std::isfinite(block.number) && block.number > 0.f, "taper scale must be finite and positive");
            break;
        default:
            block.kind = ScopeKind::Displace;
            block.seed = static_cast<u32>(ex.popI32());
            block.frequency = ex.popF32();
            block.number = ex.popF32();
            range(std::isfinite(block.number) && std::isfinite(block.frequency) && block.frequency > 0.f,
                  "displace needs a finite amount and a positive frequency");
            break;
        }
        notSculpting(ex);
        if (ex.layerScopes.size() >= kMaxSurfaceScopes) ex.trap("scope depth exceeds 32", ErrorReason::RuntimeViolation);
        ex.spend(kBlockFuel);
        openLabel(ex);
        const Scope& parent = scopes_.back();
        Scope scope{block.kind, Affine{}, static_cast<int>(blocks_.size()), ++serials_};
        // A box written inside is mapped through the scopes around the block
        // when it closes: whether it shears is the same question as outside.
        scope.unequal = parent.unequal;
        scope.sheared = parent.sheared;
        blocks_.push_back(std::move(block));
        scopes_.push_back(scope);
    }

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

    std::vector<Vec3> popPath(Execution& ex) {
        const auto list = ex.popHandle<ListValue>("list");
        if (list.type != ValueType::list(ValueType::Vec3))
            ex.trap("stack type mismatch: expected list<vec3>", ErrorReason::StackType);
        std::vector<Vec3> points;
        points.reserve(list.data->elements.size());
        for (const Value& element : list.data->elements) {
            const auto* point = std::get_if<Vec3>(&element);
            if (!point) ex.trap("stack type mismatch: expected list<vec3>", ErrorReason::StackType);
            points.push_back(*point);
        }
        return points;
    }

    // Limits, fuel, memory and work of a primitive, before it emits.
    // `checks` is the fuel of argument checks that grow faster than the
    // primitive's vertices (an extrude outline).
    void reserve(Execution& ex, Counts counts, u64 checks) {
        notSculpting(ex);
        if (vertexTotal_ + counts.vertices > kMaxVertices)
            ex.trap("model exceeds 65536 vertices", ErrorReason::RuntimeViolation);
        if (triangleTotal_ + counts.triangles > kMaxTriangles)
            ex.trap("model exceeds 131072 triangles", ErrorReason::RuntimeViolation);
        ex.spend(counts.vertices * kVertexFuel + counts.triangles * kTriangleFuel + checks);
        ex.allocateResource(counts.vertices * kVertexBytes + counts.triangles * kTriangleBytes);
        // Work stays one step per vertex and per triangle.
        ex.work(static_cast<u64>(counts.vertices) + counts.triangles);
    }

    template<class Shape> void build(Execution& ex, const Shape& shape, u32 skin, u64 checks = 0) {
        const Counts counts = count(shape);
        reserve(ex, counts, checks);
        const SkinRecord& record = skins_[skin - 1];
        const ModelTexture& texture = textures_[record.texture].image;
        const Mapping mapping{record.fit, record.density, record.shiftU, record.shiftV,
                              static_cast<double>(texture.assetWidth), static_cast<double>(texture.assetHeight),
                              record.region, record.regionX, record.regionY, record.regionW, record.regionH};
        emit(target(mapping, skin), shape);
        vertexTotal_ += counts.vertices;
        triangleTotal_ += counts.triangles;
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
        const Scope& parent = scopes_.back();
        Scope child{ScopeKind::Place, place(parent.affine, placement), parent.block, ++serials_};
        // The arguments are finite now: place() checked them.
        const bool turned = ((mask & kPlaceYaw) && !quarterTurn(placement.yaw)) ||
                            ((mask & kPlacePitch) && !quarterTurn(placement.pitch)) ||
                            ((mask & kPlaceRoll) && !quarterTurn(placement.roll));
        const bool unequal = (mask & kPlaceScale3) &&
                             (placement.scale[0] != placement.scale[1] || placement.scale[1] != placement.scale[2]);
        child.unequal = parent.unequal || unequal;
        child.sheared = parent.sheared || (parent.unequal && turned);
        ex.spend(kPlaceFuel);
        openLabel(ex);
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
            if (known.program == program.identity && known.seed == seed && known.width == width && known.height == height &&
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
        // A map's number starts at 1, so the map shows as painted.
        const auto has = [&](AuxImageKind kind) { return (textures_[texture].image.kinds >> static_cast<u32>(kind)) & 1u; };
        if (has(AuxImageKind::Roughness)) record.roughness = kSkinMapFactor;
        if (has(AuxImageKind::Metallic)) record.metallic = kSkinMapFactor;
        if (has(AuxImageKind::Emission)) record.emission = kSkinMapFactor;
        pushRecord(ex, record);
    }

    // A standard 2D generation of an embedded program, under what is left
    // of this generation's work and under its deadline and cancellation.
    // A texture the host holds already (M5) is not generated: it is charged
    // what it cost, in the order a generation charges, and keeps its size.
    Texture generate(Execution& ex, u16 index, u32 seed, u32 width, u32 height, std::vector<UniformValue> values) {
        const SkinProgram& program = model_.skins[index];
        if (ex.workBudget->remaining() == 0) ex.work(1); // traps where the 2D VM would
        Texture texture;
        texture.program = program.identity;
        texture.seed = seed;
        texture.width = width;
        texture.height = height;
        texture.image.key = textureKey(program, seed, width, height, values);
        texture.image.name = program.name;
        texture.image.assetWidth = program.file.meta.sizeX;
        texture.image.assetHeight = program.file.meta.sizeY;

        const KnownTexture* known = nullptr;
        for (const KnownTexture& record : known_)
            if (record.key == texture.image.key) { known = &record; break; }
        GenerationResult result;
        u64 fuel = 0, work = 0;
        try {
            if (known) {
                fuel = known->fuel;
                work = known->work;
                ex.spendRaw(fuel);
            } else {
                GenerationInputs inputs;
                inputs.seed = seed;
                inputs.sizeX = width;
                inputs.sizeY = height;
                inputs.limits = limits_;
                inputs.limits.workLimit = ex.workBudget->remaining();
                VM vm(program.file, inputs, std::span<const UniformValue>(values), ex.fuel);
                vm.run();
                result = vm.takeResult();
                fuel = vm.fuelBudget() - vm.fuel();
                ex.spendRaw(fuel);
                work = vm.workBudget() - vm.work();
            }
            ex.work(work);
        } catch (const FormatError& error) {
            throw Trap("skin '" + program.name + "': " + error.what(), error.reason, ErrorStage::Execute,
                       ex.activeInstruction);
        }

        texture.image.fuel = fuel;
        texture.image.work = work;
        if (known) {
            texture.image.reused = true;
            texture.image.kinds = known->kinds;
            texture.image.color.width = width;
            texture.image.color.height = height;
        } else {
            texture.image.color = std::move(result.image);
            texture.image.images = std::move(result.images);
            for (const AuxImage& image : texture.image.images) texture.image.kinds |= 1u << static_cast<u32>(image.kind);
        }
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
        case Op::SmoothBegin: case Op::RefineBegin: case Op::BendBegin: case Op::TwistBegin: case Op::TaperBegin:
        case Op::DisplaceBegin:
            blockBegin(ex, spec->op);
            break;
        case Op::SculptBegin: sculptBegin(ex); break;
        case Op::CarveBegin: carveBegin(ex); break;
        case Op::LumpBall: {
            const f32 radius = ex.popF32();
            const Vec3 at = popVec3(ex);
            range(finite(at) && std::isfinite(radius) && radius > 0.f, "ball needs a finite centre and a positive radius");
            Lump shape;
            shape.a = shape.b = {at.x, at.y, at.z};
            shape.radii[0] = shape.radii[1] = shape.radii[2] = radius;
            lump(ex, "ball", shape);
            break;
        }
        case Op::LumpBall3: {
            const Vec3 radii = popVec3(ex), at = popVec3(ex);
            range(finite(at) && finite(radii) && radii.x > 0.f && radii.y > 0.f && radii.z > 0.f,
                  "ball needs a finite centre and positive radii");
            Lump shape;
            shape.a = shape.b = {at.x, at.y, at.z};
            shape.radii[0] = radii.x;
            shape.radii[1] = radii.y;
            shape.radii[2] = radii.z;
            lump(ex, "ball", shape);
            break;
        }
        case Op::LumpCapsule: case Op::LumpCapsule2: {
            const f32 r1 = ex.popF32(), r0 = spec->op == Op::LumpCapsule2 ? ex.popF32() : r1;
            const Vec3 b = popVec3(ex), a = popVec3(ex);
            range(finite(a) && finite(b) && std::isfinite(r0) && std::isfinite(r1) && r0 > 0.f && r1 > 0.f,
                  "capsule needs finite ends and positive radii");
            Lump shape;
            shape.kind = LumpKind::Capsule;
            shape.a = {a.x, a.y, a.z};
            shape.b = {b.x, b.y, b.z};
            // Both radii scale with the scope; the third slot is unused.
            shape.radii[0] = r0;
            shape.radii[1] = r1;
            lump(ex, "capsule", shape);
            break;
        }
        case Op::LumpSlab: {
            const f32 rounding = ex.popF32();
            const Vec3 size = popVec3(ex), at = popVec3(ex);
            range(finite(at) && finite(size) && size.x > 0.f && size.y > 0.f && size.z > 0.f,
                  "slab needs a finite centre and a positive size");
            range(std::isfinite(rounding) && rounding >= 0.f &&
                      rounding * 2.f <= std::min(size.x, std::min(size.y, size.z)),
                  "slab round must be from 0 to half its smallest side");
            Lump shape;
            shape.kind = LumpKind::Slab;
            shape.a = shape.b = {at.x, at.y, at.z};
            shape.radii[0] = static_cast<double>(size.x) * 0.5;
            shape.radii[1] = static_cast<double>(size.y) * 0.5;
            shape.radii[2] = static_cast<double>(size.z) * 0.5;
            shape.round = rounding;
            lump(ex, "slab", shape);
            break;
        }

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
        case Op::Tube: {
            const u32 skin = popSkin(ex);
            const i32 segments = ex.popI32(), sides = ex.popI32();
            const f32 r1 = ex.popF32(), r0 = ex.popF32();
            const Vec3 d = popVec3(ex), c = popVec3(ex), b = popVec3(ex), a = popVec3(ex);
            build(ex, checkTube(a, b, c, d, r0, r1, sides, segments), skin);
            break;
        }
        case Op::Sweep: {
            const u32 skin = popSkin(ex);
            const auto profile = popPoints(ex);
            const auto path = popPath(ex);
            // The profile is checked like an extrude outline.
            build(ex, checkSweep(path, profile), skin, outlineFuel(profile.size(), false) + path.size() * kPathPointFuel);
            break;
        }

        case Op::SkinDraw: skinDraw(ex, ins.operand); break;
        case Op::ModelLod: ex.push(static_cast<i32>(lod_)); break;
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
        case Op::SkinFit: case Op::SkinTwoSided: case Op::SkinCrisp: case Op::SkinBlend: {
            SkinRecord record = skins_[popSkin(ex) - 1];
            if (spec->op == Op::SkinFit) record.fit = true;
            else if (spec->op == Op::SkinTwoSided) record.twoSided = true;
            else if (spec->op == Op::SkinBlend) record.alpha = SkinAlpha::Blend;
            else record.crisp = true;
            pushRecord(ex, record);
            break;
        }
        case Op::SkinRegion: {
            const f32 h = ex.popF32(), w = ex.popF32(), y = ex.popF32(), x = ex.popF32();
            SkinRecord record = skins_[popSkin(ex) - 1];
            const ModelTexture& texture = textures_[record.texture].image;
            // In binary64, where the sums of two binary32 values are exact.
            const double right = static_cast<double>(x) + static_cast<double>(w);
            const double bottom = static_cast<double>(y) + static_cast<double>(h);
            range(std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h) && w > 0.f && h > 0.f &&
                      x >= 0.f && y >= 0.f && right <= texture.assetWidth && bottom <= texture.assetHeight,
                  "skin region must lie inside the image");
            record.fit = true;
            record.region = true;
            record.regionX = x; record.regionY = y; record.regionW = w; record.regionH = h;
            pushRecord(ex, record);
            break;
        }

        case Op::OutPoint3d: case Op::OutListPoint3d: writePoint(ex, ins.operand); break;
        case Op::OutBox3d: case Op::OutListBox3d: writeBox(ex, ins.operand); break;
        case Op::PartGetPoint3d: partGetPoint(ex, ins.operand); break;
        case Op::PartRender:
            partRender(ex, ins.operand);
            return; // the caller's pointer has moved; the part's frame runs next
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

    // The model's own outputs, in declaration order; parts keep theirs.
    for (std::size_t i = 0; i < model_.outputs.size(); ++i) {
        const ModelOutputDecl& decl = model_.outputs[i];
        if (decl.owner != kRootOwner) continue;
        work.spend();
        ModelOutput output;
        output.name = decl.name;
        output.kind = decl.kind;
        output.list = decl.list;
        output.points = std::move(slots_[i].points);
        output.boxes = std::move(slots_[i].boxes);
        result.outputs.push_back(std::move(output));
    }
    return result;
}

ModelResult generateWith(const LoadedModel& model, const ModelInputs& inputs,
                         const std::span<const UniformValue>* rootValues) {
    GenerationInputs vmInputs;
    vmInputs.seed = inputs.seed;
    vmInputs.uniforms = inputs.uniforms;
    vmInputs.limits = inputs.limits;
    // Zero selects the default of a model, not that of a draw asset.
    if (vmInputs.limits.workLimit == 0) vmInputs.limits.workLimit = kDefaultModelWorkLimit;
    if (inputs.lod > kMaxLod)
        throw FormatError("model lod must be in 0..7", ErrorReason::NumericRange, ErrorStage::Arguments);
    Machine machine(model, inputs);
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
