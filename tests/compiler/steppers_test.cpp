#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/ListOperations.hpp"
#include "iprop/VmInternals.hpp"
#include "iprop/math/Color.hpp"
#include "iprop/math/Distribution.hpp"
#include "iprop/math/Vector.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

using namespace iprop;
namespace {
std::vector<u8> compileSteps(const std::string& source, bool optimize = true) {
    auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
std::string asset(const std::string& body, const std::string& helpers = {}) {
    return "asset t{size 16x16 output list point values output point state " + helpers +
        " draw{" + body + " state={seed&65535,seed>>16}}}";
}
void sameResults(const std::vector<u8>& a, const std::vector<u8>& b, u32 seed) {
    const auto left = compiler_test::execute(a, seed), right = compiler_test::execute(b, seed);
    CHECK(left.canvas.pixels == right.canvas.pixels);
    REQUIRE(left.outputs.size() == right.outputs.size());
    for (std::size_t i = 0; i < left.outputs.size(); ++i) {
        REQUIRE(left.outputs[i].points.size() == right.outputs[i].points.size());
        for (std::size_t j = 0; j < left.outputs[i].points.size(); ++j) {
            CHECK(std::bit_cast<u32>(left.outputs[i].points[j].x) == std::bit_cast<u32>(right.outputs[i].points[j].x));
            CHECK(std::bit_cast<u32>(left.outputs[i].points[j].y) == std::bit_cast<u32>(right.outputs[i].points[j].y));
        }
    }
}
void sameValue(const Value& actual, const Value& expected) {
    REQUIRE(valueType(actual) == valueType(expected));
    if (const auto* i = std::get_if<i32>(&expected)) CHECK(std::get<i32>(actual) == *i);
    else if (const auto* f = std::get_if<f32>(&expected)) CHECK(std::bit_cast<u32>(std::get<f32>(actual)) == std::bit_cast<u32>(*f));
    else if (const auto* c = std::get_if<Color>(&expected)) CHECK(std::get<Color>(actual) == *c);
    else {
        const auto a = std::get<Vec2>(actual), e = std::get<Vec2>(expected);
        CHECK(std::bit_cast<u32>(a.x) == std::bit_cast<u32>(e.x));
        CHECK(std::bit_cast<u32>(a.y) == std::bit_cast<u32>(e.y));
    }
}
struct Harness {
    LoadedFile file;
    detail::Execution ex{file};
    explicit Harness(TypeDescriptor item) {
        file.collections.types = {ValueType::stepper(item)};
        file.code.functions.emplace_back(); ex.frames.emplace_back();
        ex.fuel = 10000000; ex.frame().stream = 26;
    }
    StepperValue capture(Value lo, std::optional<Value> hi = {}, std::optional<Distribution> d = {}) {
        ex.push(lo); if (hi) ex.push(*hi); if (d) ex.push(*d);
        const bool dispatched = detail::stepSteppers(ex, {d ? Opcode::StepperWith : hi ? Opcode::StepperRange : Opcode::StepperValue, 0, 0});
        REQUIRE(dispatched);
        return std::get<StepperValue>(ex.pop());
    }
    ListValue pick(const StepperValue& handle, i32 n) {
        ex.push(handle); ex.push(n);
        const bool dispatched = detail::stepSteppers(ex, {Opcode::StepperPickList, 0, 0});
        REQUIRE(dispatched);
        CHECK(ex.frame().stream == 26);
        return std::get<ListValue>(ex.pop());
    }
};
Distribution analytic(DistributionFamily family, std::array<f32, 4> parameters) {
    return {family, parameters, {}};
}
Distribution points(DistributionFamily family, std::vector<DistributionPoint> values) {
    return {family, {}, std::make_shared<PointDistributionData>(PointDistributionData{family, std::move(values)})};
}
void rejectSteps(const std::string& body, const std::string& message) {
    const auto result = compileSource("stepper-error.ips", [&](std::string_view) { return "asset t{size 1x1 " + body + "}"; });
    CHECK_FALSE(result.succeeded()); CHECK(result.bytes.empty()); REQUIRE_FALSE(result.diagnostics.empty());
    INFO(result.diagnostics[0].message);
    CHECK(result.diagnostics[0].message.find(message) != std::string::npos);
    CHECK(result.diagnostics[0].location.has_value());
}
void put16(std::vector<u8>& bytes, std::size_t at, u16 value) {
    bytes.at(at) = static_cast<u8>(value); bytes.at(at + 1) = static_cast<u8>(value >> 8);
}
void put32(std::vector<u8>& bytes, std::size_t at, u32 value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<u8>(value >> (8 * i));
}
} // namespace

TEST_CASE("stepper lists cover four types all counts constants and equal endpoints", "[stepper]") {
    const std::vector<std::pair<Value, Value>> endpoints{
        {i32{0}, i32{5}}, {-3.f, 4.5f}, {Color{1, 32, 64, 0}, Color{255, 96, 3, 255}},
        {Vec2{-3, 4}, Vec2{5, -6}}};
    for (const auto& [lo, hi] : endpoints) for (const i32 n : {0, 1, 2, 10, 256}) {
        INFO(valueTypeName(valueType(lo)) << " count " << n);
        Harness harness(valueType(lo));
        const auto handle = harness.capture(lo, hi);
        const auto before = harness.ex.resources.bytes;
        auto result = harness.pick(handle, n);
        REQUIRE(result.data->elements.size() == static_cast<std::size_t>(n));
        CHECK(result.type == ValueType::list(valueType(lo)));
        CHECK(harness.ex.resources.bytes == before + ResourceArena::listBytes(static_cast<std::size_t>(n)));
        const auto again = harness.pick(handle, n);
        CHECK(result.data != again.data);
        for (i32 i = 0; i < n; ++i) sameValue(result.data->elements[i], again.data->elements[i]);
        if (n) sameValue(result.data->elements.front(), lo);
        if (n > 1) sameValue(result.data->elements.back(), hi);
        for (const bool constant : {false, true}) {
            const auto description = constant ? harness.capture(lo) : harness.capture(lo, lo);
            const auto repeated = harness.pick(description, n);
            for (const auto& value : repeated.data->elements) sameValue(value, lo);
        }
    }
    Harness floats(ValueType::F32);
    const auto signedZero = floats.capture(-0.f, 0.f);
    const auto zeros = floats.pick(signedZero, 2);
    sameValue(zeros.data->elements[0], -0.f);
    sameValue(zeros.data->elements[1], 0.f);
    Harness vectors(ValueType::Vec2);
    const auto signedVector = vectors.capture(Vec2{-0.f, 1.f}, Vec2{0.f, -0.f});
    const auto ends = vectors.pick(signedVector, 2);
    sameValue(ends.data->elements[0], Vec2{-0.f, 1.f}); sameValue(ends.data->elements[1], Vec2{0.f, -0.f});
}

TEST_CASE("stepper integers preserve binary64 interpolation and half-away rounding", "[stepper]") {
    const auto check = [](i32 lo, i32 hi, std::vector<i32> expected) {
        Harness h(ValueType::I32); const auto result = h.pick(h.capture(lo, hi), static_cast<i32>(expected.size()));
        for (std::size_t i = 0; i < expected.size(); ++i) CHECK(std::get<i32>(result.data->elements[i]) == expected[i]);
    };
    check(0, 5, {0, 1, 1, 2, 2, 3, 3, 4, 4, 5});
    check(0, 1, {0, 1, 1}); check(-1, 0, {-1, -1, 0}); check(-1, 1, {-1, -1, 0, 1, 1});
    check(16777217, 16777219, {16777217, 16777218, 16777218, 16777219, 16777219});
    check(-16777219, -16777217, {-16777219, -16777219, -16777218, -16777218, -16777217});
    check(std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max(),
        {std::numeric_limits<i32>::min(), -1073741824, -1, 1073741823, std::numeric_limits<i32>::max()});
    Harness floats(ValueType::F32);
    const auto largest = floats.pick(floats.capture(-std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()), 3);
    CHECK(std::get<f32>(largest.data->elements[1]) == 0.f);
}

TEST_CASE("stepper quantiles use truncated support and skip zero atoms and density gaps", "[stepper]") {
    const std::vector<Distribution> analyticDistributions{
        analytic(DistributionFamily::Gaussian, {3.f, 0.5f, 0.f, 0.f}),
        analytic(DistributionFamily::Triangular, {2.f, 3.f, 4.f, 0.f}),
        analytic(DistributionFamily::Beta, {2.f, 2.f, 2.f, 4.f})};
    for (const auto& d : analyticDistributions) {
        Harness h(ValueType::F32); const auto handle = h.capture(0.f, 6.f, d);
        const auto list = h.pick(handle, 3);
        sameValue(list.data->elements.front(), d.family == DistributionFamily::Gaussian ? 0.f : 2.f);
        sameValue(list.data->elements.back(), d.family == DistributionFamily::Gaussian ? 6.f : 4.f);
        CHECK(std::fabs(std::get<f32>(list.data->elements[1]) - 3.f) < 0.00001f);
        const auto clipped = h.pick(h.capture(2.5f, 3.5f, d), 2);
        sameValue(clipped.data->elements[0], 2.5f); sameValue(clipped.data->elements[1], 3.5f);
    }
    const auto weighted = points(DistributionFamily::Weighted,
        {{i32{0}, i32{0}}, {i32{2}, i32{1}}, {i32{4}, i32{0}}, {i32{6}, i32{3}}, {i32{8}, i32{0}}});
    Harness integers(ValueType::I32);
    const auto atoms = integers.pick(integers.capture(i32{1}, i32{7}, weighted), 5);
    const std::array<i32, 5> expected{2, 6, 6, 6, 6};
    for (std::size_t i = 0; i < expected.size(); ++i) sameValue(atoms.data->elements[i], expected[i]);
    const auto large = points(DistributionFamily::Weighted, {{i32{16777217}, i32{1}}, {i32{16777218}, i32{1}}});
    const auto exact = integers.pick(integers.capture(i32{16777216}, i32{16777219}, large), 2);
    sameValue(exact.data->elements[0], i32{16777217}); sameValue(exact.data->elements[1], i32{16777218});
    const auto density = points(DistributionFamily::Density,
        {{i32{0}, i32{0}}, {i32{1}, i32{0}}, {i32{2}, i32{1}}, {i32{3}, i32{0}},
         {i32{4}, i32{0}}, {i32{5}, i32{1}}, {i32{6}, i32{0}}, {i32{7}, i32{0}}});
    Harness floats(ValueType::F32);
    const auto continuous = floats.pick(floats.capture(0.f, 7.f, density), 257 - 1);
    sameValue(continuous.data->elements.front(), 1.f); sameValue(continuous.data->elements.back(), 6.f);
    for (const auto& value : continuous.data->elements) {
        const auto x = std::get<f32>(value);
        CHECK(((x >= 1.f && x <= 3.f) || (x >= 4.f && x <= 6.f)));
    }
    const auto boundary = floats.pick(floats.capture(0.f, 7.f, density), 3);
    sameValue(boundary.data->elements[1], 4.f);
    const auto clipped = floats.pick(floats.capture(1.5f, 5.5f, density), 2);
    sameValue(clipped.data->elements[0], 1.5f); sameValue(clipped.data->elements[1], 5.5f);
}

TEST_CASE("stepper RGBA and vec2 share one quantile coefficient in unit segment coordinates", "[stepper]") {
    const std::vector<Distribution> distributions{
        analytic(DistributionFamily::Gaussian, {0.5f, 0.2f, 0.f, 0.f}),
        analytic(DistributionFamily::Triangular, {0.2f, 0.4f, 0.8f, 0.f}),
        analytic(DistributionFamily::Beta, {2.f, 3.f, 0.1f, 0.9f}),
        points(DistributionFamily::Weighted, {{0.f, i32{0}}, {0.25f, i32{1}}, {0.75f, i32{2}}, {1.f, i32{0}}}),
        points(DistributionFamily::Density, {{0.f, i32{0}}, {0.2f, i32{0}}, {0.5f, i32{2}}, {0.8f, i32{0}}, {1.f, i32{0}}})};
    const Color lo{2, 40, 80, 0}, hi{240, 80, 10, 250}; const Vec2 a{-10, 7}, b{20, -8};
    for (const auto& d : distributions) {
        Harness colors(ValueType::Color), vectors(ValueType::Vec2);
        const auto cs = colors.pick(colors.capture(lo, hi, d), 10);
        const auto vs = vectors.pick(vectors.capture(a, b, d), 10);
        for (int i = 0; i < 10; ++i) {
            const auto q = math::truncatedDraw(d, 0.0, 1.0, static_cast<double>(i) / 9.0);
            REQUIRE(q.status == math::DrawStatus::Ok);
            const auto t = static_cast<f32>(q.value);
            sameValue(cs.data->elements[i], math::colorMixRgba(lo, hi, t));
            sameValue(vs.data->elements[i], math::vec2Mix(a, b, t));
        }
    }
}

TEST_CASE("stepper supports helpers constants lists formatter assembly and compression", "[stepper]") {
    const std::string declarations = R"(
      const distribution mass = weighted{16777217:1,16777218:3}
      const stepper<i32> s = stepper(16777216 to 16777219 with mass)
      const stepper<f32> f = stepper(-0.0)
      const list<stepper<i32>> specs = [s,s,stepper(0 to 5),stepper(4)]
      const list<stepper<i32>> unique = specs.distinct()
      const list<stepper<i32>> empty = []
      echo(x:stepper<i32>)->stepper<i32>{return x}
      transfer(xs:list<stepper<i32>>)->list<stepper<i32>>{return xs}
    )";
    const auto source = "asset t{size 16x16 output list point values output point state " + declarations + R"(
      draw{before=seed copies=transfer(unique) saved=echo(copies[0])
        items=saved.pick(2) values={items[0]==16777217,items[1]==16777218}
        list<stepper<i32>> descriptions=[] descriptions=copies descriptions=descriptions.distinct()
        stepper<i32> numeric=descriptions[1] nums=numeric.pick(n=10)
        for i in 0..list.len(nums){values={nums[i],nums[i]}}
        constants=descriptions[2].pick(256) values={constants[0],constants[255]}
        zeros=f.pick(2) values={zeros[0],zeros[1]}
        values={list.len(empty),list.len(specs)} values={list.len(descriptions),seed==before}
        state={seed&65535,seed>>16}}
    })";
    const auto reference = compileSteps(asset(R"(
      values={1,1} for i in 0..10{xs=[0,1,1,2,2,3,3,4,4,5] values={xs[i],xs[i]}}
      values={4,4} values={-0.0,-0.0} values={0,4} values={3,1}
    )"), false);
    for (const bool optimize : {false, true}) {
        const auto bytes = compileSteps(source, optimize);
        const auto file = loadFile(bytes); REQUIRE_NOTHROW(validate(file));
        CHECK_FALSE(file.collections.steppers.empty());
        const auto formatted = ips::formatProgram(ips::parse(source));
        CHECK(formatted.find("stepper(") != std::string::npos);
        CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
        for (const auto& variant : {bytes, compressFile(bytes), stripDebug(bytes), assemble(disassemble(bytes)), compileSteps(formatted, optimize)})
            for (const u32 seed : {0u, 1u, 26u, 0xffffffffu}) sameResults(variant, reference, seed);
    }
}

