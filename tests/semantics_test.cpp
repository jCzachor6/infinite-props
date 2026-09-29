#include <catch2/catch_test_macros.hpp>

#include <map>
#include <stdexcept>
#include <string>

#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"
#include "ips/Types.hpp"

// Semantic analysis: name resolution, type checking, integer-only
// conditions, part-call arity and slot kinds, import-DAG resolution.

using namespace iprop::ips;

namespace {

// A valid asset using the VM builtins (the rock sketch with stone_mass
// replaced by rect, which is a real VM primitive).
const std::string kValidRock = R"(
asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect collider

  draw(v, a) {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    rect(-w * 0.10, 0, w * 0.87, h, palette)
    if (index(3) == 0) {
      rect(-w * 0.67, 2, w * 0.39, h * 0.43, shade(palette, -4))
    }
  }
})";

const std::string kHairSource = R"(
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  input f32   volume = 1.0
  output point top

  draw(v, a) {
    top = {32, 4}
  }
})";

const std::string kPortraitSource = R"(
import hair from "hair.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  input  color hairColor = #33291f
  output point pupilCenter

  draw(v, a) {
    rect(0, 0, 64, 64, skin)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    pupilCenter = hair.outputs.top
  }
})";

std::map<std::string, PartInfo> hairParts() {
    const Program hair = parse(kHairSource);
    return {{"hair", partInfoOf(hair.asset)}};
}

// Checks `source` and expects a SourceError mentioning `messagePart`.
void expectCheckError(const std::string& source,
                      const std::map<std::string, PartInfo>& parts,
                      const char* messagePart) {
    bool threw = false;
    try {
        checkAsset(parse(source), parts);
    } catch (const SourceError& error) {
        threw = true;
        INFO(error.message);
        REQUIRE(std::string(error.message).find(messagePart) !=
                std::string::npos);
    }
    REQUIRE(threw);
}

void expectCheckError(const std::string& source, const char* messagePart) {
    expectCheckError(source, {}, messagePart);
}

std::string wrap(const std::string& body) {
    return "asset t { size 8x8 draw(v, a) { " + body + " } }";
}

} // namespace

TEST_CASE("valid programs check cleanly") {
    CHECK_NOTHROW(checkAsset(parse(kValidRock), {}));
    CHECK_NOTHROW(checkAsset(parse(kPortraitSource), hairParts()));
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
    expectCheckError("asset t { size 8x8 input u32 seed = 1 draw(v, a) { } }",
                     "reserved");
    // Reading seed is fine.
    CHECK_NOTHROW(checkAsset(parse(wrap("x = seed ^ 3")), {}));
}

TEST_CASE("input default values must match the declared type") {
    expectCheckError(
        "asset t { size 8x8 input color c = 5 draw(v, a) { } }",
        "default value");
    expectCheckError(
        "asset t { size 8x8 input f32 f = #808d74 draw(v, a) { } }",
        "default value");
    expectCheckError(
        "asset t { size 8x8 input i32 i = 2.5 draw(v, a) { } }",
        "default value");
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 input color c = #808d74 input f32 f = 2.5 "
        "input u32 u = 7 input i32 i = 3 draw(v, a) { } }"),
        {}));
}

TEST_CASE("type mismatches are rejected") {
    expectCheckError(wrap("c = #33291f\nx = c + 1"), "arithmetic requires numeric");
    expectCheckError(wrap("p = {1, 2}\nq = p * 2"), "cannot store point");
    expectCheckError(wrap("x = #33291f * 2"), "arithmetic requires numeric");
    expectCheckError(wrap("x = {1, 2} + 1"), "arithmetic requires numeric");
    expectCheckError(wrap("x = -#33291f"), "unary minus requires a numeric");
    expectCheckError(wrap("x = {1, 2, 3} == {4, 5, 6}"),
                     "comparison requires numeric");
    expectCheckError(wrap("x = 1 && 2.5"), "integer operation requires i32");
    expectCheckError(wrap("x = \"text\""), "string literals");
}

