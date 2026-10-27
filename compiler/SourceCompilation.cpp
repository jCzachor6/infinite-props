#include "SourceCompilation.hpp"

#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Constants.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Validator.hpp"
#include "ips/Imports.hpp"
#include "ips/Materials.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceText.hpp"

#include <algorithm>

namespace iprop {

SourceCompilationResult compileSource(
    std::string_view entryPath,
    const std::function<std::string(std::string_view)>& loadSource,
    SourceCompilationOptions options, const ips::ImportResolver& resolveImport) {
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
        // The host resolves each import once; the identifiers are recorded.
        const auto resolve = [&](std::string_view importer, std::string_view path) {
            auto target = resolveImport ? resolveImport(importer, path) : ips::resolveImportPath(importer, path);
            result.imports[std::string(importer)].push_back(target);
            return target;
        };
        const auto order = ips::resolveImportOrder(entryPath, loadProgram, resolve);
        if (!result.diagnostics.empty()) {
            result.status = SourceCompilationStatus::InvalidSource;
            return result;
        }
        if (result.programs.at(result.entryPath).isMaterialFile) {
            appendDiagnostic(ips::SourceError{
                result.programs.at(result.entryPath).materials.front().location,
                "a material file has no asset to compile; import it from an asset",
                result.entryPath, ips::DiagnosticCode::InvalidSyntax, ips::DiagnosticStage::Parser});
            result.status = SourceCompilationStatus::InvalidSource;
            return result;
        }

        // Each file is checked once, dependencies first, against the
        // surfaces of its own direct imports.
        stage = ips::DiagnosticStage::Typing;
        for (const auto& path : order) {
            if (result.diagnostics.size() >= limit) break;
            auto& program = result.programs.at(path);
            if (program.isMaterialFile) continue;
            for (std::size_t i = 0; i < program.imports.size(); ++i) {
                const auto& imported = result.programs.at(result.imports.at(path)[i]);
                if (imported.isMaterialFile) {
                    ips::mergeMaterialFile(program, imported);
                    program.materialImportAliases.push_back(program.imports[i].alias);
                }
            }
            try {
                ips::expandMaterials(program);
            } catch (const ips::SourceError& error) {
                auto located = error;
                if (located.path.empty()) located.path = path;
                appendDiagnostic(located);
                continue;
            }
            std::map<std::string, ips::PartInfo> surfaces;
            for (std::size_t i = 0; i < program.imports.size(); ++i) {
                const auto& imported = result.programs.at(result.imports.at(path)[i]);
                if (imported.isMaterialFile) {
                    surfaces.emplace(program.imports[i].alias, ips::PartInfo{});
                    continue;
                }
                surfaces.emplace(program.imports[i].alias, ips::partInfoOf(imported.asset));
            }
            for (auto& error : ips::checkAssetDiagnostics(program, surfaces,
                                                         limit - result.diagnostics.size())) {
                if (error.path.empty()) error.path = path;
                appendDiagnostic(error);
            }
        }
        if (!result.diagnostics.empty()) {
            result.status = SourceCompilationStatus::InvalidSource;
            return result;
        }

        // One instance per alias path: a file shared by two branches is
        // parsed once but planned twice. Expansion stops at the part limit,
        // which the planner reports at the first import past it.
        std::map<std::string, const ips::Program*> parts;
        std::map<std::string, std::string> partSources;
        const auto expand = [&](auto&& self, const std::string& path, const std::string& prefix) -> void {
            const auto& imports = result.programs.at(path).imports;
            for (std::size_t i = 0; i < imports.size(); ++i) {
                if (parts.size() >= kMaxParts) return;
                const auto& source = result.imports.at(path)[i];
                if (result.programs.at(source).isMaterialFile) continue;
                parts.emplace(prefix + imports[i].alias, &result.programs.at(source));
                partSources.emplace(prefix + imports[i].alias, source);
                self(self, source, prefix + imports[i].alias + ".");
            }
        };
        expand(expand, result.entryPath, "");
        stage = ips::DiagnosticStage::Planning;
        auto bytes = compileProgram(result.programs.at(result.entryPath), parts,
                                    options.optimize, partSources);
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