TEST_CASE("stepper capture and pick preserve eager effects snapshots and caller RNG", "[stepper]") {
    const std::string helpers = R"(
      lower()->i32{values={1,seed&65535} return 10 to 15}
      upper()->i32{values={2,seed&65535} return 20 to 30}
      shape()->distribution{values={3,seed&65535} return gaussian(float(15 to 18),2)}
      receiver()->stepper<i32>{return stepper(lower() to upper() with shape())}
      count()->i32{values={4,seed&65535} return 1 to 3}
    )";
    const auto source = asset(R"(
      items=receiver().pick(count()) values={list.len(items),seed&65535}
      lo=0 hi=5 d=gaussian(2,1) before=seed spec=stepper(lo to hi with d)
      alias=spec lo=100 hi=200 d=gaussian(150,10)
      a=alias.pick(2) b=spec.pick(2) values={a[0]==0 && a[1]==5,b[0]==0 && b[1]==5}
      constant=stepper(1 to 100) one=constant.pick(256) two=constant.pick(256)
      values={one[0]==two[0] && one[255]==two[255],seed==before}
      skipped=0 && list.len(stepper(unit()).pick(1))
    )", helpers);
    const auto reference = compileSteps(asset(R"(
      values={1,seed&65535} lower=10 to 15 values={2,seed&65535} upper=20 to 30
      values={3,seed&65535} mean=15 to 18 values={4,seed&65535} n=1 to 3
      values={n,seed&65535} values={1,1} values={1,1}
    )"), false);
    for (const bool optimize : {false, true}) for (const u32 seed : {0u, 26u, 0xffffffffu})
        sameResults(compileSteps(source, optimize), reference, seed);
    const auto drawConstant = compileSteps(asset("spec=stepper(unit()) before=seed a=spec.pick(10) values={a[0]==a[9],seed==before}"));
    const auto constantReference = compileSteps(asset("v=unit() values={1,1}"));
    sameResults(drawConstant, constantReference, 26);
}

