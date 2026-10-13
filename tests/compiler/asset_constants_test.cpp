#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "ips/AssetConstants.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "support/compiler_support.hpp"

#include <bit>

using namespace iprop;
using namespace compiler_test;

namespace {
std::vector<u8> compileConstants(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
void sameGeneration(const std::vector<u8>& actual, const std::vector<u8>& reference, u32 seed) {
    const auto a = execute(actual, seed), b = execute(reference, seed);
    REQUIRE(a.canvas.pixels == b.canvas.pixels);
    REQUIRE(a.outputs.size() == b.outputs.size());
    for (std::size_t i = 0; i < a.outputs.size(); ++i) {
        REQUIRE(a.outputs[i].points.size() == b.outputs[i].points.size());
        for (std::size_t j = 0; j < a.outputs[i].points.size(); ++j) {
            CHECK(std::bit_cast<u32>(a.outputs[i].points[j].x) == std::bit_cast<u32>(b.outputs[i].points[j].x));
            CHECK(std::bit_cast<u32>(a.outputs[i].points[j].y) == std::bit_cast<u32>(b.outputs[i].points[j].y));
        }
    }
}
}

TEST_CASE("asset constants resolve forward dependencies and all five declared types") {
    const std::string source = R"(asset t {
      size 1x1 output point checks output point normalized output point zero
      const i32 exact = min(later, 16777218)
      const i32 later = 16777217
      const f32 widened = 3
      const vec2 direction = vec2.normalize({widened, 4})
      const vec2 positiveZero = vec2.normalize({-0.0, -0.0})
      const color iron = rgba(146,156,168,255)
      const color darker = color.shade(iron, -7)
      const distribution typical = gaussian(sigma=spread,mean=0.5)
      const f32 spread = 0.18
      const distribution duplicate = typical
      exactValue()->i32 { return exact }
      directionValue()->vec2 { return direction }
      tint()->color { return darker }
      shape()->distribution { return duplicate }
      draw {
        checks={exactValue()==16777217,iron.r==146 && tint().r==139}
        normalized=directionValue() zero=positiveZero
        d=shape() value=0.0 to 1.0 with d
        pixel(0,0,tint())
      }
    })";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileConstants(source, optimize);
        CHECK(loadFile(bytes).layout.header.versionMinor == 5);
        const auto result = execute(bytes);
        CHECK(result.outputs[0].points[0].x == 1.f);
        CHECK(result.outputs[0].points[0].y == 1.f);
        CHECK(std::bit_cast<u32>(result.outputs[1].points[0].x) == 0x3f19999a);
        CHECK(std::bit_cast<u32>(result.outputs[1].points[0].y) == 0x3f4ccccd);
        CHECK(std::bit_cast<u32>(result.outputs[2].points[0].x) == 0);
        CHECK(std::bit_cast<u32>(result.outputs[2].points[0].y) == 0);
        CHECK(result.canvas.at(0,0) == Color{139,149,161,255});
        CHECK(disassemble(bytes).find("vec2.normalize") == std::string::npos);
        CHECK(disassemble(bytes).find("i32.min") == std::string::npos);
    }
}

