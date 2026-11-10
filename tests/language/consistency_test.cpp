#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <map>
#include <string>
#include <utility>

#include "Compiler.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
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

TEST_CASE("a helper cannot take the name of a builtin function") {
    for (const std::string name : {"rect", "sqrt", "pick", "choose", "poly", "ramp", "gaussian", "regions", "lerp"}) {
        INFO(name);
        expectCheckError("asset t { size 8x8 draw { } " + name + "(value: i32) { } }",
            ("function '" + name + "' has the name of a builtin; choose another name").c_str());
    }
    // Compiling without the checker reports it too, instead of leaving the helper unused.
    CHECK_THROWS_WITH(iprop::compileProgram(parse("asset t { size 8x8 draw { rect(0, 0, 1, 1, #fff) } rect(value: i32) { } }"), {}),
                      "function 'rect' has the name of a builtin; choose another name");
    // A namespace is not a function, and a builtin's name is free for a value.
    CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 draw { rect = 2 noise = rect + 1 x = noise + helper(rect) } "
                                   "helper(pick: i32) -> i32 { return pick } }"), {}));
    CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 draw { x = diagram(2) + part(1) } "
                                   "diagram(v: i32) -> i32 { return v } part(v: i32) -> i32 { return v } }"), {}));
}

TEST_CASE("an asset's declarations share one namespace") {
    const std::map<std::string, PartInfo> child{{"a", {}}};
    expectCheckError("asset t { size 8x8 input f32 a = 1 draw { } a() { } }", "function 'a' has the same name as an input");
    expectCheckError("asset t { size 8x8 output point a draw { } a() { } }", "function 'a' has the same name as an output");
    expectCheckError("asset t { size 8x8 palette a = [1] draw { } a() { } }", "palette 'a' has the same name as a function");
    expectCheckError("asset t { size 8x8 input f32 a = 1 output point a draw { } }", "output 'a' has the same name as an input");
    expectCheckError("asset t { size 8x8 output point a palette a = [1] draw { } }", "palette 'a' has the same name as an output");
    expectCheckError("asset t { size 8x8 const i32 a = 1 draw { } a() { } }", "const 'a': name collides with an asset declaration");
    for (const std::string declaration : {"input f32 a = 1", "output point a", "palette a = [1]", "a() { }"}) {
        INFO(declaration);
        expectCheckError("import a from \"a.ips\" asset t { size 8x8 " + declaration + " draw { } }", child,
                         "has the same name as an import alias");
    }
    expectCheckError("import a from \"a.ips\" asset t { size 8x8 const i32 a = 1 draw { } }", child,
                     "const 'a': name collides with an asset declaration");
}

TEST_CASE("seed and draw cannot be declared; placement names are reserved for inputs only") {
    const std::map<std::string, PartInfo> child{{"seed", {}}};
    for (const std::string source : {
            "asset t { size 8x8 input i32 seed = 1 draw { } }",
            "asset t { size 8x8 output point seed draw { } }",
            "asset t { size 8x8 palette seed = [1] draw { } }",
            "asset t { size 8x8 draw { } seed() { } }",
            "asset t { size 8x8 draw { } helper(seed: i32) { } }",
            "asset t { size 8x8 draw { seed = 1 } }",
            "asset t { size 8x8 draw { for seed in 0..2 { } } }"}) {
        INFO(source);
        expectCheckError(source, "'seed' is a reserved name");
    }
    expectCheckError("import seed from \"seed.ips\" asset t { size 8x8 draw { } }", child, "'seed' is a reserved name");
    expectCheckError("asset t { size 8x8 const i32 seed = 1 draw { } }", "const 'seed': reserved name");
    for (const std::string declaration : {"input f32 draw = 1", "output point draw", "palette draw = [1]"}) {
        INFO(declaration);
        expectCheckError("asset t { size 8x8 " + declaration + " draw { } }", "'draw' is a reserved name");
    }
    // A part call binds these to its placement, so only an input can clash.
    for (const std::string name : {"at", "rot", "mirror_x", "mirror_y"}) {
        INFO(name);
        expectCheckError("asset t { size 8x8 input i32 " + name + " = 0 draw { } }", "is a reserved name");
        CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 const i32 " + name + " = 1 draw { x = " + name + " } }"), {}));
        CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 palette " + name + " = [1] output point p draw { p = {" +
                                       name + "[0], 0} } }"), {}));
        CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 draw { " + name + " = 1 x = " + name + " } }"), {}));
    }
}

