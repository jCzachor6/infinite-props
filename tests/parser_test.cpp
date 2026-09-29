#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ips/Ast.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"

// The .ips parser: sketch grammars parse into the expected AST, malformed
// programs produce exactly one located error, and edge-case grammar forms
// (semicolons, else-if, soft keywords as values) are pinned.

using namespace iprop::ips;

namespace {

// The README §7.2 sketches.
const std::string kRockSketch = R"(asset rock {
  size 192x192
  origin 96,170
  variants 5
  archetypes 4
  input  color palette = #808d74
  output rect  collider

  draw(v: i32, a: i32) {
    w = range(21, 31)
    h = range(35, 49)
    collider = {96 - 20, 170 - 23, 40, 23}
    stone_mass(-w * 0.10, 0, w * 0.87, h, palette, seed)
    if (index(3) == 0) {
      stone_mass(-w * 0.67, 2, w * 0.39, h * 0.43,
                 shade(palette, -4), hash(seed ^ 9467))
    }
  }
})";

const std::string kPortraitSketch = R"(
import hair from "hair.ips"
import eyes from "eyes.ips"

asset portrait {
  size 64x64
  variants 8
  input  color skin = #d9b58c
  input  color hairColor = #33291f
  output point pupilCenter

  draw(v: i32, a: i32) {
    face(v, a)
    hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    eyes.draw(seed ^ 0x517cc1b7, v, a, at = {32, 24})
    pupilCenter = eyes.outputs.pupil
  }
})";

const std::string kHairSketch = R"(
// hair.ips — a part authored in its own file
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  output point top

  draw(v: i32, a: i32) {
    top = {32, 4}
  }
})";

} // namespace

TEST_CASE("the rock sketch parses into the expected AST") {
    const Program program = parse(kRockSketch);
    REQUIRE(program.imports.empty());

    const Asset& asset = program.asset;
    REQUIRE(asset.name == "rock");
    REQUIRE(asset.sizeX == 192u);
    REQUIRE(asset.sizeY == 192u);
    REQUIRE(asset.variants == 5u);
    REQUIRE(asset.archetypes == 4u);
    REQUIRE(asset.originX != nullptr);
    REQUIRE(asset.originY != nullptr);

    REQUIRE(asset.inputs.size() == 1);
    REQUIRE(asset.inputs[0].type == InputType::Color);
    REQUIRE(asset.inputs[0].name == "palette");

    REQUIRE(asset.outputs.size() == 1);
    REQUIRE(asset.outputs[0].kind == OutputKind::Rect);
    REQUIRE(asset.outputs[0].name == "collider");
    REQUIRE(!asset.outputs[0].isList);

    REQUIRE(asset.functions.size() == 1);
    const Function& draw = asset.functions[0];
    REQUIRE(draw.name == "draw");
    REQUIRE(draw.params.size() == 2);
    REQUIRE(draw.params[0].name == "v");
    REQUIRE(draw.params[1].name == "a");
    REQUIRE(draw.params[0].type == ParamType::I32);
    REQUIRE(draw.params[1].type == ParamType::I32);
    REQUIRE(!draw.returnType.has_value());
    REQUIRE(draw.body.size() == 5);

    // w = range(21, 31)
    const auto* w = std::get_if<VarDecl>(&draw.body[0]);
    REQUIRE(w != nullptr);
    REQUIRE(w->name == "w");

    // collider = {96 - 20, ...} is a vector literal.
    const auto* collider = std::get_if<VarDecl>(&draw.body[2]);
    REQUIRE(collider != nullptr);
    REQUIRE(collider->name == "collider");
    REQUIRE(std::holds_alternative<VectorLit>(*collider->value));

    // The last statement is the if.
    REQUIRE(std::holds_alternative<IfStmt>(draw.body[4]));
}

