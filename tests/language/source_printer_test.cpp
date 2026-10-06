#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <tuple>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

TEST_CASE("canonical source formatting normalizes literals names and typed functions") {
    const auto program = ips::parse(R"(asset t { size 8X8 variants 0x2
      input color c=#AbcD output list point trace palette p=[0X1,0x2]
      draw { rect(color=c,h=4,w=4,y=0,x=0) x=0.10 }
      helper(n:i32)->i32 { return n+0x1 }
      noop() { return; pixel(0,0,#fff) }
    })");
    const std::string expected = R"(asset t {
  size 8x8
  variants 2
  input color c = #aabbccdd
  output list point trace
  palette p = [1, 2]

  draw {
    rect(x = 0, y = 0, w = 4, h = 4, color = c)
    x = 0.1
  }

  helper(n: i32) -> i32 {
    return (n + 1)
  }

  noop() {
    return;
    pixel(0, 0, #ffffff)
  }
}
)";
    const auto formatted = ips::formatProgram(program);
    CHECK(formatted == expected);
    const auto reparsed = ips::parse(formatted);
    CHECK(ips::formatProgram(reparsed) == formatted);
    ips::checkAsset(program, {});
    ips::checkAsset(reparsed, {});
    CHECK(stripDebug(compileProgram(program, {})) == stripDebug(compileProgram(reparsed, {})));
}

TEST_CASE("canonical words grouping and signed literals preserve exact executable values", "[format-style]") {
    const auto original = ips::parse(R"(📦 canonical { size 8X8; origin (0),(0);
      variants 0X2; archetypes 0x1;
      📥 i32 low=(-2147483648); 📥 u32 high=0xFFFFFFFF;
      📥 f32 zero=(-0.0); 📥 f32 tiny=1.17549435e-38;
      📤 list point trace;
      🌈 p=[(-0.0),0x1,1.17549435e-38];
      ✏ {
        // 📦 in a comment stays literal
        trace={zero,tiny}; trace={p[0],p[2]};
        helper(y=1e-20,x=(-0.0));
        x=((1<1<<2)); noop();
      }
      helper(x:f32,y:f32) { trace={x,y}; }
      noop() { return; }
    })");
    const auto text = ips::formatProgram(original);
    CHECK(text.starts_with("asset canonical {\n  size 8x8\n"));
    CHECK(text.find("input i32 low = (-2147483648)") != std::string::npos);
    CHECK(text.find("input u32 high = 4294967295") != std::string::npos);
    CHECK(text.find("input f32 zero = (-0.0)") != std::string::npos);
    CHECK(text.find("helper(x = (-0.0), y = 1e-20)") != std::string::npos);
    CHECK(text.find("// 📦 in a comment stays literal") != std::string::npos);
    CHECK(text.find("helper(x: f32, y: f32)") != std::string::npos);
    CHECK(text.find("return;") != std::string::npos);
    const auto formatted = ips::parse(text);
    CHECK(ips::formatProgram(formatted) == text);
    ips::checkAsset(original, {});
    ips::checkAsset(formatted, {});
    for (const bool optimize : {false, true}) {
        const auto before = compileProgram(original, {}, optimize);
        const auto after = compileProgram(formatted, {}, optimize);
        CHECK(stripDebug(before) == stripDebug(after));
        const auto actual = compiler_test::execute(after);
        REQUIRE(actual.outputs[0].points.size() == 3);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[0].x) == 0x80000000u);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[0].y) == 0x00800000u);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[1].x) == 0x80000000u);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[1].y) == 0x00800000u);
        CHECK(std::bit_cast<u32>(actual.outputs[0].points[2].x) == 0x80000000u);
    }
}