TEST_CASE("an import alias cannot be a builtin namespace") {
    for (const std::string name : {"noise", "vec2", "dist", "cell", "diagram", "regions", "part",
                                   "parts", "voronoi", "contour", "surface"}) {
        INFO(name);
        const std::map<std::string, PartInfo> child{{name, {}}};
        expectCheckError("import " + name + " from \"child.ips\" asset t { size 8x8 draw { } }", child,
            ("import alias '" + name + "' conflicts with the " + name + " namespace; rename the alias").c_str());
    }
    // A dotted name is looked up in the builtin table, whatever a local is called.
    CHECK_NOTHROW(checkAsset(parse(wrap("noise = 2.0 x = noise.fbm(noise, 1.0, 4, 7)")), {}));
    expectCheckError(wrap("regions.volume(1)"), "undefined builtin 'regions.volume'");
    expectCheckError(wrap("diagram.sites(1)"), "undefined builtin 'diagram.sites'; did you mean 'diagram.site'?");
}

TEST_CASE("a parameter cannot hide a palette or an output; an input's name is free") {
    expectCheckError("asset t { size 8x8 palette tones = [#111, #222] draw { } paint(tones: list<color>) { } }",
        "parameter 'tones' has the same name as a palette");
    expectCheckError("asset t { size 8x8 output point tip draw { } mark(tip: vec2) { } }",
        "parameter 'tip' has the same name as an output");
    expectCheckError("asset t { size 8x8 const i32 width = 3 draw { } helper(width: i32) { } }",
        "parameter cannot shadow an asset constant");
    CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 input color tint = #fff draw { paint(tint) } "
                                   "paint(tint: color) { pixel(0, 0, tint) } }"), {}));
}

TEST_CASE("a range or constructor that constant operands always trap is rejected where it is written") {
    for (const std::string range : {"x = 3 to 1", "x = 1.0 to 0.5", "x = 1 to 0.5", "s = sampler(3 to 1)",
                                    "s = stepper(3 to 1)", "if (0) { x = 3 to 1 }"}) {
        INFO(range);
        expectCheckError(wrap(range), "random range lower bound exceeds the upper bound");
    }
    expectCheckError(wrap("x = (1.0 / 0.0) to 2.0"), "random range bounds must be finite");
    expectCheckError("asset t { size 8x8 const i32 high = 9 draw { x = high to 1 } }",
        "random range lower bound exceeds the upper bound");
    expectCheckError(wrap("d = gaussian(0, -1)"), "gaussian needs a finite mean and a finite sigma > 0");
    expectCheckError(wrap("x = 1.0 to 2.0 with gaussian(0, -1)"), "gaussian needs a finite mean and a finite sigma > 0");
    expectCheckError(wrap("d = triangular(1, 0, 2)"), "triangular needs finite min <= peak <= max with min < max");
    expectCheckError(wrap("d = beta(0, 1)"), "beta needs finite alpha > 0, beta > 0 and min < max");
    // A value only known when it runs keeps its trap, and equal bounds draw.
    for (const std::string valid : {"x = 1 to 1", "x = #fff to #000", "low = 3 x = low to 1",
                                    "sigma = -1.0 d = gaussian(0, sigma)", "x = 1 / 0"}) {
        INFO(valid);
        CHECK_NOTHROW(checkAsset(parse(wrap(valid)), {}));
    }
    CHECK_NOTHROW(checkAsset(parse("asset t { size 8x8 input i32 v = 9 draw { x = v to 1 d = gaussian(0, v) } }"), {}));
}

