#include <catch2/catch_test_macros.hpp>

#include "png.hpp"

#include "ipgen.hpp"
#include "ipsheet.hpp"
#include "support/compiler_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/test_support.hpp"

// The ipgen CLI: exit codes, PNG output, JSON, uniform overrides, error
// handling. Runs in-process via ipgenMain; stdout is captured.

using namespace iprop;
using namespace test_support;

namespace {

// A unique temp path, removed on destruction.
struct TempPath {
    std::filesystem::path path;

    explicit TempPath(const std::string& name) {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               (name + "-" + std::to_string(counter++) + ".iprop");
    }
    ~TempPath() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(pngPath(), ec);
    }
    std::filesystem::path pngPath() const {
        return std::filesystem::path(path.string().substr(0, path.string().rfind('.')) +
                                     "-0.png");
    }
};

struct CapturedStdout {
    std::ostringstream stream;
    std::streambuf* previous = std::cout.rdbuf(stream.rdbuf());
    std::ostringstream errStream;
    std::streambuf* errPrevious = std::cerr.rdbuf(errStream.rdbuf());
    ~CapturedStdout() {
        std::cout.rdbuf(previous);
        std::cerr.rdbuf(errPrevious);
    }
    std::string text() const { return stream.str(); }
    std::string errText() const { return errStream.str(); }
};

