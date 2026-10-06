#include <catch2/catch_test_macros.hpp>

#include <bit>

#include "Compiler.hpp"
#include "iprop/Debug.hpp"
#include "Plan.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Parser.hpp"
#include "ips/Printer.hpp"
#include "ips/Semantics.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

namespace {
const ips::Call& callOf(const ips::Stmt& stmt) {
    return std::get<ips::Call>(*std::get<ips::ExprStmt>(stmt).expr);
}
}

TEST_CASE("bound call plans survive nested scope preparation and are never rebound by lowering") {
    auto program = ips::parse(R"(asset t { size 8x8 palette p=[0.0,1.0]
      draw {
        sink(y=later(2),x=later(1))
        rect(color=#fff,h=4,w=4,y=0,x=0)
        ramp(t=0.5,palette=p)
      }
      later(tag:i32)->i32 { return tag }
      sink(x:f32,y:f32) {}
    })");
    auto plan = compiler::Planner(program, {}).plan();
    auto& function = plan->functions[0];
    REQUIRE(function.boundCalls.size() == 5);
    const auto& helper = callOf(program.asset.functions[0].body[0]);
    const auto& bound = function.boundCalls.at(&helper);
    CHECK(bound.arguments[0] == &helper.args[1]);
    CHECK(bound.arguments[1] == &helper.args[0]);
    CHECK(bound.signature.evaluationOrder == std::vector<std::size_t>{0,1});
    CHECK(bound.signature.effects == findOpcode(Opcode::Call)->effects);
    const auto& rectangle = callOf(program.asset.functions[0].body[1]);
    CHECK(function.boundCalls.at(&rectangle).signature.effects == findOpcode(Opcode::Rect)->effects);
    const auto& ramp = callOf(program.asset.functions[0].body[2]);
    CHECK(function.boundCalls.at(&ramp).signature.effects == OperationEffects{});
    // If lowering independently maps the spelling again these become unknown
    // parameters. The stored binding, expression identities and widening stay.
    for (auto& stmt : program.asset.functions[0].body)
        for (auto& arg : std::get<ips::Call>(*std::get<ips::ExprStmt>(stmt).expr).args)
            if (arg.name) arg.name = "renamed_after_binding";
    const compiler::CallResolver resolver(*plan, function);
    const auto lowered = std::get<compiler::UserCall>(resolver.resolve(helper).target);
    REQUIRE(lowered.arguments.size() == 2);
    CHECK(lowered.arguments[0].expression == helper.args[1].value.get());
    CHECK(lowered.arguments[0].widen);
    for (const auto& stmt : program.asset.functions[0].body)
        CHECK_NOTHROW(resolver.resolve(callOf(stmt)));
}

TEST_CASE("part binding records declaration defaults without resolving them while assembling surfaces") {
    const auto part = ips::parse(R"(asset child { size 4x4
      input f32 z=-0.0 input i32 invalid=1.5 draw {} })");
    const auto info = ips::partInfoOf(part.asset);
    const auto signature = ips::partCallSignature(info.inputs);
    CHECK(signature.evaluationOrder == std::vector<std::size_t>{7,8,0,1,2,3,4,5,6});
    CHECK(signature.effects == findOpcode(Opcode::PartDraw)->effects);
    CHECK(std::get<const ips::InputDecl*>(signature.defaults[7]) == &part.asset.inputs[0]);
    CHECK(std::get<const ips::InputDecl*>(signature.defaults[8]) == &part.asset.inputs[1]);
    CHECK(std::get<i32>(signature.defaults[4]) == 0);
    const auto root = ips::parse("import child from \"child.ips\" asset t {size 8x8 draw{child.draw(seed,v,a,at={0,0})}}");
    CHECK_NOTHROW(ips::checkAsset(root, {{"child",info}}));
    // The invalid default belongs to the imported declaration, never the call.
    const auto errors = ips::checkAssetDiagnostics(part, {});
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].location.line == part.asset.inputs[1].location.line);
    CHECK(errors[0].message.find("default") != std::string::npos);
}

