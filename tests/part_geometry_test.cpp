#include <catch2/catch_test_macros.hpp>

#include "iprop/D4Transform.hpp"
#include "iprop/PartRenderPlan.hpp"
#include "iprop/VM.hpp"
#include "support/test_support.hpp"

#include <array>
#include <limits>

using namespace iprop;
using namespace test_support;

TEST_CASE("D4 composition and inverse preserve pixel and continuous geometry") {
    const RasterPoint raster{7,4};
    const Vec2 extent{7,4};
    for (u8 a = 0; a < 8; ++a) {
        const auto first = *D4Transform::fromKind(a);
        CHECK(first.compose(first.inverse()) == D4Transform{});
        for (u8 b = 0; b < 8; ++b) {
            const auto next = *D4Transform::fromKind(b);
            const auto composed = first.compose(next);
            for (i32 y = 0; y < raster.y; ++y) {
                for (i32 x = 0; x < raster.x; ++x) {
                    const RasterPoint p{x,y};
                    CHECK(composed.mapPixel(p, raster) == next.mapPixel(
                        first.mapPixel(p, raster), first.mapExtent(raster)));
                    const Vec2 centre{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};
                    const auto mapped = composed.mapPixel(p, raster);
                    CHECK(composed.mapPoint(centre, extent) ==
                          Vec2{static_cast<f32>(mapped.x) + 0.5f, static_cast<f32>(mapped.y) + 0.5f});
                    CHECK(composed.mapPoint(centre, extent) == next.mapPoint(
                        first.mapPoint(centre, extent), first.mapExtent(extent)));
                }
            }
        }
    }
    constexpr u8 expected[4][2][2] = {
        {{0,5},{4,2}}, {{1,6},{7,3}}, {{2,4},{5,0}}, {{3,7},{6,1}}
    };
    for (i32 r = 0; r < 4; ++r)
        for (i32 x = 0; x < 2; ++x)
            for (i32 y = 0; y < 2; ++y)
                CHECK(D4Transform::fromParameters(r*90,x,y)->kind() == expected[r][x][y]);
    CHECK_FALSE(D4Transform::fromKind(8));
    CHECK_FALSE(D4Transform::fromParameters(45,0,0));
    CHECK_FALSE(D4Transform::fromParameters(0,2,0));
}

TEST_CASE("part render plans validate dimensions and placement before integer conversions") {
    Part part;
    part.sizeX = 6; part.sizeY = 4; part.origin = {1,2};
    const auto plan = PartRenderPlan::create(part, {2,3}, {9,10}, *D4Transform::fromKind(1));
    CHECK(plan.sourceScale == Vec2{3,2});
    CHECK(plan.sourceExtent == RasterPoint{18,8});
    CHECK(plan.targetExtent == RasterPoint{8,18});
    CHECK(plan.targetOrigin == RasterPoint{7,4});
    CHECK(plan.blitCost == 144);
    for (const f32 value : {0.f, -1.f, 1e30f, std::numeric_limits<f32>::infinity(),
                            std::numeric_limits<f32>::quiet_NaN()}) {
        CHECK_THROWS_AS(PartRenderPlan::create(part,{value,1},{0,0},{}), std::invalid_argument);
    }
    for (const f32 value : {2147483648.f, -1e30f, std::numeric_limits<f32>::infinity(),
                            std::numeric_limits<f32>::quiet_NaN()}) {
        CHECK_THROWS_AS(PartRenderPlan::create(part,{1,1},{value,0},{}), std::invalid_argument);
    }
    part.sizeX = kMaxCanvasSide;
    CHECK(PartRenderPlan::create(part,{1,1},{0,0},{}).sourceExtent.x == kMaxCanvasSide);
    ++part.sizeX;
    CHECK_THROWS_AS(PartRenderPlan::create(part,{1,1},{0,0},{}), std::invalid_argument);
}

