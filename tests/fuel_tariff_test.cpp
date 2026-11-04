#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/VM.hpp"
#include "iprop/VmInternals.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include <algorithm>

using namespace iprop;

TEST_CASE("tariffs round the entire opcode once and never wrap", "[fuel-v09]") {
    LoadedFile file;
    detail::Execution ex(file);
    for (const auto op : {Opcode::NoiseMask, Opcode::MaskInvert, Opcode::LayerBlur,
                          Opcode::Poly, Opcode::PartCreateRegion, Opcode::PartsFromRegions, Opcode::Pixel}) {
        const auto rate = detail::fuelPercent(op);
        for (u64 units : {u64{1}, u64{99}, u64{100}, u64{1025}, UINT64_MAX}) {
            const auto expected = units / 100 * rate + (units % 100 * rate + 99) / 100;
            CHECK(detail::fuelScaled(units, rate) == expected);
            ex.fuel = expected; ex.beginInstruction(op); ex.spend(1); ex.spend(units - 1);
            CHECK(ex.fuel == 0);
            ex.fuel = expected - 1; ex.beginInstruction(op);
            CHECK_THROWS_AS(ex.spend(units), Trap);
        }
        ex.fuel = 10000; ex.beginInstruction(op);
        for (int i = 0; i < 1000; ++i) ex.spend(1);
        CHECK(10000 - ex.fuel == detail::fuelScaled(1000, rate));
    }
    ex.fuel = UINT64_MAX; ex.beginInstruction(Opcode::NoiseMask); ex.spend(UINT64_MAX);
    CHECK_THROWS_AS(ex.spend(1), Trap);
    ex.fuel = 100; ex.beginInstruction(Opcode::Poly); ex.spend(26); ex.spendRaw(7);
    CHECK(ex.fuel == 73); // ceil(26 * .75) + seven independent clip samples
}

TEST_CASE("disk morphology matches independent integer dilation and erosion", "[masks][fuel-v09]") {
    for (const bool rgba : {false, true}) for (u8 transform = 0; transform < 8; ++transform)
    for (const i32 radius : {0, 1, 2, 4, 16, 64}) for (const bool expand : {false, true}) {
        LoadedFile file; detail::Execution ex(file);
        ex.frames.emplace_back(); ex.target = RenderTarget::create(1, 1, {1, 1}); ex.fuel = 1'000'000'000;
        auto source = std::make_shared<SurfaceBuffer>();
        source->raster.width = 4; source->raster.height = 5;
        source->format = rgba ? SurfaceBuffer::Format::RGBA8 : SurfaceBuffer::Format::A8;
        for (u32 i = 0; i < 20; ++i) {
            const auto alpha = static_cast<u8>((i * 73 + transform * 17) % 256);
            if (rgba) source->raster.pixels.push_back({17, 34, 51, alpha});
            else source->coverage.push_back(alpha);
        }
        const MaskValue mask{source, {*D4Transform::fromKind(transform), {-17, 11}}};
        const auto original = maskBounds(mask);
        const u32 padding = expand ? static_cast<u32>(radius) : 0;
        const auto width = original.width + 2 * padding, height = original.height + 2 * padding;
        std::vector<u8> expected(static_cast<std::size_t>(width) * height, expand ? 0 : 255);
        for (u32 y = 0; y < original.height; ++y) for (u32 x = 0; x < original.width; ++x) {
            const auto value = maskCoverage(mask, static_cast<i64>(original.x) + x, static_cast<i64>(original.y) + y);
            for (i32 dy = -radius; dy <= radius; ++dy) for (i32 dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) continue;
                if (expand) {
                    auto& out = expected[static_cast<std::size_t>(y + padding + dy) * width + x + padding + dx];
                    out = std::max(out, value);
                } else {
                    auto& out = expected[static_cast<std::size_t>(y) * width + x];
                    out = std::min(out, maskCoverage(mask, static_cast<i64>(original.x) + x + dx,
                        static_cast<i64>(original.y) + y + dy));
                }
            }
        }
        ex.push(mask); ex.push(radius);
        REQUIRE(detail::stepMasks(ex, {expand ? Opcode::MaskExpand : Opcode::MaskShrink, 0, 0}));
        auto result = std::get<MaskValue>(ex.pop());
        INFO("radius " << radius << " D4 " << int(transform) << " RGBA " << rgba << " expand " << expand);
        CHECK(result.buffer->coverage == expected);
        CHECK(result.mapping.origin == RasterPoint{original.x - static_cast<i32>(padding), original.y - static_cast<i32>(padding)});
        CHECK(ex.resources.bytes == maskBytes(width, height));
        result = {}; CHECK(ex.resources.bytes == 0);
        if (expand && radius == 4 && !rgba && transform == 0) {
            const auto occupied = kMaxGenerationData - maskBytes(width, height);
            ex.resources.bytes = occupied; ex.push(mask); ex.push(radius);
            CHECK_THROWS_AS(detail::stepMasks(ex, {Opcode::MaskExpand, 0, 0}), Trap);
            CHECK(ex.resources.bytes == occupied); // output refunded when deque scratch cannot fit
            CHECK(ex.frame().stack.empty());
        }
    }
}

TEST_CASE("public tariff fuel boundaries retain RNG and work independently", "[fuel-v09]") {
    const auto program = ips::parse(R"(asset t {size 16x16 output point rng draw {
        layer image {oval(8,8,6,5,#abcdef81)}
        m=noise.mask({0,0},{16,16},noise.mode_fbm,{0.1,0.1},0,seed)
        clip(m) {surface.paint(mask.expand(mask.from(image),4),#abc)}
        surface.paint(image.blur(1.5))
        rng={unit(),unit()}
    }})");
    ips::checkAsset(program, {});
    const auto bytes = compileProgram(program, {}, false); auto file = loadFile(bytes); validate(file);
    VM reference(file, {}); reference.run(); const auto expected = reference.takeResult();
    const auto fuel = reference.fuelBudget() - reference.fuel(), work = reference.workBudget() - reference.work();
    file.meta.fuel = fuel;
    VM exact(file, {}); exact.run(); CHECK(exact.fuel() == 0);
    const auto actual = exact.takeResult();
    CHECK(actual.image.pixels == expected.image.pixels);
    CHECK(actual.outputs[0].points[0].x == expected.outputs[0].points[0].x);
    CHECK(actual.outputs[0].points[0].y == expected.outputs[0].points[0].y);
    file.meta.fuel = fuel - 1;
    VM shortFuel(file, {}); CHECK_THROWS_AS(shortFuel.run(), Trap); CHECK_THROWS_AS(shortFuel.takeResult(), std::logic_error);
    file.meta.fuel = 1'000'000'000;
    GenerationInputs inputs; inputs.limits.workLimit = work;
    VM exactWork(file, inputs); exactWork.run(); CHECK(exactWork.takeResult().image.pixels == expected.image.pixels);
    inputs.limits.workLimit = work - 1;
    VM shortWork(file, inputs);
    CHECK_THROWS_AS(([&] {shortWork.run(); shortWork.takeResult();})(), Trap);
}