TEST_CASE("source-only formatting preserves unknown imports and orders known helper and part signatures") {
    const auto part = ips::parse("asset child {size 4x4 input f32 z=1.0 input f32 b=2.0 draw{}}");
    const auto root = ips::parse(R"(import child from "child.ips" asset t {size 8x8 draw {
      sink(y=2,x=1)
      child.draw(a=a,b=unit(),z=unit(),at={0,0},v=v,seed=seed)
      poly(#fff,0,0,4,0,0,4)
    } sink(x:f32,y:f32) {} })");
    const auto unknown = ips::formatProgram(root);
    CHECK(unknown.find("child.draw(a = a, b = unit(), z = unit(), at = {0, 0}, v = v, seed = seed)") != std::string::npos);
    CHECK(unknown.find("sink(x = 1, y = 2)") != std::string::npos);
    CHECK(unknown.find("poly(x0 = 0, y0 = 0, x1 = 4, y1 = 0, x2 = 0, y2 = 4, #ffffff)") != std::string::npos);
    const auto known = ips::formatProgram(root, {{"child",&part}});
    CHECK(known.find("child.draw(z = unit(), b = unit(), seed = seed, v = v, a = a, at = {0, 0})") != std::string::npos);
    const auto reparsed = ips::parse(known);
    CHECK(ips::formatProgram(reparsed, {{"child",&part}}) == known);
    for (bool optimize : {false,true})
        CHECK(stripDebug(compileProgram(root, {{"child",&part}}, optimize)) ==
              stripDebug(compileProgram(reparsed, {{"child",&part}}, optimize)));
}

TEST_CASE("shared call plans preserve effect traces signed defaults aliases and parent stream after formatting") {
    const auto part = ips::parse(R"(asset child {size 4x4
      input f32 z=-0.0 input f32 first=1.0 input f32 second=2.0
      output point defaults
      draw {defaults={z,first}; pixel(0,0,#abc);}
    })");
    const auto root = ips::parse(R"(import child from "child.ips" import twin from "child.ips"
      asset root {size 8x8 output list point trace palette p=[0.0,1.0]
      draw {
        sink(y=note(2),x=note(1))
        poly(y2=note(8),x2=note(7),y1=note(6),color=tint(),x1=note(5),y0=note(4),x0=note(3))
        pick(value2=note(11),value0=note(9),value1=note(10))
        choose(value1=note(14),seed=note(12),value0=note(13))
        ramp(t=note(15),palette=p)
        child.draw(at={note(20),note(21)},a=note(19),seed=note(17),second=note(16),v=note(18))
        twin.draw(seed,v,a,at={4,4})
        trace={22,unit()}
      }
      note(tag:i32)->i32 {trace={tag,unit()}; return 0;}
      tint()->color {trace={70,unit()}; return #abc;}
      sink(x:f32,y:f32) {}
    })");
    const std::map<std::string,const ips::Program*> parts{{"child",&part},{"twin",&part}};
    const auto formatted = ips::parse(ips::formatProgram(root, parts));
    const auto formattedPart = ips::parse(ips::formatProgram(part));
    const std::map<std::string,const ips::Program*> formattedParts{{"child",&formattedPart},{"twin",&formattedPart}};
    for (bool optimize : {false,true}) {
        const auto before = compileProgram(root,parts,optimize);
        const auto after = compileProgram(formatted,formattedParts,optimize);
        CHECK(stripDebug(before) == stripDebug(after));
        for (u32 seed : {0u,7u,0xffffffffu}) {
            const auto actual = compiler_test::execute(after,seed);
            const auto reference = compiler_test::execute(before,seed);
            CHECK(actual.canvas.pixels == reference.canvas.pixels);
            REQUIRE(actual.outputs.size() == reference.outputs.size());
            for (std::size_t i=0;i<actual.outputs.size();++i) {
                REQUIRE(actual.outputs[i].points.size() == reference.outputs[i].points.size());
                for (std::size_t j=0;j<actual.outputs[i].points.size();++j) {
                    CHECK(std::bit_cast<u32>(actual.outputs[i].points[j].x) == std::bit_cast<u32>(reference.outputs[i].points[j].x));
                    CHECK(std::bit_cast<u32>(actual.outputs[i].points[j].y) == std::bit_cast<u32>(reference.outputs[i].points[j].y));
                }
            }
            CHECK(std::bit_cast<u32>(actual.outputs[1].points[0].x) == 0x80000000u);
        }
    }
}
