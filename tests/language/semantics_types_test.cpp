#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>

#include "ips/Imports.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ips/Types.hpp"
#include "support/semantic_support.hpp"

// Semantic analysis: name resolution, type checking, integer-only
// conditions, part-call arity and slot kinds, import-DAG resolution.

using namespace iprop::ips;

using namespace semantic_test;

TEST_CASE("palette declarations are checked") {
    // A homogeneous palette of colours indexing cleanly.
    CHECK_NOTHROW(checkAsset(parse(R"(
asset t {
  size 8x8
  palette leafDark = [#365c38, #4c603e, #264a46]
  draw(v: i32, a: i32) {
    c = leafDark[0]
  }
})"), {}));

    // i32 and f32 palettes work too.
    CHECK_NOTHROW(checkAsset(parse(R"(
asset t {
  size 8x8
  palette weights = [3, 1, 2]
  palette levels = [0.5, 1.0]
  draw(v: i32, a: i32) {
    w = weights[v]
    l = levels[0]
  }
})"), {}));

    expectCheckError(
        "asset t { size 8x8 palette p = [] draw(v: i32, a: i32) { } }",
        "needs at least one element");
    expectCheckError(
        "asset t { size 8x8 palette p = [#111111, 2] draw(v: i32, a: i32) { } }",
        "elements of one type");
    expectCheckError(
        "asset t { size 8x8 palette p = [#111111 + #000000] draw(v: i32, a: i32) { } }",
        "must be literals");
    expectCheckError(
        "asset t { size 8x8 palette p = [\"x\"] draw(v: i32, a: i32) { } }",
        "string elements");
    expectCheckError(
        "asset t { size 8x8 palette p = [#111111] palette p = [#222222] draw(v: i32, a: i32) { } }",
        "duplicate palette 'p'");
    expectCheckError(
        "asset t { size 8x8 palette seed = [#111111] draw(v: i32, a: i32) { } }",
        "reserved name");
    expectCheckError(
        "asset t { size 8x8 input color c = #ffffff palette c = [#111111] draw(v: i32, a: i32) { } }",
        "collides with an input or output");
    expectCheckError(
        "asset t { size 8x8 palette f = [#111111] draw(v: i32, a: i32) { } f(x: i32) { return x } }",
        "collides with a function");
}

TEST_CASE("palette indexing is type checked") {
    CHECK_NOTHROW(checkAsset(parse(R"(
asset t {
  size 8x8
  palette leafDark = [#365c38, #4c603e, #264a46]
  draw(v: i32, a: i32) {
    c = leafDark[v]
    d = leafDark[1 + 1]
  }
})"), {}));

    expectCheckError(R"(
asset t { size 8x8 palette p = [#111111] draw(v: i32, a: i32) { c = p[0.5] } })",
        "palette index must be i32");
    expectCheckError(wrap("c = missing[0]"), "'missing' is not a palette");
    expectCheckError(R"(
asset t { size 8x8 palette p = [#111111] draw(v: i32, a: i32) { c = p } })",
        "must be indexed");
    expectCheckError(wrap("c = 5[0]"), "only palettes can be indexed");
    // A local cannot shadow a palette.
    expectCheckError(R"(
asset t { size 8x8 palette p = [1, 2] draw(v: i32, a: i32) { p = 3 } })",
        "cannot assign to palette 'p'");
    // The element type flows into the assigned local.
    expectCheckError(R"(
asset t { size 8x8 palette p = [1, 2] draw(v: i32, a: i32) { c = p[0] c = 0.5 } })",
        "local 'c' is i32");
}

TEST_CASE("ramp is checked") {
    expectCheckError(
        "asset t { size 8x8 draw(v: i32, a: i32) { x = ramp(nope, 0.5) } }",
        "is not a palette");
    expectCheckError(
        "asset t { size 8x8 palette p = [1, 2] draw(v: i32, a: i32) { x = ramp(p, 0.5) } }",
        "ramp requires a color or f32 palette");
    expectCheckError(
        "asset t { size 8x8 palette p = [1.0] draw(v: i32, a: i32) { x = ramp(p, #ffffff) } }",
        "ramp's t must be numeric");
    for (const std::string arguments : {"palette = p, t = 0.5", "t = p, palette = 0.5",
                                        "p, t = 0.5", "p, typo = 0.5"}) {
        CAPTURE(arguments);
        expectCheckError(
            "asset t { size 8x8 palette p = [1.0] "
            "draw(v: i32, a: i32) { x = ramp(" + arguments + ") } }",
            "does not take named arguments");
    }
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 palette p = [1.0, 2.0] "
        "draw(v: i32, a: i32) { x = ramp(p, 0.5) } }"),
        {}));
}

TEST_CASE("undefined names and functions are rejected with locations") {
    bool threw = false;
    try {
        checkAsset(parse(wrap("x = missing_var")), {});
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("undefined name 'missing_var'") !=
                std::string::npos);
        REQUIRE(error.location.line == 1);
        REQUIRE(error.location.column > 20); // points at the use
    }
    REQUIRE(threw);

    expectCheckError(wrap("x = missing_fn(1)"), "undefined function");
    expectCheckError(wrap("v = 5"), "cannot assign to parameter");
    expectCheckError(wrap("seed = 5"), "reserved");
}

TEST_CASE("the seed builtin is read-only via inputs") {
    // `seed` alone resolves to the builtin; assigning to it is only possible
    // if the asset declares an input named seed — which is reserved.
    expectCheckError("asset t { size 8x8 input u32 seed = 1 draw(v: i32, a: i32) { } }",
                     "reserved");
    // Reading seed is fine.
    CHECK_NOTHROW(checkAsset(parse(wrap("x = seed ^ 3")), {}));
}

TEST_CASE("input default values must match the declared type") {
    expectCheckError(
        "asset t { size 8x8 input color c = 5 draw(v: i32, a: i32) { } }",
        "default value");
    expectCheckError(
        "asset t { size 8x8 input f32 f = #808d74 draw(v: i32, a: i32) { } }",
        "default value");
    expectCheckError(
        "asset t { size 8x8 input i32 i = 2.5 draw(v: i32, a: i32) { } }",
        "default value");
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 input color c = #808d74 input f32 f = 2.5 "
        "input u32 u = 7 input i32 i = 3 draw(v: i32, a: i32) { } }"),
        {}));
}

TEST_CASE("type mismatches are rejected") {
    expectCheckError(wrap("c = #33291f\nx = c + 1"), "arithmetic requires numeric");
    expectCheckError(wrap("p = {1, 2}\nq = p % 2"), "'%' requires i32");
    expectCheckError(wrap("x = #33291f * 2"), "arithmetic requires numeric");
    expectCheckError(wrap("x = {1, 2, 3} + 1"), "arithmetic requires numeric");
    expectCheckError(wrap("x = -#33291f"), "unary minus requires a numeric");
    expectCheckError(wrap("x = {1, 2, 3} == {4, 5, 6}"),
                     "comparison requires numeric");
    expectCheckError(wrap("x = 1 && 2.5"), "integer operation requires i32");
    expectCheckError(wrap("x = 2.5 % 2"), "'%' requires i32");
    expectCheckError(wrap("x = 2.5 << 1"), "integer operation requires i32");
    expectCheckError(wrap("x = i32trunc(#ffffff)"), "must be f32");
    CHECK_NOTHROW(checkAsset(parse(wrap("x = 7 >> 1\ny = 1 << 3\nz = i32trunc(2.9)\nr = round(1.5)")), {}));
    CHECK_NOTHROW(checkAsset(parse(wrap("x = 7 % 3")), {}));
    expectCheckError(wrap("x = \"text\""), "string literals");
}

TEST_CASE("unsupported vector-scalar operators are rejected by the checker") {
    for (const std::string scalar : {"2", "2.0"}) {
        for (const std::string op : {"+", "-"}) {
            INFO("operator " << op << ", scalar " << scalar);
            const std::string message = "operator '" + op + "' does not support";
            expectCheckError(wrap("p = {4, 8} q = p " + op + " " + scalar),
                             message.c_str());
            expectCheckError(wrap("p = {4, 8} q = " + scalar + " " + op + " p"),
                             message.c_str());
        }
        expectCheckError(wrap("p = {4, 8} q = " + scalar + " / p"),
                         "operator '/' does not support");
    }
    expectCheckError(wrap("p = {4, 8} q = p * #ffffff"), "does not support");
    expectCheckError(wrap("p = {4, 8} q = #ffffff / p"), "does not support");
}

TEST_CASE("vector arity errors") {
    expectCheckError(wrap("x = {1}"), "2 (vec2), 3 (circle) or 4 (rect)");
    expectCheckError(wrap("x = {1, 2, 3, 4, 5}"), "2 (vec2), 3 (circle)");
    expectCheckError(wrap("x = {1, #808d74}"), "vector elements must be numeric");
}

TEST_CASE("output slot kinds are checked") {
    expectCheckError(
        "asset t { size 8x8 output rect r draw(v: i32, a: i32) { r = {1, 2} } }",
        "expects rect, got vec2");
    expectCheckError(
        "asset t { size 8x8 output point p draw(v: i32, a: i32) { p = {1, 2, 3, 4} } }",
        "expects vec2, got rect");
    expectCheckError(
        "asset t { size 8x8 output point p draw(v: i32, a: i32) { p = 5 } }",
        "expects vec2, got i32");
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 output point p draw(v: i32, a: i32) { p = {1, 2} } }"),
        {}));
}