TEST_CASE("stepper descriptions and constant lists share retention and structural distinctness", "[stepper]") {
    const auto bytes = compileSteps(R"(asset t{size 1x1 const distribution d=weighted{0:1,1:2}
      const stepper<i32> s=stepper(0 to 1 with d) const list<stepper<i32>> descriptions=[s,s]
      draw{a=s b=descriptions c=descriptions last=s}})", false);
    const auto file = loadFile(bytes); REQUIRE(file.collections.steppers.size() == 1);
    detail::Execution ex(file); ex.frames.emplace_back(); ex.fuel = 1000;
    REQUIRE(detail::stepSteppers(ex, {Opcode::ConstStepper, 0, 0})); const auto description = std::get<StepperValue>(ex.pop());
    REQUIRE(detail::stepCollections(ex, {Opcode::ConstList, 0, 0})); const auto list = std::get<ListValue>(ex.pop());
    CHECK(description.data == std::get<StepperValue>(list.data->elements[0]).data);
    CHECK(ex.resources.bytes == ResourceArena::descriptionBytes() + ResourceArena::listBytes(2) + ResourceArena::distributionBytes(2));
    const auto retained = ex.resources.bytes, fuel = ex.fuel;
    REQUIRE(detail::stepSteppers(ex, {Opcode::ConstStepper, 0, 0}));
    REQUIRE(detail::stepCollections(ex, {Opcode::ConstList, 0, 0}));
    CHECK(ex.resources.bytes == retained); CHECK(ex.fuel == fuel);
    const auto result = selectList(checkListSelection(list, false)); CHECK(result.count == 1);
    CHECK_THROWS_AS(checkListSelection(list, true), std::invalid_argument);
    const auto distinct = compileSteps(asset(R"(
      xs=[stepper(1),stepper(1),stepper(1 to 1),stepper(1 to 1 with gaussian(1,1))]
      fs=[stepper(-0.0),stepper(0.0),stepper(-0.0)]
      ds=[stepper(0 to 1 with weighted{0:1,1:2}),stepper(0 to 1 with weighted{0.0:1,1:2})]
      values={list.len(xs.distinct()),list.len(fs.distinct())} values={list.len(ds.distinct()),seed==seed}
    )"));
    sameResults(distinct, compileSteps(asset("values={3,2} values={2,1}")), 26);
}

