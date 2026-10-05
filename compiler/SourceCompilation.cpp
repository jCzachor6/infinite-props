#include "SourceCompilation.hpp"

#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Validator.hpp"
#include "ips/Imports.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceText.hpp"

#include <algorithm>
#include <set>

namespace iprop {

SourceCompilationResult compileSource(
    std::string_view entryPath,
    const std::function<std::string(std::string_view)>& loadSource,
    SourceCompilationOptions options) {
    SourceCompilationResult result;
    result.entryPath = entryPath;
    auto stage = ips::DiagnosticStage::Import;
    const auto limit = std::clamp<std::size_t>(options.diagnosticLimit, 1, 20);
    const auto appendDiagnostic = [&](const ips::SourceError& error) {
        if (result.diagnostics.size() >= limit) return;
        const auto phase = error.stage == ips::DiagnosticStage::Unspecified ? stage : error.stage;
        const auto code = error.code == ips::DiagnosticCode::Unspecified ? ips::defaultDiagnosticCode(phase) : error.code;
        const auto path = error.path.empty() ? result.entryPath : error.path;
        CompilationDiagnostic diagnostic{error.message, path,
            result.sources.contains(path) ? std::optional{error.location} : std::nullopt, code, phase};
        diagnostic.expectedType = error.expectedType;
        diagnostic.actualType = error.actualType;
        diagnostic.suggestions = error.suggestions;
        if (const auto found = result.sources.find(path); found != result.sources.end()) {
            diagnostic.range = ips::sourceRangeAt(found->second, error.location);
            diagnostic.sourceId = ips::sourceContentId(found->second);
            for (const auto& edit : error.edits) {
                if (const auto range = ips::sourceRangeAt(found->second, edit.location))
                    diagnostic.fixIts.push_back({*range, edit.replacement, *diagnostic.sourceId});
            }
        }
        result.diagnostics.push_back(std::move(diagnostic));
    };
    struct DiagnosticLimitReached {};
    try {
        // Import discovery may inspect complete imports from a failed parse,
        // but these stand-ins never enter result.programs or semantic analysis.
        std::map<std::string, ips::Program> discoveryOnly;
        const auto loadProgram = [&](std::string_view path) -> const ips::Program& {
            const std::string key(path);
            if (const auto found = result.programs.find(key); found != result.programs.end())
                return found->second;
            if (const auto found = discoveryOnly.find(key); found != discoveryOnly.end())
                return found->second;
            if (result.diagnostics.size() >= limit) throw DiagnosticLimitReached{};
            const auto source = result.sources.emplace(key, loadSource(path)).first;
            auto parsed = ips::parseRecovering(source->second, limit - result.diagnostics.size());
            for (auto& error : parsed.diagnostics) {
                if (error.path.empty()) error.path = key;
                appendDiagnostic(error);
            }
            if (parsed.valid())
                return result.programs.emplace(key, std::move(*parsed.program)).first->second;
            auto& discovery = discoveryOnly.try_emplace(key).first->second;
            discovery.imports = std::move(parsed.imports);
            return discovery;
        };
        (void)ips::resolveImportOrder(entryPath, loadProgram);
        if (!result.diagnostics.empty()) {
            result.status = SourceCompilationStatus::InvalidSource;
            return result;
        }

        const auto& entry = result.programs.at(result.entryPath);
        std::map<std::string, const ips::Program*> parts;
        std::map<std::string, ips::PartInfo> surfaces;
        for (const auto& import : entry.imports) {
            const auto& part = result.programs.at(import.path);
            parts.emplace(import.alias, &part);
            surfaces.emplace(import.alias, ips::partInfoOf(part.asset));
        }
        stage = ips::DiagnosticStage::Typing;
        std::set<std::string> checkedParts;
        for (const auto& import : entry.imports) {
            if (result.diagnostics.size() >= limit) break;
            if (!checkedParts.insert(import.path).second) continue;
            for (auto& error : ips::checkAssetDiagnostics(result.programs.at(import.path), {},
                                                         limit - result.diagnostics.size())) {
                if (error.path.empty()) error.path = import.path;
                appendDiagnostic(error);
            }
        }
        if (result.diagnostics.size() < limit) {
            for (const auto& error : ips::checkAssetDiagnostics(entry, surfaces,
                                                               limit - result.diagnostics.size()))
                appendDiagnostic(error);
        }
        if (!result.diagnostics.empty()) {
            result.status = SourceCompilationStatus::InvalidSource;
            return result;
        }
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
    } catch (const DiagnosticLimitReached&) {
        result.status = SourceCompilationStatus::InvalidSource;
    } catch (const ips::SourceError& error) {
        result.status = SourceCompilationStatus::InvalidSource;
        appendDiagnostic(error);
    } catch (const FormatError& error) {
        result.status = SourceCompilationStatus::InvalidSource;
        appendDiagnostic(ips::SourceError{{1,1}, error.what(), {},
            ips::DiagnosticCode::CompiledValidation, ips::DiagnosticStage::Validation});
    } catch (const std::exception& error) {
        result.status = SourceCompilationStatus::InternalError;
        result.diagnostics.push_back({error.what(), {}, std::nullopt});
    }
    return result;
}

} // namespace iprop
