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

TEST_CASE("size accepts both width x height and two integers") {
    const Program a = parse("asset a { size 192x192 }");
    REQUIRE(a.asset.sizeX == 192u);
    REQUIRE(a.asset.sizeY == 192u);

    const Program b = parse("asset b { size 64 32 }");
    REQUIRE(b.asset.sizeX == 64u);
    REQUIRE(b.asset.sizeY == 32u);
}

TEST_CASE("palette declarations and indexing parse") {
    const std::string source = R"(
asset p {
  size 16x16
  palette leafDark = [#365c38, #4c603e, #264a46, #704430, #54625c]
  palette weights = [3, 1, 2]
  palette trailing = [#ffffff,]

  draw(v: i32, a: i32) {
    c = leafDark[leavesIndex]
    w = weights[0]
  }
})";
    const Program program = parse(source);
    const Asset& asset = program.asset;

    REQUIRE(asset.palettes.size() == 3);

    const PaletteDecl& leafDark = asset.palettes[0];
    REQUIRE(leafDark.name == "leafDark");
    REQUIRE(leafDark.elements.size() == 5);
    REQUIRE(std::holds_alternative<Literal>(*leafDark.elements[0]));
    REQUIRE(std::get_if<Literal>(leafDark.elements[0].get())->kind ==
            Literal::Kind::Color);

    const PaletteDecl& weights = asset.palettes[1];
    REQUIRE(weights.name == "weights");
    REQUIRE(weights.elements.size() == 3);
    REQUIRE(std::holds_alternative<Literal>(*weights.elements[2]));

    const PaletteDecl& trailing = asset.palettes[2];
    REQUIRE(trailing.elements.size() == 1); // the trailing comma is allowed

    // The index expressions in draw.
    const auto& body = asset.functions[0].body;
    REQUIRE(body.size() == 2);
    const auto* c = std::get_if<VarDecl>(&body[0]);
    REQUIRE(c != nullptr);
    const auto* cIndex = std::get_if<Index>(c->value.get());
    REQUIRE(cIndex != nullptr);
    REQUIRE(std::holds_alternative<Identifier>(*cIndex->object));
    REQUIRE(std::get_if<Identifier>(cIndex->object.get())->name == "leafDark");
    REQUIRE(std::holds_alternative<Identifier>(*cIndex->index));

    // An integer literal index.
    const auto* w = std::get_if<VarDecl>(&body[1]);
    const auto* wIndex = std::get_if<Index>(w->value.get());
    REQUIRE(wIndex != nullptr);
    REQUIRE(std::get_if<Literal>(wIndex->index.get())->kind == Literal::Kind::Int);
}

TEST_CASE("zero-parameter functions and uppercase X sizes parse") {
    const Program program =
        parse("asset z { size 192X192 draw() { stone_mass() } }");
    REQUIRE(program.asset.sizeX == 192u);
    REQUIRE(program.asset.sizeY == 192u);
    REQUIRE(program.asset.functions[0].params.empty());
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

TEST_CASE("parseTokens accepts a pre-lexed stream") {
    const auto tokens = lex("asset t { size 8x8 }");
    const Program program = parseTokens(tokens);
    REQUIRE(program.asset.name == "t");
    REQUIRE(program.asset.sizeX == 8u);
}
