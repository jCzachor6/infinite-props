#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
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
std::vector<u8> compileMath(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}
}

TEST_CASE("integer bounds select exact overloads in helpers and named calls") {
    const std::string source = R"(asset t {
      size 1x1 output point checks output point mixed
      low(x:i32,y:i32)->i32 { return min(y=y,x=x) }
      high(x:i32,y:i32)->i32 { return max(x,y) }
      bound(x:i32,lo:i32,hi:i32)->i32 { return clamp(max=hi,x=x,min=lo) }
      draw {
        checks = {
          (low(16777217,16777218)==16777217) &&
          (high(2147483646,2147483647)==2147483647) &&
          (low(-2147483647,-2147483648)==-2147483648) &&
          (bound(16777217,16777218,16777216)==16777217),
          (bound(2147483647,-2147483648,2147483647)==2147483647) &&
          (bound(-2147483648,2147483647,-2147483648)==-2147483648) &&
          (bound(-2147483648,2147483647,2147483647)==2147483647)
        }
        x=1.0 x=min(1,2.0) x=max(1.0,2) x=clamp(x=3,min=0,max=2.0)
        y=1.0 y=clamp(3.0,0,2) y=clamp(3,0.0,2)
        mixed={x,y}
      }
    })";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileMath(source, optimize);
        const auto result = execute(bytes);
        CHECK(result.outputs[0].points[0].x == 1.f);
        CHECK(result.outputs[0].points[0].y == 1.f);
        CHECK(result.outputs[1].points[0].x == 2.f);
        CHECK(result.outputs[1].points[0].y == 2.f);
        const auto text = disassemble(bytes);
        for (const auto name : {"i32.min", "i32.max", "i32.clamp"})
            CHECK(text.find(name) != std::string::npos);
    }
}

TEST_CASE("integer overload migration reports exact local types without an artifact") {
    for (const auto expression : {"min(1,2)", "max(1,2)", "clamp(1,0,2)"}) {
        const std::string source = "asset t { size 1x1 draw { x=1.0 x=" + std::string(expression) + " } }";
        const auto result = compileSource("migration.ips", [&](std::string_view) { return source; });
        CHECK_FALSE(result.succeeded());
        CHECK(result.bytes.empty());
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].expectedType == "f32");
        CHECK(result.diagnostics[0].actualType == "i32");
        CHECK(result.diagnostics[0].message == "local 'x' is f32, cannot assign i32");
        const auto formatted = ips::formatProgram(ips::parse(source));
        CHECK(formatted.find("float(") == std::string::npos);
        const std::string repaired = "asset t { size 1x1 draw { x=1.0 x=float(" +
            std::string(expression) + ") } }";
        CHECK_NOTHROW(compileMath(repaired, true));
    }
}

TEST_CASE("normalize folds finite values and retains unused nonfinite traps") {
    const std::string source = R"(asset t { size 1x1 output point p output point z
      draw { p=vec2.normalize({3,4}) z=vec2.normalize({-0.0,-0.0}) }
    })";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileMath(source, optimize);
        const auto result = execute(bytes);
        CHECK(std::bit_cast<u32>(result.outputs[0].points[0].x) == 0x3f19999a);
        CHECK(std::bit_cast<u32>(result.outputs[0].points[0].y) == 0x3f4ccccd);
        CHECK(std::bit_cast<u32>(result.outputs[1].points[0].x) == 0);
        CHECK(std::bit_cast<u32>(result.outputs[1].points[0].y) == 0);
        for (const auto value : {"{1.0/0.0,1}", "{1,0.0/0.0}", "{1,-1.0/0.0}"}) {
            const auto invalid = compileMath("asset t { size 1x1 draw { vec2.normalize(" +
                std::string(value) + ") } }", optimize);
            try {
                (void)execute(invalid);
                FAIL("missing normalize trap");
            } catch (const FormatError& error) {
                CHECK(error.reason == ErrorReason::NumericRange);
                CHECK(std::string(error.what()).find("vec2.normalize components must be finite") != std::string::npos);
            }
        }
    }
}

TEST_CASE("folded integer bounds retain the oldest written format") {
    for (const std::string expression : {"min(16777217,16777218)", "max(-2147483648,2147483647)",
                                        "clamp(16777217,16777218,16777216)"}) {
        const std::string source = "asset t { size 1x1 output point p draw { p={" + expression + ",0} } }";
    }
}