TEST_CASE("the portrait sketch parses imports, named args and members") {
    const Program program = parse(kPortraitSketch);
    REQUIRE(program.imports.size() == 2);
    REQUIRE(program.imports[0].alias == "hair");
    REQUIRE(program.imports[0].path == "hair.ips");
    REQUIRE(program.imports[1].alias == "eyes");
    REQUIRE(program.imports[1].path == "eyes.ips");

    const Asset& asset = program.asset;
    REQUIRE(asset.name == "portrait");
    REQUIRE(asset.inputs.size() == 2);
    REQUIRE(asset.inputs[0].name == "skin");
    REQUIRE(asset.inputs[1].name == "hairColor");
    REQUIRE(asset.outputs.size() == 1);
    REQUIRE(asset.outputs[0].name == "pupilCenter");

    REQUIRE(asset.functions.size() == 1);
    const auto& body = asset.functions[0].body;
    REQUIRE(body.size() == 4);

    // face(v, a) — a plain call.
    const auto* face = std::get_if<ExprStmt>(&body[0]);
    REQUIRE(face != nullptr);
    const auto* faceCall = std::get_if<Call>(face->expr.get());
    REQUIRE(faceCall != nullptr);
    REQUIRE(faceCall->args.size() == 2);

    // hair.draw(seed ^ 0x9e3779b9, v, a, at = {32, 4}, color = hairColor)
    const auto* hairDraw = std::get_if<ExprStmt>(&body[1]);
    REQUIRE(hairDraw != nullptr);
    const auto* hairCall = std::get_if<Call>(hairDraw->expr.get());
    REQUIRE(hairCall != nullptr);
    REQUIRE(std::holds_alternative<Member>(*hairCall->callee));
    REQUIRE(hairCall->args.size() == 5);
    REQUIRE(hairCall->args[3].name == "at");
    REQUIRE(hairCall->args[4].name == "color");
    REQUIRE(std::holds_alternative<VectorLit>(*hairCall->args[3].value));

    // pupilCenter = eyes.outputs.pupil — a three-segment member chain.
    const auto* pupil = std::get_if<VarDecl>(&body[3]);
    REQUIRE(pupil != nullptr);
    REQUIRE(pupil->name == "pupilCenter");
    REQUIRE(std::holds_alternative<Member>(*pupil->value));
}

TEST_CASE("the hair sketch parses") {
    const Program program = parse(kHairSketch);
    const Asset& asset = program.asset;
    REQUIRE(asset.name == "hair");
    REQUIRE(asset.sizeX == 64u);
    REQUIRE(asset.sizeY == 32u);
    REQUIRE(asset.inputs.size() == 1);
    REQUIRE(asset.inputs[0].type == InputType::Color);
    REQUIRE(asset.inputs[0].name == "color");
    REQUIRE(asset.outputs.size() == 1);
    REQUIRE(asset.outputs[0].kind == OutputKind::Point);
    REQUIRE(asset.outputs[0].name == "top");
    REQUIRE(asset.functions.size() == 1);

    const auto& body = asset.functions[0].body;
    REQUIRE(body.size() == 1);
    const auto* top = std::get_if<VarDecl>(&body[0]);
    REQUIRE(top != nullptr);
    REQUIRE(std::holds_alternative<VectorLit>(*top->value));
}

TEST_CASE("expression precedence and forms") {
    const std::string source = R"(
asset expr {
  draw(v: i32, a: i32) {
    x = a + b * c - -d / !e % ~f
    y = 1 + 2 * 3 == 7 && 1 < 2 || 0 > 1
    z = a.b.c.d(1, named = 2, 3)
    q = {1, 2 + 3,}
    r = (a + b) * (c - d)
    s = true
    t = 0x10 ^ 0x1f & 0x0f | 0x01
    if (x) { return 1 } else { return }
  }
})";
    const Program program = parse(source);
    const auto& body = program.asset.functions[0].body;
    REQUIRE(body.size() == 8);

    // x = a + b * c - -d / !e % ~f  →  (a + b*c) - ((-d) / ((!e) % (~f)))
    const auto* x = std::get_if<VarDecl>(&body[0]);
    REQUIRE(x != nullptr);
    const auto* topSub = std::get_if<Binary>(x->value.get());
    REQUIRE(topSub != nullptr);
    REQUIRE(topSub->op == Binary::Op::Sub);
    const auto* leftAdd = std::get_if<Binary>(topSub->left.get());
    REQUIRE(leftAdd != nullptr);
    REQUIRE(leftAdd->op == Binary::Op::Add);
    const auto* rightMul = std::get_if<Binary>(leftAdd->right.get());
    REQUIRE(rightMul != nullptr);
    REQUIRE(rightMul->op == Binary::Op::Mul);

    // z = a.b.c.d(...) — member chain call with mixed args.
    const auto* z = std::get_if<VarDecl>(&body[2]);
    REQUIRE(z != nullptr);
    const auto* chain = std::get_if<Call>(z->value.get());
    REQUIRE(chain != nullptr);
    REQUIRE(std::holds_alternative<Member>(*chain->callee));
    REQUIRE(chain->args.size() == 3);
    REQUIRE(chain->args[1].name == "named");

    // q = {1, 2 + 3,} — a trailing comma is allowed.
    const auto* q = std::get_if<VarDecl>(&body[3]);
    REQUIRE(q != nullptr);
    const auto* vec = std::get_if<VectorLit>(q->value.get());
    REQUIRE(vec != nullptr);
    REQUIRE(vec->elements.size() == 2);

    // The if/else is last.
    const auto* ifStmt = std::get_if<IfStmt>(&body[7]);
    REQUIRE(ifStmt != nullptr);
    REQUIRE(ifStmt->elseBranch.size() == 1);
}

