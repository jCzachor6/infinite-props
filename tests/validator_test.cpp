#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "support/test_support.hpp"

#include "iprop/Format.hpp"
#include "iprop/Validator.hpp"

using namespace iprop;
using namespace test_support;

namespace {

// Entry size of a §6.1 function entry: 18 fixed bytes + one per type byte.
std::size_t entrySize(const FuncSpec& f) {
    return 18 + f.paramTypes.size() + f.resultTypes.size() + f.localTypes.size();
}

// Relative offset (to the code section start) of function fn's first
// instruction. Layout (spec §6): count, the WHOLE function table, then the
// concatenated streams — so the table contributes all entry sizes, and only
// the earlier streams' instruction bytes come before fn's stream.
std::size_t streamRel(const std::vector<FuncSpec>& funcs, std::size_t fn) {
    std::size_t rel = 4;
    for (const auto& f : funcs) {
        rel += entrySize(f);
    }
    for (std::size_t i = 0; i < fn; ++i) {
        rel += funcs[i].instructions.size();
    }
    return rel;
}

// Builds a file and expects validate() to throw a located FormatError at
// codeSectionOffset + rel, mentioning `messagePart`.
void expectValidateError(const std::vector<u8>& fileBytes, std::size_t rel,
                         const char* messagePart) {
    const auto layout = parseLayout(fileBytes);
    const std::size_t base = layout.find(SectionId::Code)->offset;
    bool threw = false;
    try {
        const auto loaded = loadFile(fileBytes);
        validate(loaded);
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == base + rel);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

// A part.draw call with all arguments pushed in the pinned order
// (spec §9.10: uniforms…, seed, variant, archetype, atX, atY).
std::vector<u8> partCall(u16 partIndex) {
    std::vector<u8> body;
    for (int i = 0; i < 3; ++i) {
        for (const auto& b : ins(0x10, 0, 0)) body.push_back(b); // seed, variant, archetype
    }
    for (int i = 0; i < 2; ++i) {
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // atX, atY
    }
    for (const auto& b : ins(0xA0, 0, partIndex)) body.push_back(b);
    return body;
}

} // namespace

TEST_CASE("valid battery passes validation") {
    // 1. Trivial draw.
    {
        const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                                    codeSection({drawSpec(retIns())}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 2. Draw with a colour uniform: params (i32, i32, color).
    {
        const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0,
                                      {{0, 1, {1, 2, 3, 255}, 0, 0}}, {});
        const auto body = [] {
            std::vector<u8> b;
            for (const auto& i : ins(0x13, 0, 2)) b.push_back(i); // local.get 2 (palette)
            for (const auto& i : ins(0x10, 0, 0)) b.push_back(i); // const.i32 0
            for (const auto& i : ins(0x51, 0, 0)) b.push_back(i); // color.shade
            for (const auto& i : ins(0x16, 0, 0)) b.push_back(i); // drop
            for (const auto& i : retIns()) b.push_back(i);
            return b;
        }();
        const auto bytes = fullFile(meta, richPool({"draw", "palette"}),
                                    codeSection({drawSpec(body, 0, {0, 0, 2})}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 3. Helper function with a result, called from draw.
    {
        std::vector<u8> callBody;
        for (const auto& i : ins(0x08, 0, 1)) callBody.push_back(i); // call 1
        for (const auto& i : ins(0x16, 0, 0)) callBody.push_back(i); // drop
        for (const auto& i : retIns()) callBody.push_back(i);
        std::vector<u8> helperBody;
        for (const auto& i : ins(0x10, 0, 0)) helperBody.push_back(i); // const.i32 0
        for (const auto& i : retIns()) helperBody.push_back(i);
        const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                                    codeSection({drawSpec(callBody),
                                                 {kAnonymousName, {}, {0}, {}, helperBody}},
                                                {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 4. Part call + part.get on a part-owned slot.
    {
        std::vector<u8> body;
        for (const auto& i : ins(0x10, 0, 0)) body.push_back(i); // seed (i32)
        for (const auto& i : ins(0x10, 0, 0)) body.push_back(i); // variant
        for (const auto& i : ins(0x10, 0, 0)) body.push_back(i); // archetype
        for (const auto& i : ins(0x11, 0, 0)) body.push_back(i); // atX (f32)
        for (const auto& i : ins(0x11, 0, 0)) body.push_back(i); // atY (f32)
        for (const auto& i : ins(0xA0, 0, 0)) body.push_back(i); // part.draw 0
        for (const auto& i : ins(0x96, 0, 0)) body.push_back(i); // part.get.point 0
        for (const auto& i : ins(0x16, 0, 0)) body.push_back(i); // drop
        for (const auto& i : ins(0x16, 0, 0)) body.push_back(i); // drop
        for (const auto& i : retIns()) body.push_back(i);

        const auto bytes = fullFile(minimalMeta(), richPool({"draw", "pupil"}),
                                    codeSection({drawSpec(body), {kAnonymousName, {0, 0}, {}, {}, retIns()}},
                                                {{1, 8, 8, 0, 0, {}, {{0, 0, 1}}}}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 5. Structured control flow: block, if/else, loop, br_if, br.
    {
        std::vector<u8> body;
        const auto c0 = ins(0x10, 0, 0); // const.i32 0
        const auto drop = ins(0x16, 0, 0);
        for (const auto& i : ins(0x01, 0, 0)) body.push_back(i); // block
        body.insert(body.end(), c0.begin(), c0.end());
        for (const auto& i : ins(0x03, 0, 0)) body.push_back(i); // if
        body.insert(body.end(), c0.begin(), c0.end());
        body.insert(body.end(), drop.begin(), drop.end());       // then arm
        for (const auto& i : ins(0x04, 0, 0)) body.push_back(i); // else
        body.insert(body.end(), c0.begin(), c0.end());
        body.insert(body.end(), drop.begin(), drop.end());       // else arm
        for (const auto& i : ins(0x05, 0, 0)) body.push_back(i); // end
        for (const auto& i : ins(0x02, 0, 0)) body.push_back(i); // loop
        body.insert(body.end(), c0.begin(), c0.end());
        for (const auto& i : ins(0x07, 0, 0)) body.push_back(i); // br_if 0
        for (const auto& i : ins(0x06, 0, 0)) body.push_back(i); // br 0
        for (const auto& i : ins(0x05, 0, 0)) body.push_back(i); // end
        for (const auto& i : ins(0x05, 0, 0)) body.push_back(i); // end
        for (const auto& i : retIns()) body.push_back(i);

        const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                                    codeSection({drawSpec(body)}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 6. Poly with a static vertex count and matching stack.
    {
        std::vector<u8> body;
        for (int i = 0; i < 6; ++i) {
            for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        }
        for (const auto& b : ins(0x12, 0, 0)) body.push_back(b); // const.color 0
        for (const auto& b : ins(0x83, 0, 3)) body.push_back(b); // poly 3
        for (const auto& b : retIns()) body.push_back(b);

        const auto bytes = fullFile(minimalMeta(), constSection({"draw"}, {}, {0.f}, {{0, 0, 0, 255}}),
                                    codeSection({drawSpec(body)}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 7. Structured outputs on matching slots (point + rect list).
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        for (const auto& b : ins(0x90, 0, 0)) body.push_back(b); // out.point 0
        for (int i = 0; i < 4; ++i) {
            for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        }
        for (const auto& b : ins(0x94, 0, 1)) body.push_back(b); // out.list.rect 1
        for (const auto& b : retIns()) body.push_back(b);

        const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {},
                                      {{0, 0, 1}, {1, 1, 2}});
        const auto bytes = fullFile(meta, richPool({"draw", "anchor", "colliders"}),
                                    codeSection({drawSpec(body)}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
}

TEST_CASE("entry naming and signature violations") {
    const auto pool = namesPool({"draw", "zzz", "palette"});

    // Entry not named "draw" (single function, stream at rel 24).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(retIns(), 1)}, {})),
                        24, "must be named");

    // Wrong parameter count (empty params: entry 18, stream at rel 22).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(retIns(), 0, {})}, {})),
                        22, "signature");

    // Wrong parameter type (f32 instead of i32).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(retIns(), 0, {0, 1})}, {})),
                        24, "signature");

    // Non-empty results (entry 21, stream at rel 25).
    expectValidateError(
        fullFile(minimalMeta(), pool, codeSection({{0, {0, 0}, {0}, {}, retIns()}}, {})),
        25, "no results");

    // Uniform param typed wrongly (i32 instead of colour; stream at rel 25).
    {
        const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0,
                                      {{0, 2, {1, 2, 3, 255}, 0, 0}}, {});
        expectValidateError(fullFile(meta, pool, codeSection({drawSpec(retIns(), 0, {0, 0, 0})}, {})),
                            25, "signature");
    }
}

TEST_CASE("per-function instruction violations") {
    const auto pool = richPool({"draw"});

    // Reserved opcode, aux byte, unknown opcode, reserved colour opcode
    // (all at instruction 0).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x0F, 0, 0))}, {})),
                        24, "reserved opcode");
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x00, 1, 0))}, {})),
                        24, "aux byte");
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0xA1, 0, 0))}, {})),
                        24, "reserved opcode");
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x52, 0, 0))}, {})),
                        24, "reserved opcode");

    // Stack underflow on the first instruction.
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x20, 0, 0)) body.push_back(b); // i32.add
        expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(body)}, {})),
                            24, "underflow");
    }
    // Type mismatch at instruction 1.
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        for (const auto& b : ins(0x20, 0, 0)) body.push_back(b); // i32.add
        expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(body)}, {})),
                            28, "type mismatch");
    }
    // if without an integer condition.
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        for (const auto& b : ins(0x03, 0, 0)) body.push_back(b); // if
        expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(body)}, {})),
                            28, "if condition");
    }
    // Unbalanced block: reported at the end of the stream (1 instruction).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x01, 0, 0))}, {})),
                        28, "unbalanced");
    // end without block.
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x05, 0, 0))}, {})),
                        24, "end without");
    // else without if.
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x04, 0, 0))}, {})),
                        24, "else without");
    // br depth out of range.
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x06, 0, 5))}, {})),
                        24, "branch depth");
    // br_if depth out of range (condition first, then depth).
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x01, 0, 0)) body.push_back(b); // block
        for (const auto& b : ins(0x10, 0, 0)) body.push_back(b); // const.i32 0
        for (const auto& b : ins(0x07, 0, 1)) body.push_back(b); // br_if 1
        expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(body)}, {})),
                            32, "branch depth");
    }
    // call index out of range.
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x08, 0, 7))}, {})),
                        24, "call index");
    // Const index out of range (empty i32 pool).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x10, 0, 3))}, {})),
                        24, "constant index");
    // Local index out of range (2 params only).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0x13, 0, 9))}, {})),
                        24, "local index");
    // local.set type mismatch at instruction 1.
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
        for (const auto& b : ins(0x14, 0, 0)) body.push_back(b); // local.set 0 (i32)
        expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(body)}, {})),
                            28, "type mismatch");
    }
    // then-branch leaves values on the stack.
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x01, 0, 0)) body.push_back(b); // block
        for (const auto& b : ins(0x10, 0, 0)) body.push_back(b); // const.i32 0
        for (const auto& b : ins(0x03, 0, 0)) body.push_back(b); // if
        for (const auto& b : ins(0x10, 0, 0)) body.push_back(b); // const.i32 0 (leaked)
        for (const auto& b : ins(0x04, 0, 0)) body.push_back(b); // else
        expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(body)}, {})),
                            40, "then-branch");
    }
    // poly underflow: colour but not enough vertices.
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x12, 0, 0)) body.push_back(b); // const.color 0
        for (const auto& b : ins(0x83, 0, 3)) body.push_back(b); // poly 3
        expectValidateError(
            fullFile(minimalMeta(), constSection({"draw"}, {}, {}, {{0, 0, 0, 255}}),
                     codeSection({drawSpec(body)}, {})),
            28, "underflow");
    }
}