TEST_CASE("a spelling from before 1.0 names its replacement") {
    const std::pair<const char*, const char*> functions[] = {
        {"x = noise(1.0, 2.0, 3)", "undefined function 'noise'; it was renamed to 'noise.value'"},
        {"x = fault(1.0, 2.0, 3)", "undefined function 'fault'; it was renamed to 'noise.fault'"},
        {"x = warp(1.0, 2.0, 0.5, 3)", "undefined function 'warp'; it was renamed to 'noise.warp'"},
        {"v = fold({1, 2}, 3)", "undefined function 'fold'; it was renamed to 'vec2.fold'"},
        {"v = polar({1, 2})", "undefined function 'polar'; it was renamed to 'vec2.polar'"},
        {"x = fill(1.0, 2.0)", "undefined function 'fill'; it was renamed to 'dist.fill'"},
        {"stream_push(1)", "undefined function 'stream_push'; it was renamed to 'stream.push'"},
        {"stream_pop()", "undefined function 'stream_pop'; it was renamed to 'stream.pop'"},
        {"c = color.set.a(#fff, 3)", "undefined builtin 'color.set.a'; it was renamed to 'color.set_a'"},
        {"x = i32trunc(1.5)", "undefined function 'i32trunc'; it was removed, use int(x)"},
        {"x = pick(3)", "pick expects at least two choices; use index(n) to draw an index below n"},
        {"poly(#fff, 0, 0, 4, 0, 4, 4)", "poly takes its color last, after the vertices: poly(x0, y0, ..., color)"},
    };
    for (const auto& [body, message] : functions) {
        INFO(body);
        expectCheckError(wrap(body), message);
    }
    const std::string setup = "layer image { } region = mask.from(image) cells = voronoi({0, 0}, {8, 8}, 3, 1) "
                              "tiles = regions(cells) one = part.create(tiles, 0) all = parts.from_regions(tiles) ";
    const std::pair<const char*, const char*> arguments[] = {
        {"m = noise.mask({0, 0}, {8, 8}, kind = 0, frequency = {1, 1}, threshold = 0.5, seed = 1)",
            "function 'noise.mask' has no argument 'kind'; it was renamed to 'mode'"},
        {"v = vec2.rot({1, 0}, a = 1.0)", "function 'vec2.rot' has no argument 'a'; it was renamed to 'angle'"},
        {"v = vec2.fold({1, 0}, n = 3)", "function 'vec2.fold' has no argument 'n'; it was renamed to 'sectors'"},
        {"x = dist.fill(d = 1.0, w = 2.0)", "function 'dist.fill' has no argument 'd'; it was renamed to 'distance'"},
        {"surface.paint(image = image)", "function 'surface.paint' has no argument 'image'; it was renamed to 'source'"},
        {"n = part.id(p = one)", "function 'part.id' has no argument 'p'; it was renamed to 'part'"},
        {"n = regions.count(rs = tiles)", "function 'regions.count' has no argument 'rs'; it was renamed to 'regions'"},
        {"gap = regions.gap(source = tiles, width = 1)", "function 'regions.gap' has no argument 'source'; it was renamed to 'regions'"},
        {"n = diagram.count(d = cells)", "function 'diagram.count' has no argument 'd'; it was renamed to 'diagram'"},
        {"n = parts.count(source = all)", "function 'parts.count' has no argument 'source'; it was renamed to 'parts'"},
    };
    for (const auto& [body, message] : arguments) {
        INFO(body);
        expectCheckError(wrap(setup + body), message);
    }
    // The current spellings of every case above.
    CHECK_NOTHROW(checkAsset(parse(wrap(setup +
        "a = noise.value(1.0, 2.0, 3) + noise.fault({1, 2}, 3) + noise.warp(1.0, 2.0, 0.5, 3) "
        "v = vec2.polar(vec2.fold({1, 2}, sectors = 3)) w = vec2.rot(v, angle = 1.0) "
        "x = dist.fill(distance = 1.0, width = 2.0) stream.push(1) stream.pop() "
        "c = color.set_a(#fff, 3) i = int(1.5) k = index(3) choice = pick(1, 2) "
        "poly(0, 0, 4, 0, 4, 4, #fff) poly(color = c, x0 = 0, y0 = 0, x1 = 4, y1 = 0, x2 = 4, y2 = 4) "
        "m = noise.mask({0, 0}, {8, 8}, mode = noise.mode_value, frequency = {1, 1}, threshold = 0.5, seed = 1) "
        "surface.paint(source = image) surface.paint(source = region, color = c) "
        "n = part.id(part = one) + regions.count(regions = tiles) + diagram.count(diagram = cells) + "
        "parts.count(parts = all) gap = regions.gap(regions = tiles, width = 1)")), {}));
}

TEST_CASE("builtin names follow one convention") {
    // A builtin is a bare word or one namespace and one member. A helper can
    // use neither form's namespace, so a later builtin never takes its name.
    for (const auto& [name, spec] : builtinSpecs()) {
        (void)spec;
        INFO(name);
        const auto dot = name.find('.');
        CHECK(name.find('.', dot == std::string::npos ? 0 : dot + 1) == std::string::npos);
        if (dot == std::string::npos) {
            CHECK(name.find('_') == std::string::npos);
            CHECK(isBuiltinFunctionName(name));
        } else {
            CHECK(isBuiltinNamespace(name.substr(0, dot)));
            CHECK_FALSE(isBuiltinFunctionName(name));
        }
    }
    for (const std::string former : {"noise", "fault", "warp", "fold", "polar", "fill", "i32trunc",
                                     "stream_push", "stream_pop", "color.set.a"})
        CHECK_FALSE(builtinSpecs().contains(former));
    // The handle a namespace works on is named after it.
    for (const auto& [name, spec] : builtinSpecs()) {
        for (const std::string space : {"part", "regions", "diagram", "parts"}) {
            if (!name.starts_with(space + ".") || name == "part.create" || name == "parts.from_regions") continue;
            INFO(name);
            for (const auto& overload : spec.overloads) CHECK(overload.params.front().name == space);
        }
    }
}