TEST_CASE("size accepts both width x height and two integers") {
    const Program a = parse("asset a { size 192x192 }");
    REQUIRE(a.asset.sizeX == 192u);
    REQUIRE(a.asset.sizeY == 192u);

    const Program b = parse("asset b { size 64 32 }");
    REQUIRE(b.asset.sizeX == 64u);
    REQUIRE(b.asset.sizeY == 32u);
}

TEST_CASE("user functions beyond draw parse") {
    const std::string source = R"(
asset multi {
  draw(v: i32, a: i32) {
    stone_mass(0, 0)
  }
  helper(p: i32) {
    return p
  }
})";
    const Program program = parse(source);
    REQUIRE(program.asset.functions.size() == 2);
    REQUIRE(program.asset.functions[0].name == "draw");
    REQUIRE(program.asset.functions[1].name == "helper");
    REQUIRE(program.asset.functions[1].params.size() == 1);
}

TEST_CASE("while loops and list outputs parse") {
    const std::string source = R"(
asset loop {
  output list point pts
  draw(v: i32, a: i32) {
    i = 0
    while (i < 10) {
      i = i + 1
    }
  }
})";
    const Program program = parse(source);
    REQUIRE(program.asset.outputs.size() == 1);
    REQUIRE(program.asset.outputs[0].isList);
    REQUIRE(program.asset.outputs[0].kind == OutputKind::Point);
    REQUIRE(program.asset.outputs[0].name == "pts");

    const auto& body = program.asset.functions[0].body;
    REQUIRE(body.size() == 2);
    const auto* loop = std::get_if<WhileStmt>(&body[1]);
    REQUIRE(loop != nullptr);
    REQUIRE(loop->body.size() == 1);
}

TEST_CASE("semicolons are optional statement separators") {
    const std::string source = R"(
asset semi {
  draw(v: i32, a: i32) {
    w = 1;
    h = 2
    ;
    x = 3;
  }
  helper() {
    return;
  }
})";
    const Program program = parse(source);
    const auto& body = program.asset.functions[0].body;
    REQUIRE(body.size() == 3);
    REQUIRE(std::get_if<VarDecl>(&body[0])->name == "w");
    REQUIRE(std::get_if<VarDecl>(&body[1])->name == "h");
    REQUIRE(std::get_if<VarDecl>(&body[2])->name == "x");

    // 'return;' is a bare return.
    const auto& helper = program.asset.functions[1].body;
    REQUIRE(helper.size() == 1);
    const auto* ret = std::get_if<ReturnStmt>(&helper[0]);
    REQUIRE(ret != nullptr);
    REQUIRE(ret->value == nullptr);
}

TEST_CASE("else-if chains and nested blocks parse") {
    const std::string source = R"(
asset nested {
  draw(v: i32, a: i32) {
    if (a) {
      x = 1
    } else if (b) {
      while (c) {
        if (d) {
          y = 2
        }
      }
    } else {
      x = 3
    }
  }
})";
    const Program program = parse(source);
    const auto& body = program.asset.functions[0].body;
    REQUIRE(body.size() == 1);
    const auto* outer = std::get_if<IfStmt>(&body[0]);
    REQUIRE(outer != nullptr);
    REQUIRE(outer->thenBranch.size() == 1);
    REQUIRE(outer->elseBranch.size() == 1); // a single nested if

    const auto* elseIf = std::get_if<IfStmt>(&outer->elseBranch[0]);
    REQUIRE(elseIf != nullptr);
    REQUIRE(elseIf->thenBranch.size() == 1);
    const auto* loop = std::get_if<WhileStmt>(&elseIf->thenBranch[0]);
    REQUIRE(loop != nullptr);
    REQUIRE(std::holds_alternative<IfStmt>(loop->body[0]));
}