TEST_CASE("slot and part violations") {
    const auto pool = richPool({"draw", "anchor", "colliders"});

    // Slot index out of range (one point slot, out.point 5 at instruction 2).
    {
        const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {},
                                      {{0, 0, 1}});
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b);
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b);
        for (const auto& b : ins(0x90, 0, 5)) body.push_back(b); // out.point 5
        expectValidateError(fullFile(meta, pool, codeSection({drawSpec(body)}, {})), 32, "slot index");
    }
    // Slot kind mismatch (out.point on a rect slot).
    {
        const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {},
                                      {{1, 0, 1}});
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b);
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b);
        for (const auto& b : ins(0x90, 0, 0)) body.push_back(b); // out.point 0
        expectValidateError(fullFile(meta, pool, codeSection({drawSpec(body)}, {})), 32, "kind");
    }
    // Singular op on a list slot, and list op on a singular slot.
    {
        const auto listMeta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {},
                                          {{0, 1, 1}});
        std::vector<u8> body;
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b);
        for (const auto& b : ins(0x11, 0, 0)) body.push_back(b);
        for (const auto& b : ins(0x90, 0, 0)) body.push_back(b); // out.point 0
        expectValidateError(fullFile(listMeta, pool, codeSection({drawSpec(body)}, {})), 32,
                            "expected a singular");

        const auto singularMeta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {},
                                              {{0, 0, 1}});
        std::vector<u8> listBody;
        for (const auto& b : ins(0x11, 0, 0)) listBody.push_back(b);
        for (const auto& b : ins(0x11, 0, 0)) listBody.push_back(b);
        for (const auto& b : ins(0x93, 0, 0)) listBody.push_back(b); // out.list.point 0
        expectValidateError(fullFile(singularMeta, pool, codeSection({drawSpec(listBody)}, {})), 32,
                            "expected a list");
    }
    // part.get on a root-owned slot.
    {
        const auto meta = metaSection(8, 8, 0, 0, 1, 1, 1000, false, {}, 0, 0, 0, {},
                                      {{0, 0, 1}});
        expectValidateError(fullFile(meta, pool, codeSection({drawSpec(ins(0x96, 0, 0))}, {})),
                            24, "root-owned");
    }
    // part.draw index out of range (no parts).
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0xA0, 0, 3))}, {})),
                        24, "part index");
    // part.draw with a wrong argument type (i32 where f32 atY is expected).
    {
        std::vector<u8> body;
        for (const auto& b : ins(0x10, 0, 0)) body.push_back(b); // const.i32 0
        for (const auto& b : ins(0xA0, 0, 0)) body.push_back(b); // part.draw 0
        const std::vector<FuncSpec> funcs = {
            {0, {0, 0}, {}, {}, body},
            {kAnonymousName, {0, 0}, {}, {}, retIns()},
        };
        const auto bytes = fullFile(minimalMeta(), pool,
                                    codeSection(funcs, {{1, 8, 8, 0, 0, {}, {}}}));
        const std::size_t rel = streamRel(funcs, 0) + 4; // the part.draw instruction
        expectValidateError(bytes, rel, "type mismatch");
    }
}