TEST_CASE("all D4 images match output bounds with asymmetric scale origin and placement") {
    // Deliberately independent expected edge bounds for rect(1,1,3,1) in 6x4.
    const std::array<OutputRect,8> bounds{{
        {1,1,3,1}, {1,2,1,3}, {2,2,3,1}, {2,1,1,3},
        {2,1,3,1}, {1,2,3,1}, {1,1,1,3}, {2,2,1,3}}};
    const auto append = [](std::vector<u8>& body, Opcode op, u16 operand = 0, u8 aux = 0) {
        const auto bytes = ins(static_cast<u8>(op), aux, operand);
        body.insert(body.end(), bytes.begin(), bytes.end());
    };
    for (u8 kind = 0; kind < 8; ++kind) {
        std::vector<u8> helper;
        for (u16 c : {u16{1},u16{1},u16{2},u16{1}}) append(helper,Opcode::ConstF32,c);
        append(helper,Opcode::ConstColor); append(helper,Opcode::Rect);
        for (u16 c : {u16{1},u16{1},u16{2},u16{1}}) append(helper,Opcode::ConstF32,c);
        append(helper,Opcode::OutRect,1);
        append(helper,Opcode::Return);
        std::vector<u8> part;
        append(part,Opcode::Call,2); append(part,Opcode::Return);
        std::vector<u8> draw;
        for (int i=0;i<3;++i) append(draw,Opcode::ConstI32);
        append(draw,Opcode::ConstF32,3); append(draw,Opcode::ConstF32,4);
        append(draw,Opcode::PartDraw,0,kind);
        append(draw,Opcode::PartGetRect,1); append(draw,Opcode::OutRect,0);
        append(draw,Opcode::Return);
        const auto bytes = fullFile(
            metaSection(16,16,0,0,1,1,1000000,false,{},0,0,0,{},{{1,0,1}}),
            constSection({"draw","root","part"},{0},{0.f,1.f,3.f,8.f,5.f},{{255,0,0,255}}),
            codeSection({drawSpec(draw), {kAnonymousName,{0,0},{},{},part},
                         {kAnonymousName,{},{},{},helper}},
                        {{1,6,4,1,1,{},{{1,0,2}}}}));
        auto file = loadFile(bytes);
        validate(file);
        for (const Vec2 scale : {Vec2{2,1}, Vec2{1,3}, Vec2{1,1}}) {
            GenerationInputs inputs;
            inputs.sizeX = static_cast<u32>(16*scale.x);
            inputs.sizeY = static_cast<u32>(16*scale.y);
            VM vm(file,inputs); vm.run();
            const auto result = vm.result();
            const auto expected = bounds[kind];
            const auto output = result.outputs[0].rects[0];
            CHECK(output.x == expected.x); CHECK(output.y == expected.y);
            CHECK(output.w == expected.w); CHECK(output.h == expected.h);
            const f32 x0 = 8-scale.x + output.x*scale.x;
            const f32 y0 = 5-scale.y + output.y*scale.y;
            for (u32 y=0;y<result.image.height;++y) {
                for (u32 x=0;x<result.image.width;++x) {
                    const bool painted = static_cast<f32>(x) >= x0 && static_cast<f32>(y) >= y0 &&
                        static_cast<f32>(x) < x0+output.w*scale.x &&
                        static_cast<f32>(y) < y0+output.h*scale.y;
                    CHECK((result.image.at(x,y).a != 0) == painted);
                }
            }
        }
    }
}

TEST_CASE("nested axis swaps preserve image output alignment and restore helper context") {
    const auto append = [](std::vector<u8>& body, Opcode op, u16 operand = 0, u8 aux = 0) {
        const auto bytes = ins(static_cast<u8>(op),aux,operand);
        body.insert(body.end(),bytes.begin(),bytes.end());
    };
    const auto callPart = [&](std::vector<u8>& body, u16 index, u8 kind, u16 x, u16 y) {
        for (int i=0;i<3;++i) append(body,Opcode::ConstI32);
        append(body,Opcode::ConstF32,x); append(body,Opcode::ConstF32,y);
        append(body,Opcode::PartDraw,index,kind);
    };
    std::vector<u8> a;
    for (u16 i : {u16{1},u16{1},u16{3},u16{1}}) append(a,Opcode::ConstF32,i);
    append(a,Opcode::ConstColor); append(a,Opcode::Rect);
    for (u16 i : {u16{1},u16{1},u16{3},u16{1}}) append(a,Opcode::ConstF32,i);
    append(a,Opcode::OutRect,1); append(a,Opcode::Return);
    std::vector<u8> helper;
    callPart(helper,0,1,2,2); // A at raster (2,2), origin (1,1), inside B
    append(helper,Opcode::PartGetRect,1);
    for (u16 i : {u16{3},u16{2},u16{1},u16{0}}) append(helper,Opcode::LocalSet,i);
    // A's raster offset in B is (1,0), hence its asset offset is (1,0).
    append(helper,Opcode::LocalGet,0); append(helper,Opcode::ConstF32,1);
    append(helper,Opcode::F32Add);
    for (u16 i : {u16{1},u16{2},u16{3}}) append(helper,Opcode::LocalGet,i);
    append(helper,Opcode::OutRect,2); append(helper,Opcode::Return);
    std::vector<u8> b;
    append(b,Opcode::Call,3); append(b,Opcode::Return);
    std::vector<u8> draw;
    callPart(draw,1,3,4,5);
    append(draw,Opcode::PartGetRect,2); append(draw,Opcode::OutRect,0);
    append(draw,Opcode::Return);
    const auto bytes = fullFile(
        metaSection(16,16,0,0,1,1,1000000,false,{},0,0,0,{},{{1,0,1}}),
        constSection({"draw","root","a","b"},{0},{0,1,2,3,8,5},{{255,0,0,255}}),
        codeSection({drawSpec(draw),{kAnonymousName,{0,0},{},{},a},
                     {kAnonymousName,{0,0},{},{},b},
                     {kAnonymousName,{},{},{1,1,1,1},helper}},
                    {{1,6,4,1,1,{},{{1,0,2}}},{2,8,8,1,1,{},{{1,0,3}}}}));
    auto file = loadFile(bytes);
    validate(file);
    GenerationInputs inputs; inputs.sizeX = 32; inputs.sizeY = 16;
    VM vm(file,inputs); vm.run();
    const auto result = vm.result();
    const auto rect = result.outputs[0].rects[0];
    CHECK(rect.x == 3.f); CHECK(rect.y == 2.f); CHECK(rect.w == 3.f); CHECK(rect.h == 1.f);
    for (u32 y=0;y<16;++y)
        for (u32 x=0;x<32;++x)
            CHECK((result.image.at(x,y).a != 0) == (x >= 12 && x < 18 && y == 6));
}
