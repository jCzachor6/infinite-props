#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/Format.hpp"
#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;

namespace {

constexpr std::size_t kNoOffset = static_cast<std::size_t>(-1);

FuncSpec draw(const std::vector<u8>& body, const std::vector<u8>& params = {0, 0},
              u32 nameIndex = 0) {
    return {nameIndex, params, {}, {}, body};
}

std::vector<u8> ret() { return ins(0x09, 0, 0); } // return

// Validates and runs the file, returning the canvas.
Canvas runFile(const std::vector<u8>& bytes, const GenerationInputs& inputs = {}) {
    auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, inputs);
    vm.run();
    return vm.canvas();
}

// Expects run() to trap with a message mentioning `msg`; when `rel` is given,
// the trap offset must be codeSectionOffset + rel. For traps thrown before
// execution (offset 0), use expectTrapAt.
void expectTrap(const std::vector<u8>& bytes, const GenerationInputs& inputs,
                const char* msg, std::size_t rel = kNoOffset, bool validateFirst = true) {
    auto loaded = loadFile(bytes);
    if (validateFirst) {
        validate(loaded);
    }
    VM vm(loaded, inputs);
    bool threw = false;
    try {
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        CHECK(std::string(error.what()).find(msg) != std::string::npos);
        if (rel != kNoOffset) {
            const std::size_t base = loaded.layout.find(SectionId::Code)->offset;
            CHECK(error.offset == base + rel);
        }
    }
    REQUIRE(threw);
}

// Expects a trap with an exact absolute file offset (pre-execution traps
// report offset 0).
void expectTrapAt(const std::vector<u8>& bytes, const GenerationInputs& inputs,
                  const char* msg, std::size_t absoluteOffset) {
    auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, inputs);
    bool threw = false;
    try {
        vm.run();
    } catch (const Trap& error) {
        threw = true;
        REQUIRE(std::string(error.what()).find(msg) != std::string::npos);
        REQUIRE(error.offset == absoluteOffset);
    }
    REQUIRE(threw);
}

} // namespace

TEST_CASE("a trivial draw produces a transparent meta-sized canvas") {
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection({draw(ret())}, {}));
    const auto canvas = runFile(bytes);
    REQUIRE(canvas.width == 8);
    REQUIRE(canvas.height == 8);
    REQUIRE(canvas.scaleX == 1.f);
    REQUIRE(canvas.scaleY == 1.f);
    REQUIRE(canvas.pixels.size() == 64);
    for (const auto& pixel : canvas.pixels) {
        REQUIRE(pixel == Color{0, 0, 0, 0});
    }
}

TEST_CASE("a requested size scales the canvas") {
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection({draw(ret())}, {}));
    const auto canvas = runFile(bytes, {0, 0, 0, 16, 8, {}});
    REQUIRE(canvas.width == 16);
    REQUIRE(canvas.height == 8);
    REQUIRE(canvas.scaleX == 2.f);
    REQUIRE(canvas.scaleY == 1.f);
}

TEST_CASE("locals behave per spec §8: params, extra locals, zero defaults") {
    std::vector<u8> body;
    for (const auto& b : ins(0x10, 0, 0)) body.push_back(b); // const.i32 42
    for (const auto& b : ins(0x14, 0, 2)) body.push_back(b); // local.set 2
    for (const auto& b : ins(0x13, 0, 2)) body.push_back(b); // local.get 2
    for (const auto& b : ins(0x14, 0, 0)) body.push_back(b); // local.set 0 (variant param)
    for (const auto& b : ins(0x13, 0, 0)) body.push_back(b); // local.get 0
    for (const auto& b : ins(0x16, 0, 0)) body.push_back(b); // drop
    for (const auto& b : ret()) body.push_back(b);

    const auto funcs = std::vector<FuncSpec>{{0, {0, 0}, {}, {0}, body}}; // one i32 local
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection(funcs, {}));
    REQUIRE_NOTHROW(runFile(bytes));

    // An unset extra local reads as its zero value.
    std::vector<u8> zeroBody;
    for (const auto& b : ins(0x13, 0, 2)) zeroBody.push_back(b); // local.get 2
    for (const auto& b : ins(0x16, 0, 0)) zeroBody.push_back(b); // drop
    for (const auto& b : ret()) zeroBody.push_back(b);
    const auto zeroBytes = fullFile(minimalMeta(), richPool({"draw"}),
                                    codeSection({{0, {0, 0}, {}, {0}, zeroBody}}, {}));
    REQUIRE_NOTHROW(runFile(zeroBytes));
}