TEST_CASE("part signature violations are located at the part function") {
    const auto pool = namesPool({"draw"});
    const auto funcs = std::vector<FuncSpec>{
        drawSpec(retIns()),                            // entry: 20 bytes, stream at 24
        {kAnonymousName, {0}, {}, {}, retIns()},   // part fn: 1 param, entry 19
    };
    const std::size_t rel = streamRel(funcs, 1);
    const auto bytes = fullFile(minimalMeta(), pool,
                                codeSection(funcs, {{1, 8, 8, 0, 0, {}, {}}}));
    expectValidateError(bytes, rel, "part function signature");
}

TEST_CASE("helper return type mismatch is located") {
    const auto pool = richPool({"draw"});
    std::vector<u8> body;
    for (const auto& b : ins(0x11, 0, 0)) body.push_back(b); // const.f32 0
    for (const auto& b : retIns()) body.push_back(b);
    const auto funcs = std::vector<FuncSpec>{
        drawSpec(retIns()),
        {kAnonymousName, {}, {0}, {}, body}, // results: i32, body pushes f32
    };
    const std::size_t rel = streamRel(funcs, 1) + 4; // the return instruction
    const auto bytes = fullFile(minimalMeta(), pool, codeSection(funcs, {}));
    expectValidateError(bytes, rel, "return type mismatch");
}

