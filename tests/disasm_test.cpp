#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "ipdis.hpp"
#include "ipval.hpp"
#include "ipa.hpp"
#include "json.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

// The introspection tools: ipdis produces stable, reassemblable
// disassembly; ipval reports validity with exit codes and a
// machine-readable JSON report.

using namespace iprop;

namespace {

const std::string kFixture = R"(
asset t {
  size 8x8
  input color base = #3355aa
  output point p

  draw {
    x = 2 + 3 * 4
    w = x - 6
    rect(0, 0, w, w, base)
    p = {x, w}
    if (x == 14) {
      pixel(0, 0, color.shade(base, 10))
    }
    i = 0
    while (i < 2) {
      pixel(i, 1, #ffffff)
      i = i + 1
    }
  }
})";

const std::string kRock = R"(
asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect collider

  draw {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    rect(-w * 0.10, 0, w * 0.87, h, palette)
    if (index(3) == 0) {
      rect(-w * 0.67, 2, w * 0.39, h * 0.43, color.shade(palette, -4))
    }
  }
})";

std::vector<u8> compileChecked(const std::string& source) {
    const ips::Program program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {}, false); // pin the reference tool/debug layout
}

Canvas executeBytes(const std::vector<u8>& bytes) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{7, 0, 0, 0, 0, {}});
    vm.run();
    return vm.result().image;
}

} // namespace

TEST_CASE("the disassembly is stable golden output") {
    const std::string expected = R"(meta: size=8x8 origin=0,0 variants=1 archetypes=1 fuel=2000000
input: color base = #3355aaff
output: point p
fn draw ( i32, i32, color ) -> ( ):
  locals: i32 i32 i32
  const.i32 14
  local.set 3
  local.get 3
  const.i32 6
  i32.sub
  local.set 4
  const.i32 0
  i32.to.f32
  const.i32 0
  i32.to.f32
  local.get 4
  i32.to.f32
  local.get 4
  i32.to.f32
  local.get 2
  rect
  local.get 3
  i32.to.f32
  local.get 4
  i32.to.f32
  out.point p
  local.get 3
  const.i32 14
  i32.eq
  if
  const.i32 0
  const.i32 0
  local.get 2
  const.i32 10
  color.shade
  pixel
  end
  const.i32 0
  local.set 5
  block
  loop
  local.get 5
  const.i32 2
  i32.lt_s
  const.i32 0
  i32.eq
  br_if 1
  local.get 5
  const.i32 1
  const.color #ffffffff
  pixel
  local.get 5
  const.i32 1
  i32.add
  local.set 5
  br 0
  end
  end
endfn
)";
    REQUIRE(disassemble(compileChecked(kFixture)) == expected);
}

TEST_CASE("disassembled output reassembles to the same execution") {
    const std::vector<u8> original = compileChecked(kRock);
    const std::vector<u8> roundTrip = assemble(disassemble(original));

    const Canvas fromOriginal = executeBytes(original);
    const Canvas fromRoundTrip = executeBytes(roundTrip);
    REQUIRE(fromOriginal.width == fromRoundTrip.width);
    REQUIRE(fromOriginal.height == fromRoundTrip.height);
    for (std::size_t i = 0; i < fromOriginal.pixels.size(); ++i) {
        INFO("pixel " << i);
        REQUIRE(fromOriginal.pixels[i] == fromRoundTrip.pixels[i]);
    }
}

TEST_CASE("part tables disassemble and reassemble") {
    const std::string hair = R"(
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  output point top
  draw {
    rect(0, 0, 64, 32, color)
    top = {32, 4}
  }
})";
    const std::string portrait = R"(
import hair from "hair.ips"

asset portrait {
  size 64x64
  output point anchor

  draw {
    hair.draw(seed ^ 7, v, a, at = {32, 4})
    anchor = hair.outputs.top
  }
})";
    const ips::Program hairProgram = ips::parse(hair);
    const ips::Program portraitProgram = ips::parse(portrait);
    ips::checkAsset(hairProgram, {});
    ips::checkAsset(portraitProgram, {{"hair", partInfoOf(hairProgram.asset)}});

    const auto original = compileProgram(portraitProgram, {{"hair", &hairProgram}});
    const std::string text = disassemble(original);
    REQUIRE(text.find("part hair:") != std::string::npos);
    REQUIRE(text.find("part.draw hair") != std::string::npos);
    REQUIRE(text.find("part.get.point top") != std::string::npos);

    const auto roundTrip = assemble(text);
    const auto loaded = loadFile(roundTrip);
    CHECK_NOTHROW(validate(loaded));
    VM vm(loaded, GenerationInputs{3, 0, 0, 0, 0, {}});
    CHECK_NOTHROW(vm.run());
    const GenerationResult result = vm.result();
    const NamedOutput* anchor = result.find("anchor");
    REQUIRE(anchor != nullptr);
    REQUIRE(anchor->points.size() == 1);
    REQUIRE(anchor->points[0].x == 32.f);
    REQUIRE(anchor->points[0].y == 4.f);
}

TEST_CASE("ipval reports valid files as valid") {
    const ValidationReport report = validateReport(compileChecked(kFixture));
    REQUIRE(report.valid);
    REQUIRE(report.violations.empty());
}

