#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Compiler.hpp"
#include "ipa.hpp"
#include "ipdis.hpp"
#include "iprop/Debug.hpp"
#include "iprop/VM.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

// The debug section: the compiled file carries the source map and local
// names, ipdis --source annotates the disassembly, and stripping removes
// the section with identical execution.

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
      pixel(0, 0, shade(base, 10))
    }
    i = 0
    while (i < 2) {
      pixel(i, 1, #ffffff)
      i = i + 1
    }
  }
})";

std::vector<u8> compileChecked(const std::string& source) {
    const ips::Program program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {});
}

} // namespace

TEST_CASE("compiled files carry the debug flag, source map and local names") {
    const auto bytes = compileChecked(kFixture);
    const auto loaded = loadFile(bytes);
    REQUIRE(loaded.layout.header.hasDebug());

    const Section* debugSection = loaded.layout.find(SectionId::Debug);
    REQUIRE(debugSection != nullptr);
    const DebugInfo debug = parseDebug(bytes, *debugSection);
    REQUIRE(debug.functions.size() == loaded.code.functions.size());

    const DebugFunction& draw = debug.functions[0];
    REQUIRE(draw.lines.size() == loaded.code.functions[0].code.size() / 4);
    REQUIRE(draw.localNames.size() ==
            loaded.code.functions[0].params.size() +
                loaded.code.functions[0].locals.size());

    // The local names resolve through the pool: v, a, base, x, w, i.
    const std::vector<std::string> expected = {"v", "a", "base", "x", "w", "i"};
    REQUIRE(draw.localNames.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(loaded.pool.strings.at(draw.localNames[i]) == expected[i]);
    }

    // The first instruction is the folded `x = 2 + 3 * 4`; its line matches
    // the assignment's location in the AST.
    const ips::Program program = ips::parse(kFixture);
    const auto& body = program.asset.functions[0].body;
    const auto& decl = std::get<ips::VarDecl>(body[0]);
    REQUIRE(draw.lines[0] == decl.location.line);
}

TEST_CASE("ipdis --source annotates the disassembly") {
    const std::string annotated = disassembleWithSource(compileChecked(kFixture));
    REQUIRE(annotated.find("; locals: x w i") != std::string::npos);
    REQUIRE(annotated.find("; line ") != std::string::npos);

    // The plain disassembly stays clean (and reassemblable).
    const std::string plain = disassemble(compileChecked(kFixture));
    REQUIRE(plain.find("; line ") == std::string::npos);
    CHECK_NOTHROW(assemble(plain));
}

TEST_CASE("stripping removes the debug section with identical execution") {
    const std::vector<u8> original = compileChecked(kFixture);
    const std::vector<u8> stripped = stripDebug(original);

    const auto loaded = loadFile(stripped);
    validate(loaded);
    REQUIRE(!loaded.layout.header.hasDebug());
    REQUIRE(loaded.layout.find(SectionId::Debug) == nullptr);

    // The non-debug sections are byte-identical to the original's.
    const auto originalLoaded = loadFile(original);
    for (const SectionId id :
         {SectionId::Meta, SectionId::Const, SectionId::Code}) {
        const Section* before = originalLoaded.layout.find(id);
        const Section* after = loaded.layout.find(id);
        REQUIRE(before != nullptr);
        REQUIRE(after != nullptr);
        REQUIRE(before->size == after->size);
        REQUIRE(std::equal(original.begin() + before->offset,
                           original.begin() + before->offset + before->size,
                           stripped.begin() + after->offset));
    }

    // Execution is identical.
    for (const u32 seed : {0u, 7u, 42u}) {
        VM before(originalLoaded, GenerationInputs{seed, 0, 0, 0, 0, {}});
        VM after(loaded, GenerationInputs{seed, 0, 0, 0, 0, {}});
        before.run();
        after.run();
        REQUIRE(before.canvas().pixels == after.canvas().pixels);
        REQUIRE(before.result().outputs.size() == after.result().outputs.size());
    }
}

TEST_CASE("stripping a file without a debug section leaves it unchanged") {
    const auto noDebug = assemble(disassemble(compileChecked(kFixture)));
    REQUIRE(stripDebug(noDebug) == noDebug);
}

TEST_CASE("the ipdis and ipc CLIs expose the source map and the strip") {
    const std::vector<u8> bytes = compileChecked(kFixture);
    const auto path = std::filesystem::temp_directory_path() / "dbg-t.iprop";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
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
    };
    Argv argv{"ipdis", "--source", path.string().c_str()};
    REQUIRE(ipdisMain(3, argv.pointers.data()) == 0);
    std::filesystem::remove(path);
}
