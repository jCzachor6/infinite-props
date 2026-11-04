#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include <array>
#include <bit>
#include <limits>
#include <thread>

using namespace iprop;
namespace {
LoadedFile program(const std::string& body, const std::string& extra = "", u32 side = 64) {
    const auto source = ips::parse("asset Metered { size " + std::to_string(side) + "x" + std::to_string(side) +
        " output point state " + extra + " draw { " + body + "; state = {unit(), unit()} } }");
    ips::checkAsset(source, {});
    auto file = loadFile(compileProgram(source, {}, false));
    validate(file);
    // Work must protect even a host/program with a large fuel allowance.
    file.meta.fuel = 1'000'000'000;
    return file;
}
struct Pipeline {
    const char* name;
    const char* source;
    Opcode operation;
};
constexpr std::array pipelines{
    Pipeline{"capture", "layer image(alpha=surface.source_over) { pixel(0,0,#abc) }; surface.paint(image)", Opcode::LayerBegin},
    Pipeline{"sampler batch", "values=sampler(0.0 to 1.0 with gaussian(0.5,0.15)).pick(256); state={values[255],0}", Opcode::SamplerPickList},
    Pipeline{"stepper batch", "values=stepper(0.0 to 1.0 with gaussian(0.5,0.15)).pick(256); state={values[255],0}", Opcode::StepperPickList},
    Pipeline{"noise mask", "m = noise.mask({0,0},{64,64},noise.mode_cell_edge,{0.1,0.1},0.2,seed); surface.paint(m,#abc)", Opcode::NoiseMask},
    Pipeline{"morphology", "layer image(alpha=surface.source_over) { rect(8,8,40,40,#abc8) }; m=mask.expand(mask.from(image),4); surface.paint(m,#abc)", Opcode::MaskExpand},
    Pipeline{"algebra", "layer image(alpha=surface.source_over) { rect(8,8,40,40,#abc8) }; m=mask.invert(mask.from(image)); surface.paint(m,#abc)", Opcode::MaskInvert},
    Pipeline{"layer blur", "layer image(alpha=surface.source_over) { rect(8,8,40,40,#abc8) }; surface.paint(image.blur(2.0))", Opcode::LayerBlur},
    Pipeline{"mask blur", "layer image(alpha=surface.source_over) { rect(8,8,40,40,#abc8) }; surface.paint(mask.from(image).blur(2.0),#abc)", Opcode::MaskBlur},
    Pipeline{"layer paint", "layer image(alpha=surface.source_over) { rect(8,8,40,40,#abc8) }; surface.paint(image)", Opcode::SurfacePaint},
    Pipeline{"mask paint", "layer image(alpha=surface.source_over) { rect(8,8,40,40,#abc8) }; surface.paint(mask.from(image),#abc)", Opcode::SurfacePaintMask},
    Pipeline{"polygon", "poly(#abc8,1,1,61,3,59,61,2,59)", Opcode::Poly},
    Pipeline{"curve", "curve({1,1},{32,1},{60,60},2,#abc8)", Opcode::Curve},
    Pipeline{"oval", "oval(1,1,62,62,#abc8)", Opcode::Oval},
    Pipeline{"voronoi", "d=voronoi({0,0},{64,64},36,seed); state=diagram.site(d,0)", Opcode::Voronoi},
    Pipeline{"contour", "layer image(alpha=surface.source_over) { rect(4,4,56,56,#fff) }; c=contour(mask.from(image),0.5); state={regions.count(regions(c)),0}", Opcode::Contour},
    Pipeline{"offset", "r=regions(voronoi({0,0},{64,64},36,seed)); r=regions.gap(r,0.5); state={regions.count(r),0}", Opcode::RegionsGap},
    Pipeline{"region raster", "r=regions(voronoi({0,0},{64,64},16,seed)); surface.paint(regions.mask(r),#abc)", Opcode::RegionsMaskAll},
    Pipeline{"bevel", "r=regions(voronoi({0,0},{64,64},16,seed)); surface.paint(regions.bevel(r,2,{-1,-1}))", Opcode::RegionsBevel},
    Pipeline{"part scan", "layer image(alpha=surface.source_over) { rect(4,4,56,56,#abc8) }; p=part.create(image); part.draw(p)", Opcode::PartCreateLayer},
    Pipeline{"part mask", "layer image(alpha=surface.source_over) { rect(4,4,56,56,#abc8) }; p=part.create(image); surface.paint(part.mask(p),#abc)", Opcode::PartMask},
    Pipeline{"part draw", "layer image(alpha=surface.source_over) { rect(4,4,56,56,#abc8) }; p=part.fill(part.create(mask.from(image)),#abc); part.draw(p)", Opcode::DynamicPartDraw},
};
struct Poll {
    detail::WorkBudget* budget = nullptr;
    u32 pc = 0;
    u32 inOperation = 0;
    u64 first = 0, last = 0;
    static bool stop(void* pointer) {
        auto& p = *static_cast<Poll*>(pointer);
        if (!p.budget || !(p.budget->location.flags & ErrorLocation::PC) || p.budget->location.pc != p.pc) return false;
        const auto used = p.budget->used();
        if (p.inOperation == 0) p.first = used;
        p.last = used;
        return ++p.inOperation == 2;
    }
};
u32 operationPC(const LoadedFile& file, Opcode opcode) {
    const auto code = file.code.entry().code;
    for (std::size_t pc = 0; pc < code.size() / 4; ++pc)
        if (decodeInstruction(code.subspan(pc * 4, 4)).opcode == opcode) return static_cast<u32>(pc);
    FAIL("pipeline operation not found"); return 0;
}
void failed(VM& vm, ErrorReason reason) {
    try { vm.run(); FAIL("generation succeeded past its limit"); }
    catch (const Trap& error) { CHECK(error.reason == reason); CHECK(error.stage == ErrorStage::Execute); }
    CHECK_THROWS_AS(vm.result(), std::logic_error);
    CHECK_THROWS_AS(vm.takeResult(), std::logic_error);
    CHECK_THROWS_AS(vm.run(), std::logic_error);
}
} // namespace

