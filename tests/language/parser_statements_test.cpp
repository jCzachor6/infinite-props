#include <catch2/catch_test_macros.hpp>

#include <string>

#include "ips/Ast.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"

// The .ips parser: sketch grammars parse into the expected AST, malformed
// programs produce exactly one located error, and edge-case grammar forms
// (semicolons, else-if, soft keywords as values) are pinned.

using namespace iprop::ips;

TEST_CASE("expression precedence and forms") {
    const std::string source = R"(
asset expr {
  draw {
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

TEST_CASE("user functions beyond draw parse") {
    const std::string source = R"(
asset multi {
  draw {
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
  draw {
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

TEST_CASE("for loops parse into their statements") {
    const std::string source = R"(
asset loop {
  draw {
    for i in 0..10 {
      pixel(i, 0, #ffffff)
    }
  }
})";
    const Program program = parse(source);
    const auto& body = program.asset.functions[0].body;
    const auto* loop = std::get_if<ForStmt>(&body[0]);
    REQUIRE(loop != nullptr);
    REQUIRE(loop->variable == "i");
    REQUIRE(std::holds_alternative<Literal>(*loop->from));
    REQUIRE(std::get<Literal>(*loop->from).kind == Literal::Kind::Int);
    REQUIRE(std::get<Literal>(*loop->from).text == "0");
    REQUIRE(std::get<Literal>(*loop->to).text == "10");
    REQUIRE(loop->body.size() == 1);
}

TEST_CASE("break and continue parse into their statements") {
    const std::string source = R"(
asset loop {
  draw {
    i = 0
    while (i < 10) {
      i = i + 1
      if (i == 3) { continue }
      if (i == 7) { break }
    }
  }
})";
    const Program program = parse(source);
    const auto& body = program.asset.functions[0].body;
    const auto* loop = std::get_if<WhileStmt>(&body[1]);
    REQUIRE(loop != nullptr);
    REQUIRE(loop->body.size() == 3);
    const auto* inner = std::get_if<IfStmt>(&loop->body[1]);
    REQUIRE(inner != nullptr);
    REQUIRE(inner->thenBranch.size() == 1);
    REQUIRE(std::holds_alternative<ContinueStmt>(inner->thenBranch[0]));
    const auto* inner2 = std::get_if<IfStmt>(&loop->body[2]);
    REQUIRE(inner2 != nullptr);
    REQUIRE(std::holds_alternative<BreakStmt>(inner2->thenBranch[0]));
}

TEST_CASE("semicolons are optional statement separators") {
    const std::string source = R"(
asset semi {
  draw {
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
  draw {
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
  draw {
    u = 1.5
    n = ageFromUnit(u)
  }
  ageFromUnit(u: f32) -> i32 {
    return i32trunc(u * 10)
  }
  leaf(x: f32, y: f32, rx: f32, ry: f32, lean: f32, c: color) -> color {
    return color.shade(c, 1)
  }
})";
    const Program program = parse(source);

    const Function& draw = program.asset.functions[0];
    REQUIRE(draw.params.empty());
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

TEST_CASE("named arguments may come first") {
    const Program program =
        parse("asset n { draw { f(at = {0, 0}, 5) } }");
    const auto& body = program.asset.functions[0].body;
    const auto* stmt = std::get_if<ExprStmt>(&body[0]);
    const auto* call = std::get_if<Call>(stmt->expr.get());
    REQUIRE(call->args.size() == 2);
    REQUIRE(call->args[0].name == "at");
    REQUIRE(!call->args[1].name.has_value());
}
