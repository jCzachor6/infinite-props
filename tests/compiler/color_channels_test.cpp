#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Instruction.hpp"
#include "iprop/VM.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"

#include "support/compiler_support.hpp"

using namespace iprop;
using namespace compiler_test;

namespace {
// Odd multipliers permute 0-255, so every channel takes all 256 values.
Color channelSample(u32 i) {
    return {static_cast<u8>(i), static_cast<u8>(255 - i), static_cast<u8>(i * 7),
            static_cast<u8>(i * 13 + 5)};
}

std::string colorText(Color c) {
    char text[10];
    std::snprintf(text, sizeof text, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return text;
}

std::vector<u8> compileSource(const std::string& source, bool optimize) {
    const auto program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, optimize);
}

// Exact output bits of every point, joined across output slots.
std::vector<u32> pointBits(const std::vector<u8>& bytes, u32 seed) {
    std::vector<u32> bits;
    for (const auto& output : execute(bytes, seed).outputs)
        for (const auto& point : output.points) {
            bits.push_back(std::bit_cast<u32>(point.x));
            bits.push_back(std::bit_cast<u32>(point.y));
        }
    return bits;
}

bool usesChannelOpcode(const std::vector<u8>& bytes) {
    for (const auto& function : loadFile(bytes).code.functions)
        for (std::size_t i = 0; i < function.code.size() / 4; ++i) {
            const auto op = decodeInstruction(function.code.subspan(i * 4, 4)).opcode;
            if (op == Opcode::ColorR || op == Opcode::ColorG || op == Opcode::ColorB || op == Opcode::ColorA)
                return true;
        }
    return false;
}

std::string checkError(const std::string& source) {
    try {
        ips::checkAsset(ips::parse(source), {});
    } catch (const ips::SourceError& error) {
        return error.message;
    }
    FAIL("expected a source error");
    return {};
}
} // namespace

TEST_CASE("color.r/g/b/a read every channel byte as a zero-extended i32") {
    std::string ipa = "meta: size=1x1 origin=0,0 fuel=1000000\noutput: list point rg\noutput: list point ba\nfn draw ( ) -> ( ):\n";
    for (u32 i = 0; i < 256; ++i) {
        const auto color = "const.color " + colorText(channelSample(i)) + "\n";
        for (const char* channel : {"r", "g", "b", "a"}) {
            ipa += color + "color." + channel + "\ni32.to.f32\n";
            if (channel[0] == 'g') ipa += "out.list.point rg\n";
            if (channel[0] == 'a') ipa += "out.list.point ba\n";
        }
    }
    ipa += "endfn\n";
    const auto bytes = assemble(ipa);
    CHECK(bytes[6] == 0);
    CHECK(assemble(disassemble(bytes)) == bytes);
    const auto result = run(bytes, 0, 0);
    REQUIRE(result.outputs.size() == 2);
    REQUIRE(result.outputs[0].points.size() == 256);
    REQUIRE(result.outputs[1].points.size() == 256);
    for (u32 i = 0; i < 256; ++i) {
        INFO(i);
        const Color c = channelSample(i);
        CHECK(result.outputs[0].points[i].x == static_cast<f32>(c.r));
        CHECK(result.outputs[0].points[i].y == static_cast<f32>(c.g));
        CHECK(result.outputs[1].points[i].x == static_cast<f32>(c.b));
        CHECK(result.outputs[1].points[i].y == static_cast<f32>(c.a));
    }
}

TEST_CASE("source channel reads return every byte of runtime colors as i32") {
    const std::string source = R"(asset t {
  size 1x1
  output list point rg
  output list point ba
  draw {
    for i in 0..256 {
      c = rgba(i, 255 - i, (i * 7) & 255, (i * 13 + 5) & 255)
      red = c.r
      rg = {red, c.g}
      ba = {c.b, c.a}
    }
  }
})";
    for (const bool optimize : {false, true}) {
        const auto bytes = compileSource(source, optimize);
        CHECK(bytes[6] == 0);
        CHECK(usesChannelOpcode(bytes));
        const auto result = execute(bytes);
        REQUIRE(result.outputs.size() == 2);
        REQUIRE(result.outputs[0].points.size() == 256);
        for (u32 i = 0; i < 256; ++i) {
            INFO(i);
            const Color c = channelSample(i);
            CHECK(result.outputs[0].points[i].x == static_cast<f32>(c.r));
            CHECK(result.outputs[0].points[i].y == static_cast<f32>(c.g));
            CHECK(result.outputs[1].points[i].x == static_cast<f32>(c.b));
            CHECK(result.outputs[1].points[i].y == static_cast<f32>(c.a));
        }
    }
}

