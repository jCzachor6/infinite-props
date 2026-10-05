#include "SourceCompilation.hpp"

#include "Compiler.hpp"
#include "iprop/Compression.hpp"
#include "iprop/Debug.hpp"
#include "iprop/Validator.hpp"
#include "ips/Imports.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"

namespace iprop {

SourceCompilationResult compileSource(
    std::string_view entryPath,
    const std::function<std::string(std::string_view)>& loadSource,
    SourceCompilationOptions options) {
    SourceCompilationResult result;
    result.entryPath = entryPath;
    try {
        const auto loadProgram = [&](std::string_view path) -> const ips::Program& {
            const std::string key(path);
            if (const auto found = result.programs.find(key); found != result.programs.end())
                return found->second;
            const auto source = result.sources.emplace(key, loadSource(path)).first;
            return result.programs.emplace(key, ips::parse(source->second)).first->second;
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
        for (const auto& import : entry.imports) {
            try {
                ips::checkAsset(result.programs.at(import.path), {});
            } catch (ips::SourceError& error) {
                if (error.path.empty()) error.path = import.path;
                throw;
            }
        }
        ips::checkAsset(entry, surfaces);
        auto bytes = compileProgram(entry, parts, options.optimize);
        if (options.stripDebug) bytes = stripDebug(bytes);
        if (options.compress) bytes = compressFile(bytes);
        // Validate the exact artifact returned to ordinary compile and check,
        // including transformations after compileProgram's own validation.
        validate(loadFile(bytes));
        result.bytes = std::move(bytes);
        result.status = SourceCompilationStatus::Success;
    } catch (const ips::SourceError& error) {
        result.status = SourceCompilationStatus::InvalidSource;
        result.diagnostics.push_back({error.message,
            error.path.empty() ? result.entryPath : error.path,
            result.sources.contains(error.path.empty() ? result.entryPath : error.path)
                ? std::optional{error.location} : std::nullopt});
    } catch (const std::exception& error) {
        result.status = SourceCompilationStatus::InternalError;
        result.diagnostics.push_back({error.what(), {}, std::nullopt});
    }
    return result;
}

} // namespace iprop