TEST_CASE("independent work counts are deterministic and preserve fuel pixels and RNG", "[execution-limits]") {
    for (const auto& p : pipelines) {
        CAPTURE(p.name);
        const auto file = program(p.source);
        GenerationInputs a; a.seed = 26;
        VM reference(file, a); reference.run();
        const auto used = reference.workBudget() - reference.work();
        REQUIRE(used > 4096);
        const auto expected = reference.takeResult();
        auto b = a; b.limits.workLimit = used;
        b.limits.deadline = ExecutionLimits::Clock::now() + std::chrono::minutes(1);
        VM exact(file, b); exact.run();
        CHECK(exact.work() == 0);
        CHECK(exact.fuel() == reference.fuel());
        CHECK(exact.canvas().pixels == expected.image.pixels);
        CHECK(exact.debugStack().empty());
        // Every pipeline shares one generation budget and traps at one unit
        // below its observed deterministic count, rather than resetting per op.
        b.limits.workLimit = used - 1;
        VM shortRun(file, b); failed(shortRun, ErrorReason::WorkExhausted);
        VM again(file, a); again.run();
        CHECK(again.workBudget() - again.work() == used);
        const auto actual = again.takeResult();
        REQUIRE(actual.outputs.back().points.size() == expected.outputs.back().points.size());
        CHECK(std::bit_cast<u32>(actual.outputs.back().points[0].x) == std::bit_cast<u32>(expected.outputs.back().points[0].x));
        CHECK(std::bit_cast<u32>(actual.outputs.back().points[0].y) == std::bit_cast<u32>(expected.outputs.back().points[0].y));
    }
}

TEST_CASE("every long native pipeline polls inside the active builtin", "[execution-limits]") {
    for (const auto& p : pipelines) {
        CAPTURE(p.name);
        const auto file = program(p.source);
        Poll poll; poll.pc = operationPC(file, p.operation);
        GenerationInputs inputs; inputs.seed = 26;
        inputs.limits.cancelled = Poll::stop; inputs.limits.context = &poll;
        VM vm(file, inputs); poll.budget = vm.executionBudget().get();
        failed(vm, ErrorReason::Cancelled);
        CHECK(poll.inOperation == 2);
        CHECK(poll.last >= poll.first);
        // Reservations can advance the counter before any elements are
        // touched. The callback still runs inside that opcode, including its
        // initialization/copy, rather than waiting for the next instruction.
    }
}