TEST_CASE("typed parameters and explicit return types parse") {
    const std::string source = R"(
asset typed {
  draw(v: i32, a: i32) {
    u = 1.5
    n = ageFromUnit(u)
  }
  ageFromUnit(u: f32) -> i32 {
    return i32trunc(u * 10)
  }
  leaf(x: f32, y: f32, rx: f32, ry: f32, lean: f32, c: color) -> color {
    return shade(c, 1)
  }
})";
    const Program program = parse(source);

    const Function& draw = program.asset.functions[0];
    REQUIRE(draw.params.size() == 2);
    REQUIRE(draw.params[0].name == "v");
    REQUIRE(draw.params[0].type == ParamType::I32);
    REQUIRE(draw.params[1].type == ParamType::I32);
    REQUIRE(!draw.returnType.has_value()); // void draws have no arrow

    const Function& age = program.asset.functions[1];
    REQUIRE(age.name == "ageFromUnit");
    REQUIRE(age.params.size() == 1);
    REQUIRE(age.params[0].name == "u");
    REQUIRE(age.params[0].type == ParamType::F32);
    REQUIRE(age.returnType == ParamType::I32);

    const Function& leaf = program.asset.functions[2];
    REQUIRE(leaf.params.size() == 6);
    REQUIRE(leaf.params[0].type == ParamType::F32);
    REQUIRE(leaf.params[4].type == ParamType::F32);
    REQUIRE(leaf.params[5].name == "c");
    REQUIRE(leaf.params[5].type == ParamType::Color);
    REQUIRE(leaf.returnType == ParamType::Color);
}

