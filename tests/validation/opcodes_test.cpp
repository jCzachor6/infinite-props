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

TEST_CASE("format 0.1 remains supported and rejects every new math opcode") {
    const auto legacyBody = retIns();
    auto legacy = fullFile(minimalMeta(), richPool({"draw"}),
                           codeSection({drawSpec(legacyBody)}, {}));
    legacy[6] = 1;
    CHECK_THROWS_AS(loadFile(legacy),FormatError);
    for (const u8 opcode : {u8{0x4F}, u8{0x53}, u8{0x54}, u8{0x55}, u8{0x56},
                            u8{0x57}, u8{0x58}, u8{0x59}, u8{0x5A}}) {
        const unsigned args = opcode == 0x56 || opcode == 0x57 ||
                              opcode == 0x58 || opcode == 0x5A ? 2 : 1;
        std::vector<u8> body;
        for (unsigned i = 0; i < args; ++i) {
            const auto constant = ins(0x11, 0, 0);
            body.insert(body.end(), constant.begin(), constant.end());
        }
        for (const auto& instruction : {ins(opcode, 0, 0), ins(0x16, 0, 0), retIns()}) {
            body.insert(body.end(), instruction.begin(), instruction.end());
        }
        const std::vector<FuncSpec> functions = {drawSpec(body)};
        auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection(functions, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
        bytes[6] = 1;
        CHECK_THROWS_AS(loadFile(bytes),FormatError);
    }
}

TEST_CASE("vec2 parameter, result and local types require format 0.3") {
    std::vector<u8> resultBody;
    for (const auto& instruction : {ins(0x11, 0, 0), ins(0x11, 0, 0),
                                    ins(0x5B, 0, 0), retIns()}) {
        resultBody.insert(resultBody.end(), instruction.begin(), instruction.end());
    }
    std::vector<u8> localBody;
    for (const auto& instruction : {ins(0x13, 0, 0), ins(0x16, 0, 0), retIns()}) {
        localBody.insert(localBody.end(), instruction.begin(), instruction.end());
    }
    const std::vector<FuncSpec> helpers = {
        {kAnonymousName, {3}, {}, {}, retIns()},
        {kAnonymousName, {}, {3}, {}, resultBody},
        {kAnonymousName, {}, {}, {3}, localBody}};
    for (std::size_t i = 0; i < helpers.size(); ++i) {
        INFO("vec2 type list " << i);
        const std::vector<FuncSpec> functions = {drawSpec(retIns()), helpers[i]};
        auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                              codeSection(functions, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
        for (const u8 minor : {u8{1}, u8{2}}) {
            INFO("minor " << static_cast<unsigned>(minor));
            bytes[6] = minor;
            CHECK_THROWS_AS(loadFile(bytes),FormatError);
        }
    }
    // Even an empty instruction stream must not bypass the type-version gate.
    const std::vector<FuncSpec> functions = {
        {0, {}, {}, {3}, {}}};
    auto bytes = fullFile(minimalMeta(), richPool({"draw"}), codeSection(functions, {}));
    REQUIRE_NOTHROW(validate(loadFile(bytes)));
    bytes[6] = 2;
    CHECK_THROWS_AS(loadFile(bytes),FormatError);
}

TEST_CASE("valid battery passes validation") {
    // 1. Trivial draw.
    {
        const auto bytes = fullFile(minimalMeta(), richPool({"draw"}),
                                    codeSection({drawSpec(retIns())}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
    // 2. Draw with a colour uniform: params (i32, i32, color).
    {
        const auto meta = metaSection(8, 8, 0, 0, 1000, false, {}, 0, 0, 0,
                                      {{0, 1, {1, 2, 3, 255}, 0, 0}}, {});
        const auto body = [] {
            std::vector<u8> b;
            for (const auto& i : ins(0x13, 0, 0)) b.push_back(i); // local.get 2 (palette)
            for (const auto& i : ins(0x10, 0, 0)) b.push_back(i); // const.i32 0
            for (const auto& i : ins(0x51, 0, 0)) b.push_back(i); // color.shade
            for (const auto& i : ins(0x16, 0, 0)) b.push_back(i); // drop
            for (const auto& i : retIns()) b.push_back(i);
            return b;
        }();
        const auto bytes = fullFile(meta, richPool({"draw", "palette"}),
                                    codeSection({drawSpec(body, 0, {2})}, {}));
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
        for (const auto& i : ins(0x11, 0, 0)) body.push_back(i); // atX (f32)
        for (const auto& i : ins(0x11, 0, 0)) body.push_back(i); // atY (f32)
        for (const auto& i : ins(0xA0, 0, 0)) body.push_back(i); // part.draw 0
        for (const auto& i : ins(0x96, 0, 0)) body.push_back(i); // part.get.point 0
        for (const auto& i : ins(0x16, 0, 0)) body.push_back(i); // drop
        for (const auto& i : ins(0x16, 0, 0)) body.push_back(i); // drop
        for (const auto& i : retIns()) body.push_back(i);

        const auto bytes = fullFile(minimalMeta(), richPool({"draw", "pupil"}),
                                    codeSection({drawSpec(body), {kAnonymousName, {}, {}, {}, retIns()}},
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

        const auto meta = metaSection(8, 8, 0, 0, 1000, false, {}, 0, 0, 0, {},
                                      {{0, 0, 1}, {1, 1, 2}});
        const auto bytes = fullFile(meta, richPool({"draw", "anchor", "colliders"}),
                                    codeSection({drawSpec(body)}, {}));
        REQUIRE_NOTHROW(validate(loadFile(bytes)));
    }
}

TEST_CASE("Bezier additions validate stack types and require format 0.3") {
    for (const u8 opcode : {u8{0x86}, u8{0x87}, u8{0x88}}) {
        INFO("opcode " << static_cast<unsigned>(opcode));
        const int vectors = opcode == 0x86 ? 3 : opcode == 0x87 ? 4 : 5;
        Prog valid;
        for (int i = 0; i < vectors; ++i) valid.F(0.f).F(0.f).emit(0x5B);
        if (opcode != 0x88) valid.F(1.f).C({255, 0, 0, 255});
        valid.emit(opcode);
        if (opcode == 0x88) valid.emit(0x16);
        REQUIRE_NOTHROW(validate(loadFile(buildProgFile(valid))));
        Prog wrong;
        for (int i = 0; i < vectors - 1; ++i) wrong.F(0.f).F(0.f).emit(0x5B);
        wrong.F(0.f); // the last control (or query argument) has the wrong type
        if (opcode != 0x88) wrong.F(1.f).C({255, 0, 0, 255});
        const std::size_t at = wrong.body.size();
        wrong.emit(opcode);
        const std::vector<FuncSpec> functions = {drawSpec(wrong.body)};
        expectValidateError(buildProgFile(wrong), streamRel(functions, 0) + at, "type mismatch");
        // A bare opcode reaches its own version gate without a vec2.make
        // instruction or type-list gate failing first.
        const std::vector<FuncSpec> bare = {drawSpec(ins(opcode, 0, 0))};
        auto legacy = fullFile(minimalMeta(), richPool({"draw"}), codeSection(bare, {}));
        for (const u8 minor : {u8{1}, u8{2}}) {
            legacy[6] = minor;
            CHECK_THROWS_AS(loadFile(legacy),FormatError);
        }
    }
}