TEST_CASE("cheap scalar fuel cannot bypass work through helpers and many small calls", "[execution-limits]") {
    auto file = program("for i in 0..1000 { state={heavy(float(i)),unit()} }",
        "heavy(x:f32)->f32 { return noise.cell(x*0.01,0.5,7) }");
    VM baseline(file, {}); baseline.run();
    CHECK(baseline.fuelBudget() - baseline.fuel() < 50'000);
    CHECK(baseline.workBudget() - baseline.work() > baseline.fuelBudget() - baseline.fuel());
    GenerationInputs inputs; inputs.limits.workLimit = 20'000;
    VM limited(file, inputs); failed(limited, ErrorReason::WorkExhausted);
    CHECK(limited.fuel() > 0);
}

TEST_CASE("work overflow is rejected before subtraction and zero selects a finite default", "[execution-limits]") {
    ExecutionLimits limits; limits.workLimit = 9;
    detail::WorkBudget budget(limits); budget.spend(8);
    CHECK(budget.remaining() == 1);
    CHECK_THROWS_AS(budget.spend(std::numeric_limits<u64>::max()), Trap);
    CHECK(budget.remaining() == 1);
    limits.workLimit = 0;
    CHECK(detail::WorkBudget(limits).budget() == kDefaultWorkLimit);
    limits.workLimit = std::numeric_limits<u64>::max();
    CHECK(detail::WorkBudget(limits).budget() == std::numeric_limits<u64>::max());
}

TEST_CASE("cancellation tokens can be shared with another thread and deadline uses monotonic time", "[execution-limits]") {
    auto token = std::make_shared<CancellationToken>();
    CHECK_FALSE(token->cancelled());
    std::thread worker([&] { token->cancel(); }); worker.join();
    CHECK(token->cancelled());
    ExecutionLimits limits; limits.cancellation = token;
    detail::WorkBudget cancelled(limits);
    try { cancelled.poll(); FAIL("cancel was ignored"); }
    catch (const Trap& error) { CHECK(error.reason == ErrorReason::Cancelled); }
    limits.cancellation.reset(); limits.deadline = ExecutionLimits::Clock::now() - std::chrono::seconds(1);
    detail::WorkBudget expired(limits);
    try { expired.poll(); FAIL("deadline was ignored"); }
    catch (const Trap& error) { CHECK(error.reason == ErrorReason::DeadlineExceeded); }
    CHECK(expired.used() == 0);
    auto file = program("pixel(0,0,#abc)");
    GenerationInputs inputs; inputs.limits = limits;
    CHECK_THROWS_AS(VM(file, inputs), Trap);
    CHECK(file.meta.fuel == 1'000'000'000);
    file.meta.fuel = 5'000'000;
    VM defaultFuel(file, {}); CHECK(defaultFuel.fuelBudget() == 5'000'000);
}

TEST_CASE("prepaid large copies still poll between at most 1024 initialized elements", "[execution-limits]") {
    struct CopyPoll {
        std::vector<u32>* output;
        std::size_t previous = 0, maximum = 0;
        u32 calls = 0;
        static bool stop(void* pointer) {
            auto& p = *static_cast<CopyPoll*>(pointer);
            const auto size = p.output->size();
            p.maximum = std::max(p.maximum, size - p.previous);
            p.previous = size;
            return ++p.calls == 5;
        }
    };
    std::vector<u32> data;
    CopyPoll poll{&data};
    ExecutionLimits limits; limits.cancelled = CopyPoll::stop; limits.context = &poll;
    detail::WorkBudget work(limits);
    try { detail::initializeWork(data, 100'000, 7u, work); FAIL("copy ignored cancellation"); }
    catch (const Trap& error) { CHECK(error.reason == ErrorReason::Cancelled); }
    CHECK(poll.maximum <= kWorkPollInterval);
    CHECK(data.size() < 100'000);
    CHECK(poll.calls == 5);
}

TEST_CASE("stopping geometry allocation and moves releases all unpublished memory", "[execution-limits]") {
    LoadedFile file;
    detail::Execution ex(file);
    const auto original = ex.resources.bytes;
    ExecutionLimits limits; limits.workLimit = 100;
    ex.workBudget = std::make_shared<detail::WorkBudget>(limits);
    ex.resources.workBudget = ex.workBudget;
    // Resource headers and capacity accounting share their ordinary RAII path.
    {
        detail::ResourceVector<u32,4> values{detail::ResourceAllocator<u32,4>(ex.resources)};
        try { for (u32 i = 0; i < 1000; ++i) values.push_back(i); FAIL("unlimited buffer growth"); }
        catch (const Trap& error) { CHECK(error.reason == ErrorReason::WorkExhausted); }
    }
    CHECK(ex.resources.bytes == original);
}

TEST_CASE("scalar noise reserves work independently of its one-fuel legacy tariff", "[execution-limits]") {
    const auto scalar = [](Opcode opcode, std::vector<Value> args, u64 expected, u64 extraFuel = 0) {
        LoadedFile file; file.code.functions.emplace_back(); detail::Execution ex(file);
        ex.frames.emplace_back(); ex.frame().stack = std::move(args); ex.fuel = 1000;
        REQUIRE(detail::stepSeeded(ex, {opcode,0,0}));
        CHECK(ex.workBudget->used() == expected);
        CHECK(ex.fuel == 1000 - extraFuel); // VM's base instruction unit is separate.
    };
    // Four lattice corners, three triangle corners, and 25 Worley candidates
    // plus five row steps. Every declared fBm iteration and gradient is counted.
    scalar(Opcode::Noise, {0.2f,0.3f,i32{7}}, 4);
    scalar(Opcode::Fault, {0.2f,0.3f,i32{7}}, 3);
    scalar(Opcode::NoiseGrad, {0.2f,0.3f,i32{7}}, 4);
    scalar(Opcode::NoiseCell, {0.2f,0.3f,i32{7}}, 30);
    scalar(Opcode::NoiseCell2, {0.2f,0.3f,i32{7}}, 30, 24);
    scalar(Opcode::NoiseCellId, {0.2f,0.3f,i32{7}}, 30, 24);
    scalar(Opcode::NoiseFbm, {0.2f,0.3f,i32{8},i32{7}}, 40);
    scalar(Opcode::NoiseRidge, {0.2f,0.3f,i32{8},i32{7}}, 40);
    scalar(Opcode::Warp, {0.2f,0.3f,0.2f,i32{7}}, 60);
    scalar(Opcode::NoiseWarpVec, {0.2f,0.3f,0.0f,i32{7}}, 0);
    scalar(Opcode::NoiseFbmConfig, {0.2f,0.3f,i32{8},2.0f,0.0f,i32{7}}, 5, 7);
}
