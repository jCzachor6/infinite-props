#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "support/test_support.hpp"
#include "iprop/VM.hpp"

using namespace iprop;
using namespace test_support;
using Catch::Matchers::ContainsSubstring;

namespace {
struct Program : Prog {
    Program& op(Opcode opcode, u16 operand = 0) {
        emit(static_cast<u8>(opcode), operand);
        return *this;
    }
};

void valid(const Prog& p) {
    const auto bytes = buildProgFile(p);
    REQUIRE_NOTHROW(validate(loadFile(bytes)));
}

void invalid(const Prog& p, const char* message) {
    const auto bytes = buildProgFile(p);
    REQUIRE_THROWS_WITH(validate(loadFile(bytes)), ContainsSubstring(message));
}
}

TEST_CASE("control joins preserve types rather than only stack height") {
    for (const Opcode opcode : {Opcode::Vec2Mul, Opcode::Vec2Div}) {
        for (const bool hasElse : {false, true}) {
            Program p;
            p.F(1.f).F(2.f); p.op(Opcode::Vec2Make);
            p.I(3).I(0); p.op(Opcode::If).op(Opcode::Drop);
            p.F(3.f);
            if (hasElse) p.op(Opcode::Else);
            p.op(Opcode::End).op(opcode).op(Opcode::Drop);
            invalid(p, "stack types differ from entry");
            // Running without validation must still produce the VM error.
            if (!hasElse) expectProgTrap(p, "stack type mismatch");
        }
    }
}

TEST_CASE("branches preserve the entire target stack prefix") {
    for (const Opcode jump : {Opcode::Br, Opcode::BrIf}) {
        Program p;
        p.F(1.f); p.op(Opcode::Block).op(Opcode::Drop); p.I(1);
        if (jump == Opcode::BrIf) p.I(1);
        p.op(jump).op(Opcode::End).op(Opcode::Drop);
        invalid(p, "branch stack types");
    }
    Program missing;
    missing.I(1); missing.op(Opcode::Block).op(Opcode::Drop).op(Opcode::Br);
    invalid(missing, "branch stack types");
}

TEST_CASE("br_if retains excess values on its fall-through edge") {
    for (const i32 condition : {0, 1}) {
        Program p;
        p.op(Opcode::Block); p.F(7.f).I(condition);
        p.op(Opcode::BrIf).op(Opcode::Drop).op(Opcode::End);
        valid(p);
        const auto bytes = buildProgFile(p);
        const auto loaded = loadFile(bytes);
        VM vm(loaded, {});
        REQUIRE_NOTHROW(vm.run());
        CHECK(vm.debugStack().empty());
    }
    Program bad;
    bad.op(Opcode::Block); bad.F(7.f).I(0);
    bad.op(Opcode::BrIf).op(Opcode::End);
    invalid(bad, "stack types differ");
}

TEST_CASE("unreachable arms restore the reachable alternate or branch target") {
    Program p;
    p.op(Opcode::Block); p.I(0); p.op(Opcode::If).op(Opcode::Br, 1);
    p.op(Opcode::Drop).op(Opcode::Else); // dead drop is stack-polymorphic
    p.op(Opcode::End).op(Opcode::End).op(Opcode::Return).op(Opcode::Drop);
    valid(p);

    Program branchExit;
    branchExit.op(Opcode::Block).op(Opcode::Br).op(Opcode::End).op(Opcode::Drop);
    invalid(branchExit, "underflow"); // br reaches end, making drop reachable

    Program loop;
    loop.op(Opcode::Loop).op(Opcode::Br).op(Opcode::End).op(Opcode::Drop);
    valid(loop); // a back-edge never makes the code after an infinite loop live

    Program deadIndex;
    deadIndex.op(Opcode::Return).op(Opcode::LocalGet, 100);
    invalid(deadIndex, "local index");
}

TEST_CASE("else belongs to exactly one if even in dead code") {
    Program block;
    block.op(Opcode::Block).op(Opcode::Else);
    invalid(block, "else without if");
    Program duplicate;
    duplicate.I(0); duplicate.op(Opcode::If).op(Opcode::Else).op(Opcode::Else);
    invalid(duplicate, "duplicate else");
}

TEST_CASE("heterogeneous helper results retain their declared stack order") {
    Program draw;
    draw.op(Opcode::Call, 1).op(Opcode::LocalSet, 0).op(Opcode::Drop);
    Program helper;
    helper.F(1.f).I(2); helper.op(Opcode::Return);
    const auto bytes = fullFile(minimalMeta(),
        constSection({"draw"}, helper.ints, helper.floats, {}),
        codeSection({{0, {}, {}, {0}, draw.body},
                     {kAnonymousName, {}, {1, 0}, {}, helper.body}}, {}));
    const auto loaded = loadFile(bytes);
    REQUIRE_NOTHROW(validate(loaded));
    VM vm(loaded, {});
    REQUIRE_NOTHROW(vm.run());
}