TEST_CASE("part import cycles are rejected") {
    const auto pool = richPool({"draw"});
    std::vector<u8> drawsB = partCall(1); // part.draw 1
    for (const auto& b : retIns()) drawsB.push_back(b);
    std::vector<u8> drawsA = partCall(0); // part.draw 0
    for (const auto& b : retIns()) drawsA.push_back(b);

    const auto funcs = std::vector<FuncSpec>{
        drawSpec(retIns()),
        {kAnonymousName, {0, 0}, {}, {}, drawsB},
        {kAnonymousName, {0, 0}, {}, {}, drawsA},
    };
    const std::size_t rel = streamRel(funcs, 2); // back edge reported at fn 2
    const auto bytes = fullFile(minimalMeta(), pool,
                                codeSection(funcs, {{1, 8, 8, 0, 0, {}, {}},
                                                    {2, 8, 8, 0, 0, {}, {}}}));
    expectValidateError(bytes, rel, "cycle");
}

TEST_CASE("an acyclic part graph passes") {
    const auto pool = richPool({"draw"});
    std::vector<u8> drawsB = partCall(1); // part.draw 1
    for (const auto& b : retIns()) drawsB.push_back(b);
    const auto funcs = std::vector<FuncSpec>{
        drawSpec(retIns()),
        {kAnonymousName, {0, 0}, {}, {}, drawsB},
        {kAnonymousName, {0, 0}, {}, {}, retIns()},
    };
    const auto bytes = fullFile(minimalMeta(), pool,
                                codeSection(funcs, {{1, 8, 8, 0, 0, {}, {}},
                                                    {2, 8, 8, 0, 0, {}, {}}}));
    REQUIRE_NOTHROW(validate(loadFile(bytes)));
}
