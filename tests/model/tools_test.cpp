// The CLI tools on model files (spec models v1.2): ipval, ipgen and ipsheet
// run in-process, as the 2D tool tests do. A texture written by `ipgen` for
// a model equals what `ipgen` writes for the skin's own program.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "SourceCompilation.hpp"
#include "ipdis.hpp"
#include "ipgen.hpp"
#include "ipsheet.hpp"
#include "ipval.hpp"
#include "model_tools.hpp"
#include "png.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace iprop;
using Catch::Matchers::ContainsSubstring;

namespace {

const char* const kTile = R"(
asset tile {
  size 16x8
  input color tint = #8a5a2b
  output normal relief
  draw {
    rect(0, 0, 16, 8, tint)
    rect(1, 1, 6, 6, color.shade(tint, 20))
    layer height(alpha = surface.source_over) {
      rect(0, 0, 16, 8, #404040)
      rect(1, 1, 6, 6, #ffffff)
    }
    relief = height.normal(2.0, 0.5)
  }
}
)";

const char* const kCrate = R"(
import tile from "tile.ips"
asset crate {
  size 12x12x12
  scale 50
  input f32 wide = 8
  render {
    s = tile.draw(seed.initial().fork("tile"), size = {32, 16})
    box({0, 0, 0}, {wide, 6.0 to 10.0, 8}, s)
  }
}
)";

std::vector<u8> compile(const std::string& entry) {
    const std::map<std::string, std::string> sources{{"crate.ips", kCrate}, {"tile.ips", kTile}};
    const auto result = compileSource(entry, [&](std::string_view path) { return sources.at(std::string(path)); });
    REQUIRE(result.succeeded());
    return result.bytes;
}

// A directory of its own for one test, removed with everything in it.
struct Workspace {
    std::filesystem::path root;
    Workspace() {
        static int counter = 0;
        root = std::filesystem::temp_directory_path() /
               ("iprop-model-tools-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root);
    }
    ~Workspace() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::string path(const std::string& name) const { return (root / name).string(); }
    std::string write(const std::string& name, const std::vector<u8>& bytes) const {
        std::ofstream out(root / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return path(name);
    }
    std::vector<u8> read(const std::string& name) const {
        std::ifstream in(root / name, std::ios::binary);
        return {(std::istreambuf_iterator<char>(in)), {}};
    }
    bool has(const std::string& name) const { return std::filesystem::exists(root / name); }
};

struct Run {
    int code = 0;
    std::string out, err;
};

template <class Main> Run run(Main main, std::vector<std::string> arguments) {
    std::ostringstream out, err;
    auto* previousOut = std::cout.rdbuf(out.rdbuf());
    auto* previousErr = std::cerr.rdbuf(err.rdbuf());
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const int code = main(static_cast<int>(arguments.size()), argv.data());
    std::cout.rdbuf(previousOut);
    std::cerr.rdbuf(previousErr);
    return {code, out.str(), err.str()};
}

u32 word(const std::vector<u8>& bytes, std::size_t at) {
    return static_cast<u32>(bytes[at]) | static_cast<u32>(bytes[at + 1]) << 8 | static_cast<u32>(bytes[at + 2]) << 16 |
           static_cast<u32>(bytes[at + 3]) << 24;
}

} // namespace

TEST_CASE("ipval tells a model by its magic") {
    const auto model = compile("crate.ips");
    CHECK(validateReport(model).valid);
    CHECK(validateReport(compile("tile.ips")).valid);

    // Damage past the header is found by the model loader, with an offset.
    auto cut = model;
    cut.resize(cut.size() - 7);
    const auto report = validateReport(cut);
    CHECK_FALSE(report.valid);
    REQUIRE(report.violations.size() == 1);
    CHECK_FALSE(report.violations[0].message.empty());

    // The same bytes under the 2D magic are no 2D program.
    auto renamed = model;
    renamed[2] = 'R';
    renamed[3] = 'P';
    CHECK_FALSE(validateReport(renamed).valid);

    const Workspace files;
    const auto path = files.write("crate.iprop3d", model);
    const auto valid = run(ipvalMain, {"ipval", path});
    CHECK(valid.code == 0);
    CHECK_THAT(valid.out, ContainsSubstring("valid: "));
    CHECK(run(ipvalMain, {"ipval", "--json", files.write("cut.iprop3d", cut)}).code == 1);

    // The assembly tools do not cover models yet, and say so.
    const auto listing = run(ipdisMain, {"ipdis", path});
    CHECK(listing.code == 1);
    CHECK(listing.out.empty());
    CHECK_THAT(listing.err, ContainsSubstring("this is an .iprop3d model; ipdis does not read models yet"));
}

TEST_CASE("ipgen writes a binary glTF and a summary for a model") {
    const Workspace files;
    const auto path = files.write("crate.iprop3d", compile("crate.ips"));
    const auto result = run(ipgenMain, {"ipgen", path, "--seed", "4", "--work-stats"});
    INFO(result.err);
    REQUIRE(result.code == 0);
    CHECK_THAT(result.out, ContainsSubstring("\"seed\": 4"));
    CHECK_THAT(result.out, ContainsSubstring("\"size\": {\"x\": 12, \"y\": 12, \"z\": 12}"));
    CHECK_THAT(result.out, ContainsSubstring("\"units_per_meter\": 50"));
    CHECK_THAT(result.out, ContainsSubstring("\"vertices\": 24"));
    CHECK_THAT(result.out, ContainsSubstring("\"triangles\": 12"));
    CHECK_THAT(result.out, ContainsSubstring("\"work\": {\"budget\": "));
    CHECK_THAT(result.out, ContainsSubstring("{\"name\": \"wide\", \"type\": \"f32\", \"default\": 8}"));
    CHECK_THAT(result.out, ContainsSubstring("\"name\": \"tile\""));
    CHECK_THAT(result.out, ContainsSubstring("\"size\": {\"x\": 32, \"y\": 16}"));
    CHECK_THAT(result.out, ContainsSubstring("{\"kind\": \"normal\", \"name\": \"relief\"}"));
    CHECK_THAT(result.out, ContainsSubstring("\"alpha\": \"opaque\""));
    CHECK_THAT(result.out, ContainsSubstring("crate-4.glb"));

    // The container: a header, a JSON chunk and a binary chunk, each padded.
    const auto glb = files.read("crate-4.glb");
    REQUIRE(glb.size() > 28);
    CHECK(std::string(glb.begin(), glb.begin() + 4) == "glTF");
    CHECK(word(glb, 4) == 2);
    CHECK(word(glb, 8) == glb.size());
    const u32 jsonLength = word(glb, 12);
    CHECK(word(glb, 16) == 0x4E4F534A);
    CHECK(jsonLength % 4 == 0);
    REQUIRE(20 + jsonLength + 8 <= glb.size());
    const std::string json(glb.begin() + 20, glb.begin() + 20 + jsonLength);
    const u32 binLength = word(glb, 20 + jsonLength);
    CHECK(word(glb, 24 + jsonLength) == 0x004E4942);
    CHECK(binLength % 4 == 0);
    CHECK(28 + jsonLength + binLength == glb.size());
    CHECK_THAT(json, ContainsSubstring("\"buffers\":[{\"byteLength\":" + std::to_string(binLength) + "}]"));
    CHECK_THAT(json, ContainsSubstring("\"POSITION\":0,\"NORMAL\":1,\"TANGENT\":2,\"TEXCOORD_0\":3"));
    CHECK_THAT(json, ContainsSubstring("\"normalTexture\""));
    CHECK_THAT(json, ContainsSubstring("\"scale\":[0.02,0.02,0.02]"));
    CHECK_THAT(json, ContainsSubstring("\"wrapS\":10497")); // the box repeats its texture
    // Braces and brackets balance: a cheap check that the hand-written JSON closes.
    int depth = 0;
    bool text = false;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"' && (i == 0 || json[i - 1] != '\\')) text = !text;
        if (text) continue;
        if (c == '{' || c == '[') ++depth;
        if (c == '}' || c == ']') --depth;
        REQUIRE(depth >= 0);
    }
    CHECK(depth == 0);
    CHECK_FALSE(text);
}

TEST_CASE("ipgen previews, dumps and writes the textures of a model") {
    const Workspace files;
    const auto path = files.write("crate.iprop3d", compile("crate.ips"));

    // A PNG output is the preview, and then no glTF is written.
    const auto preview = run(ipgenMain, {"ipgen", path, "-o", files.path("view.png"), "--preview-size", "96x64",
                                         "--view", "200,-10", "--background", "#10203040"});
    INFO(preview.err);
    REQUIRE(preview.code == 0);
    CHECK_FALSE(files.has("crate-0.glb"));
    const auto image = tools::readPng(files.path("view.png"));
    CHECK(image.width == 96);
    CHECK(image.height == 64);
    // The corner shows the background, the middle the model.
    CHECK(image.rgba[0] == 0x10);
    CHECK(image.rgba[3] == 0x40);
    const std::size_t middle = (static_cast<std::size_t>(32) * 96 + 48) * 4;
    CHECK(image.rgba[middle + 3] == 0xff);

    // Both at once, with the textures beside them.
    const auto both = run(ipgenMain, {"ipgen", path, "-o", files.path("crate.glb"), "--preview", files.path("crate.png"),
                                      "--skins-dir", files.path("skins")});
    REQUIRE(both.code == 0);
    CHECK(files.has("crate.glb"));
    CHECK(files.has("crate.png"));
    CHECK_THAT(both.out, ContainsSubstring("00-tile.png"));

    // The texture is the image `ipgen` makes from the skin's own program,
    // for the seed, the size and the inputs the model gave it.
    const auto dump = run(ipgenMain, {"ipgen", path, "--dump", "-o", files.path("unused.glb")});
    REQUIRE(dump.code == 0);
    CHECK_THAT(dump.out, ContainsSubstring("\"positions\": ["));
    const auto skinFile = files.write("tile.iprop", compile("tile.ips"));
    const auto loaded = model::loadModel(files.read("crate.iprop3d"));
    model::ModelInputs inputs;
    const auto generated = model::generateModel(loaded, inputs);
    REQUIRE(generated.textures.size() == 1);
    // The seed of the skin is a fork of the model's seed: read it back from
    // a second model that publishes the same texture key for a known seed.
    const auto colour = tools::readPng(files.path("skins/00-tile.png"));
    const auto normal = tools::readPng(files.path("skins/00-tile-normal.png"));
    CHECK(colour.width == 32);
    CHECK(colour.height == 16);
    CHECK(colour.rgba == tools::canvasImage(generated.textures[0].color).rgba);
    REQUIRE(generated.textures[0].images.size() == 1);
    CHECK(normal.rgba == tools::canvasImage(generated.textures[0].images[0].image).rgba);
    // A model that draws the skin with the literal seed 77 matches `ipgen --seed 77`.
    const std::map<std::string, std::string> sources{
        {"fixed.ips", "import tile from \"tile.ips\"\nasset fixed {\n  size 4x4x4\n  render {\n"
                      "    box({0, 0, 0}, {2, 2, 2}, tile.draw(77, tint = #336699, size = {32, 16}))\n  }\n}\n"},
        {"tile.ips", kTile}};
    const auto fixed = compileSource("fixed.ips", [&](std::string_view name) { return sources.at(std::string(name)); });
    REQUIRE(fixed.succeeded());
    const auto fixedPath = files.write("fixed.iprop3d", fixed.bytes);
    REQUIRE(run(ipgenMain, {"ipgen", fixedPath, "--skins-dir", files.path("fixed-skins"), "-o", files.path("fixed.glb")}).code == 0);
    REQUIRE(run(ipgenMain, {"ipgen", skinFile, "--seed", "77", "--size", "32x16", "--uniform", "tint=#336699", "-o",
                            files.path("alone.png"), "--normal-output", files.path("alone-normal.png")}).code == 0);
    CHECK(tools::readPng(files.path("fixed-skins/00-tile.png")).rgba == tools::readPng(files.path("alone.png")).rgba);
    CHECK(tools::readPng(files.path("fixed-skins/00-tile-normal.png")).rgba == tools::readPng(files.path("alone-normal.png")).rgba);

    // The dump is the same twice and differs for another seed or input.
    const auto again = run(ipgenMain, {"ipgen", path, "--dump", "-o", files.path("unused.glb")});
    CHECK(again.out == dump.out);
    const auto other = run(ipgenMain, {"ipgen", path, "--dump", "--seed", "1", "-o", files.path("unused.glb")});
    CHECK(other.out != dump.out);
    const auto wider = run(ipgenMain, {"ipgen", path, "--dump", "--uniform", "wide=11.5", "-o", files.path("unused.glb")});
    REQUIRE(wider.code == 0);
    CHECK(wider.out != dump.out);
}

TEST_CASE("ipgen keeps the options of images and of models apart") {
    const Workspace files;
    const auto model = files.write("crate.iprop3d", compile("crate.ips"));
    const auto image = files.write("tile.iprop", compile("tile.ips"));

    for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{
             {"--size", "8x8"}, {"--normal-output", files.path("n.png")}, {"--derive-normal"}}) {
        std::vector<std::string> arguments{"ipgen", model, "-o", files.path("m.glb")};
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        const auto result = run(ipgenMain, arguments);
        CHECK(result.code == 1);
        CHECK_THAT(result.err, ContainsSubstring("do not apply to a model"));
    }
    CHECK_FALSE(files.has("m.glb"));
    // A model has no part records, so leaving them out changes nothing: the
    // editor and the documentation checks render any compiled source alike.
    const auto plain = run(ipgenMain, {"ipgen", model, "--seed", "3", "-o", files.path("plain.png")});
    const auto omitted = run(ipgenMain, {"ipgen", model, "--seed", "3", "-o", files.path("omitted.png"), "--omit-part-outputs"});
    REQUIRE(plain.code == 0);
    REQUIRE(omitted.code == 0);
    CHECK(files.read("plain.png") == files.read("omitted.png"));
    for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{
             {"--preview", files.path("p.png")}, {"--dump"}, {"--skins-dir", files.path("s")}, {"--view", "10,10"},
             {"--preview-size", "64x64"}, {"--background", "#000000"}}) {
        std::vector<std::string> arguments{"ipgen", image, "-o", files.path("i.png")};
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        const auto result = run(ipgenMain, arguments);
        CHECK(result.code == 1);
        CHECK_THAT(result.err, ContainsSubstring("needs a model (.iprop3d)"));
    }
    CHECK_FALSE(files.has("i.png"));

    CHECK(run(ipgenMain, {"ipgen", model, "--view", "10"}).code == 1);
    CHECK(run(ipgenMain, {"ipgen", model, "--view", "10,95"}).code == 1);
    CHECK(run(ipgenMain, {"ipgen", model, "--preview-size", "0x10"}).code == 1);
    CHECK(run(ipgenMain, {"ipgen", model, "--background", "red"}).code == 1);
    const auto unknown = run(ipgenMain, {"ipgen", model, "--uniform", "tall=3", "-o", files.path("u.glb")});
    CHECK(unknown.code == 1);
    CHECK_THAT(unknown.err, ContainsSubstring("unknown uniform 'tall'"));
    // A limit stops the generation before any file is written.
    const auto limited = run(ipgenMain, {"ipgen", model, "--work-limit", "1", "-o", files.path("limited.glb")});
    CHECK(limited.code == 1);
    CHECK_THAT(limited.err, ContainsSubstring("generation failed"));
    CHECK_FALSE(files.has("limited.glb"));
    CHECK(run(ipgenMain, {"ipgen", model, "-o", files.path("a.png"), "--preview", files.path("b.png")}).code == 1);
}