TEST_CASE("source formatting preserves every control and expression branch") {
    const auto source = R"(import tile from "dir\\tile\"ż.ips"
      asset all { size 16x16 origin 1,2 variants 3 archetypes 4
        input u32 offset=0x10 input f32 opacity=0.5
        output rect r output circle c output list point trace
        palette colors=[#f00,#0f0]
        draw {
          q = {1,2} x=q.x y=q.y
          r = {1,2,3,4} c={1,2,3}
          for i in (v+1)..(v+4) { if (i==2) { continue } else if(i!=3) { break } }
          while ((x<y) && (x<=y) || (x>y) && (x>=y)) { x=x+1 }
          x=((1+2)*3-4)/5 y=(6%3)^(7&3)|(8<<1)>>(~2)
          x=-sqrt(x) flag=!false
          pixel(0,0,colors[v])
          tile.draw(seed,v,a, color=#fff,at=q,rot=1)
          return;
        }
        helper(p:vec2,c:color) -> vec2 { return p*2.0 }
      })";
    const auto formatted = ips::formatProgram(ips::parse(source));
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
    CHECK(ips::parse(formatted).imports[0].path == "dir\\tile\"ż.ips");
    CHECK(formatted.find("color = #ffffff, at = q, rot = 1") != std::string::npos);
    CHECK(formatted.find("helper(p: vec2, c: color) -> vec2") != std::string::npos);
}

TEST_CASE("composed examples and backend patterns roundtrip to identical executable bytes") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const auto read = [&](const std::filesystem::path& path) {
        std::ifstream file(root / path, std::ios::binary);
        REQUIRE(file);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    for (const auto path : {"examples/portrait.ips", "examples/plant.ips",
            "backends/html5/test/fixtures/aliases.ips", "backends/html5/test/fixtures/random-sugar.ips",
            "backends/html5/test/fixtures/named-arguments.ips", "backends/html5/test/fixtures/direct-seed.ips"}) {
        INFO(path);
        auto original = ips::parse(read(path));
        auto roundtrip = ips::parse(ips::formatProgram(original));
        CHECK(ips::formatProgram(roundtrip) == ips::formatProgram(original));
        std::map<std::string, ips::Program> originals, formatted;
        std::map<std::string, const ips::Program*> originalParts, formattedParts;
        for (const auto& import : original.imports) {
            auto child = ips::parse(read(std::filesystem::path(path).parent_path() / import.path));
            formatted.emplace(import.alias, ips::parse(ips::formatProgram(child)));
            originals.emplace(import.alias, std::move(child));
            originalParts.emplace(import.alias, &originals.at(import.alias));
            formattedParts.emplace(import.alias, &formatted.at(import.alias));
        }
        CHECK(stripDebug(compileProgram(original, originalParts)) == stripDebug(compileProgram(roundtrip, formattedParts)));
    }
}

TEST_CASE("source trivia owns exact comment bytes and Unicode source locations") {
    const std::string source = "// żółć 🌱\nasset t { draw { x=1 /* między */ + // operand\n2; // after\n} }\n// eof";
    const auto tokens = ips::lex(source);
    REQUIRE(tokens.front().leadingTrivia.size() == 1);
    const auto& leading = tokens.front().leadingTrivia[0];
    CHECK(leading.text == "// żółć 🌱");
    CHECK(leading.location.byteOffset == 0);
    CHECK(leading.location.endByteOffset == source.find('\n'));
    CHECK(tokens.front().location.line == 2);
    CHECK(tokens.front().location.column == 1);
    const auto find = [&](std::string_view text) -> const ips::Token& {
        const auto at = std::find_if(tokens.begin(), tokens.end(), [&](const auto& token) { return token.text == text; });
        REQUIRE(at != tokens.end());
        return *at;
    };
    const auto& one = find("1");
    REQUIRE(one.trailingTrivia.size() == 1);
    CHECK(one.trailingTrivia[0].text == "/* między */");
    CHECK(one.trailingTrivia[0].location.column == 22);
    CHECK(one.trailingTrivia[0].location.byteOffset == source.find("/* między */"));
    REQUIRE(find("+").trailingTrivia.size() == 1);
    CHECK(find("+").trailingTrivia[0].text == "// operand");
    REQUIRE(find(";").trailingTrivia.size() == 1);
    CHECK(find(";").trailingTrivia[0].text == "// after");
    REQUIRE(tokens.back().leadingTrivia.size() == 1);
    CHECK(tokens.back().leadingTrivia[0].text == "// eof");
    auto program = ips::parseTokens(tokens);
    CHECK(program.sourceTokens.front().leadingTrivia[0].text == leading.text);
    CHECK(program.asset.functions[0].body[0].sourceRange.end > program.asset.functions[0].body[0].sourceRange.begin);
    const auto formatted = ips::formatProgram(program);
    CHECK(ips::formatProgram(ips::parse(formatted)) == formatted);
}

TEST_CASE("canonical source retains comment attachment across declarations statements and nested expressions") {
    const std::string source = R"(// file header
asset t { /* asset body */
  // draw lead
  draw { // draw body
    // assignment lead
    x = (/* grouped */ 0x1 + // right operand
      (2 /* operand suffix */ * 3)); // assignment suffix
    if (x > 0) { /* then */ pixel(0, 0, #Abc) } /* else marker */ else { // else body
      pixel(1, 1, #fff) // call suffix
    }
    // closing block
  }
  // color declaration
  input color c = (#Abc /* default suffix */)
  input i32 offset = (- /* signed default */ 0x1)
  input f32 scale = (-0.0 /* signed zero */)
  // output declaration
  output point p
  palette colors = [#fff, /* second color */ #000, /* trailing comma */]
  size 8x8 // size suffix
} // asset suffix
// file suffix
)";
    const auto original = ips::parse(source);
    const auto formatted = ips::formatProgram(original);
    INFO(formatted);
    CHECK(formatted.find("// file header\nasset t") != std::string::npos);
    CHECK(formatted.find("// color declaration\n  input color c") != std::string::npos);
    CHECK(formatted.find("// output declaration\n  output point p") != std::string::npos);
    CHECK(formatted.find("// draw lead\n  draw") != std::string::npos);
    CHECK(formatted.find("// assignment lead\n    x") != std::string::npos);
    CHECK(formatted.find("#aabbcc /* default suffix */") != std::string::npos);
    CHECK(formatted.find("// assignment suffix") < formatted.find("if ("));
    for (const auto marker : {"/* asset body */", "// draw body", "/* grouped */", "// right operand", "/* operand suffix */",
            "/* then */", "/* else marker */", "// else body", "// call suffix", "// closing block", "/* second color */",
            "/* trailing comma */", "/* signed default */", "/* signed zero */", "// size suffix", "// asset suffix", "// file suffix"}) {
        INFO(marker);
        const auto at = formatted.find(marker);
        CHECK(at != std::string::npos);
        CHECK(formatted.find(marker, at + std::string(marker).size()) == std::string::npos);
    }
    const auto roundtrip = ips::parse(formatted);
    CHECK(ips::formatProgram(roundtrip) == formatted);
    CHECK(stripDebug(compileProgram(original, {})) == stripDebug(compileProgram(roundtrip, {})));
}

TEST_CASE("commented calls retain written named argument order and executable effects") {
    const std::string source = R"(asset t { size 8x8 output point rng
      draw {
        rect(color = #fFf, /* height before x */ h = range(2, 5),
          x /* name suffix */ = range(0, 2), w = range(2, 5), // y follows
          y = range(0, 2))
        pixel(color = color.shade(color = #ff0000, /* nested named */ amount = (index(8) - 4)), y = 6, x = 6)
        f(second = index(8), /* between helper args */ first = index(8))
        rng = {unit(), unit()}
      }
      f(first: i32, second: i32) { pixel(first, second, #fff) }
    })";
    const auto original = ips::parse(source);
    const auto formatted = ips::formatProgram(original);
    INFO(formatted);
    CHECK(formatted.find("rect(color = #ffffff, /* height before x */ h = range(2, 5),") != std::string::npos);
    CHECK(formatted.find("x /* name suffix */ = range(0, 2), w = range(2, 5), // y follows") != std::string::npos);
    CHECK(formatted.find("f(second = index(8), /* between helper args */ first = index(8))") != std::string::npos);
    const auto roundtrip = ips::parse(formatted);
    CHECK(ips::formatProgram(roundtrip) == formatted);
    ips::checkAsset(original, {});
    ips::checkAsset(roundtrip, {});
    const auto originalBytes = stripDebug(compileProgram(original, {}));
    const auto formattedBytes = stripDebug(compileProgram(roundtrip, {}));
    CHECK(originalBytes == formattedBytes);
    for (const auto seed : {0u, 1u, 42u, 0xFFFFFFFFu}) {
        const auto before = compiler_test::execute(originalBytes, seed);
        const auto after = compiler_test::execute(formattedBytes, seed);
        CHECK(before.canvas.pixels == after.canvas.pixels);
        REQUIRE(before.outputs.size() == 1);
        REQUIRE(after.outputs.size() == 1);
        REQUIRE(before.outputs[0].points.size() == 1);
        REQUIRE(after.outputs[0].points.size() == 1);
        CHECK(before.outputs[0].points[0].x == after.outputs[0].points[0].x);
        CHECK(before.outputs[0].points[0].y == after.outputs[0].points[0].y);
    }
}

TEST_CASE("formatter preserves delimiter multiline and empty block trivia idempotently") {
    for (const auto source : {
        "asset t { size 8x8 draw { /* empty\n\tżółć // nested-looking /* text */ } }",
        "asset t { size 8x8 draw { ; /* empty separator */ x = (( /* open */ 1 /* close */ )); // statement\n } }",
        "asset t { size 8x8 draw { if (true) {} else /* before if */ if (/* condition */ false) { // branch\n } } }",
        "asset t { size 8x8 draw { pixel( /* open args */ 0, 0, #fff /* close args */ ) } }",
        "asset t { size /* width */ 8 /* height */ 8 // size\n draw {} }"
    }) {
        INFO(source);
        const auto original = ips::parse(source);
        const auto formatted = ips::formatProgram(original);
        INFO(formatted);
        const auto roundtrip = ips::parse(formatted);
        CHECK(ips::formatProgram(roundtrip) == formatted);
        CHECK(stripDebug(compileProgram(original, {})) == stripDebug(compileProgram(roundtrip, {})));
        std::vector<std::string> before, after;
        const auto collect = [](const auto& tokens, auto& comments) {
            for (const auto& token : tokens) {
                for (const auto& trivia : token.leadingTrivia) comments.push_back(trivia.text);
                for (const auto& trivia : token.trailingTrivia) comments.push_back(trivia.text);
            }
        };
        collect(original.sourceTokens, before);
        collect(roundtrip.sourceTokens, after);
        CHECK(before == after);
        const auto recovering = ips::parseRecovering(source);
        REQUIRE(recovering.program);
        CHECK(ips::formatProgram(*recovering.program) == formatted);
    }
    const auto malformed = ips::parseRecovering("// lead\nimport tile /* alias */ from \"tile.ips\"; // import\nasset t { draw {x= }}");
    REQUIRE_FALSE(malformed.program);
    REQUIRE(malformed.imports.size() == 1);
    CHECK(malformed.imports[0].alias == "tile");
    CHECK(malformed.imports[0].path == "tile.ips");
    CHECK(malformed.imports[0].location.byteOffset == 8);
}

TEST_CASE("comments at every grammar token boundary preserve formatting and executable code") {
    const std::string source = R"(asset t { size 8x8 origin (0), (0) variants 2 archetypes 3
      input color c=(#Abc) output point p palette colors=[#fff,#000,]
      draw {
        x=(1+2)*3 q={0,0} p=q
        pixel(color=c,y=0,x=0)
        x=helper(second=2,first=1)
        for i in 0..2 { if(i==1) {continue} else if(i==2) {break} }
        while(x<0) {x=x+1}
        return;
      }
      helper(first:i32,second:i32)->i32 {return first+second}
    })";
    const auto baseline = stripDebug(compileProgram(ips::parse(source), {}));
    const auto tokens = ips::lex(source);
    for (const auto& token : tokens) {
        for (const auto comment : {"/* boundary */", "// boundary\n"}) {
            INFO(token.location.byteOffset);
            INFO(comment);
            auto commented = source;
            commented.insert(token.location.byteOffset, comment);
            const auto original = ips::parse(commented);
            const auto formatted = ips::formatProgram(original);
            INFO(formatted);
            const auto roundtrip = ips::parse(formatted);
            REQUIRE(ips::formatProgram(roundtrip) == formatted);
            CHECK(stripDebug(compileProgram(roundtrip, {})) == baseline);
            const auto marker = formatted.find("boundary");
            REQUIRE(marker != std::string::npos);
            CHECK(formatted.find("boundary", marker + 8) == std::string::npos);
        }
    }
}

TEST_CASE("independent review corpus preserves exact trivia ownership and context") {
    // The independent review's nine accepted-source counterexamples.
    const std::vector<std::pair<std::string, std::string>> corpus{
        {"multiline_removed_group", "asset t { size 8x8 draw { x=(1 /* multi\nline */) /* after */ } }"},
        {"multiline_removed_separator", "asset t { size 8x8 draw { x=1 /* multi\nline */; /* after */ pixel(0,0,#fff) } }"},
        {"leading_removed_separator", "asset t { size 8x8 draw { x=1\n// M\n; } }"},
        {"empty_else_line", "asset t { size 8x8 draw { if(true) {} else {\n// else body\n} } }"},
        {"empty_else_block", "asset t { size 8x8 draw { if(true) {} else { /* else body */ } } }"},
        {"duplicate_size", "asset t { size 4x4 /* first size */ size 8x8 /* final size */ draw {} }"},
        {"duplicate_origin", "asset t { size 8x8 origin 1,1 /* first origin */ origin 0,0 /* final origin */ draw {} }"},
        {"duplicate_variants", "asset t { size 8x8 variants 1 /* first variants */ variants 2 /* final variants */ draw {} }"},
        {"duplicate_archetypes", "asset t { size 8x8 archetypes 1 /* first archetypes */ archetypes 2 /* final archetypes */ draw {} }"}
    };
    using Attachment = std::tuple<std::string, ips::TokenKind, std::string, bool, std::size_t>;
    const auto attachments = [](const ips::Program& program) {
        std::vector<Attachment> result;
        std::size_t depth = 0;
        for (const auto& token : program.sourceTokens) {
            for (const auto& trivia : token.leadingTrivia)
                result.emplace_back(trivia.text, token.kind, token.text, true, depth);
            if (token.kind == ips::TokenKind::LBrace) ++depth;
            if (token.kind == ips::TokenKind::RBrace) --depth;
            for (const auto& trivia : token.trailingTrivia)
                result.emplace_back(trivia.text, token.kind, token.text, false, depth);
        }
        return result;
    };
    for (const auto& [name, source] : corpus) {
        INFO(name);
        const auto original = ips::parse(source);
        const auto formatted = ips::formatProgram(original);
        INFO(formatted);
        const auto roundtrip = ips::parse(formatted);
        REQUIRE(ips::formatProgram(roundtrip) == formatted);
        CHECK(attachments(roundtrip) == attachments(original));
        CHECK(stripDebug(compileProgram(original, {})) == stripDebug(compileProgram(roundtrip, {})));
        if (name.starts_with("empty_else")) CHECK(formatted.find(" else {") != std::string::npos);
        if (name.starts_with("duplicate")) {
            CHECK(formatted.find("/* first") < formatted.find("/* final"));
            CHECK(formatted.find("/* final") < formatted.rfind('}'));
            CHECK(original.asset.sizeX == roundtrip.asset.sizeX);
            CHECK(original.asset.variants == roundtrip.asset.variants);
            CHECK(original.asset.archetypes == roundtrip.asset.archetypes);
        }
    }
}

TEST_CASE("combined delimiter and metadata trivia remains stable after repeated formatting") {
    const std::vector<std::string> corpus{
        "asset t { size 8x8 draw { x=((1 /* multi\nline */) /* inner */) /* outer */; /* separator */ pixel(0,0,#fff) } }",
        "asset t { size 8x8 draw { x=(1 /* multi\nline */ + 2); /* separator */ pixel(0,0,#fff) } }",
        "asset t { size 8x8 draw { x=1;\n// M\n; /* spare separator */ } }",
        "asset t { size 8x8 draw { if(true) {} else { /* inline\nbody */\n// closing body\n} /* after else */ } }",
        "asset t { size 4x4; // first size\n origin {1,2}, 3 /* superseded */ size 8x8; /* final */ origin 0,0 draw {} }",
        "asset t { size 8x8 origin \"discarded string\", 0 /* superseded */ origin 0,0 variants 1; /* old variants */ variants 3 draw {} }"
    };
    for (const auto& source : corpus) {
        INFO(source);
        const auto original = ips::parse(source);
        const auto once = ips::formatProgram(original);
        INFO(once);
        const auto twice = ips::formatProgram(ips::parse(once));
        REQUIRE(twice == once);
        CHECK(ips::formatProgram(ips::parse(twice)) == once);
        CHECK(stripDebug(compileProgram(original, {})) == stripDebug(compileProgram(ips::parse(twice), {})));
    }
}

TEST_CASE("long trailing comments survive reordered nested named calls") {
    const std::string comment = "/* " + std::string(4096, 'X') + " */";
    const std::string source = "asset t { size 8x8 draw { "
        "rect(color=#fff,h=range(1,2),x=0,w=1,y=0) " + comment + " } }";
    const auto original = ips::parse(source);
    const auto formatted = ips::formatProgram(original);
    CHECK(formatted.find("rect(x = 0, y = 0, w = 1, h = range(1, 2), color = #ffffff)") != std::string::npos);
    const auto marker = formatted.find(comment);
    REQUIRE(marker != std::string::npos);
    CHECK(formatted.find(comment, marker + comment.size()) == std::string::npos);
    const auto roundtrip = ips::parse(formatted);
    CHECK(ips::formatProgram(roundtrip) == formatted);
    CHECK(ips::formatProgram(ips::parse(ips::formatProgram(roundtrip))) == formatted);
    CHECK(stripDebug(compileProgram(original, {})) == stripDebug(compileProgram(roundtrip, {})));
}
