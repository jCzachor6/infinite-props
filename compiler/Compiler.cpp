#include "Compiler.hpp"

#include "iprop/Validator.hpp"
#include "Assemble.hpp"
#include "Emit.hpp"
#include "Plan.hpp"
#include "Optimizer.hpp"
#include "iprop/Debug.hpp"

namespace iprop {

// The compile pipeline: plan (name resolution, local allocation, function
// ordering, expression typing and folding) → emit (typed AST → IR) → assemble
// (IR → .iprop bytes) → validate (dogfooding, spec §14).
std::vector<u8> compileProgram(
    const ips::Program& entry,
    const std::map<std::string, const ips::Program*>& parts, bool optimize) {
    std::unique_ptr<compiler::Plan> plan = compiler::Planner(entry, parts).plan();

    compiler::Emitter emitter(*plan);
    for (auto& function : plan->functions) {
        try {
            emitter.emitFunction(function);
        } catch (ips::SourceError& error) {
            if (error.path.empty()) error.path = plan->sourcePath(function.owner);
            throw;
        }
    }

    std::vector<u8> bytes = compiler::Assembler(*plan).assemble();
    if (optimize) {
        compiler::eliminateDeadCode(*plan);
        compiler::compactPools(*plan);
        auto candidate = compiler::Assembler(*plan).assemble();
        // Cost is the complete artifact, including pools, tables and debug.
        // Keep the reference lowering if either output mode would grow.
        if (candidate.size() <= bytes.size() && stripDebug(candidate).size() <= stripDebug(bytes).size())
            bytes = std::move(candidate);
    }

    // Dogfood: the compiled file must pass the runtime validator.
    try {
        const auto loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        throw ips::SourceError{{1, 1},
                               std::string("compiled program failed validation: ") +
                                   error.what()};
    }
    return bytes;
}

} // namespace iprop