TEST_CASE("locals cannot hold compounds and are typed on first assignment") {
    expectCheckError(wrap("p = {1, 2, 3}"), "cannot store circle in local 'p'");
    CHECK_NOTHROW(checkAsset(parse(wrap("p = {1, 2}\nq = p.x + p.y")), {}));
    expectCheckError(wrap("x = 1\nx = 2.5"), "local 'x' is i32");
    expectCheckError(wrap("x = 2.5\nx = 1"), "local 'x' is f32");
    CHECK_NOTHROW(checkAsset(parse(wrap("x = 1\nx = 2\ny = 2.5\ny = 3.5")), {}));
}

TEST_CASE("duplicate declarations and missing requirements") {
    expectCheckError("asset t { size 8x8 input color c = #ffffff input color c = #000000 "
                     "draw(v: i32, a: i32) { } }", "duplicate input 'c'");
    expectCheckError("asset t { size 8x8 output point p output point p "
                     "draw(v: i32, a: i32) { } }", "duplicate output 'p'");
    expectCheckError("asset t { size 8x8 input color p = #ffffff output point p "
                     "draw(v: i32, a: i32) { } }", "collides with an input");
    expectCheckError("asset t { size 8x8 draw(v: i32, a: i32) { } helper(x: i32) { return 1 } "
                     "helper(x: i32) { return 2 } }", "duplicate function");
    expectCheckError("import a from \"a.ips\" import a from \"a.ips\" "
                     "asset t { size 8x8 draw(v: i32, a: i32) { } }",
                     {{"a", {}}}, "duplicate import alias");
    expectCheckError("asset t { draw(v: i32, a: i32) { } }", "missing 'size'");
    expectCheckError("asset t { size 8x8 }", "missing 'draw");
    expectCheckError("asset t { size 8x8 draw(v: i32) { } }", "exactly two parameters");
    expectCheckError("asset t { size 8x8 input color at = #ffffff draw(v: i32, a: i32) { } }",
                     "reserved");
}