TEST_CASE("normalization constant folding retains extreme and subnormal encodings") {
    struct Case { const char* vector; u32 x, y; };
    // Produce subnormals by exact arithmetic on normal literals; this also
    // tests the optimizer's transport of the intermediate and result bits.
    const Case cases[] = {
        {"{3.4028234663852886e38,3.4028234663852886e38}", 0x3f3504f3, 0x3f3504f3},
        {"{-3.4028234663852886e38,3.4028234663852886e38}", 0xbf3504f3, 0x3f3504f3},
        {"{1.1754943508222875e-38*0.00000011920928955078125,"
         "1.1754943508222875e-38*0.0000002384185791015625}", 0x3ee4f92e, 0x3f64f92e},
        {"{1.0,1.1754943508222875e-38*0.00000011920928955078125}", 0x3f800000, 1},
        {"{1.1754943508222875e-38*0.00000011920928955078125,-0.0}", 0x3f800000, 0x80000000},
    };
    for (const auto& test : cases) {
        const std::string source = "asset t {size 1x1 output point p draw{p=vec2.normalize(" +
            std::string(test.vector) + ")}}";
        for (const bool optimize : {false, true}) {
            const auto bytes = compileMath(source, optimize);
            const auto result = execute(bytes);
            CHECK(std::bit_cast<u32>(result.outputs[0].points[0].x) == test.x);
            CHECK(std::bit_cast<u32>(result.outputs[0].points[0].y) == test.y);
        }
    }
}

TEST_CASE("math overloads and normalization preserve argument RNG order and stream") {
    const auto wrap = [](const std::string& body) {
        return "asset t { size 1x1 output point p output point state "
            "draw { " + body + " state={seed&65535,seed>>16} } }";
    };
    const std::string calls = "n=min(y=int(unit()*1000),x=int(unit()*1000)) "
        "m=max(y=int(unit()*1000),x=int(unit()*1000)) "
        "c=clamp(max=int(unit()*1000),x=int(unit()*1000),min=int(unit()*1000)) "
        "q=vec2.normalize(v={unit(),unit()}) p={n+m+c+q.x,q.y}";
    const std::string sequential = "nx=int(unit()*1000) ny=int(unit()*1000) n=min(nx,ny) "
        "mx=int(unit()*1000) my=int(unit()*1000) m=max(mx,my) "
        "cx=int(unit()*1000) ca=int(unit()*1000) cb=int(unit()*1000) c=clamp(cx,ca,cb) "
        "qx=unit() qy=unit() q=vec2.normalize({qx,qy}) p={n+m+c+q.x,q.y}";
    const std::string pure = "before=seed n=min(before,7) m=max(before,3) c=clamp(before,8,1) "
        "q=vec2.normalize({float(n),float(m)}) "
        "if(false && vec2.normalize({1.0/0.0,0}).x>0.0) { pixel(0,0,#fff) } p={seed==before,c==clamp(before,1,8)}";
    for (const bool optimize : {false, true}) {
        const auto actual = compileMath(wrap(calls), optimize);
        const auto reference = compileMath(wrap(sequential), false);
        for (const u32 seed : {0u, 1u, 42u, 0xffffffffu}) {
            const auto a = execute(actual, seed), b = execute(reference, seed);
            for (std::size_t i = 0; i < a.outputs.size(); ++i) {
                CHECK(std::bit_cast<u32>(a.outputs[i].points[0].x) == std::bit_cast<u32>(b.outputs[i].points[0].x));
                CHECK(std::bit_cast<u32>(a.outputs[i].points[0].y) == std::bit_cast<u32>(b.outputs[i].points[0].y));
            }
            const auto p = execute(compileMath(wrap(pure), optimize), seed);
            CHECK(p.outputs[0].points[0].x == 1.f);
            CHECK(p.outputs[0].points[0].y == 1.f);
        }
    }
}

TEST_CASE("new math instructions roundtrip through formatter assembler and compression") {
    const std::string source = R"(asset t {size 1x1 input i32 n=16777217 output point p
      draw{p=vec2.normalize(v={float(clamp(max=2147483647,min=-2147483648,x=n)),
          float(max(y=n,x=min(y=16777218,x=n)))});}})";
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    for (const bool optimize : {false, true}) {
        const auto bytes = stripDebug(compileMath(source, optimize));
        CHECK(bytes == stripDebug(compileMath(formatted, optimize)));
        const auto text = disassemble(bytes);
        const auto restored = assemble(text);
        CHECK(disassemble(restored) == text);
        const auto packed = compressFile(bytes);
        CHECK(decompressFile(stripDebug(packed)) == bytes);
        for (const auto& candidate : {restored, packed}) {
            const auto result = execute(candidate), original = execute(bytes);
            CHECK(std::bit_cast<u32>(result.outputs[0].points[0].x) == std::bit_cast<u32>(original.outputs[0].points[0].x));
            CHECK(std::bit_cast<u32>(result.outputs[0].points[0].y) == std::bit_cast<u32>(original.outputs[0].points[0].y));
        }
    }
}
