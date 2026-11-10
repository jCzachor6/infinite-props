#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"
#include "support/compiler_support.hpp"
#include "support/semantic_support.hpp"

// One rule per construct: the cases here used to behave differently from a
// sibling construct, or failed with a message about something else.

using namespace iprop::ips;
using namespace semantic_test;

namespace {
std::string parseError(const std::string& source) {
    try { (void)parse(source); } catch (const SourceError& error) { return error.message; }
    return {};
}
}

TEST_CASE("a word cannot be attached to a number") {
    CHECK(parseError(wrap("x = 3px")) == "unexpected 'px' directly after a number");
    CHECK(parseError(wrap("x = 1.5px")) == "unexpected 'px' directly after a number");
    CHECK(parseError("asset t { size 8x8 input i32 n = 3helper() { } draw { } }") ==
          "unexpected 'helper' directly after a number");
    // The size spelling and a separated word are unaffected.
    CHECK_NOTHROW(parse("asset t { size 16x16 draw { for i in 0..3 { } x = 1 to 3 } }"));
}

TEST_CASE("a call on a value names the function to use instead") {
    expectCheckError(wrap("v = {3, 4} x = v.len()"),
        "vec2 has no method 'len'; write vec2.len(...) with the value as its first argument");
    expectCheckError(wrap("x = [1, 2].len()"),
        "list<i32> has no method 'len'; write list.len(...) with the value as its first argument");
    expectCheckError(wrap("x = 1.5 y = x.abs()"), "f32 has no method 'abs'; write abs(...) with the value as an argument");
    expectCheckError(wrap("x = unit() x.foo()"), "f32 has no method 'foo'");
    // A name that is not a value is still looked up as an imported part.
    expectCheckError(wrap("missing.draw(1, at = {0, 0})"), "'missing' is not an imported part");
}

TEST_CASE("an output is write-only and says so") {
    expectCheckError("asset t { size 8x8 output point p draw { p = {1, 2} x = p } }",
        "output 'p' is write-only; keep the value in a local to read it");
}

TEST_CASE("storage and return messages describe every value type") {
    expectCheckError("asset t { size 8x8 draw { x = helper() } helper() { } }",
        "cannot store void in local 'x'; the call returns no value");
    expectCheckError(wrap("v = {1, 2, 3}"), "cannot store circle in local 'v'; it can only be assigned to an output");
    expectCheckError("asset t { size 8x8 draw { helper() } helper() { return [1.5] } }",
        "declares no return type; add '-> list<f32>' after its parameters");
    CHECK(parseError("asset t { size 8x8 const u32 c = 1 draw { } }") == "u32 is only an input type; use i32 here");
}

TEST_CASE("the local limit is a located source error") {
    std::string locals;
    for (int i = 0; i < 1100; ++i) locals += "a" + std::to_string(i) + " = v ";
    compiler_test::rejectSource("asset t { input i32 v = 1 size 8x8 draw { " + locals + "} }",
        "function 'draw' has more than 1024 parameters and locals");
}
