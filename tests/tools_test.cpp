#include <catch2/catch_test_macros.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ipgen.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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
    const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0,
                                  {{0, 1, {10, 20, 30, 255}, 0, 0}}, {{0, 0, 2}});
    std::vector<u8> body;
    for (const auto b : ins(0x10, 0, 1)) body.push_back(b); // const.i32 1 (x)
    for (const auto b : ins(0x10, 0, 1)) body.push_back(b); // const.i32 1 (y)
    for (const auto b : ins(0x13, 0, 2)) body.push_back(b); // local.get 2 (palette)
    for (const auto b : ins(0x80, 0, 0)) body.push_back(b); // pixel
    for (const auto b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 1
    for (const auto b : ins(0x11, 0, 1)) body.push_back(b); // const.f32 2
    for (const auto b : ins(0x90, 0, 0)) body.push_back(b); // out.point 0
    for (const auto b : retIns()) body.push_back(b);

    const auto pool = constSection({"draw", "palette", "anchor"}, {0, 1}, {1.f, 2.f}, {});
    return fullFile(meta, pool, codeSection({{0, {0, 0, 2}, {}, {}, body}}, {}));
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
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(png.c_str(), &w, &h, &channels, 4);
    REQUIRE(pixels != nullptr);
    REQUIRE(w == 8);
    REQUIRE(h == 8);
    const std::size_t at = (1 * 8 + 1) * 4;
    REQUIRE(pixels[at] == 10);
    REQUIRE(pixels[at + 1] == 20);
    REQUIRE(pixels[at + 2] == 30);
    REQUIRE(pixels[at + 3] == 255);
    stbi_image_free(pixels);

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

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(png.c_str(), &w, &h, &channels, 4);
    REQUIRE(pixels != nullptr);
    const std::size_t at = (1 * 8 + 1) * 4;
    REQUIRE(pixels[at] == 1);
    REQUIRE(pixels[at + 1] == 2);
    REQUIRE(pixels[at + 2] == 3);
    REQUIRE(pixels[at + 3] == 255);
    stbi_image_free(pixels);
}

TEST_CASE("a bad uniform value or name fails with exit 1") {
    TempPath temp("fixture");
    writeFixture(temp);

    {
        CapturedStdout captured;
        const char* args[] = {"ipgen", temp.path.c_str(), "--uniform", "palette=nope", nullptr};
        REQUIRE(ipgenMain(4, const_cast<char**>(args)) == 1);
    }
    {
        CapturedStdout captured;
        const char* args[] = {"ipgen", temp.path.c_str(), "--uniform", "unknown=5", nullptr};
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
    const char* args[] = {"ipgen", temp.path.c_str(), nullptr};
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

    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(png.c_str(), &w, &h, &channels, 4);
    REQUIRE(pixels != nullptr);
    REQUIRE(w == 16);
    REQUIRE(h == 8);
    stbi_image_free(pixels);
}

TEST_CASE("unknown options and missing files fail") {
    TempPath temp("fixture");
    writeFixture(temp);
    CapturedStdout captured;
    {
        const char* args[] = {"ipgen", temp.path.c_str(), "--bogus", nullptr};
        REQUIRE(ipgenMain(3, const_cast<char**>(args)) == 1);
    }
    {
        const char* args[] = {"ipgen", "/nonexistent/path.iprop", nullptr};
        REQUIRE(ipgenMain(2, const_cast<char**>(args)) == 1);
    }
}
