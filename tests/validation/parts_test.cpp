#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "support/test_support.hpp"
#include "support/validation_support.hpp"

#include "iprop/Format.hpp"
#include "iprop/Validator.hpp"

using namespace iprop;
using namespace test_support;
using namespace validation_test;

namespace {

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

TEST_CASE("part transform kinds are bounded and require format 0.3") {
    for (u8 kind = 0; kind <= 8; ++kind) {
        auto body = partCall(0);
        const std::size_t callOffset = body.size() - 4;
        body[callOffset + 1] = kind;
        const auto end = retIns();
        body.insert(body.end(), end.begin(), end.end());
        const std::vector<FuncSpec> funcs = {
            drawSpec(body), {kAnonymousName, {0,0}, {}, {}, retIns()}};
        const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                                    codeSection(funcs, {{1, 4, 2, 0, 0, {}, {}}}));
        const std::size_t rel = streamRel(funcs, 0) + callOffset;
        if (kind == 8) {
            expectValidateError(bytes, rel, "unknown part transform kind");
            continue;
        }
        CHECK_NOTHROW(validate(loadFile(bytes)));
        for (const u8 minor : {u8{1}, u8{2}}) {
            auto legacy = bytes;
            legacy[6] = minor;
            if (kind == 0) {
                CHECK_NOTHROW(validate(loadFile(legacy)));
            } else {
                expectValidateError(legacy, rel, "part transforms require format 0.3");
            }
        }
    }
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
