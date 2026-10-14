#include <catch2/catch_test_macros.hpp>

#include "SourceCompilation.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "iprop/Validator.hpp"
#include "ips/Imports.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iprop;

namespace {

using Sources = std::map<std::string, std::string>;

SourceCompilationResult compileTree(const Sources& sources, const std::string& entry,
                                    SourceCompilationOptions options = {},
                                    std::map<std::string, int>* reads = nullptr) {
    return compileSource(entry, [&](std::string_view path) {
        if (reads) ++(*reads)[std::string(path)];
        const auto found = sources.find(std::string(path));
        if (found == sources.end()) throw std::runtime_error("no such file");
        return found->second;
    }, options);
}

std::vector<u8> build(const Sources& sources, const std::string& entry, SourceCompilationOptions options = {}) {
    const auto result = compileTree(sources, entry, options);
    std::string diagnostics;
    for (const auto& diagnostic : result.diagnostics) diagnostics += diagnostic.path + ": " + diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(result.succeeded());
    return result.bytes;
}

GenerationResult generate(const std::vector<u8>& bytes, u32 seed = 7, u32 variant = 0) {
    const auto file = loadFile(bytes);
    validate(file);
    VM vm(file, GenerationInputs{seed,0,0,{{"v",{InputType::U32,{},0.f,variant}}}});
    vm.run();
    return vm.result();
}

struct Instance {
    std::string name;
    u32 parent;
};

std::vector<Instance> instances(const std::vector<u8>& bytes) {
    const auto file = loadFile(bytes);
    std::vector<Instance> result;
    for (const auto& part : file.code.parts) {
        REQUIRE(part.nameIndex.has_value());
        result.push_back({file.pool.strings.at(*part.nameIndex), part.parent});
    }
    return result;
}

void checkPoint(const OutputPoint& point, f32 x, f32 y) {
    CHECK(point.x == x);
    CHECK(point.y == y);
}

constexpr Color rgb(u8 r, u8 g, u8 b) { return Color{r, g, b, 255}; }

const Sources kSword{
    {"weapons/sword.ips", R"(import hilt from "parts/hilt/hilt.ips"

asset sword {
  size 64x64
  origin 32, 62
  output point bladeJoin

  draw {
    rect(31, 8, 2, 40, #c8c8c8)
    hilt.draw(seed=seed,   at={32, 62})
    bladeJoin = hilt.outputs.bladeJoin
  }
})"},
    {"weapons/parts/hilt/hilt.ips", R"(import grip from "grip.ips"
import pommel from "./pommel.ips"

asset hilt {
  size 16x24
  origin 8, 22
  output point bladeJoin

  draw {
    grip.draw(seed=hash(seed ^ 101),   at={8, 20})
    bladeJoin = grip.outputs.top
    rect(2, 5, 12, 2, #a0a0a0)
    pommel.draw(seed=hash(seed ^ 202),   at={8, 20})
  }
})"},
    {"weapons/parts/hilt/grip.ips", R"(asset grip {
  size 6x14
  origin 3, 14
  output point top

  draw {
    rect(1, 0, 4, 14, #806046)
    top = {3, 0}
  }
})"},
    {"weapons/parts/hilt/pommel.ips", R"(asset pommel {
  size 6x6
  origin 3, 3

  draw {
    oval(3, 3, 2.5, 2.5, #909090)
  }
})"},
};

} // namespace

TEST_CASE("import paths resolve lexically against the importing file") {
    CHECK(ips::resolveImportPath("sword.ips", "parts/hilt/hilt.ips") == "parts/hilt/hilt.ips");
    CHECK(ips::resolveImportPath("parts/hilt/hilt.ips", "grip.ips") == "parts/hilt/grip.ips");
    CHECK(ips::resolveImportPath("parts/hilt/hilt.ips", "./grip.ips") == "parts/hilt/grip.ips");
    CHECK(ips::resolveImportPath("parts/hilt/hilt.ips", "../shared/gem.ips") == "parts/shared/gem.ips");
    CHECK(ips::resolveImportPath("a/b.ips", "x/../../c.ips") == "c.ips");
    CHECK(ips::resolveImportPath("b.ips", "../c.ips") == "../c.ips");
    CHECK(ips::resolveImportPath("../b.ips", "../c.ips") == "../../c.ips");
    CHECK(ips::resolveImportPath("/abs/m.ips", "q.ips") == "/abs/q.ips");
    CHECK(ips::resolveImportPath("m.ips", "/abs/./q.ips") == "/abs/q.ips");
    CHECK(ips::resolveImportPath("a//b.ips", "c.ips") == "a/c.ips");
}

TEST_CASE("three nested levels compile from relative imports and publish root coordinates") {
    for (const bool optimize : {false, true}) {
        std::map<std::string, int> reads;
        const auto result = compileTree(kSword, "weapons/sword.ips", {optimize, true, false}, &reads);
        REQUIRE(result.succeeded());
        CHECK(reads.size() == 4);
        for (const auto& [path, count] : reads) { INFO(path); CHECK(count == 1); }

        // Instances in preorder of import order, named by alias path.
        const auto parts = instances(result.bytes);
        REQUIRE(parts.size() == 3);
        CHECK(parts[0].name == "hilt");
        CHECK(parts[0].parent == kRootParent);
        CHECK(parts[1].name == "hilt.grip");
        CHECK(parts[1].parent == 0);
        CHECK(parts[2].name == "hilt.pommel");
        CHECK(parts[2].parent == 0);
        CHECK(result.bytes[6] == 7);

        // grip.top = {3, 0} reads as {8, 6} in the hilt, which the sword
        // reads as {32, 46}; the host receives every copy in root space.
        const auto generated = generate(result.bytes);
        REQUIRE(generated.outputs.size() == 3);
        CHECK(generated.outputs[0].name == "bladeJoin");
        CHECK(generated.outputs[1].name == "bladeJoin");
        CHECK(generated.outputs[2].name == "top");
        for (const auto& output : generated.outputs) {
            REQUIRE(output.points.size() == 1);
            checkPoint(output.points[0], 32.f, 46.f);
        }
        // Each child is composited into its parent in call order.
        CHECK(generated.image.at(31, 20) == rgb(0xc8, 0xc8, 0xc8));
        CHECK(generated.image.at(31, 50) == rgb(0x80, 0x60, 0x46));
        CHECK(generated.image.at(27, 45) == rgb(0xa0, 0xa0, 0xa0));
        CHECK(generated.image.at(32, 60) == rgb(0x90, 0x90, 0x90));
    }
}

TEST_CASE("a file shared by two branches is planned as independent instances") {
    const Sources sources{
        {"colony.ips", R"(import left from "mushroom.ips"
import right from "mushroom.ips"
asset colony {
  size 16x16
  output point leftTop
  output point rightTop
  draw {
    left.draw(seed, at = {4, 15}, tint = #ff0000)
    leftTop = left.outputs.top
    right.draw(seed, at = {12, 15}, tint = #0000ff, mirror_x = true)
    rightTop = right.outputs.top
  }
})"},
        {"mushroom.ips", R"(import stem from "parts/stem.ips"
import cap from "parts/cap.ips"
asset mushroom {
  size 8x8
  origin 4, 8
  input color tint = #ffffff
  output point top
  draw {
    stem.draw(seed, at = {4, 8})
    cap.draw(seed, at = {4, 5}, tint = tint)
    top = cap.outputs.top
  }
})"},
        {"parts/stem.ips", R"(asset stem { size 2x4 origin 1, 4 draw { rect(0, 0, 2, 4, #e0d0b0) } })"},
        {"parts/cap.ips", R"(asset cap {
  size 8x4
  origin 4, 4
  input color tint = #ffffff
  output point top
  draw {
    rect(0, 0, 8, 4, tint)
    top = {5, 0}
  }
})"},
    };
    std::map<std::string, int> reads;
    const auto result = compileTree(sources, "colony.ips", {}, &reads);
    REQUIRE(result.succeeded());
    for (const auto& [path, count] : reads) { INFO(path); CHECK(count == 1); }
    const auto parts = instances(result.bytes);
    REQUIRE(parts.size() == 6);
    const std::vector<std::string> names{"left", "left.stem", "left.cap", "right", "right.stem", "right.cap"};
    const std::vector<u32> parents{kRootParent, 0, 0, kRootParent, 3, 3};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        CHECK(parts[i].name == names[i]);
        CHECK(parts[i].parent == parents[i]);
    }

    // Slots: root outputs, then left.top, left.cap.top, right.top,
    // right.cap.top. Mirroring maps the right cap's {5, 1} to {3, 1}.
    const auto generated = generate(result.bytes);
    REQUIRE(generated.outputs.size() == 6);
    checkPoint(generated.outputs[0].points.at(0), 5.f, 8.f);
    checkPoint(generated.outputs[1].points.at(0), 11.f, 8.f);
    checkPoint(generated.outputs[2].points.at(0), 5.f, 8.f);
    checkPoint(generated.outputs[3].points.at(0), 5.f, 8.f);
    checkPoint(generated.outputs[4].points.at(0), 11.f, 8.f);
    checkPoint(generated.outputs[5].points.at(0), 11.f, 8.f);
    // Each branch passes its own input to its own cap.
    CHECK(generated.image.at(2, 9) == rgb(255, 0, 0));
    CHECK(generated.image.at(13, 9) == rgb(0, 0, 255));
    CHECK(generated.image.at(4, 13) == rgb(0xe0, 0xd0, 0xb0));
}

TEST_CASE("nested D4 transforms map outputs to the root exactly once") {
    const std::vector<std::string> transforms{
        "", ", rot = 90", ", rot = 180", ", rot = 270", ", mirror_x = true", ", mirror_y = true",
        ", rot = 90, mirror_x = true"};
    for (const auto& outer : transforms) {
        for (const auto& inner : transforms) {
            INFO("outer" << outer << " inner" << inner);
            const Sources sources{
                {"root.ips", R"(import mid from "mid.ips"
asset root { size 16x16 output point mark
  draw { mid.draw(seed, at = {5, 4})" + outer + R"() mark = mid.outputs.mark } })"},
                {"mid.ips", R"(import leaf from "leaf.ips"
asset mid { size 6x6 output point mark
  draw { leaf.draw(seed, at = {1, 2})" + inner + R"() mark = leaf.outputs.mark } })"},
                {"leaf.ips", R"(asset leaf { size 4x3 output point mark
  draw { pixel(1, 0, #ff0000) mark = {1.5, 0.5} } })"},
            };
            const auto generated = generate(build(sources, "root.ips"));
            std::vector<std::pair<int, int>> marked;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    if (generated.image.at(x, y) == rgb(255, 0, 0)) marked.emplace_back(x, y);
            REQUIRE(marked.size() == 1);
            // The marker pixel's centre, through both levels, in every slot.
            for (const auto& output : generated.outputs) {
                REQUIRE(output.points.size() == 1);
                checkPoint(output.points[0], marked[0].first + 0.5f, marked[0].second + 0.5f);
            }
        }
    }
}

TEST_CASE("nested rects circles lists and helper writes keep one transform per call") {
    const Sources sources{
        {"root.ips", R"(import mid from "mid.ips"
asset root { size 32x32 output rect box output circle zone
  draw {
    mid.draw(seed, at = {0, 0})
    mid.draw(seed, at = {16, 8})
    box = mid.outputs.box
    zone = mid.outputs.zone
  } })"},
        {"mid.ips", R"(import leaf from "leaf.ips"
asset mid { input i32 a = 0  size 8x8 output rect box output circle zone
  draw {
    leaf.draw(seed, v=0, at = {0, 0})
    leaf.draw(seed, v=1, at = {4, 4})
    box = leaf.outputs.box
    zone = leaf.outputs.zone
  } })"},
        {"leaf.ips", R"(asset leaf { input i32 v = 0  size 4x4 output rect box
  output circle zone
  output list point marks
  draw {
    if (v == 0) { box = {0, 1, 2, 1} }
    zone = {1, 1, 0.5}
    note(0.5)
  }
  note(x: f32) { marks = {x, x} } })"},
    };
    for (const bool optimize : {false, true}) {
        const auto generated = generate(build(sources, "root.ips", {optimize, true, true}));
        REQUIRE(generated.outputs.size() == 7);
        // The box is written only by each mid's first leaf call: the second
        // call neither replaces nor re-transforms it.
        for (const std::size_t slot : {0u, 2u, 4u}) {
            INFO(slot);
            REQUIRE(generated.outputs[slot].rects.size() == 1);
            const auto& box = generated.outputs[slot].rects[0];
            CHECK(box.x == 16.f);
            CHECK(box.y == 9.f);
            CHECK(box.w == 2.f);
            CHECK(box.h == 1.f);
        }
        for (const std::size_t slot : {1u, 3u, 5u}) {
            INFO(slot);
            REQUIRE(generated.outputs[slot].circles.size() == 1);
            const auto& zone = generated.outputs[slot].circles[0];
            CHECK(zone.x == 21.f);
            CHECK(zone.y == 13.f);
            CHECK(zone.r == 0.5f);
        }
        // The helper's list writes append in execution order, each mapped
        // through the ancestors of its own call.
        const auto& marks = generated.outputs[6].points;
        REQUIRE(marks.size() == 4);
        checkPoint(marks[0], 0.5f, 0.5f);
        checkPoint(marks[1], 4.5f, 4.5f);
        checkPoint(marks[2], 16.5f, 8.5f);
        checkPoint(marks[3], 20.5f, 12.5f);
    }
}

TEST_CASE("nested parts keep isolated RNG streams at every level") {
    const std::string leaf = R"(asset leaf { size 4x4 draw { x = 0 to 3 pixel(x, 0 to 3, #ffffff) } })";
    const std::string mid = R"(import leaf from "leaf.ips"
asset mid { input i32 a = 0 input i32 v = 0  size 8x8 output point drawn
  draw {
    leaf.draw(seed ^ 5, at = {0, 0})
    drawn = {unit(), unit()}
    leaf.draw(seed ^ 9, at = {4, 4})
  } })";
    const auto root = [](const std::string& body) {
        return "import mid from \"mid.ips\"\nasset root { size 16x16 output point after output point inner\n"
               "draw { " + body + " after = {unit(), unit()} } }";
    };
    const auto nested = build({{"root.ips", root("mid.draw(seed, at = {0, 0}) inner = mid.outputs.drawn")},
                               {"mid.ips", mid}, {"leaf.ips", leaf}}, "root.ips");
    const auto bare = build({{"root.ips", root("")}, {"mid.ips", mid}, {"leaf.ips", leaf}}, "root.ips");
    const auto alone = build({{"mid.ips", R"(asset mid { size 8x8 output point drawn
  draw { drawn = {unit(), unit()} } })"}}, "mid.ips");
    for (const u32 seed : {0u, 1u, 0xdeadbeefu}) {
        INFO(seed);
        const auto withParts = generate(nested, seed);
        const auto withoutParts = generate(bare, seed);
        // The parent stream skips the whole subtree.
        CHECK(std::bit_cast<u32>(withParts.outputs[0].points[0].x) ==
              std::bit_cast<u32>(withoutParts.outputs[0].points[0].x));
        CHECK(std::bit_cast<u32>(withParts.outputs[0].points[0].y) ==
              std::bit_cast<u32>(withoutParts.outputs[0].points[0].y));
        // The mid stream starts from its own seed, unaffected by its child.
        const auto reference = generate(alone, seed);
        CHECK(std::bit_cast<u32>(withParts.outputs[1].points[0].x) ==
              std::bit_cast<u32>(reference.outputs[0].points[0].x));
        CHECK(std::bit_cast<u32>(withParts.outputs[1].points[0].y) ==
              std::bit_cast<u32>(reference.outputs[0].points[0].y));
    }
}

TEST_CASE("import cycles name the whole chain including equivalent paths") {
    const auto check = [](const Sources& sources, const std::string& entry, const std::string& chain,
                          const std::string& path) {
        const auto result = compileTree(sources, entry);
        CHECK(result.status == SourceCompilationStatus::InvalidSource);
        CHECK(result.bytes.empty());
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].code == ips::DiagnosticCode::ImportCycle);
        CHECK(result.diagnostics[0].message == "import cycle: " + chain);
        CHECK(result.diagnostics[0].path == path);
        CHECK(result.diagnostics[0].location.has_value());
    };
    const std::string leaf = "asset leaf { size 2x2 draw { } }";
    check({{"self.ips", "import me from \"./self.ips\"\n" + leaf}}, "self.ips",
          "self.ips -> self.ips", "self.ips");
    check({{"a.ips", "import b from \"dir/b.ips\"\n" + leaf},
           {"dir/b.ips", "import c from \"c.ips\"\n" + leaf},
           {"dir/c.ips", "import a from \"../dir/../a.ips\"\n" + leaf}}, "a.ips",
          "a.ips -> dir/b.ips -> dir/c.ips -> a.ips", "dir/c.ips");
    // A file reached from two branches is not a cycle.
    const auto diamond = compileTree({{"top.ips", "import l from \"x/l.ips\" import r from \"y/r.ips\"\n" + leaf},
                                      {"x/l.ips", "import s from \"../s.ips\"\n" + leaf},
                                      {"y/r.ips", "import s from \"../x/../s.ips\"\n" + leaf},
                                      {"s.ips", leaf}}, "top.ips");
    CHECK(diamond.succeeded());
    CHECK(instances(diamond.bytes).size() == 4);
}

TEST_CASE("the part limit counts expanded instances before planning them") {
    // Level i imports level i+1 twice: depth 7 expands to 254 instances.
    const auto tree = [](int depth, int extra) {
        Sources sources;
        for (int level = 0; level < depth; ++level) {
            const std::string next = "l" + std::to_string(level + 1) + ".ips";
            std::string source = "import a from \"" + next + "\"\nimport b from \"" + next + "\"\n";
            if (level == 0)
                for (int i = 0; i < extra; ++i)
                    source += "import x" + std::to_string(i) + " from \"l" + std::to_string(depth) + ".ips\"\n";
            source += "asset l" + std::to_string(level) +
                      " { size 2x2 draw { a.draw(seed, at = {0, 0}) b.draw(seed, at = {0, 0}) } }";
            sources.emplace("l" + std::to_string(level) + ".ips", source);
        }
        sources.emplace("l" + std::to_string(depth) + ".ips", "asset leaf { size 1x1 draw { pixel(0, 0, #fff) } }");
        return sources;
    };
    const auto full = compileTree(tree(7, 2), "l0.ips", {false, true, true});
    REQUIRE(full.succeeded());
    CHECK(instances(full.bytes).size() == 256);
    CHECK(generate(full.bytes).image.at(0, 0) == rgb(255, 255, 255));

    for (const auto& [depth, extra] : std::vector<std::pair<int, int>>{{7, 3}, {9, 0}, {24, 0}}) {
        INFO(depth << " " << extra);
        const auto result = compileTree(tree(depth, extra), "l0.ips");
        CHECK(result.status == SourceCompilationStatus::InvalidSource);
        REQUIRE(result.diagnostics.size() == 1);
        CHECK(result.diagnostics[0].code == ips::DiagnosticCode::ResourceLimit);
        CHECK(result.diagnostics[0].stage == ips::DiagnosticStage::Planning);
        CHECK(result.diagnostics[0].message.find("too many part instances") != std::string::npos);
        CHECK(result.diagnostics[0].location.has_value());
    }
}

TEST_CASE("nested instances survive strip compression and the assembler roundtrip") {
    const auto debug = build(kSword, "weapons/sword.ips", {true, false, false});
    const auto reference = generate(debug);
    const auto text = disassemble(debug);
    CHECK(text.find("part hilt instance:") != std::string::npos);
    CHECK(text.find("part hilt.grip instance hilt:") != std::string::npos);
    CHECK(text.find("part hilt.pommel instance hilt:") != std::string::npos);
    for (const auto& bytes : {stripDebug(debug), compressFile(debug), compressFile(stripDebug(debug)),
                              assemble(text)}) {
        CHECK(instances(bytes).size() == 3);
        CHECK(instances(bytes)[1].name == "hilt.grip");
        const auto generated = generate(bytes);
        CHECK(generated.image.pixels == reference.image.pixels);
        REQUIRE(generated.outputs.size() == reference.outputs.size());
        for (std::size_t i = 0; i < generated.outputs.size(); ++i)
            checkPoint(generated.outputs[i].points.at(0), 32.f, 46.f);
    }
}

TEST_CASE("the mushroom colony example places each firefly over its cap") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "examples/colony";
    Sources sources;
    for (const auto* path : {"colony.ips", "mushroom/mushroom.ips", "mushroom/stem.ips", "mushroom/cap.ips"}) {
        std::ifstream stream(root / path, std::ios::binary);
        REQUIRE(stream);
        sources.emplace(path, std::string(std::istreambuf_iterator<char>(stream), {}));
    }
    const auto bytes = build(sources, "colony.ips");
    const auto parts = instances(bytes);
    REQUIRE(parts.size() == 9);
    CHECK(parts[1].name == "tall.stem");
    CHECK(parts[8].name == "leaning.cap");
    CHECK(parts[8].parent == 6);
    for (const u32 seed : {0u, 5u, 77u}) {
        const auto generated = generate(bytes, seed);
        // The peaks the root reads equal the caps' host values; each stem
        // height raises its cap by one pixel per unit.
        const auto& peaks = generated.outputs[0].points;
        REQUIRE(peaks.size() == 3);
        const std::vector<std::pair<f32, f32>> expected{{10.f, 11.5f}, {24.f, 16.5f}, {37.f, 13.5f}};
        for (std::size_t i = 0; i < 3; ++i) {
            checkPoint(peaks[i], expected[i].first, expected[i].second);
            CHECK(generated.image.at(static_cast<u32>(expected[i].first),
                                     static_cast<u32>(expected[i].second) - 3) == rgb(0xff, 0xf3, 0xa0));
        }
    }
}