TEST_CASE("conditions must be integers") {
    expectCheckError(wrap("if (1.5) { x = 1 }"), "condition must be an integer");
    expectCheckError(wrap("while ({1, 2}) { x = 1 }"), "condition must be an integer");
    expectCheckError(wrap("if ({1, 2}) { x = 1 }"), "condition must be an integer");
    CHECK_NOTHROW(checkAsset(parse(wrap("if (1) { x = 2 }")), {}));
}

TEST_CASE("vector arity errors") {
    expectCheckError(wrap("x = {1}"), "2 (point), 3 (circle) or 4 (rect)");
    expectCheckError(wrap("x = {1, 2, 3, 4, 5}"), "2 (point), 3 (circle)");
    expectCheckError(wrap("x = {1, #808d74}"), "vector elements must be numeric");
}

TEST_CASE("output slot kinds are checked") {
    expectCheckError(
        "asset t { size 8x8 output rect r draw(v, a) { r = {1, 2} } }",
        "expects rect, got point");
    expectCheckError(
        "asset t { size 8x8 output point p draw(v, a) { p = {1, 2, 3, 4} } }",
        "expects point, got rect");
    expectCheckError(
        "asset t { size 8x8 output point p draw(v, a) { p = 5 } }",
        "expects point, got i32");
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 output point p draw(v, a) { p = {1, 2} } }"),
        {}));
}

TEST_CASE("locals cannot hold compounds and are typed on first assignment") {
    expectCheckError(wrap("p = {1, 2}"), "cannot store point in local 'p'");
    expectCheckError(wrap("x = 1\nx = 2.5"), "local 'x' is i32");
    expectCheckError(wrap("x = 2.5\nx = 1"), "local 'x' is f32");
    CHECK_NOTHROW(checkAsset(parse(wrap("x = 1\nx = 2\ny = 2.5\ny = 3.5")), {}));
}