TEST_CASE("stepper checks counts handles mass memory and fuel without publishing partial lists", "[stepper]") {
    Harness h(ValueType::I32); const auto handle = h.capture(i32{0}, i32{5});
    const auto trap = [&](StepperValue value, i32 n, ErrorReason reason) {
        h.ex.frame().stack.clear(); h.ex.push(value); h.ex.push(n);
        const auto lists = h.ex.resources.lists.size();
        try { detail::stepSteppers(h.ex, {Opcode::StepperPickList, 0, 0}); FAIL("invalid stepper pick accepted"); }
        catch (const Trap& error) { CHECK(error.reason == reason); }
        CHECK(h.ex.resources.lists.size() == lists); CHECK(h.ex.frame().stack.size() <= 1); CHECK(h.ex.frame().stream == 26);
    };
    trap(handle, -1, ErrorReason::NumericRange); trap(handle, 257, ErrorReason::NumericRange);
    trap({handle.type, {}}, 0, ErrorReason::RuntimeViolation);
    trap({handle.type, {}}, -1, ErrorReason::NumericRange);
    h.ex.resources.bytes = kMaxGenerationData; trap(handle, 1, ErrorReason::OutOfMemory);
    h.ex.resources.bytes = 256; h.ex.fuel = 2; trap(handle, 3, ErrorReason::FuelExhausted);
    h.ex.fuel = 4; trap(handle, 3, ErrorReason::FuelExhausted); // fails after one private element
    h.ex.fuel = 1000;
    const auto absent = h.capture(i32{10}, i32{20}, points(DistributionFamily::Weighted, {{i32{0}, i32{1}}}));
    CHECK(h.pick(absent, 0).data->elements.empty()); trap(absent, 1, ErrorReason::NumericRange);
    const auto equal = h.capture(i32{0}, i32{0}, points(DistributionFamily::Weighted, {{i32{0}, i32{1}}}));
    sameValue(h.pick(equal, 1).data->elements[0], i32{0});
    const auto outside = h.capture(i32{10}, i32{10}, analytic(DistributionFamily::Beta, {2, 2, 0, 1}));
    trap(outside, 1, ErrorReason::NumericRange);
    auto file = loadFile(compileSteps("asset t{size 1x1 draw{if(false){s=stepper(0 to 5)} xs=s.pick(0)}}"));
    VM vm(file, {}); CHECK_THROWS_AS(vm.run(), Trap);
    CHECK_THROWS_AS(vm.result(), std::logic_error); CHECK_THROWS_AS(vm.run(), std::logic_error);
}

