#include <catch2/catch_test_macros.hpp>

#include "Compiler.hpp"
#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"
#include "support/compiler_support.hpp"

using namespace iprop;

TEST_CASE("draw slots contain only authored inputs in declaration order") {
    const auto empty = compiler_test::compileChecked("asset t { size 1x1 draw {} }");
    const auto plain = loadFile(empty);
    CHECK(plain.layout.header.versionMinor == 7);
    CHECK(plain.meta.inputs.empty());
    CHECK(plain.code.entry().params.empty());
    const auto child = ips::parse(R"(asset child { size 1x1
      input f32 a=2 input color v=#123 input i32 variants=4 input i32 archetypes=5
      output point p draw{p={a+v.r,variants+archetypes}} })");
    const auto root = ips::parse(R"(import child from "child.ips" asset root {size 2x2
      input color v=#234 input f32 a=7 input i32 variants=8 input i32 archetypes=9
      draw{child.draw(seed,at={0,0},v=v,a=a,variants=variants,archetypes=archetypes)} })");
    const auto file = loadFile(compileProgram(root, {{"child", &child}}));
    CHECK(file.code.entry().params == std::vector<ValueType>{ValueType::Color,ValueType::F32,ValueType::I32,ValueType::I32});
    REQUIRE(file.code.parts.size() == 1);
    const auto& part = file.code.functions[file.code.parts[0].functionIndex];
    CHECK(part.params == std::vector<ValueType>{ValueType::F32,ValueType::Color,ValueType::I32,ValueType::I32});
    validate(file);
    VM vm(file, {}); vm.run();
    CHECK(vm.result().outputs[0].points[0].x == 41.f);
    CHECK(vm.result().outputs[0].points[0].y == 17.f);
    CHECK(file.containerHeader.versionMinor == 7);
}

TEST_CASE("retired names have no implicit binding or count declaration syntax") {
    for (const auto* name : {"v","a"}) {
        CHECK_THROWS_AS(compiler_test::compileChecked("asset t {size 1x1 draw{x="+std::string(name)+"}}"),ips::SourceError);
        CHECK_NOTHROW(compiler_test::compileChecked("asset t {size 1x1 draw{"+std::string(name)+"=1;x="+name+"}}"));
    }
    for (const auto* name : {"variants","archetypes"}) {
        CHECK_THROWS_AS(compiler_test::compileChecked("asset t {size 1x1 "+std::string(name)+" 3 draw{}}"),ips::SourceError);
        CHECK_NOTHROW(compiler_test::compileChecked("asset t {size 1x1 const i32 "+std::string(name)+"=3 draw{x="+name+"}}"));
    }
    const auto part = ips::parse("asset child {size 1x1 draw{}}");
    for (const auto* arguments : {"seed,0,0,at={0,0}","seed,v=0,at={0,0}","seed,a=0,at={0,0}"}) {
        const auto root=ips::parse("import child from \"child.ips\" asset root {size 1x1 draw{child.draw("+std::string(arguments)+")}}");
        CHECK_THROWS_AS(compileProgram(root,{{"child",&part}}),ips::SourceError);
    }
}