TEST_CASE("duplicate declarations and missing requirements") {
    expectCheckError("asset t { size 8x8 input color c = #ffffff input color c = #000000 "
                     "draw(v, a) { } }", "duplicate input 'c'");
    expectCheckError("asset t { size 8x8 output point p output point p "
                     "draw(v, a) { } }", "duplicate output 'p'");
    expectCheckError("asset t { size 8x8 input color p = #ffffff output point p "
                     "draw(v, a) { } }", "collides with an input");
    expectCheckError("asset t { size 8x8 draw(v, a) { } helper(x) { return 1 } "
                     "helper(x) { return 2 } }", "duplicate function");
    expectCheckError("import a from \"a.ips\" import a from \"a.ips\" "
                     "asset t { size 8x8 draw(v, a) { } }",
                     {{"a", {}}}, "duplicate import alias");
    expectCheckError("asset t { draw(v, a) { } }", "missing 'size'");
    expectCheckError("asset t { size 8x8 }", "missing 'draw");
    expectCheckError("asset t { size 8x8 draw(v) { } }", "exactly two parameters");
    expectCheckError("asset t { size 8x8 input color at = #ffffff draw(v, a) { } }",
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

TEST_CASE("user functions: arity, types, returns, recursion") {
    const std::string valid = R"(
asset t {
  size 8x8
  draw(v, a) {
    x = double(2)
    rect(0, 0, x, x, #ffffff)
  }
  double(n) {
    return n * 2
  }
})";
    CHECK_NOTHROW(checkAsset(parse(valid), {}));

    // Forward calls work (lazy checking); recursion does not.
    const std::string forward = R"(
asset t {
  size 8x8
  draw(v, a) {
    x = later(1)
  }
  later(n) {
    return n + 1
  }
})";
    CHECK_NOTHROW(checkAsset(parse(forward), {}));

    expectCheckError(
        "asset t { size 8x8 draw(v, a) { x = f(1) } f(n) { return f(n) } }",
        "recursive call");
    expectCheckError(
        "asset t { size 8x8 draw(v, a) { x = f(1, 2) } f(n) { return n } }",
        "expects 1 arguments");
    expectCheckError(
        "asset t { size 8x8 draw(v, a) { f(n = 1) } f(n) { return n } }",
        "does not take named arguments");
    expectCheckError(
        "asset t { size 8x8 draw(v, a) { x = f(1.5) } f(n) { return n } }",
        "must be i32");
    expectCheckError(
        "asset t { size 8x8 draw(v, a) { x = f() } f() { } }",
        "cannot store void");
    // A void function called as a statement is fine.
    CHECK_NOTHROW(checkAsset(parse(
        "asset t { size 8x8 draw(v, a) { f() } f() { } }"), {}));
}

TEST_CASE("inconsistent return types are rejected") {
    expectCheckError(
        "asset t { size 8x8 draw(v, a) { x = f(a) } "
        "f(n) { if (n) { return 1 } return 2.5 } }",
        "inconsistent return type");
}

TEST_CASE("part calls are validated against the part surface") {
    CHECK_NOTHROW(checkAsset(parse(kPortraitSource), hairParts()));

    // Wrong positional arity.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw(v, a) { hair.draw(1, 2, at = {0, 0}) } }",
        hairParts(), "exactly three positional");
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw(v, a) { hair.draw(1, 2, 3, 4, at = {0, 0}) } }",
        hairParts(), "exactly three positional");

    // Missing `at`.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw(v, a) { hair.draw(1, 2, 3) } }",
        hairParts(), "requires an 'at");

    // Wrong `at` type.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw(v, a) { hair.draw(1, 2, 3, at = 5) } }",
        hairParts(), "'at' must be a point");

    // Unknown uniform.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw(v, a) { hair.draw(1, 2, 3, at = {0, 0}, bogus = 1) } }",
        hairParts(), "has no input 'bogus'");

    // Uniform of the wrong type.
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 "
        "draw(v, a) { hair.draw(1, 2, 3, at = {0, 0}, volume = #ffffff) } }",
        hairParts(), "must be f32, got color");

    // Unknown part.
    expectCheckError(
        "import nope from \"nope.ips\"\nasset t { size 8x8 "
        "draw(v, a) { nope.draw(1, 2, 3, at = {0, 0}) } }",
        "was not provided");
}

TEST_CASE("part output reads are validated") {
    CHECK_NOTHROW(checkAsset(parse(kPortraitSource), hairParts()));

    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 output point p "
        "draw(v, a) { hair.draw(1, 2, 3, at = {0, 0}) p = hair.outputs.nope } }",
        hairParts(), "has no output 'nope'");
    expectCheckError(
        "import hair from \"hair.ips\"\nasset t { size 8x8 output rect r "
        "draw(v, a) { hair.draw(1, 2, 3, at = {0, 0}) r = hair.outputs.top } }",
        hairParts(), "expects rect, got point");
    expectCheckError(
        "asset t { size 8x8 draw(v, a) { x = bogus.outputs.p } }",
        "not an imported part");

    // List outputs cannot be read.
    const std::string listPart = R"(
asset lp {
  size 8x8
  output list point pts
  draw(v, a) {
    pts = {1, 2}
  }
})";
    const Program listProgram = parse(listPart);
    const std::map<std::string, PartInfo> listParts = {
        {"lp", partInfoOf(listProgram.asset)}};
    expectCheckError(
        "import lp from \"lp.ips\"\nasset t { size 8x8 output point p "
        "draw(v, a) { lp.draw(1, 2, 3, at = {0, 0}) p = lp.outputs.pts } }",
        listParts, "cannot read list output");
}

TEST_CASE("partInfoOf builds a part surface") {
    const Program hair = parse(kHairSource);
    const PartInfo info = partInfoOf(hair.asset);
    REQUIRE(info.inputs.size() == 2);
    REQUIRE(info.inputs[0].name == "color");
    REQUIRE(info.inputs[0].type == InputType::Color);
    REQUIRE(info.inputs[1].name == "volume");
    REQUIRE(info.inputs[1].type == InputType::F32);
    REQUIRE(info.outputs.size() == 1);
    REQUIRE(info.outputs[0].name == "top");
    REQUIRE(info.outputs[0].kind == OutputKind::Point);
    REQUIRE(!info.outputs[0].isList);
}

