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
        const auto attempt = [&](auto pass) {
            auto candidatePlan = *plan;
            pass(candidatePlan);
            compiler::eliminateDeadCode(candidatePlan);
            compiler::compactPools(candidatePlan);
            auto candidate = compiler::Assembler(candidatePlan).assemble();
            // Compare complete artifacts at every pass, including pools,
            // tables and debug; retain the best plan in both output modes.
            if (candidate.size() <= bytes.size() && stripDebug(candidate).size() <= stripDebug(bytes).size()) {
                *plan = std::move(candidatePlan);
                bytes = std::move(candidate);
            }
        };
        attempt(compiler::eliminateDeadCode);
        attempt(compiler::foldAndPeephole);
        attempt(compiler::reuseCommonExpressions);
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