TEST_CASE("constant builtins agree with runtime including named arguments and distribution draws") {
    std::size_t checked = 0;
    for (const auto& [name, spec] : ips::builtinSpecs()) {
        for (const auto& overload : spec.overloads) {
            if (!ips::constantBuiltin(spec, overload)) continue;
            std::string call = name + "(";
            // Reverse textual named arguments to exercise canonical binding.
            for (std::size_t j = overload.params.size(); j > 0; --j) {
                const auto& param = overload.params[j - 1];
                if (j != overload.params.size()) call += ",";
                call += param.name + "=";
                if (param.type.isList() && param.type.item().isSampler()) {
                    const auto item = param.type.item().item();
                    call += "[sampler(" + std::string(item == ips::ValueType::Color ? "#915f34af" :
                        item == ips::ValueType::Vec2 ? "{0.375,0.625}" : item == ips::ValueType::F32 ? "0.375" : "3") + ")]";
                } else if (param.type.isList()) call += param.type.element == ips::ValueType::Color ? "[#915f34af]" :
                    param.type.element == ips::ValueType::Vec2 ? "[{0.375,0.625}]" :
                    param.type.element == ips::ValueType::Distribution ? "[gaussian(0.5,0.18)]" :
                    param.type.element == ips::ValueType::F32 ? "[0.375]" : "[3]";
                else if (param.type == ips::ValueType::Color) call += "#915f34af";
                else if (param.type == ips::ValueType::Vec2) call += "{0.375,0.625}";
                else if (param.type == ips::ValueType::I32) call += param.name == "seed" ? "0x31ac975b" : "3";
                else if (param.name == "min") call += "0.0";
                else if (param.name == "max") call += "1.0";
                else call += "0.375";
            }
            call += ")";
            std::string observation;
            switch (overload.result) {
            case ips::ValueType::I32: observation = "p={value&65535,value>>16}"; break;
            case ips::ValueType::F32: observation = "p={value,0}"; break;
            case ips::ValueType::Vec2: observation = "p=value"; break;
            case ips::ValueType::Color: observation = "pixel(0,0,value) p={value.r,value.a}"; break;
            case ips::ValueType::Distribution: observation = "p={0.0 to 1.0 with value, 0 to 1 with value}"; break;
            default: FAIL("unexpected constant result type");
            }
            const auto wrap = [&](const std::string& declaration, const std::string& body) {
                return "asset t {size 1x1 output point p output point state " + declaration +
                    " draw{" + body + observation + " state={seed&65535,seed>>16}}}";
            };
            INFO(call);
            const auto reference = compileConstants(wrap("", "value=" + call + " "), false);
            for (const bool optimize : {false, true}) {
                const auto actual = compileConstants(wrap("const " + std::string(ips::typeName(overload.result)) +
                    " value=" + call, ""), optimize);
                for (const u32 seed : {0u,1u,26u,0xffffffffu}) sameGeneration(actual, reference, seed);
            }
            ++checked;
        }
    }
    CHECK(checked >= 60);
}

TEST_CASE("const arithmetic and short circuit preserve exact bits and runtime traps") {
    const std::vector<std::pair<std::string, std::string>> expressions{
        {"i32", "2147483647+1"}, {"i32", "-2147483648%-1"}, {"i32", "-27/4"},
        {"i32", "(-1 >> 35) ^ ~7"}, {"i32", "!7 || 3 && 2"},
        {"i32", "false && (1/0==0)"}, {"i32", "true || (1/0==0)"},
        {"i32", "round(-1.5)"}, {"i32", "round(0.4999999701976776)"},
        {"i32", "round(1.4999998807907104)"}, {"i32", "round(-1.4999998807907104)"},
        {"f32", "(3+4.5)/2"}, {"f32", "-0.0"},
        {"vec2", "2*{3,4}-{1,2}"}, {"vec2", "-({3,4}*{2,3})/{2,2}"},
        {"vec2", "vec2.normalize({1.1754943508222875e-38*0.00000011920928955078125,"
                 "1.1754943508222875e-38*0.0000002384185791015625})"},
        {"color", "rgba(-1,260,75,512)"}, {"color", "rgba(255,0,128,-7)"},
    };
    for (const auto& [type, expression] : expressions) {
        INFO(expression);
        const std::string observation = type == "i32" ? "p={value&65535,value>>16}" :
            type == "f32" ? "p={value,0}" : type == "color" ? "pixel(0,0,value) p={value.r,value.a}" : "p=value";
        const auto reference = compileConstants("asset t{size 1x1 output point p draw{value=" + expression + " " + observation + "}}", false);
        for (const bool optimize : {false, true})
            sameGeneration(compileConstants("asset t{size 1x1 output point p const " + type + " value=" +
                expression + " draw{" + observation + "}}", optimize), reference, 7);
    }
}

