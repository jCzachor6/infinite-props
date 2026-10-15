#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "Pools.hpp"
#include "ips/BuiltinArguments.hpp"
#include "iprop/Collections.hpp"
#include "iprop/Instruction.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"

#include <bit>
#include <map>
#include <string>
#include <vector>

using namespace iprop;
using namespace compiler_test;
namespace {
std::vector<u8> compileRoot(const std::map<std::string, std::string>& sources, bool optimize) {
    SourceCompilationOptions options; options.optimize = optimize; options.compress = false;
    const auto result = compileSource("root.ips", [&](std::string_view path) { return sources.at(std::string(path)); }, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(result.succeeded());
    return result.bytes;
}
std::vector<u32> outputBits(const RunOutcome& outcome) {
    std::vector<u32> bits;
    for (const auto& output : outcome.outputs) {
        for (const auto& point : output.points) { bits.push_back(std::bit_cast<u32>(point.x)); bits.push_back(std::bit_cast<u32>(point.y)); }
        for (const auto& rect : output.rects)
            for (const auto value : {rect.x, rect.y, rect.w, rect.h}) bits.push_back(std::bit_cast<u32>(value));
        for (const auto& circle : output.circles)
            for (const auto value : {circle.x, circle.y, circle.r}) bits.push_back(std::bit_cast<u32>(value));
    }
    return bits;
}
} // namespace

TEST_CASE("stepper constants have their own pool, limit and accounting", "[review]") {
    compiler::Pools pools;
    const auto stepper = [](i32 value) {
        auto data = std::make_shared<SamplerData>();
        data->type = ValueType::stepper(ValueType::I32);
        data->lower = value;
        return StepperValue{data->type, data};
    };
    for (i32 i = 0; i < 65536; ++i) REQUIRE(pools.addStepper(stepper(i)) == static_cast<u16>(i));
    CHECK(pools.addStepper(stepper(7)) == 7);
    try {
        (void)pools.addStepper(stepper(65536));
        FAIL("a stepper past the pool limit was accepted");
    } catch (const compiler::PoolLimit& error) {
        CHECK(std::string(error.what()) == "too many stepper constants");
    }
    auto data = std::make_shared<SamplerData>();
    data->type = ValueType::sampler(ValueType::I32);
    data->lower = i32{1};
    CHECK(pools.addSampler(SamplerValue{data->type, data}) == 0);
}

TEST_CASE("optimization keeps every constant pool operand consistent", "[review]") {
    // Lists, samplers, steppers, palettes, distributions, list methods,
    // layers and clips: every pool kind a compaction remaps.
    const std::string source = R"(asset t { size 8x8
  output point p
  const list<distribution> shapes = [weighted{0:1,1:1,2:1}, gaussian(0,1)]
  palette widths = [0.0 to 2.0, 0.5, 1.0 to 3.0]
  draw {
    s = sampler(0.0 to 2.0 with shapes[0]) t = stepper(1 to 4)
    xs = [1, 2.5, 3].sort() ys = [sampler(1 to 2), sampler(3 to 9)]
    v = s.pick(3) w = t.pick(2) q = widths[1].pick()
    layer L { pixel(1,1,#fff) for i in 0..list.len(xs) { pixel(i, 2, #f00) } }
    clip (mask.from(L)) { surface.paint(L) }
    d = weighted{1:list.len(v), 2:list.len(w)}
    y = 0.5 to 1.5 with d
    p = {q + v[0] + float(w[1]), ys[1].pick() + y}
  }
})";
    const auto plain = compileRoot({{"root.ips", source}}, false);
    const auto optimized = compileRoot({{"root.ips", source}}, true);
    for (const u32 seed : {0u, 7u, 26u, 0xffffffffu}) {
        INFO("seed " << seed);
        const auto a = execute(plain, seed), b = execute(optimized, seed);
        CHECK(a.canvas.pixels == b.canvas.pixels);
        CHECK(outputBits(a) == outputBits(b));
    }
}

TEST_CASE("surface.paint lowers and formats through its overloads", "[review]") {
    const std::string source = "asset t { size 2x2 draw { layer image { pixel(0, 0, #fff) } region = mask.from(image) "
        "surface.paint(image) surface.paint(image, surface.source_over) surface.paint(image = image, blend = surface.legacy) "
        "surface.paint(region, #ff0000) surface.paint(region, color = #00ff00, blend = surface.source_over) "
        "surface.paint(image = region, color = #0000ff) } }";
    const auto bytes = compileRoot({{"root.ips", source}}, false);
    const auto text = disassemble(bytes);
    std::vector<std::string> paints;
    for (std::size_t at = text.find("surface.paint"); at != std::string::npos; at = text.find("surface.paint", at + 1))
        paints.push_back(text.substr(at, text.find('\n', at) - at));
    CHECK(paints == std::vector<std::string>{"surface.paint 0", "surface.paint 2", "surface.paint 1",
                                             "surface.paint.mask 0", "surface.paint.mask 2", "surface.paint.mask 0"});
    // The formatter binds the same overloads without types.
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    CHECK(stripDebug(compileRoot({{"root.ips", formatted}}, false)) == stripDebug(bytes));
}

TEST_CASE("part output slots skip resource outputs wherever they are declared", "[review]") {
    const std::map<std::string, std::string> sources{
        {"root.ips", "import part from \"part.ips\" asset t { size 8x8 output point a output rect r output circle c "
                     "draw { part.draw(seed, at = {0, 0}) a = part.outputs.a r = part.outputs.r c = part.outputs.c } }"},
        {"part.ips", "asset t { size 4x4 output mask m output point a output layer l output rect r output mask n "
                     "output circle c draw { layer image { pixel(0, 0, #fff) } l = image m = mask.from(image) n = mask.from(image) "
                     "a = {1, 2} r = {3, 4, 5, 6} c = {7, 8, 9} } }"}};
    for (const bool optimize : {false, true}) {
        const auto outcome = execute(compileRoot(sources, optimize), 3);
        REQUIRE(outcome.outputs.size() >= 3);
        CHECK(outcome.outputs[0].points.at(0).x == 1.f);
        CHECK(outcome.outputs[0].points.at(0).y == 2.f);
        CHECK(outcome.outputs[1].rects.at(0).w == 5.f);
        CHECK(outcome.outputs[2].circles.at(0).r == 9.f);
    }
}

TEST_CASE("missing type descriptors are compiler bugs at every writer", "[review]") {
    const std::vector<TypeDescriptor> types{ValueType::list(ValueType::I32)};
    Writer writer;
    CHECK(typeDescriptorIndex(ValueType::list(ValueType::I32), types) == 0);
    CHECK_THROWS_AS(typeDescriptorIndex(ValueType::sampler(ValueType::I32), types), std::logic_error);
    CHECK_THROWS_AS(writeTypeReference(writer, ValueType::list(ValueType::F32), types), std::logic_error);
    compiler::Pools pools;
    CHECK_THROWS_AS(pools.typeIndexOf(ValueType::list(ValueType::Vec2)), std::logic_error);
}

TEST_CASE("every pool-indexing operand names its pool", "[review]") {
    using K = OperandKind;
    const std::vector<std::pair<Opcode, K>> expected{
        {Opcode::LocalGet, K::Local}, {Opcode::LocalSet, K::Local}, {Opcode::LocalTee, K::Local}, {Opcode::LayerBegin, K::Local},
        {Opcode::ConstI32, K::IntConst}, {Opcode::ConstF32, K::FloatConst}, {Opcode::ConstColor, K::ColorConst},
        {Opcode::ConstList, K::ListConst}, {Opcode::ConstSampler, K::SamplerConst},
        {Opcode::ConstStepper, K::StepperConst}, {Opcode::ConstDistribution, K::DistributionConst},
        {Opcode::ListBegin, K::TypeDescriptor}, {Opcode::ListAppend, K::TypeDescriptor}, {Opcode::ListEnd, K::TypeDescriptor},
        {Opcode::ListGet, K::TypeDescriptor}, {Opcode::ListLen, K::TypeDescriptor},
        {Opcode::ListDistinct, K::TypeDescriptor}, {Opcode::ListSort, K::TypeDescriptor},
        {Opcode::SamplerValue, K::TypeDescriptor}, {Opcode::SamplerRange, K::TypeDescriptor}, {Opcode::SamplerWith, K::TypeDescriptor},
        {Opcode::SamplerPick, K::TypeDescriptor}, {Opcode::SamplerPickList, K::TypeDescriptor},
        {Opcode::StepperValue, K::TypeDescriptor}, {Opcode::StepperRange, K::TypeDescriptor}, {Opcode::StepperWith, K::TypeDescriptor},
        {Opcode::StepperPickList, K::TypeDescriptor},
        {Opcode::OutMask, K::ResourceExport}, {Opcode::OutLayer, K::ResourceExport},
        {Opcode::PartGetMask, K::ResourceExport}, {Opcode::PartGetLayer, K::ResourceExport},
        {Opcode::DistributionBegin, K::Immediate}, {Opcode::DistributionAppend, K::Immediate},
        {Opcode::SurfacePaint, K::Immediate}, {Opcode::SurfacePaintMask, K::Immediate},
        {Opcode::PolyList, K::None}, {Opcode::LayerBlur, K::None}, {Opcode::MaskBlur, K::None}};
    for (const auto& [opcode, kind] : expected) {
        INFO(opcodeMnemonic(opcode));
        CHECK(operandKind(opcode) == kind);
    }
}

TEST_CASE("list polygons and variadic polygons lower through their own instruction", "[review]") {
    const auto text = disassemble(compileRoot({{"root.ips", "asset t { size 8x8 draw { "
        "poly([{0, 0}, {4, 0}, {0, 4}], #ffffff) poly(#ff0000, 0, 0, 4, 0, 0, 4) } }"}}, false));
    CHECK(text.find("poly.list\n") != std::string::npos);
    CHECK(text.find("poly 3\n") != std::string::npos);
}

TEST_CASE("every read of a palette shares one collection constant", "[review]") {
    const auto bytes = compileRoot({{"root.ips", "asset t { size 1x1 output point p palette values = [3, 1, 2] "
        "draw { a = list.len(values) b = values.sort() p = {a + list.len(b), values[0] + b[0]} } }"}}, false);
    const auto file = loadFile(bytes);
    CHECK(file.collections.lists.size() == 1);
    const auto outcome = execute(bytes, 0);
    CHECK(outcome.outputs[0].points.at(0).x == 6.f);
    CHECK(outcome.outputs[0].points.at(0).y == 4.f);
}

TEST_CASE("named part call slots match the part parameter list", "[review]") {
    const std::vector<std::string> uniforms{"tint", "size"};
    const auto parameters = ips::partParameters(uniforms);
    CHECK(parameters[ips::kPartSeedSlot].name == "seed");
    CHECK(parameters[ips::kPartAtSlot].name == "at");
    CHECK(parameters[ips::kPartTransformBegin].name == "rot");
    CHECK(parameters[ips::kPartTransformBegin + 1].name == "mirror_x");
    CHECK(parameters[ips::kPartTransformEnd - 1].name == "mirror_y");
    CHECK(parameters[ips::kPartFirstInput].name == "tint");
    CHECK(parameters.size() == ips::kPartFirstInput + uniforms.size());
}
