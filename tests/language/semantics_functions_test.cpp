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

TEST_CASE("break and continue are rejected outside loops") {
    expectCheckError(wrap("break"), "'break' outside a loop");
    expectCheckError(wrap("continue"), "'continue' outside a loop");
    // Inside an if, but still outside any loop: rejected too.
    expectCheckError(wrap("if (1) { break }"), "'break' outside a loop");
    expectCheckError(wrap("if (1) { continue }"), "'continue' outside a loop");
    // Inside a loop, both are fine — also nested in an if.
    CHECK_NOTHROW(checkAsset(parse(wrap(R"(
        i = 0
        while (i < 3) {
          i = i + 1
          if (i == 2) { continue }
          if (i == 3) { break }
        })")),
        {}));
    // In a helper function, the loop depth resets per function body.
    expectCheckError(
        "asset t { size 8x8 draw { }\n"
        "  helper() { break } }",
        "'break' outside a loop");
}

TEST_CASE("for loops are checked") {
    // The bounds must be i32.
    expectCheckError(wrap("for i in 0.0..5.0 { }"), "'for' bounds must be i32");
    expectCheckError(wrap("for i in 0..5.5 { }"), "'for' bounds must be i32");
    // The counter name cannot collide with the asset's I/O or a parameter.
    expectCheckError("asset t { size 8x8 input u32 n = 1 "
                     "draw { for n in 0..2 { } } }",
                     "inputs cannot be loop counters");
    expectCheckError("asset t { size 8x8 output point p "
                     "draw { for p in 0..2 { } } }",
                     "outputs cannot be loop counters");
    expectCheckError(wrap("for v in 0..2 { }"),
                     "'v' is a reserved name");
    expectCheckError(wrap("for seed in 0..2 { }"), "reserved");
    // A matching existing local is reused; a type clash is an error.
    expectCheckError(wrap("x = 1.5\nfor x in 0..2 { }"),
                     "but the loop counter is i32");
    CHECK_NOTHROW(checkAsset(parse(wrap(R"(
        x = 1
        for x in 0..2 { } )")),
        {}));
    // break/continue are loop statements here too.
    CHECK_NOTHROW(checkAsset(parse(wrap(R"(
        for i in 0..3 {
          if (i == 1) { continue }
          if (i == 2) { break }
        })")),
        {}));
}

TEST_CASE("conditions must be integers") {
    expectCheckError(wrap("if (1.5) { x = 1 }"), "condition must be an integer");
    expectCheckError(wrap("while ({1, 2}) { x = 1 }"), "condition must be an integer");
    expectCheckError(wrap("if ({1, 2}) { x = 1 }"), "condition must be an integer");
    CHECK_NOTHROW(checkAsset(parse(wrap("if (1) { x = 2 }")), {}));
}

TEST_CASE("user functions: arity, types, returns, recursion") {
    const std::string valid = R"(
asset t {
  size 8x8
  draw {
    x = double(2)
    rect(0, 0, x, x, #ffffff)
  }
  double(n: i32) -> i32 {
    return n * 2
  }
})";
    CHECK_NOTHROW(checkAsset(parse(valid), {}));

    // Forward calls work (lazy checking); recursion does not.
    const std::string forward = R"(
asset t {
  size 8x8
  draw {
    x = later(1)
  }
  later(n: i32) -> i32 {
    return n + 1
  }
})";
    CHECK_NOTHROW(checkAsset(parse(forward), {}));

    expectCheckError(
        "asset t { size 8x8 draw { x = f(1) } f(n: i32) -> i32 { return f(n) } }",
        "recursive call");
    expectCheckError(
        "asset t { size 8x8 draw { x = f(1, 2) } f(n: i32) -> i32 { return n } }",
        "expects 1 arguments");
    expectCheckError(
        "asset t { size 8x8 draw { f(n = 1) } f(n: i32) -> i32 { return n } }",
        "does not take named arguments");
    expectCheckError(
        "asset t { size 8x8 draw { x = f(1.5) } f(n: i32) -> i32 { return n } }",
        "must be i32");
    expectCheckError(
        "asset t { size 8x8 draw { x = f() } f() { } }",
        "cannot store void");
    // A void function called as a statement is fine.
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 draw { f() } f() { } }"), {}));
}

TEST_CASE("declared return types are checked against the body") {
    // i32 return values widen to a declared f32 result.
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 draw { x = g(a) } "
        "g(n: i32) -> f32 { return n } }"), {}));
    // A value that does not fit the declared type is rejected.
    expectCheckError(
        "asset t { size 8x8 draw { x = f(a) } "
        "f(n: i32) -> i32 { if (n) { return 1 } return 2.5 } }",
        "return type of 'f' is i32, got f32");
    // A returning function must declare its return type (no inference).
    expectCheckError(
        "asset t { size 8x8 draw { x = f(1) } "
        "f(n: i32) { return n } }",
        "declares no return type");
    // A declared return type forbids bare returns.
    expectCheckError(
        "asset t { size 8x8 draw { f(1) } "
        "f(n: i32) -> i32 { return } }",
        "has no value");
    // draw cannot declare a return type.
    expectCheckError(
        "asset t { size 8x8 draw -> i32 { return 1 } }",
        "'draw' cannot return a value");
    expectCheckError(
        "asset t { size 8x8 draw(v: f32, a: i32) { } }",
        "takes no declared parameters");
}

TEST_CASE("typed parameters drive call-site validation") {
    const std::string source = R"(
asset t {
  size 8x8
  draw {
    x = h(1)         // an i32 argument widens to the f32 parameter
    y = h(1.5)
    c = tint(#ff0000)
  }
  h(u: f32) -> f32 {
    return u
  }
  tint(c: color) -> color {
    return color.shade(c, 1)
  }
})";
    CHECK_NOTHROW(checkAsset(parse(source), {}));

    expectCheckError(
        "asset t { size 8x8 draw { x = h(#ff0000) } "
        "h(u: f32) -> f32 { return u } }",
        "argument 1 of 'h' must be f32, got color");
    expectCheckError(
        "asset t { size 8x8 draw { c = tint(1) } "
        "tint(c: color) -> color { return c } }",
        "argument 1 of 'tint' must be color, got i32");
}

TEST_CASE("loop control cannot escape into a lazily checked helper") {
    for (const std::string control : {"break", "continue"}) {
        for (const std::string loop : {"while (1)", "for i in 0..2"}) {
            for (const bool helperFirst : {false, true}) {
                INFO(control << ", " << loop << ", helper first " << helperFirst);
                const std::string helper = "helper() { " + control + " }";
                const std::string draw = "draw { " + loop +
                                         " { helper() break } }";
                const std::string source = "asset t { size 8x8 " +
                    (helperFirst ? helper + draw : draw + helper) + " }";
                const std::string message = "'" + control + "' outside a loop";
                expectCheckError(source, {}, message.c_str());
            }
        }
    }
    // Checking a valid helper must also restore the caller's loop context.
    CHECK_NOTHROW(checkAsset(parse(R"(
asset t {
  size 8x8
  draw { while (1) { helper() break } }
  helper() { for i in 0..2 { continue } }
})"), {}));
}