TEST_CASE("constant effects cycles and invalid constructors fail without compiled bytes") {
    struct Case { const char* declarations; ips::DiagnosticCode code; const char* message; };
    const Case cases[] = {
        {"const i32 x=seed", ips::DiagnosticCode::AssetConstantEffect, "cannot read 'seed'"},
        {"input i32 n=1 const i32 x=n", ips::DiagnosticCode::AssetConstantEffect, "cannot read 'n'"},
        {"const i32 x=0 && index(1)", ips::DiagnosticCode::AssetConstantEffect, "RNG or generation effects"},
        {"const f32 x=unit()", ips::DiagnosticCode::AssetConstantEffect, "RNG or generation effects"},
        {"const i32 x=1 to 1", ips::DiagnosticCode::AssetConstantEffect, "random ranges"},
        {"const i32 x=choose(7,1,2)", ips::DiagnosticCode::AssetConstantEffect, "RNG or generation effects"},
        {"const f32 x=roll(1,0,1)", ips::DiagnosticCode::AssetConstantEffect, "RNG or generation effects"},
        {"const i32 x=helper() helper()->i32{return 1}", ips::DiagnosticCode::AssetConstantEffect, "user helper"},
        {"const distribution x=gaussian(0,1) gaussian(first:f32,second:f32)->distribution{return beta(1,1)}",
            ips::DiagnosticCode::AssetConstantEffect, "user helper"},
        {"const i32 x=y const i32 y=x", ips::DiagnosticCode::AssetConstantCycle, "x -> y -> x"},
        {"const i32 x=x", ips::DiagnosticCode::AssetConstantCycle, "x -> x"},
        {"const i32 x=0 && y const i32 y=x", ips::DiagnosticCode::AssetConstantCycle, "cyclic"},
        {"const distribution bad=gaussian(0,0)", ips::DiagnosticCode::AssetConstantValue, "sigma > 0"},
        {"const distribution bad=gaussian(1.0/0.0,1)", ips::DiagnosticCode::AssetConstantValue, "finite mean"},
        {"const distribution bad=gaussian(0,0.0/0.0)", ips::DiagnosticCode::AssetConstantValue, "finite sigma"},
        {"const distribution bad=triangular(1,0,2)", ips::DiagnosticCode::AssetConstantValue, "min <= peak <= max"},
        {"const distribution bad=triangular(1,1,1)", ips::DiagnosticCode::AssetConstantValue, "min < max"},
        {"const distribution bad=beta(0,1)", ips::DiagnosticCode::AssetConstantValue, "alpha > 0"},
        {"const distribution bad=beta(1,1,2,1)", ips::DiagnosticCode::AssetConstantValue, "min < max"},
        {"const vec2 bad=vec2.normalize({1.0/0.0,1})", ips::DiagnosticCode::AssetConstantValue, "components must be finite"},
        {"const i32 bad=1/0", ips::DiagnosticCode::AssetConstantValue, "divide by zero"},
        {"const i32 bad=-2147483648/-1", ips::DiagnosticCode::AssetConstantValue, "integer overflow"},
        {"const f32 bad=noise.grad(1.0/0.0,0,1)", ips::DiagnosticCode::AssetConstantValue, "coordinate out of range"},
        {"const i32 bad=1.0", ips::DiagnosticCode::TypeError, "needs a i32, got f32"},
        {"const color bad={1,2}", ips::DiagnosticCode::TypeError, "needs a color, got vec2"},
        {"const i32 x=1 const i32 x=2", ips::DiagnosticCode::TypeError, "duplicate constant"},
        {"const i32 seed=1", ips::DiagnosticCode::TypeError, "reserved name"},
        {"input i32 x=1 const i32 x=2", ips::DiagnosticCode::TypeError, "collides"},
        {"output point x const i32 x=2", ips::DiagnosticCode::TypeError, "collides"},
        {"palette x=[1,2] const i32 x=2", ips::DiagnosticCode::TypeError, "collides"},
        {"x(){} const i32 x=2", ips::DiagnosticCode::TypeError, "collides"},
        {"const i32 x=1 helper(x:i32){}", ips::DiagnosticCode::TypeError, "cannot shadow"},
    };
    for (const auto& test : cases) {
        INFO(test.declarations);
        const std::string source = "asset t {size 1x1 " + std::string(test.declarations) + " draw{}}";
        for (const bool optimize : {false, true}) {
            SourceCompilationOptions options; options.optimize = optimize;
            const auto result = compileSource("invalid.ips", [&](std::string_view) { return source; }, options);
            CHECK_FALSE(result.succeeded()); CHECK(result.bytes.empty());
            REQUIRE(result.diagnostics.size() == 1);
            CHECK(result.diagnostics[0].code == test.code);
            CHECK(result.diagnostics[0].message.find(test.message) != std::string::npos);
            CHECK(result.diagnostics[0].range.has_value());
            CHECK(result.diagnostics[0].path == "invalid.ips");
        }
    }
    for (const auto body : {"x=2", "for x in 0..2{}"})
        CHECK_THROWS_AS(compileConstants("asset t{size 1x1 const i32 x=1 draw{" + std::string(body) + "}}", false), ips::SourceError);
}