TEST_CASE("stepper source rejects unsupported types missing counts and const generation", "[stepper]") {
    for (const auto& [body, message] : std::vector<std::pair<std::string, std::string>>{
        {"draw{s=stepper(1).pick()}", "requires one i32 count"},
        {"draw{s=stepper(1).pick(1.0)}", "count must be i32"},
        {"draw{s=stepper(1).pick(-1)}", "between 0 and 256"},
        {"draw{s=stepper(1).pick(257)}", "between 0 and 256"},
        {"draw{s=stepper(1).pick(count=1)}", "named 'n'"},
        {"draw{s=stepper(1).pick(1,2)}", "one i32 count"},
        {"draw{s=stepper(2 to 1)}", "lower bound exceeds"},
        {"draw{s=stepper([1])}", "stepper capture needs"},
        {"draw{s=stepper(sampler(1))}", "stepper capture needs"},
        {"draw{s=sampler(stepper(1))}", "sampler capture needs"},
        {"draw{s=stepper(gaussian(0,1))}", "stepper capture needs"},
        {"draw{s=stepper(0.0 to 1.0 with gaussian(0,0))}", "sigma"},
        {"draw{s=[stepper(1),stepper(1.0)]}", "list element needs stepper<i32>"},
        {"draw{s=[stepper(1),sampler(1)]}", "list element needs stepper<i32>"},
        {"draw{s=[stepper(1)].sort()}", ".sort does not support"},
        {"draw{sampler<i32> s=stepper(1)}", "sampler<i32>"},
        {"draw{stepper<i32> s=sampler(1)}", "stepper<i32>"},
        {"const list<i32> xs=stepper(0 to 5).pick(10) draw{}", "generation effects"},
        {"const stepper<i32> s=stepper(unit()) draw{}", "not allowed"},
        {"const stepper<i32> a=b const stepper<i32> b=a draw{}", "cyclic"},
        {"palette p=[stepper(1)] draw{}", "needs i32"},
        {"bad(s:stepper<distribution>){} draw{}", "supports only"},
        {"bad(s:stepper<sampler<i32>>){} draw{}", "supports only"},
        {"bad(s:list<list<stepper<i32>>>){} draw{}", "nested lists"}}) rejectSteps(body, message);
}

