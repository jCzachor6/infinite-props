#include "SourceCompilation.hpp"

#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Validator.hpp"
#include "ips/Imports.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceText.hpp"

namespace iprop {

SourceCompilationResult compileSource(
    std::string_view entryPath,
    const std::function<std::string(std::string_view)>& loadSource,
    SourceCompilationOptions options) {
    SourceCompilationResult result;
    result.entryPath = entryPath;
    auto stage = ips::DiagnosticStage::Import;
    try {
        const auto loadProgram = [&](std::string_view path) -> const ips::Program& {
            const std::string key(path);
            if (const auto found = result.programs.find(key); found != result.programs.end())
                return found->second;
            const auto source = result.sources.emplace(key, loadSource(path)).first;
            try {
                return result.programs.emplace(key, ips::parse(source->second)).first->second;
            } catch (ips::SourceError& error) {
                if (error.stage == ips::DiagnosticStage::Unspecified) error.stage = ips::DiagnosticStage::Parser;
                if (error.code == ips::DiagnosticCode::Unspecified) error.code = ips::defaultDiagnosticCode(error.stage);
                throw;
            }
        };
        (void)ips::resolveImportOrder(entryPath, loadProgram);

        const auto& entry = result.programs.at(result.entryPath);
        std::map<std::string, const ips::Program*> parts;
        std::map<std::string, ips::PartInfo> surfaces;
        for (const auto& import : entry.imports) {
            const auto& part = result.programs.at(import.path);
            parts.emplace(import.alias, &part);
            surfaces.emplace(import.alias, ips::partInfoOf(part.asset));
        }
        stage = ips::DiagnosticStage::Typing;
        for (const auto& import : entry.imports) {
            try {
                ips::checkAsset(result.programs.at(import.path), {});
            } catch (ips::SourceError& error) {
                if (error.path.empty()) error.path = import.path;
                throw;
            }
        }
        ips::checkAsset(entry, surfaces);
        stage = ips::DiagnosticStage::Planning;
        auto bytes = compileProgram(entry, parts, options.optimize);
        stage = ips::DiagnosticStage::Validation;
        if (options.stripDebug) bytes = stripDebug(bytes);
        if (options.compress) bytes = compressFile(bytes);
        // Validate the exact artifact returned to ordinary compile and check,
        // including transformations after compileProgram's own validation.
        validate(loadFile(bytes));
        result.bytes = std::move(bytes);
        result.status = SourceCompilationStatus::Success;
    } catch (const ips::SourceError& error) {
        result.status = SourceCompilationStatus::InvalidSource;
        const auto phase = error.stage == ips::DiagnosticStage::Unspecified ? stage : error.stage;
        const auto code = error.code == ips::DiagnosticCode::Unspecified ? ips::defaultDiagnosticCode(phase) : error.code;
        CompilationDiagnostic diagnostic{error.message,
            error.path.empty() ? result.entryPath : error.path,
            result.sources.contains(error.path.empty() ? result.entryPath : error.path)
                ? std::optional{error.location} : std::nullopt, code, phase};
        diagnostic.expectedType=error.expectedType;
        diagnostic.actualType=error.actualType;
        diagnostic.suggestions=error.suggestions;
        if (const auto found=result.sources.find(diagnostic.path); found!=result.sources.end()) {
            diagnostic.range=ips::sourceRangeAt(found->second,error.location);
            diagnostic.sourceId=ips::sourceContentId(found->second);
            for (const auto& edit : error.edits) {
                if (const auto range=ips::sourceRangeAt(found->second,edit.location))
                    diagnostic.fixIts.push_back({*range,edit.replacement,*diagnostic.sourceId});
            }
        }
        result.diagnostics.push_back(std::move(diagnostic));
    } catch (const std::exception& error) {
        result.status = SourceCompilationStatus::InternalError;
        result.diagnostics.push_back({error.what(), {}, std::nullopt});
    }
    return result;
}

} // namespace iprop
