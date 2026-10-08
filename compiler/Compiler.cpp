#include "Compiler.hpp"

#include "iprop/Validator.hpp"
#include "Assemble.hpp"
#include "Emit.hpp"
#include "Plan.hpp"
#include "Optimizer.hpp"
#include "iprop/Debug.hpp"

namespace iprop {
namespace {

// The compile pipeline: plan (name resolution, local allocation, function
// ordering, expression typing and folding) → emit (typed AST → IR) → assemble
// (IR → .iprop bytes) → validate (dogfooding, spec §14).
std::vector<u8> compileImpl(
    const ips::Program& entry,
    const std::map<std::string, const ips::Program*>& parts, bool optimize,
    const std::map<std::string, std::string>& partSources) {
    std::unique_ptr<compiler::Plan> plan = compiler::Planner(entry, parts, partSources).plan();

    std::optional<compiler::Plan> originalPlan;
    if (optimize) originalPlan.emplace(*plan);
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
            try {
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
            } catch (const compiler::PoolLimit&) { /* keep the reference plan */ }
        };
        attempt(compiler::eliminateDeadCode);
        attempt(compiler::foldAndPeephole);
        attempt(compiler::reuseCommonExpressions);
        attempt(compiler::reduceStrength);

        // Re-emit from the complete planning tables: earlier passes may have
        // removed local slots that the immutable AST still refers to.
        try {
            auto unrolled = *originalPlan;
            compiler::Emitter loopEmitter(unrolled, true);
            for (auto& function : unrolled.functions) loopEmitter.emitFunction(function);
            compiler::eliminateDeadCode(unrolled);
            compiler::foldAndPeephole(unrolled);
            compiler::reuseCommonExpressions(unrolled);
            compiler::reduceStrength(unrolled);
            compiler::eliminateDeadCode(unrolled);
            compiler::compactPools(unrolled);
            auto candidate = compiler::Assembler(unrolled).assemble();
            if (candidate.size() <= bytes.size() && stripDebug(candidate).size() <= stripDebug(bytes).size())
                bytes = std::move(candidate);
        } catch (const compiler::PoolLimit&) { /* keep the previous best artifact */ }
    }

    // Dogfood: the compiled file must pass the runtime validator.
    try {
        const auto loaded = loadFile(bytes);
        validate(loaded);
    } catch (const FormatError& error) {
        throw ips::SourceError{{1, 1},
                               std::string("compiled program failed validation: ") +
                                   error.what(), {}, ips::DiagnosticCode::CompiledValidation,
                                   ips::DiagnosticStage::Validation};
    }
    return bytes;
}

} // namespace

std::vector<u8> compileProgram(const ips::Program& entry,
    const std::map<std::string, const ips::Program*>& parts, bool optimize,
    const std::map<std::string, std::string>& partSources) {
    try { return compileImpl(entry, parts, optimize, partSources); }
    catch (const compiler::PoolLimit& error) {
        throw ips::SourceError{{1,1}, error.what(), {}, ips::DiagnosticCode::ResourceLimit,
                              ips::DiagnosticStage::Planning};
    }
}

} // namespace iprop