TEST_CASE("stepper capture rejects dynamic invalid bounds and uninitialized distributions before allocation", "[stepper]") {
    const auto infinity = std::bit_cast<f32>(0x7f800000u), nan = std::bit_cast<f32>(0x7fc00000u);
    struct Invalid { TypeDescriptor type; Value lo, hi; std::optional<Distribution> distribution; };
    for (const auto& invalid : std::vector<Invalid>{
        {ValueType::I32, i32{2}, i32{1}, {}}, {ValueType::F32, 0.f, infinity, {}},
        {ValueType::F32, nan, 1.f, {}}, {ValueType::Vec2, Vec2{0,0}, Vec2{nan,1}, {}},
        {ValueType::F32, 0.f, 1.f, Distribution{}},
        {ValueType::F32, 0.f, 1.f, analytic(DistributionFamily::Gaussian, {0,0,0,0})}}) {
        Harness h(invalid.type);
        CHECK_THROWS_AS(h.capture(invalid.lo, invalid.hi, invalid.distribution), Trap);
        CHECK(h.ex.resources.bytes == 0); CHECK(h.ex.resources.descriptions.empty()); CHECK(h.ex.frame().stream == 26);
    }
}

TEST_CASE("stepper empty picks defer all distribution mass checks and charge exact kernel fuel", "[stepper]") {
    const std::vector<Distribution> distributions{
        analytic(DistributionFamily::Gaussian, {0.5f,0.2f,0,0}),
        analytic(DistributionFamily::Triangular, {0,0.5f,1,0}),
        analytic(DistributionFamily::Beta, {2,3,0,1}),
        points(DistributionFamily::Weighted, {{0.f,i32{0}},{0.25f,i32{1}},{0.75f,i32{3}},{1.f,i32{0}}}),
        points(DistributionFamily::Density, {{0.f,i32{0}},{0.25f,i32{1}},{0.75f,i32{1}},{1.f,i32{0}}})};
    for (const auto& d : distributions) {
        Harness h(ValueType::F32); const auto absent = h.capture(100.f, 101.f, d);
        auto fuel = h.ex.fuel; CHECK(h.pick(absent, 0).data->elements.empty()); CHECK(h.ex.fuel == fuel);
        CHECK_THROWS_AS(h.pick(absent, 1), Trap); h.ex.frame().stack.clear();
        const auto handle = h.capture(0.f, 1.f, d); fuel = h.ex.fuel;
        const auto generated = h.pick(handle, 10); CHECK(generated.data->elements.size() == 10);
        u64 expected = 20; // stored element and nonconstant step, excluding the dispatch base
        for (int i=0;i<10;++i) {
            const auto quantile=math::truncatedDraw(d,0.0,1.0,static_cast<double>(i)/9.0);
            REQUIRE(quantile.status==math::DrawStatus::Ok);
            expected+=math::pointDrawWork(d,0.0,1.0)+quantile.steps;
        }
        CHECK(fuel-h.ex.fuel==expected);
    }
    const auto density=points(DistributionFamily::Density,{{0.f,i32{0}},{1.f,i32{0}},{2.f,i32{1}}});
    Harness h(ValueType::F32);
    sameValue(h.pick(h.capture(0.f,0.f,density),1).data->elements[0],0.f); // sampler's closed natural support rule
    const auto beta=analytic(DistributionFamily::Beta,{2,3,0,1});
    sameValue(h.pick(h.capture(1.f,1.f,beta),1).data->elements[0],1.f);
    CHECK_THROWS_AS(h.pick(h.capture(1.f,2.f,beta),1),Trap); // touching support has no interval mass
}

