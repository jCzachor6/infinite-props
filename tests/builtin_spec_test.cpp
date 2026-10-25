#include <catch2/catch_test_macros.hpp>
#include "Compiler.hpp"
#include "iprop/OpcodeSpec.hpp"
#include "iprop/VM.hpp"
#include "ips/BuiltinSpec.hpp"
#include "ips/CallPlan.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

#include <set>
#include <algorithm>

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
            const ips::BuiltinSpec concrete{spec.lowering,{overload}};
            const auto signature = ips::builtinCallSignature(concrete, overload.params.size(), {1,1}, name);
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
                             spec.lowering == ips::BuiltinLowering::SurfacePaint ||
                             (overload.result.isResource() && std::any_of(overload.params.begin(),overload.params.end(),
                                [](const auto& parameter){return parameter.type.isResource();}))));
                    CHECK((effects.checks & static_cast<u16>(EffectCheck::UnassignedHandle)) != 0);
                } else if (overload.adaptation != ips::BuiltinAdaptation::Direct) {
                    std::vector<ValueType> stack;
                    for (const auto& step : ips::builtinExpansion(overload)) {
                        if (const auto* argument = std::get_if<std::size_t>(&step))
                            stack.push_back(runtimeType(overload.params[*argument].type));
                        else {
                            const auto& contract = *findOpcode(std::get<Opcode>(step))->stack;
                            REQUIRE(stack.size() >= contract.inputCount);
                            const auto base = stack.size() - contract.inputCount;
                            for (std::size_t i = 0; i < contract.inputCount; ++i)
                                CHECK(accepts(contract.inputs[i], stack[base + i]));
                            stack.resize(base);
                            if (contract.result) stack.push_back(*contract.result);
                        }
                    }
                    if (overload.result == T::Void) CHECK(stack.empty());
                    else {
                        REQUIRE(stack.size() == 1);
                        CHECK(stack.back() == runtimeType(overload.result));
                    }
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
                const std::string value = type == T::Layer ? "image" : type == T::Mask ? "region" : name == "surface.paint" && param.name == "blend" ? "surface.legacy" :
                    type.isList() ? (type.item().isStepper() ?
                    (type.item().item() == T::Color ? "[stepper(#abc)]" : type.item().item() == T::Vec2 ? "[stepper({1,1})]" :
                     type.item().item() == T::F32 ? "[stepper(1.0)]" : "[stepper(1)]") : type.item().isSampler() ?
                    (type.item().item() == T::Color ? "[sampler(#abc)]" : type.item().item() == T::Vec2 ? "[sampler({1,1})]" :
                     type.item().item() == T::F32 ? "[sampler(1.0)]" : "[sampler(1)]") : type.element == T::Vec2 ? "[{1,1},{6,1},{1,6}]" :
                    type.element == T::Color ? "[#112233]" : type.element == T::Distribution ? "[gaussian(1,1)]" :
                    type.element == T::F32 ? "[1.0]" : "[1]") : type == T::Color ? "#112233" : type == T::Vec2 ? "{1,1}" :
                    type == T::F32 ? (overload.result == T::Distribution ? std::to_string(position) + ".0" : "1.0") : "1";
                args += value;
                named = param.name + "=" + value + (named.empty() ? "" : "," + named);
            }
            const bool layer = std::any_of(overload.params.begin(),overload.params.end(),[](const auto& param){return param.type.isResource();});
            const bool mask = std::any_of(overload.params.begin(),overload.params.end(),[](const auto& param){return param.type==T::Mask;});
            const std::string capture = layer ? "layer image{} " + std::string(mask ? "region=mask.from(image) " : "") : "";
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
