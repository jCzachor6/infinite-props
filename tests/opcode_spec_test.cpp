#include <catch2/catch_test_macros.hpp>
#include "iprop/OpcodeSpec.hpp"
#include "iprop/VM.hpp"
#include "support/test_support.hpp"

#include <set>

using namespace iprop;
using namespace test_support;

namespace {
void append(std::vector<u8>& body, Opcode op, u16 operand=0, u8 aux=0) {
    const auto bytes=ins(static_cast<u8>(op),aux,operand);
    body.insert(body.end(),bytes.begin(),bytes.end());
}
std::vector<u8> program(const OpcodeSpec& spec, const std::vector<ValueType>& arguments,
                        u8 aux=0) {
    std::vector<u8> helper;
    if (spec.opcode == Opcode::StreamPop) {
        append(helper,Opcode::ConstI32); append(helper,Opcode::StreamPush);
    }
    // Distribution constructors take increasing parameters (1, 2, 3, 4) so
    // that min < max holds; every other f32 operand is 1.
    const bool increasing = spec.stack && spec.stack->result == ValueType::Distribution;
    u16 nextFloat = 0;
    for (const auto type:arguments) {
        switch (type.kind) {
        case ValueType::I32: append(helper,Opcode::ConstI32); break;
        case ValueType::F32: append(helper,Opcode::ConstF32,increasing ? nextFloat++ : 0); break;
        case ValueType::Color: append(helper,Opcode::ConstColor); break;
        case ValueType::Vec2:
            append(helper,Opcode::ConstF32); append(helper,Opcode::ConstF32);
            append(helper,Opcode::Vec2Make); break;
        case ValueType::Distribution:
            append(helper,Opcode::ConstF32); append(helper,Opcode::ConstF32);
            append(helper,Opcode::DistributionGaussian); break;
        default: throw std::logic_error("test requires a scalar opcode");
        }
    }
    append(helper,spec.opcode,0,aux); append(helper,Opcode::Return);
    std::vector<u8> draw;
    append(draw,Opcode::Call,1);
    std::vector<u8> results;
    if (spec.stack->result) { results.push_back(static_cast<u8>(*spec.stack->result)); append(draw,Opcode::Drop); }
    append(draw,Opcode::Return);
    return fullFile(minimalMeta(),constSection({"draw"},{1},{1.f,2.f,3.f,4.f},{{10,20,30,255}}),
        codeSection({drawSpec(draw),{kAnonymousName,{},results,{},helper}},{}));
}
} // namespace

TEST_CASE("opcode descriptors agree with runtime stack effects and reject wrong argument types") {
    std::set<std::string> names;
    for (const auto& spec:opcodeSpecs()) {
        INFO(spec.mnemonic);
        REQUIRE(names.insert(spec.mnemonic).second);
        CHECK(findOpcode(spec.opcode) == &spec);
        CHECK(findOpcode(spec.mnemonic) == &spec);
        if (spec.stack) {
            // An unreachable fixed-signature instruction validates like a reachable one.
            auto body=retIns();
            append(body,spec.opcode);
            CHECK_NOTHROW(validate(loadFile(fullFile(minimalMeta(),richPool({"draw"}),codeSection({drawSpec(body)},{})))));
        }
        if (!spec.stack) continue;
        const auto& sig=*spec.stack;
        for (bool scalar:{false,true}) {
            std::vector<ValueType> args;
            for (std::size_t i=0;i<sig.inputCount;++i) {
                const auto mask=sig.inputs[i];
                args.push_back(mask == StackType::I32 ? ValueType::I32 :
                    mask == StackType::F32 ? ValueType::F32 : mask == StackType::Color ? ValueType::Color :
                    mask == StackType::Distribution ? ValueType::Distribution :
                    mask == StackType::ScalarOrVec2 && scalar ? ValueType::F32 : ValueType::Vec2);
            }
            const auto file=loadFile(program(spec,args));
            REQUIRE_NOTHROW(validate(file));
            VM vm(file,{});
            REQUIRE_NOTHROW(vm.run()); // independently implemented VM must consume/return the declared types
            CHECK(vm.debugStack().empty());
            for (std::size_t i=0;i<args.size();++i) {
                const auto original=args[i];
                for (const auto wrong:{ValueType::I32,ValueType::F32,ValueType::Color,ValueType::Vec2,ValueType::Distribution}) {
                    if (accepts(sig.inputs[i],wrong)) continue;
                    args[i]=wrong;
                    CHECK_THROWS_AS(validate(loadFile(program(spec,args))),FormatError);
                }
                args[i]=original;
            }
            CHECK_THROWS_AS(validate(loadFile(program(spec,args,1))),FormatError);
            if (!args.empty()) {
                args.pop_back();
                CHECK_THROWS_AS(validate(loadFile(program(spec,args))),FormatError);
            }
        }
    }
    CHECK(findOpcode(static_cast<Opcode>(0xff)) == nullptr);
    CHECK(findOpcode("unknown") == nullptr);
    CHECK(names.size() == 193);
}