TEST_CASE("sampler and stepper constants with matching payloads retain separate types and pools", "[stepper]") {
    const auto source=asset(R"(
      before=seed a=sampleDescriptions[0].pick(2) b=stepDescriptions[0].pick(2)
      values={b[0],b[1]} values={seed!=before,list.len(sampleDescriptions)==list.len(stepDescriptions)}
    )",R"(
      const distribution d=weighted{0:1,1:1}
      const sampler<i32> randomSpec=sampler(0 to 1 with d)
      const stepper<i32> stepSpec=stepper(0 to 1 with d)
      const list<sampler<i32>> sampleDescriptions=[randomSpec]
      const list<stepper<i32>> stepDescriptions=[stepSpec]
    )");
    const auto reference=compileSteps(asset("a=sampler(0 to 1 with weighted{0:1,1:1}).pick(2) values={0,1} values={1,1}"));
    for(const bool optimize:{false,true}) {
        const auto bytes=compileSteps(source,optimize);const auto file=loadFile(bytes);
        CHECK(file.collections.samplers.size()==1);CHECK(file.collections.steppers.size()==1);
        CHECK(file.collections.lists.size()==2);CHECK(file.collections.distributions.size()==1);
        for(const auto& variant:{bytes,assemble(disassemble(bytes)),compressFile(bytes)})
            for(const u32 seed:{0u,26u,0xffffffffu})sameResults(variant,reference,seed);
    }
}

