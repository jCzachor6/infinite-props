#include "compiler_support.hpp"
#include "Compiler.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

namespace compiler_test {
using namespace iprop;

std::vector<u8> compileChecked(const std::string& source) {
    const ips::Program program = ips::parse(source);
    ips::checkAsset(program, {});
    return compileProgram(program, {});
}

RunOutcome execute(const std::vector<u8>& bytes, u32 seed) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    VM vm(loaded, GenerationInputs{seed, 0, 0, {}});
    vm.run();
    const GenerationResult result = vm.result();
    return {result.image, result.outputs};
}

RunOutcome run(const std::vector<u8>& bytes, u32 v, u32 a) {
    const auto loaded = loadFile(bytes);
    validate(loaded);
    GenerationInputs inputs;
    inputs.seed = 42;
    UniformValue value;
    value.type = InputType::U32;
    value.u = v;
    inputs.uniforms.emplace("v", value);
    value.u = a;
    inputs.uniforms.emplace("a", value);
    VM vm(loaded, inputs);
    vm.run();
    const GenerationResult result = vm.result();
    return {result.image, result.outputs};
}

} // namespace compiler_test