TEST_CASE("ipsheet fills a sheet with previews of a model") {
    const Workspace files;
    const auto model = files.write("crate.iprop3d", compile("crate.ips"));
    const auto image = files.write("tile.iprop", compile("tile.ips"));

    const auto sheet = run(ipsheetMain, {"ipsheet", model, "--seed", "3", "--seeds", "5", "--columns", "3", "--cell", "48",
                                         "--uniform", "wide=6", "-o", files.path("sheet.png")});
    INFO(sheet.err);
    REQUIRE(sheet.code == 0);
    const auto png = tools::readPng(files.path("sheet.png"));
    CHECK(png.width == 3 * 48);
    CHECK(png.height == 2 * 48);
    // The sixth cell has no seed and stays empty; the fifth is drawn.
    const auto alpha = [&](u32 x, u32 y) { return png.rgba[(static_cast<std::size_t>(y) * png.width + x) * 4 + 3]; };
    CHECK(alpha(2 * 48 + 24, 48 + 24) == 0);
    CHECK(alpha(48 + 24, 48 + 24) == 0xff);

    const auto wrong = run(ipsheetMain, {"ipsheet", model, "--image", "normal", "-o", files.path("x.png")});
    CHECK(wrong.code == 1);
    CHECK_THAT(wrong.err, ContainsSubstring("does not apply to a model"));
    const auto flat = run(ipsheetMain, {"ipsheet", image, "--cell", "64", "-o", files.path("y.png")});
    CHECK(flat.code == 1);
    CHECK_THAT(flat.err, ContainsSubstring("needs a model (.iprop3d)"));
    CHECK(run(ipsheetMain, {"ipsheet", model, "--cell", "8"}).code == 1);
    CHECK(run(ipsheetMain, {"ipsheet", model, "--uniform", "nope=1", "-o", files.path("z.png")}).code == 1);
    // A 2D sheet is unchanged by all this.
    CHECK(run(ipsheetMain, {"ipsheet", image, "--seeds", "2", "-o", files.path("flat.png")}).code == 0);
    CHECK(tools::readPng(files.path("flat.png")).width == 32);
}