TEST_CASE("stepper binary descriptors constants instructions and references reject corruption", "[stepper]") {
    const auto bytes = stripDebug(compileSteps(R"(asset t{size 1x1
      const stepper<f32> s=stepper(0.0 to 1.0 with beta(2,3)) const list<stepper<f32>> specs=[s]
      draw{items=specs value=s xs=stepper(2).pick(2) ys=stepper(0 to 5).pick(2)
        zs=stepper(0.0 to 1.0 with gaussian(0.5,0.2)).pick(2)}})", false));
    const auto file = loadFile(bytes); const auto pool = file.layout.find(SectionId::Steppers)->offset;
    const auto lists = file.layout.find(SectionId::Collections)->offset, types = file.layout.find(SectionId::Types)->offset;
    for (const auto& [at, value] : std::vector<std::pair<std::size_t, u8>>{{pool+6,3},{pool+7,1},{pool+16,0},{types+5,4}}) {
        auto bad=bytes;bad.at(at)=value;CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    for (const auto at : {pool+4,lists+8}) {auto bad=bytes;put16(bad,at,65535);CHECK_THROWS_AS(loadFile(bad),FormatError);}
    for (const auto& [at,value] : std::vector<std::pair<std::size_t,u32>>{{pool,65537},{pool+8,0x7f800000},{pool+8,0x40000000},{pool+17,0}}) {
        auto bad=bytes;put32(bad,at,value);CHECK_THROWS_AS(loadFile(bad),FormatError);
    }
    for (const auto& fn : file.code.functions) for (std::size_t i=0;i<fn.code.size();i+=4) {
        const auto ins=decodeInstruction(fn.code.subspan(i,4));
        if (ins.opcode<Opcode::StepperValue || ins.opcode>Opcode::ConstStepper) continue;
        auto bad=bytes;bad.at(fn.fileOffset+i+1)=1;CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
        bad=bytes;put16(bad,fn.fileOffset+i+2,65535);CHECK_THROWS_AS(validate(loadFile(bad)),FormatError);
    }
    for (u16 minor=1;minor<7;++minor) {auto bad=bytes;put16(bad,6,minor);CHECK_THROWS_AS(loadFile(bad),FormatError);}
    CHECK_THROWS_AS(assemble("meta: size=1x1\ntypes: sampler<i32>\nfn draw ( ) -> ( ):\nreturn\nstepper.pick.list 0\nendfn\n"),std::exception);
    CHECK_THROWS_AS(assemble("meta: size=1x1\nfn draw ( ) -> ( ):\nreturn\nconst.stepper 0\nendfn\n"),std::exception);
}

TEST_CASE("stepper quantile failures trap like sampler draws before charging kernel iterations", "[stepper]") {
    // A far Gaussian tail iterates and then reports no mass. The quantile
    // must report the sampler's range error even when that iteration count
    // exceeds the remaining fuel.
    const auto tail = analytic(DistributionFamily::Gaussian, {0.f, 1.f, 0.f, 0.f});
    const auto draw = math::truncatedDraw(tail, 40.0, 41.0, 0.0);
    REQUIRE(draw.status == math::DrawStatus::NoMass);
    REQUIRE(draw.steps > 0);
    Harness harness(ValueType::F32);
    const auto handle = harness.capture(40.f, 41.f, tail);
    // Base batch fuel for two elements plus one per nonconstant step.
    harness.ex.fuel = 3;
    harness.ex.push(handle); harness.ex.push(i32{2});
    try {
        detail::stepSteppers(harness.ex, {Opcode::StepperPickList, 0, 0});
        FAIL("far tail quantile did not trap");
    } catch (const Trap& trap) {
        CHECK(trap.reason == ErrorReason::NumericRange);
        CHECK(std::string(trap.what()).find("random range has no probability under the distribution") != std::string::npos);
    }
}

TEST_CASE("description captures reject mistyped endpoints as stack type errors", "[stepper]") {
    // Unvalidated bytecode may push an f32 bound for an i32 description. That
    // is a stack type error, as for any other typed pop, not a range error.
    for (const bool stepper : {false, true}) for (const bool upper : {false, true}) {
        INFO((stepper ? "stepper" : "sampler") << (upper ? " upper" : " lower"));
        LoadedFile file;
        file.collections.types = {stepper ? ValueType::stepper(ValueType::I32) : ValueType::sampler(ValueType::I32)};
        file.code.functions.emplace_back();
        detail::Execution ex(file); ex.frames.emplace_back(); ex.fuel = 100;
        if (upper) { ex.push(i32{1}); ex.push(2.5f); } else { ex.push(2.5f); ex.push(i32{1}); }
        const auto step = stepper ? detail::stepSteppers : detail::stepSamplers;
        try {
            step(ex, {stepper ? Opcode::StepperRange : Opcode::SamplerRange, 0, 0});
            FAIL("mistyped bound accepted");
        } catch (const Trap& trap) {
            CHECK(trap.reason == ErrorReason::StackType);
            CHECK(std::string(trap.what()).find(stepper ? "stepper bound type mismatch" : "sampler bound type mismatch") != std::string::npos);
        }
        CHECK(ex.resources.bytes == 0); CHECK(ex.resources.descriptions.empty());
    }
}