TEST_CASE("call and return move results between frames") {
    std::vector<u8> body;
    for (const auto& b : ins(0x08, 0, 1)) body.push_back(b); // call 1
    for (const auto& b : ins(0x16, 0, 0)) body.push_back(b); // drop (the i32 result)
    for (const auto& b : ret()) body.push_back(b);

    std::vector<u8> helper;
    for (const auto& b : ins(0x10, 0, 0)) helper.push_back(b); // const.i32 7
    for (const auto& b : ret()) helper.push_back(b);

    const auto funcs = std::vector<FuncSpec>{
        {0, {0, 0}, {}, {}, body},
        {kAnonymousName, {}, {0}, {}, helper},
    };
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection(funcs, {}));
    REQUIRE_NOTHROW(runFile(bytes));
}

TEST_CASE("nested calls pass values through") {
    std::vector<u8> body;
    for (const auto& b : ins(0x08, 0, 1)) body.push_back(b); // call 1 (no results)
    for (const auto& b : ret()) body.push_back(b);

    std::vector<u8> middle;
    for (const auto& b : ins(0x08, 0, 2)) middle.push_back(b); // call 2
    for (const auto& b : ins(0x16, 0, 0)) middle.push_back(b); // drop
    for (const auto& b : ret()) middle.push_back(b);

    std::vector<u8> leaf;
    for (const auto& b : ins(0x10, 0, 0)) leaf.push_back(b); // const.i32 9
    for (const auto& b : ret()) leaf.push_back(b);

    const auto funcs = std::vector<FuncSpec>{
        {0, {0, 0}, {}, {}, body},
        {kAnonymousName, {}, {}, {}, middle},
        {kAnonymousName, {}, {0}, {}, leaf},
    };
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection(funcs, {}));
    REQUIRE_NOTHROW(runFile(bytes));
}

TEST_CASE("a function may end without an explicit return") {
    std::vector<u8> body;
    for (const auto& b : ins(0x08, 0, 1)) body.push_back(b); // call 1
    for (const auto& b : ret()) body.push_back(b);

    const auto funcs = std::vector<FuncSpec>{
        {0, {0, 0}, {}, {}, body},
        {kAnonymousName, {}, {}, {}, ins(0x00, 0, 0)}, // just a nop, no return
    };
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection(funcs, {}));
    REQUIRE_NOTHROW(runFile(bytes));
}

TEST_CASE("fuel exhaustion traps at the overspending instruction") {
    // 11 nops; the 11th spends beyond a budget of 10.
    std::vector<u8> body;
    for (int i = 0; i < 11; ++i) {
        for (const auto& b : ins(0x00, 0, 0)) body.push_back(b);
    }
    const auto meta = metaSection(8, 8, 0, 0, 1, 1, 10, false, {}, 0, 0, 0, {}, {});
    const auto bytes = fullFile(meta, richPool({"draw"}), codeSection({draw(body)}, {}));

    // Draw stream starts at rel 24 (entry = 20); instruction 10 at +40.
    expectTrap(bytes, {}, "fuel exhausted", 24 + 40);

    // A budget of exactly 11 runs.
    const auto okMeta = metaSection(8, 8, 0, 0, 1, 1, 11, false, {}, 0, 0, 0, {}, {});
    REQUIRE_NOTHROW(runFile(fullFile(okMeta, richPool({"draw"}), codeSection({draw(body)}, {}))));
}

TEST_CASE("out-of-range variant and archetype trap before execution") {
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection({draw(ret())}, {}));
    expectTrapAt(bytes, {0, 5, 0, 0, 0, {}}, "variant", 0);
    expectTrapAt(bytes, {0, 0, 9, 0, 0, {}}, "archetype", 0);
}

TEST_CASE("runtime stack underflow traps with a located message") {
    // Deliberately NOT validated: the VM must still trap, never misbehave.
    const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                                codeSection({draw(ins(0x16, 0, 0))}, {}));
    expectTrap(bytes, {}, "stack underflow", 24, /*validateFirst=*/false);
}

TEST_CASE("uniform defaults and overrides reach the entry params") {
    const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0,
                                  {{0, 1, {10, 20, 30, 255}, 0, 0}}, {});
    std::vector<u8> body;
    for (const auto& b : ins(0x13, 0, 2)) body.push_back(b); // local.get 2 (palette)
    for (const auto& b : ins(0x16, 0, 0)) body.push_back(b); // drop
    for (const auto& b : ret()) body.push_back(b);
    const auto bytes = fullFile(meta, richPool({"draw", "palette"}),
                                codeSection({{0, {0, 0, 2}, {}, {}, body}}, {}));

    // Default value.
    REQUIRE_NOTHROW(runFile(bytes));

    // Override with a matching type.
    GenerationInputs inputs;
    inputs.uniforms["palette"] = {InputType::Color, {1, 2, 3, 255}, 0.f, 0};
    REQUIRE_NOTHROW(runFile(bytes, inputs));

    // Override with the wrong type traps.
    GenerationInputs bad;
    bad.uniforms["palette"] = {InputType::F32, {}, 1.f, 0};
    expectTrapAt(bytes, bad, "uniform override type mismatch", 0);
}
