#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "iprop/VM.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include <set>

using namespace iprop;

TEST_CASE("every builtin overload compiles and agrees with its VM opcode contract") {
    using T = ips::ValueType;
    const auto runtimeType = [](T type) {
        switch(type) {
        case T::I32: return ValueType::I32;
        case T::F32: return ValueType::F32;
        case T::Color: return ValueType::Color;
        case T::Vec2: return ValueType::Vec2;
        case T::Distribution: return ValueType::Distribution;
        default: throw std::logic_error("not a VM value");
        }
    };
    for (const auto& [name,spec] : ips::builtinSpecs()) {
        for (const auto& overload : spec.overloads) {
            INFO(name << " arity " << overload.params.size());
            const auto effects = ips::builtinEffects(spec, &overload);
            const auto signature = ips::builtinCallSignature(spec, overload.params.size(), {1,1}, name);
            CHECK(signature.effects == effects);
            CHECK_FALSE(effects.conservative);
            if (overload.result == T::Void)
                CHECK((effects.rng != RngEffect::None || effects.writesCanvas || effects.writesOutputs));
            if (overload.opcode) {
                const auto* opcode = findOpcode(*overload.opcode);
                REQUIRE(opcode);
                CHECK(effects == opcode->effects);
                if (!opcode->stack) {
                    REQUIRE((spec.lowering == ips::BuiltinLowering::ListLength || spec.lowering == ips::BuiltinLowering::Polygon ||
                             spec.lowering == ips::BuiltinLowering::SurfacePaint));
                } else {
                CHECK(opcode->stack->inputCount == overload.params.size());
                for (std::size_t i=0;i<overload.params.size();++i)
                    CHECK(accepts(opcode->stack->inputs[i],runtimeType(overload.params[i].type)));
                if (overload.result == T::Void) CHECK_FALSE(opcode->stack->result);
                else CHECK(opcode->stack->result == runtimeType(overload.result));
                }
            }
            std::string args;
            std::string named;
            std::set<std::string> names;
            int position = 0;
            for (const auto& param : overload.params) {
                CHECK_FALSE(param.name.empty());
                CHECK(names.insert(param.name).second);
                const auto type = param.type;
                if (!args.empty()) args += ",";
                ++position;
                // Exact argument types exercise each overload, including identity
                // casts; distribution parameters increase so that min < max.
                const std::string value = type == T::Layer ? "image" : name == "surface.paint" && param.name == "blend" ? "surface.legacy" :
                    type.isList() ? (type.item().isSampler() ?
                    (type.item().item() == T::Color ? "[sampler(#abc)]" : type.item().item() == T::Vec2 ? "[sampler({1,1})]" :
                     type.item().item() == T::F32 ? "[sampler(1.0)]" : "[sampler(1)]") : type.element == T::Vec2 ? "[{1,1},{6,1},{1,6}]" :
                    type.element == T::Color ? "[#112233]" : type.element == T::Distribution ? "[gaussian(1,1)]" :
                    type.element == T::F32 ? "[1.0]" : "[1]") : type == T::Color ? "#112233" : type == T::Vec2 ? "{1,1}" :
                    type == T::F32 ? (overload.result == T::Distribution ? std::to_string(position) + ".0" : "1.0") : "1";
                args += value;
                named = param.name + "=" + value + (named.empty() ? "" : "," + named);
            }
            const std::string capture = spec.lowering == ips::BuiltinLowering::SurfacePaint ? "layer image{} " : "";
            const auto program = ips::parse("asset t { size 8x8 draw { " + capture +
                "stream_push(1) " + name + "(" + args + ") } }");
            REQUIRE_NOTHROW(ips::checkAsset(program,{}));
            const auto bytes = compileProgram(program,{});
            const auto reordered = ips::parse("asset t { size 8x8 draw { " + capture +
                "stream_push(1) " + name + "(" + named + ") } }");
            REQUIRE_NOTHROW(ips::checkAsset(reordered,{}));
            CHECK(bytes == compileProgram(reordered,{}));
            const auto file = loadFile(bytes);
            REQUIRE_NOTHROW(validate(file));
            VM vm(file,{});
            CHECK_NOTHROW(vm.run());
        }
    }
}