TEST_CASE("builtin arity and argument types are checked") {
    expectCheckError(wrap("rect(0, 0, 4, 4)"), "expects 5 arguments");
    expectCheckError(wrap("range(1)"), "expects 2 arguments");
    expectCheckError(wrap("shade(5, -2)"), "must be color, got i32");
    expectCheckError(wrap("pixel(0.5, 0, #ffffff)"), "must be i32, got f32");
    expectCheckError(wrap("hash(1, 2)"), "expects 1 arguments");
    expectCheckError(wrap("rect(0, 0, 4, 4, #ffffff, 1)"), "expects 5 arguments");
    // i32 widens to f32 parameters.
    CHECK_NOTHROW(checkAsset(parse(wrap("rect(0, 0, 4, 4, #ffffff)")), {}));
    CHECK_NOTHROW(checkAsset(parse(wrap("x = range(1, 2)")), {}));
}

TEST_CASE("poly validates its vertex pairs") {
    expectCheckError(wrap("poly(#ffffff, 0, 0, 1, 1)"), "at least three");
    expectCheckError(wrap("poly(#ffffff, 0, 0, 1, 1, 2, 2, 3)"), "at least three");
    expectCheckError(wrap("poly(1, 0, 0, 1, 1, 2, 2)"), "must be color");
    expectCheckError(wrap("poly(#ffffff, 0, 0, 1, 1, 2, c = 2)"), "named arguments");
    CHECK_NOTHROW(checkAsset(
        parse(wrap("poly(#ffffff, 0, 0, 1, 0, 0, 1)")), {}));
}

TEST_CASE("type helpers behave") {
    REQUIRE(std::string(typeName(ValueType::I32)) == "i32");
    REQUIRE(isNumeric(ValueType::I32));
    REQUIRE(isNumeric(ValueType::F32));
    REQUIRE(!isNumeric(ValueType::Color));
    REQUIRE(isStorable(ValueType::Color));
    REQUIRE(isStorable(ValueType::Vec2));
    REQUIRE(!isStorable(ValueType::Rect));
    REQUIRE(promote(ValueType::I32, ValueType::I32) == ValueType::I32);
    REQUIRE(promote(ValueType::I32, ValueType::F32) == ValueType::F32);
    REQUIRE(argFits(ValueType::F32, ValueType::I32));
    REQUIRE(!argFits(ValueType::I32, ValueType::F32));
}