TEST_CASE("import cycles are rejected with the cycle path") {
    const std::map<std::string, std::string> files = {
        {"a.ips", "import b from \"b.ips\"\nasset a { size 8x8 draw(v, a) { } }"},
        {"b.ips", "import a from \"a.ips\"\nasset b { size 8x8 draw(v, a) { } }"},
    };
    const auto loader = [&](std::string_view path) {
        const auto found = files.find(std::string(path));
        if (found == files.end()) {
            throw std::runtime_error("no such file");
        }
        return found->second;
    };

    bool threw = false;
    try {
        (void)resolveImports("a.ips", loader);
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("import cycle") !=
                std::string::npos);
        REQUIRE(std::string(error.message).find("a.ips") != std::string::npos);
        REQUIRE(std::string(error.message).find("b.ips") != std::string::npos);
    }
    REQUIRE(threw);

    // A three-file cycle not passing through the entry.
    const std::map<std::string, std::string> three = {
        {"x.ips", "import y from \"y.ips\"\nasset x { size 8x8 draw(v, a) { } }"},
        {"y.ips", "import z from \"z.ips\"\nasset y { size 8x8 draw(v, a) { } }"},
        {"z.ips", "import y from \"y.ips\"\nasset z { size 8x8 draw(v, a) { } }"},
    };
    const auto loader3 = [&](std::string_view path) {
        const auto found = three.find(std::string(path));
        if (found == three.end()) {
            throw std::runtime_error("no such file");
        }
        return found->second;
    };
    bool threw3 = false;
    try {
        (void)resolveImports("x.ips", loader3);
    } catch (const SourceError& error) {
        threw3 = true;
        REQUIRE(std::string(error.message).find("y.ips -> z.ips -> y.ips") !=
                std::string::npos);
    }
    REQUIRE(threw3);
}

TEST_CASE("import resolution returns dependencies first and reports bad files") {
    const std::map<std::string, std::string> files = {
        {"a.ips", "import b from \"b.ips\"\nimport c from \"c.ips\"\n"
                  "asset a { size 8x8 draw(v, a) { } }"},
        {"b.ips", "import c from \"c.ips\"\nasset b { size 8x8 draw(v, a) { } }"},
        {"c.ips", "asset c { size 8x8 draw(v, a) { } }"},
    };
    const auto loader = [&](std::string_view path) {
        const auto found = files.find(std::string(path));
        if (found == files.end()) {
            throw std::runtime_error("no such file");
        }
        return found->second;
    };

    const auto order = resolveImports("a.ips", loader);
    REQUIRE(order == std::vector<std::string>{"c.ips", "b.ips", "a.ips"});

    const auto badLoader = [](std::string_view) -> std::string {
        throw std::runtime_error("boom");
    };
    bool threw = false;
    try {
        (void)resolveImports("missing.ips", badLoader);
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("cannot load") !=
                std::string::npos);
        REQUIRE(std::string(error.message).find("boom") != std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("type helpers behave") {
    REQUIRE(std::string(typeName(ValueType::I32)) == "i32");
    REQUIRE(isNumeric(ValueType::I32));
    REQUIRE(isNumeric(ValueType::F32));
    REQUIRE(!isNumeric(ValueType::Color));
    REQUIRE(isStorable(ValueType::Color));
    REQUIRE(!isStorable(ValueType::Point));
    REQUIRE(promote(ValueType::I32, ValueType::I32) == ValueType::I32);
    REQUIRE(promote(ValueType::I32, ValueType::F32) == ValueType::F32);
    REQUIRE(argFits(ValueType::F32, ValueType::I32));
    REQUIRE(!argFits(ValueType::I32, ValueType::F32));
}
