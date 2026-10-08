#include "ips/Imports.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

#include "ips/Ast.hpp"
#include "ips/SourceError.hpp"

namespace iprop::ips {

std::string resolveImportPath(std::string_view importer, std::string_view path) {
    std::string joined;
    if (!path.starts_with('/')) {
        const auto slash = importer.rfind('/');
        if (slash != std::string_view::npos) joined.assign(importer.substr(0, slash + 1));
    }
    joined.append(path);
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= joined.size()) {
        const auto end = std::min(joined.find('/', start), joined.size());
        const std::string segment = joined.substr(start, end - start);
        if (segment == "..") {
            if (!segments.empty() && segments.back() != ".." && !segments.back().empty()) segments.pop_back();
            else segments.push_back(segment);
        } else if (segment != "." && !(segment.empty() && !segments.empty())) {
            segments.push_back(segment); // a leading empty segment keeps '/'
        }
        start = end + 1;
    }
    std::string result;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i) result += '/';
        result += segments[i];
    }
    return result.empty() ? std::string(".") : result;
}

std::vector<std::string> resolveImportOrder(
    std::string_view entryPath,
    const std::function<const Program&(std::string_view path)>& loadProgram,
    const ImportResolver& resolve) {
    std::vector<std::string> order;
    std::set<std::string> done;
    std::vector<std::string> chain;
    std::set<std::string> inChain;
    std::function<void(const std::string&, SourceLocation, const std::string&)> visit =
        [&](const std::string& path, SourceLocation importedAt, const std::string& importer) {
            if (done.count(path) != 0) {
                return;
            }
            if (inChain.count(path) != 0) {
                // Find where the cycle starts and describe it.
                std::string message = "import cycle: ";
                std::size_t start = 0;
                while (chain[start] != path) {
                    ++start;
                }
                for (std::size_t i = start; i < chain.size(); ++i) {
                    message += chain[i] + " -> ";
                }
                message += path;
                throw SourceError{importedAt, std::move(message), importer,
                    DiagnosticCode::ImportCycle, DiagnosticStage::Import};
            }
            inChain.insert(path);
            chain.push_back(path);

            const Program* program = nullptr;
            try {
                program = &loadProgram(path);
            } catch (SourceError& error) {
                if (error.path.empty()) error.path = path;
                throw;
            } catch (const std::runtime_error& e) {
                throw SourceError{importedAt,
                                  std::string("cannot load '") + path +
                                      "': " + e.what(), importer,
                                      DiagnosticCode::SourceIO, DiagnosticStage::IO};
            }
            for (const auto& import : program->imports) {
                visit(resolve ? resolve(path, import.path) : resolveImportPath(path, import.path),
                      import.location, path);
            }
            chain.pop_back();
            inChain.erase(path);
            done.insert(path);
            order.push_back(path);
        };

    visit(std::string(entryPath), {1,1}, std::string(entryPath));
    return order;
}

} // namespace iprop::ips