TEST_CASE("ipval reports the first violation with its offset") {
    // A truncated header.
    const std::vector<u8> shortFile = {0x49, 0x50, 0x52};
    const ValidationReport shortReport = validateReport(shortFile);
    REQUIRE(!shortReport.valid);
    REQUIRE(shortReport.violations.size() == 1);
    REQUIRE(shortReport.violations[0].message.find("too small") !=
            std::string::npos);

    // A bad magic.
    std::vector<u8> badMagic = compileChecked(kFixture);
    badMagic[0] = 0x00;
    const ValidationReport magicReport = validateReport(badMagic);
    REQUIRE(!magicReport.valid);
    REQUIRE(magicReport.violations.size() == 1);
    REQUIRE(magicReport.violations[0].offset == 0);
    REQUIRE(magicReport.violations[0].message.find("magic") !=
            std::string::npos);
}

namespace {
// Builds a char* argv from string literals; the storage stays alive for the
// duration of the call.
struct Argv {
    std::vector<std::string> storage;
    std::vector<char*> pointers;
    explicit Argv(std::initializer_list<const char*> args) {
        for (const char* arg : args) {
            storage.emplace_back(arg);
        }
        for (auto& text : storage) {
            pointers.push_back(text.data());
        }
    }
    int ipval() { return ipvalMain(static_cast<int>(pointers.size()), pointers.data()); }
    int ipdis() { return ipdisMain(static_cast<int>(pointers.size()), pointers.data()); }
};

std::filesystem::path writeTemp(const std::string& name,
                                const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << contents;
    return path;
}
} // namespace

TEST_CASE("the CLI exit codes distinguish valid, invalid and usage errors") {
    const std::vector<u8> validBytes = compileChecked(kFixture);
    const auto validPath = std::filesystem::temp_directory_path() / "ipval-valid.iprop";
    {
        std::ofstream out(validPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(validBytes.data()),
                  static_cast<std::streamsize>(validBytes.size()));
    }
    const auto badPath = writeTemp("ipval-bad.iprop", "junk");

    REQUIRE(Argv{"ipval", validPath.string().c_str()}.ipval() == 0);
    REQUIRE(Argv{"ipval", badPath.string().c_str()}.ipval() == 1);
    REQUIRE(Argv{"ipval", "--json", badPath.string().c_str()}.ipval() == 1);
    REQUIRE(Argv{"ipval", "no-such-file.iprop"}.ipval() == 2);
    REQUIRE(Argv{"ipval"}.ipval() == 2); // no input

    REQUIRE(Argv{"ipdis", validPath.string().c_str()}.ipdis() == 0);
    REQUIRE(Argv{"ipdis", badPath.string().c_str()}.ipdis() == 1);
    REQUIRE(Argv{"ipdis"}.ipdis() == 2);

    std::filesystem::remove(validPath);
    std::filesystem::remove(badPath);
}

TEST_CASE("ipval --json writes one valid JSON report for any path bytes") {
    struct CaptureStdout {
        std::ostringstream text;
        std::streambuf* previous = std::cout.rdbuf(text.rdbuf());
        ~CaptureStdout() { std::cout.rdbuf(previous); }
    };
    const auto report = [](std::initializer_list<const char*> args, int expectedExit) {
        CaptureStdout capture;
        REQUIRE(Argv(args).ipval() == expectedExit);
        return capture.text.str();
    };
    const auto expected = [](const std::string& file, const char* valid, const char* violations) {
        return "{\n  \"file\": " + tools::Json(file).dump() + ",\n  \"valid\": " + valid +
            ",\n  \"violations\": " + violations + "\n}\n";
    };
    const std::vector<u8> validBytes = compileChecked(kFixture);
    const auto validPath = std::filesystem::temp_directory_path() / "ipval-json-valid.iprop";
    {
        std::ofstream out(validPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(validBytes.data()),
                  static_cast<std::streamsize>(validBytes.size()));
    }
    const auto badPath = writeTemp("ipval-json-bad.iprop", "junk");

    REQUIRE(report({"ipval", "--json", validPath.string().c_str()}, 0) ==
            expected(validPath.string(), "true", "[]"));
    REQUIRE(report({"ipval", "--json", badPath.string().c_str()}, 1) ==
            expected(badPath.string(), "false", "[\n    {\n      \"offset\": 4,\n"
                     "      \"message\": \"file too small: header truncated\"\n    }\n  ]"));

    // A control byte is escaped and each maximal ill-formed UTF-8 subpart
    // becomes U+FFFD; neither the file nor its name needs to exist.
    const char* const missing = "\"missing-\\u0001-\xEF\xBF\xBD-\xEF\xBF\xBD-"
        "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD-\xC5\xBC.iprop\"";
    REQUIRE(report({"ipval", "--json", "missing-\x01-\xff-\xe2\x82-\xed\xa0\x80-\xc5\xbc.iprop"}, 2) ==
            "{\n  \"file\": " + std::string(missing) + ",\n  \"valid\": false,\n  \"violations\": [\n"
            "    {\n      \"offset\": 0,\n      \"message\": \"cannot open file\"\n    }\n  ]\n}\n");

    std::filesystem::remove(validPath);
    std::filesystem::remove(badPath);
}