TEST_CASE("each part owns its constants") {
    const std::map<std::string,std::string> files{
        {"root.ips", "import child from \"child.ips\" asset t{size 1x1 const i32 n=3 output point p draw{child.draw(seed,0,0,at={0,0}) p={n,child.outputs.p.x}}}"},
        {"child.ips", "asset c{size 1x1 const i32 n=7 output point p get()->i32{return n} draw{p={get(),0}}}"},
    };
    for (const bool optimize : {false, true}) {
        SourceCompilationOptions options; options.optimize = optimize;
        const auto compiled = compileSource("root.ips", [&](std::string_view path) { return files.at(std::string(path)); }, options);
        REQUIRE(compiled.succeeded());
        const auto result = execute(compiled.bytes);
        CHECK(result.outputs[0].points[0].x == 3.f); CHECK(result.outputs[0].points[0].y == 7.f);
    }
}

TEST_CASE("const palette reads retain legacy fallback and immutable declarations are stack bounded") {
    const auto program = ips::parse(R"(asset t{size 1x1 palette tones=[#123,#456,#789]
      const color last=tones[-1] const color blend=ramp(t=0.5,palette=tones)
      const i32 channels=last.r+blend.r draw{pixel(0,0,blend)}})");
    const auto constants = ips::resolveAssetConstants(program);
    CHECK(std::get<Color>(constants.at("last")) == Color{119,136,153,255});
    CHECK(std::get<Color>(constants.at("blend")) == Color{68,85,102,255});
    std::string chain = "asset t{size 1x1 output point p ";
    for (std::size_t i = 0; i < 1500; ++i) chain += "const i32 n" + std::to_string(i) + "=" +
        (i == 1499 ? "1" : "n" + std::to_string(i + 1) + "+1") + " ";
    chain += "draw{p={n0,0}}}";
    CHECK(execute(compileConstants(chain, false)).outputs[0].points[0].x == 1500.f);
}

TEST_CASE("const formatter comments assembly debug stripping and oldest formats roundtrip") {
    const std::string source = R"(asset t {
      size 1x1 output point p
      // A forward reference stays a reference after formatting.
      const vec2 direction=vec2.normalize({n,4}); // direction
      const i32 n=3
      draw{p=direction}
    })";
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(formatted.find("// direction") != std::string::npos);
    CHECK(formatted.find("forward reference") != std::string::npos);
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    for (const bool optimize : {false, true}) {
        const auto bytes = compileConstants(source, optimize);
        CHECK(loadFile(bytes).layout.header.versionMinor == 4);
        CHECK(stripDebug(bytes) == stripDebug(compileConstants(formatted, optimize)));
        const auto restored = assemble(disassemble(bytes));
        CHECK(disassemble(restored) == disassemble(bytes));
        const auto packed = compressFile(bytes);
        CHECK(loadFile(packed).layout.header.versionMinor == 4);
        sameGeneration(packed, bytes, 42); sameGeneration(restored, bytes, 42);
    }
}