TEST_CASE("channel reads accept every color base and fold constant literals") {
    const std::string source = R"(asset t { input i32 v = 0
  size 1x1
  input color tint = #10203040
  palette metals = [#705845, #c8c8c8c0]
  output list point p
  make(level: i32) -> color {
    return rgba(level, level + 1, level + 2, 0)
  }
  draw {
    p = {#abc.r, #abcd.a}
    p = {#aabbcc.a, #00000000.a}
    p = {(#9c785aff).g, #9c785a80.a}
    local = #123456
    p = {local.r, tint.a}
    p = {metals[1].a, metals[v].b}
    p = {make(7).g, make(7).a}
    p = {color.mix(#000000, #ffffff, 0.5).r, rgba(300, -4, 9, 128).g}
    p = {color.set.a(local, 77).a, float(c(#fedcba).b) / 255.0}
  }
  c(value: color) -> color {
    return value
  }
})";
    const std::vector<f32> expected = {0xaa, 0xdd, 255, 0, 0x78, 0x80, 0x12, 0x40, 0xc0, 0x45,
                                       8, 0, 127, 0, 77, static_cast<f32>(0xba) / 255.0f};
    for (const bool optimize : {false, true}) {
        INFO(optimize);
        const auto result = execute(compileSource(source, optimize));
        REQUIRE(result.outputs[0].points.size() * 2 == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            INFO(i);
            const auto& point = result.outputs[0].points[i / 2];
            CHECK(std::bit_cast<u32>(i % 2 ? point.y : point.x) == std::bit_cast<u32>(expected[i]));
        }
    }
    // A literal's channel folds in the source front end, even without optimization.
    CHECK_FALSE(usesChannelOpcode(compileSource(
        "asset t { size 1x1 output point p draw { p = {#9c785a.r, (#9c785a40).a} } }", false)));
    const std::string local = "asset t { size 1x1 output point p draw { c = #9c785a p = {c.r, c.g} } }";
    CHECK(usesChannelOpcode(compileSource(local, false)));
    CHECK_FALSE(usesChannelOpcode(compileSource(local, true)));
    CHECK(pointBits(compileSource(local, true), 0) == pointBits(compileSource(local, false), 0));
}

TEST_CASE("a channel read evaluates its base once and each read is its own call") {
    const auto program = [](const std::string& body) {
        return "asset t { size 1x1 output list point p\n"
               "  make() -> color { pixel(0, 0, #fff) return rgba(index(256), index(256), index(256), index(256)) }\n"
               "  draw { " + body + " p = {index(1000), unit()} } }";
    };
    const std::vector<std::pair<std::string, std::string>> equivalents = {
        {"r = make().r p = {r, 0}", "c = make() r = c.r p = {r, 0}"},
        {"r = make().r g = make().g p = {r, g}",
         "c1 = make() r = c1.r c2 = make() g = c2.g p = {r, g}"},
        {"x = (make().a + 1) * 2 p = {x, 0}", "c = make() x = (c.a + 1) * 2 p = {x, 0}"},
    };
    for (const auto& [direct, stored] : equivalents) {
        INFO(direct);
        for (const bool optimize : {false, true})
            for (const u32 seed : {0u, 7u, 26u, 0xffffffffu})
                CHECK(pointBits(compileSource(program(direct), optimize), seed) ==
                      pointBits(compileSource(program(stored), false), seed));
    }
    // Two reads of one stored color share a single draw from the stream.
    const auto shared = compileSource(program("c = make() p = {c.r, c.r}"), true);
    for (const u32 seed : {3u, 99u}) {
        const auto bits = pointBits(shared, seed);
        CHECK(bits[0] == bits[1]);
    }
}

TEST_CASE("channel diagnostics name the expected type and the available channels") {
    const auto wrap = [](const std::string& body) {
        return "asset t { size 8x8 draw { " + body + " } }";
    };
    CHECK(checkError(wrap("c = #336699 r = c.red")) ==
          "color has only .r, .g, .b and .a channels, got '.red'");
    CHECK(checkError(wrap("c = #336699 r = c.x")) ==
          "color has only .r, .g, .b and .a channels, got '.x'");
    CHECK(checkError(wrap("n = 3 r = n.r")) == "'.r' reads a color channel and needs a color, got i32");
    CHECK(checkError(wrap("f = 0.5 x = f.x")) == "'.x' reads a vec2 component and needs a vec2, got f32");
    CHECK(checkError(wrap("p = {1.0, 2.0} r = p.r")) == "vec2 has only .x and .y components, got 'r'");
    CHECK(checkError(wrap("n = 3 m = n.w")) == "i32 has no components, got '.w'");
    CHECK(checkError(wrap("c = #336699 r = c.r.g")) ==
          "'.g' reads a color channel and needs a color, got i32");
    try {
        ips::checkAsset(ips::parse(wrap("n = 3 r = n.a")), {});
        FAIL("expected a type error");
    } catch (const ips::SourceError& error) {
        CHECK(error.code == ips::DiagnosticCode::TypeError);
        CHECK(error.expectedType == "color");
        CHECK(error.actualType == "i32");
        CHECK(error.location.line == 1);
        CHECK(error.location.column == 39); // the channel name after the dot
    }
    try {
        (void)ips::parse(wrap("c = #336699 c.r = 100"));
        FAIL("expected a parse error");
    } catch (const ips::SourceError& error) {
        CHECK(std::string(error.message).starts_with("cannot assign to '.r': members are read-only"));
        CHECK(error.location.column == 41);
    }
}

TEST_CASE("channel reads format canonically and survive the assembler roundtrip") {
    const std::string source = R"(asset t { size 4x4 palette p=[#102030,#405060]
      output point q
      draw { c=color.mix(p[0],(#9c785a),0.5) q={(c).r+p[1].g,color.shade(c,3).a} pixel(0,0,rgba(c.r+5,c.g-3,c.b,c.a)) } })";
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(formatted.find("q = {(c.r + p[1].g), color.shade(c, 3).a}") != std::string::npos);
    CHECK(formatted.find("pixel(0, 0, rgba((c.r + 5), (c.g - 3), c.b, c.a))") != std::string::npos);
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    const auto original = compileSource(source, false), reformatted = compileSource(formatted, false);
    CHECK(stripDebug(original) == stripDebug(reformatted));
    for (const bool optimize : {false, true}) {
        const auto bytes = stripDebug(compileSource(source, optimize));
        const auto text = disassemble(bytes);
        const auto restored = assemble(text);
        CHECK(disassemble(restored) == text);
        CHECK(execute(restored).canvas.pixels == execute(bytes).canvas.pixels);
        CHECK(pointBits(restored, 0) == pointBits(compileSource(source, !optimize), 0));
    }
    CHECK(disassemble(compileSource(source, false)).find("color.r\n") != std::string::npos);
}

TEST_CASE("writers leave the reserved former version bytes zero") {
    const auto reserved = [](const std::vector<u8>& bytes) {
        return std::all_of(bytes.begin() + 4, bytes.begin() + 8, [](u8 byte) { return byte == 0; });
    };
    const std::string plain = "asset t { size 1x1 output point p draw { c = #9c785a pixel(0, 0, c) p = {1, 2} } }";
    const std::string channels = "asset t { size 1x1 output point p draw { c = #9c785a pixel(0, 0, c) p = {c.r, c.a} } }";
    for (const bool optimize : {false, true}) {
        CHECK(reserved(compileSource(plain, optimize)));
        CHECK(reserved(compileSource(channels, optimize)));
    }
    const std::string ipa = "meta: size=1x1 origin=0,0 fuel=100\nfn draw ( ) -> ( ):\n  const.color #9c785aff\n";
    CHECK(reserved(assemble(ipa + "  drop\nendfn\n")));
    CHECK(reserved(assemble(ipa + "  color.b\n  drop\nendfn\n")));
    // The compressed envelope and the stripped file keep them zero too.
    const auto packed = compressFile(compileSource(channels, false));
    REQUIRE(parseHeader(packed).hasCompression());
    CHECK(reserved(packed));
    CHECK(reserved(stripDebug(compileSource(channels, false))));
    const auto loaded = loadFile(packed);
    CHECK(reserved(std::vector<u8>(loaded.bytes().begin(), loaded.bytes().end())));
    CHECK(pointBits(packed, 0) == pointBits(compileSource(channels, true), 0));
}