TEST_CASE("untyped parameters are rejected pre-1.0") {
    // The language breaks backward compatibility before the 1.0 freeze:
    // every parameter must declare its type.
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset m { draw(v, a) { } }", "':' and a parameter type"},
        {"asset m { draw(v: i32, a) { } }", "':' and a parameter type"},
        {"asset m { f(x, y: f32) { } }", "':' and a parameter type"},
        {"asset m { f(x) -> i32 { return 1 } }", "':' and a parameter type"},
    };
    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("u32 parameters are rejected") {
    bool threw = false;
    try {
        (void)parse("asset u { draw(v: u32, a: i32) { } }");
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("uniform-only") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("malformed parameter types produce one located error") {
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset a { draw(v:) { } }", "parameter type"},
        {"asset a { draw(v: blob) { } }", "parameter type"},
        {"asset a { draw(v: i32, a: i32) -> { } }", "parameter type"},
        {"asset a { f(x f32) { } }", "':' and a parameter type"},
    };
    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("zero-parameter functions and uppercase X sizes parse") {
    const Program program =
        parse("asset z { size 192X192 draw() { stone_mass() } }");
    REQUIRE(program.asset.sizeX == 192u);
    REQUIRE(program.asset.sizeY == 192u);
    REQUIRE(program.asset.functions[0].params.empty());
}

TEST_CASE("named arguments may come first") {
    const Program program =
        parse("asset n { draw(v: i32, a: i32) { f(at = {0, 0}, 5) } }");
    const auto& body = program.asset.functions[0].body;
    const auto* stmt = std::get_if<ExprStmt>(&body[0]);
    const auto* call = std::get_if<Call>(stmt->expr.get());
    REQUIRE(call->args.size() == 2);
    REQUIRE(call->args[0].name == "at");
    REQUIRE(!call->args[1].name.has_value());
}

TEST_CASE("string escapes survive in import paths") {
    const Program program = parse(R"(import hair from "dir\\sub.ips"
asset p { draw(v: i32, a: i32) { } })");
    REQUIRE(program.imports[0].path == "dir\\sub.ips");
}

TEST_CASE("soft keywords work as values and names") {
    // The hair sketch declares an input literally named `color`; using it as
    // an expression identifier must parse.
    const std::string source = R"(
asset hair {
  size 64x32
  origin 32,4
  input color color = #33291f
  output point top

  draw(v: i32, a: i32) {
    top = {32, 4}
    tint = shade(color, -4)
    thing = color
  }
})";
    const Program program = parse(source);
    REQUIRE(program.asset.inputs[0].name == "color");
    const auto& body = program.asset.functions[0].body;
    REQUIRE(body.size() == 3);
    // `tint = shade(color, -4)` — color as a positional argument.
    const auto* tint = std::get_if<VarDecl>(&body[1]);
    REQUIRE(tint != nullptr);
    const auto* shadeCall = std::get_if<Call>(tint->value.get());
    REQUIRE(shadeCall != nullptr);
    REQUIRE(shadeCall->args.size() == 2);
    REQUIRE(std::holds_alternative<Identifier>(*shadeCall->args[0].value));
    // `thing = color` — color as the whole right-hand side.
    const auto* thing = std::get_if<VarDecl>(&body[2]);
    REQUIRE(thing != nullptr);
    REQUIRE(std::holds_alternative<Identifier>(*thing->value));
}

TEST_CASE("size values out of range are rejected") {
    bool threw = false;
    try {
        (void)parse("asset big { size 99999999999x1 }");
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(std::string(error.message).find("width out of range") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("an empty program is one located error") {
    bool threw = false;
    try {
        (void)parse("");
    } catch (const SourceError& error) {
        threw = true;
        REQUIRE(error.location.line == 1);
        REQUIRE(error.location.column == 1);
        REQUIRE(std::string(error.message).find("expected an 'asset' block") !=
                std::string::npos);
    }
    REQUIRE(threw);
}

TEST_CASE("malformed programs produce one located error") {
    struct Case {
        const char* source;
        const char* messagePart;
    };
    const Case cases[] = {
        {"asset { }", "expected an asset name"},
        {"asset rock", "expected '{'"},
        {"asset rock { size }", "expected a width"},
        {"asset rock { size 192 abc }", "height"},
        {"asset rock { input color c }", "'='"},
        {"asset rock { input bogus c = 1 }", "input type"},
        {"asset rock { output blob c }", "output kind"},
        {"asset rock { draw v }", "'(' after the function name"},
        {"asset rock { draw(v: i32, a: i32) { w = } }", "expected an expression"},
        {"asset rock { draw(v: i32, a: i32) { 5 = 3 } }", "expected a statement"},
        {"asset rock { draw(v: i32, a: i32) { foo; } }", "must be a call"},
        {"asset rock { draw(v: i32, a: i32) { if x { } } }", "'(' after 'if'"},
        {"asset rock { draw(v: i32, a: i32) { if (x) 5 } }", "expected '{'"},
        {"asset rock { draw(v: i32, a: i32) {", "unterminated"},
        {"import hair from \"hair.ips\"", "expected an 'asset' block"},
        {"import hair from 5", "quoted file path"},
        {"asset a { } asset b { }", "multiple asset blocks"},
        {"asset rock { draw(v: i32, a: i32) { w = - } }", "expected an expression"},
        {"asset rock { draw(v: i32, a: i32) { w = {1, 2 } }", "unterminated"},
        {"asset rock { draw(v: i32, a: i32) { return 1 2 } }", "expected a statement"},
    };

    for (const Case& testCase : cases) {
        INFO(testCase.source);
        bool threw = false;
        try {
            (void)parse(testCase.source);
        } catch (const SourceError& error) {
            threw = true;
            REQUIRE(error.location.line == 1);
            REQUIRE(std::string(error.message).find(testCase.messagePart) !=
                    std::string::npos);
        }
        REQUIRE(threw);
    }
}

TEST_CASE("parse errors point at the right line and column") {
    const std::string source = "asset rock {\n  size 192x192\n  input color c = }\n}";
    try {
        (void)parse(source);
        FAIL("expected a SourceError");
    } catch (const SourceError& error) {
        // The offending token is the `}` on line 3, column 19.
        REQUIRE(error.location.line == 3);
        REQUIRE(error.location.column == 19);

        // The caret snippet lands under the token.
        const std::string formatted = formatError(source, error);
        REQUIRE(formatted.find("3:19: error:") != std::string::npos);
        REQUIRE(formatted.find("input color c = }") != std::string::npos);
        REQUIRE(formatted.find(std::string(18, ' ') + "^") != std::string::npos);
    }
}

TEST_CASE("parseTokens accepts a pre-lexed stream") {
    const auto tokens = lex("asset t { size 8x8 }");
    const Program program = parseTokens(tokens);
    REQUIRE(program.asset.name == "t");
    REQUIRE(program.asset.sizeX == 8u);
}
