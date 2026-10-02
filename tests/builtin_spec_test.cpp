#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "iprop/VM.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

using namespace iprop;

TEST_CASE("every builtin overload compiles and agrees with its VM opcode contract") {
    using T = ips::ValueType;
    const auto runtimeType = [](T type) {
        switch(type) {
        case T::I32: return ValueType::I32;
        case T::F32: return ValueType::F32;
        case T::Color: return ValueType::Color;
        case T::Vec2: return ValueType::Vec2;
        default: throw std::logic_error("not a VM value");
        }
    };
    for (const auto& [name,spec] : ips::builtinSpecs()) {
        for (const auto& overload : spec.overloads) {
            INFO(name << " arity " << overload.params.size());
            if (overload.opcode) {
                const auto* opcode = findOpcode(*overload.opcode);
                REQUIRE(opcode);
                REQUIRE(opcode->stack);
                CHECK(opcode->stack->inputCount == overload.params.size());
                for (std::size_t i=0;i<overload.params.size();++i)
                    CHECK(accepts(opcode->stack->inputs[i],runtimeType(overload.params[i])));
                if (spec.result == T::Void) CHECK_FALSE(opcode->stack->result);
                else CHECK(opcode->stack->result == runtimeType(spec.result));
            }
            std::string args;
            for (const auto type : overload.params) {
                if (!args.empty()) args += ",";
                // Integer literals for every numeric argument exercise widening.
                args += type == T::Color ? "#112233" : type == T::Vec2 ? "{1,1}" : "1";
            }
            const auto program = ips::parse("asset t { size 8x8 draw(v:i32,a:i32) { "
                "stream_push(1) " + name + "(" + args + ") } }");
            REQUIRE_NOTHROW(ips::checkAsset(program,{}));
            const auto file = loadFile(compileProgram(program,{}));
            REQUIRE_NOTHROW(validate(file));
            VM vm(file,{});
            CHECK_NOTHROW(vm.run());
        }
    }
}