// A fixture file: 8x8, colour uniform "palette" (default #0A141E), a point
// output "anchor"; draw paints pixel(1,1) with the palette and writes
// out.point (1,2).
std::vector<u8> fixtureFile() {
    const auto meta = metaSection(8, 8, 0, 0, 1000, false, {}, 0, 0, 0,
                                  {{0, 1, {10, 20, 30, 255}, 0, 0}}, {{0, 0, 2}});
    std::vector<u8> body;
    for (const auto b : ins(0x10, 0, 1)) body.push_back(b); // const.i32 1 (x)
    for (const auto b : ins(0x10, 0, 1)) body.push_back(b); // const.i32 1 (y)
    for (const auto b : ins(0x13, 0, 0)) body.push_back(b); // local.get 2 (palette)
    for (const auto b : ins(0x80, 0, 0)) body.push_back(b); // pixel
    for (const auto b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 1
    for (const auto b : ins(0x11, 0, 1)) body.push_back(b); // const.f32 2
    for (const auto b : ins(0x90, 0, 0)) body.push_back(b); // out.point 0
    for (const auto b : retIns()) body.push_back(b);

    const auto pool = constSection({"draw", "palette", "anchor"}, {0, 1}, {1.f, 2.f}, {});
    return fullFile(meta, pool, codeSection({{0, {2}, {}, {}, body}}, {}));
}

void writeFixture(const TempPath& temp) {
    const auto bytes = fixtureFile();
    std::ofstream out(temp.path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("ipgen generates a PNG and the result JSON") {
    TempPath temp("fixture");
    writeFixture(temp);
    CapturedStdout captured;

    const std::string file = temp.path.string();
    const std::string png = temp.pngPath().string();
    const char* args[] = {"ipgen", file.c_str(), "-o", png.c_str(), nullptr};
    const int code = ipgenMain(4, const_cast<char**>(args));
    INFO("stderr: " << captured.errText());
    REQUIRE(code == 0);

    // PNG decodes to the expected pixels.
    const auto image = tools::readPng(png);
    REQUIRE(image.width == 8);
    REQUIRE(image.height == 8);
    const std::size_t at = (1 * 8 + 1) * 4;
    REQUIRE(image.rgba[at] == 10);
    REQUIRE(image.rgba[at + 1] == 20);
    REQUIRE(image.rgba[at + 2] == 30);
    REQUIRE(image.rgba[at + 3] == 255);

    // JSON mentions the output, the fuel and the size.
    const std::string json = captured.text();
    REQUIRE(json.find("\"anchor\"") != std::string::npos);
    REQUIRE(json.find("\"kind\":\"point\"") != std::string::npos);
    REQUIRE(json.find("\"x\":1") != std::string::npos);
    REQUIRE(json.find("\"fuel\"") != std::string::npos);
    REQUIRE(json.find("\"size\"") != std::string::npos);
}

TEST_CASE("--uniform overrides the palette") {
    TempPath temp("fixture");
    writeFixture(temp);
    CapturedStdout captured;

    const std::string file = temp.path.string();
    const std::string png = temp.pngPath().string();
    const char* args[] = {"ipgen", file.c_str(), "--uniform",
                          "palette=#010203FF", "-o", png.c_str(), nullptr};
    REQUIRE(ipgenMain(6, const_cast<char**>(args)) == 0);

    const auto image = tools::readPng(png);
    const std::size_t at = (1 * 8 + 1) * 4;
    REQUIRE(image.rgba[at] == 1);
    REQUIRE(image.rgba[at + 1] == 2);
    REQUIRE(image.rgba[at + 2] == 3);
    REQUIRE(image.rgba[at + 3] == 255);
}

TEST_CASE("a bad uniform value or name fails with exit 1") {
    TempPath temp("fixture");
    writeFixture(temp);
    const std::string file = temp.path.string();

    {
        CapturedStdout captured;
        const char* args[] = {"ipgen", file.c_str(), "--uniform", "palette=nope", nullptr};
        REQUIRE(ipgenMain(4, const_cast<char**>(args)) == 1);
    }
    {
        CapturedStdout captured;
        const char* args[] = {"ipgen", file.c_str(), "--uniform", "unknown=5", nullptr};
        REQUIRE(ipgenMain(4, const_cast<char**>(args)) == 1);
    }
}

TEST_CASE("a corrupt file fails with a located error") {
    TempPath temp("corrupt");
    {
        const std::vector<u8> garbage = {0x49, 0x50, 0x52, 0x50, 0x00, 0x01, 0xFF};
        std::ofstream out(temp.path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(garbage.data()),
                  static_cast<std::streamsize>(garbage.size()));
    }
    CapturedStdout captured;
    const std::string file = temp.path.string();
    const char* args[] = {"ipgen", file.c_str(), nullptr};
    REQUIRE(ipgenMain(2, const_cast<char**>(args)) == 1);
    REQUIRE(captured.errText().find("ipgen:") != std::string::npos);
}

TEST_CASE("--size changes the output dimensions") {
    TempPath temp("fixture");
    writeFixture(temp);
    CapturedStdout captured;

    const std::string file = temp.path.string();
    const std::string png = temp.pngPath().string();
    const char* args[] = {"ipgen", file.c_str(), "--size", "16x8", "-o",
                          png.c_str(), nullptr};
    REQUIRE(ipgenMain(6, const_cast<char**>(args)) == 0);

    const auto image = tools::readPng(png);
    REQUIRE(image.width == 16);
    REQUIRE(image.height == 8);
}

TEST_CASE("retired host controls are rejected as unknown options") {
    TempPath temp("fixture");
    writeFixture(temp);
    CapturedStdout captured;
    const std::string file = temp.path.string();
    const std::string png = temp.pngPath().string();
    const char* args[] = {"ipgen", file.c_str(), "--variant", "5", "-o", png.c_str(), nullptr};
    REQUIRE(ipgenMain(6, const_cast<char**>(args)) == 1);
    REQUIRE(captured.errText().find("variant") != std::string::npos);
}

TEST_CASE("unknown options and missing files fail") {
    TempPath temp("fixture");
    writeFixture(temp);
    CapturedStdout captured;
    const std::string file = temp.path.string();
    {
        const char* args[] = {"ipgen", file.c_str(), "--bogus", nullptr};
        REQUIRE(ipgenMain(3, const_cast<char**>(args)) == 1);
    }
    {
        const char* args[] = {"ipgen", "/nonexistent/path.iprop", nullptr};
        REQUIRE(ipgenMain(2, const_cast<char**>(args)) == 1);
    }
}

TEST_CASE("seed sheets keep declared input overrides and wrap consecutive seeds") {
    TempPath temp("seed-sheet");
    const auto bytes = compiler_test::compileChecked("asset t {size 1x1 input color v=#123 input i32 a=2 draw{pixel(0,0,rgb(v.r,a,seed&255))}}");
    {
        std::ofstream file(temp.path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    CapturedStdout captured;
    const auto file = temp.path.string(), png = temp.pngPath().string();
    const char* args[] = {"ipsheet",file.c_str(),"--seed","4294967295","--seeds","3","--columns","2",
                         "--uniform","v=#445566","--uniform","a=7","-o",png.c_str(),nullptr};
    REQUIRE(ipsheetMain(14,const_cast<char**>(args)) == 0);
    const auto image=tools::readPng(png);
    CHECK(image.width == 2);
    CHECK(image.height == 2);
    CHECK(image.rgba == std::vector<u8>{68,7,255,255,68,7,0,255,68,7,1,255,0,0,0,0});
    const char* retired[] = {"ipsheet",file.c_str(),"--variants","2",nullptr};
    CHECK(ipsheetMain(4,const_cast<char**>(retired)) == 1);
    CHECK(captured.errText().find("unknown option") != std::string::npos);
}

TEST_CASE("PNG files round-trip RGBA pixels and reject other forms") {
    const tools::Image image{3, 2, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                    255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0}};
    auto png = tools::encodePng(image);
    const auto decoded = tools::decodePng(png);
    REQUIRE(decoded.width == 3);
    REQUIRE(decoded.height == 2);
    REQUIRE(decoded.rgba == image.rgba);

    auto corrupt = png;
    corrupt[20] ^= 1; // width byte inside IHDR, under its checksum
    REQUIRE_THROWS_AS(tools::decodePng(corrupt), std::runtime_error);
    png[25] = 2; // RGB colour type
    REQUIRE_THROWS(tools::decodePng(png));
    REQUIRE_THROWS(tools::encodePng({2, 2, {0, 0, 0, 0}}));
}
