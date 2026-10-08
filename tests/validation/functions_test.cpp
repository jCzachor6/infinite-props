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
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0xFD, 0, 0))}, {})),
                        24, "reserved opcode");
    expectValidateError(fullFile(minimalMeta(), pool, codeSection({drawSpec(ins(0xFE, 0, 0))}, {})),
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
