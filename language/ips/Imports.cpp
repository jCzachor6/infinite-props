#include "ips/Imports.hpp"

#include <set>
#include <map>
#include <utility>

#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"

namespace iprop::ips {

std::vector<std::string> resolveImportOrder(
    std::string_view entryPath,
    const std::function<const Program&(std::string_view path)>& loadProgram) {
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
                throw SourceError{importedAt, std::move(message), importer};
            }
            inChain.insert(path);
            chain.push_back(path);

            const Program* program = nullptr;
            try {
                program = &loadProgram(path);
            } catch (SourceError& error) {
                if (error.path.empty()) error.path = path;
                throw;
            } catch (const std::exception& e) {
                throw SourceError{importedAt,
                                  std::string("cannot load '") + path +
                                      "': " + e.what(), importer};
            }
            for (const auto& import : program->imports) {
                visit(import.path, import.location, path);
            }
            chain.pop_back();
            inChain.erase(path);
            done.insert(path);
            order.push_back(path);
        };

    visit(std::string(entryPath), {1,1}, std::string(entryPath));
    return order;
}

std::vector<std::string> resolveImports(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource) {
    std::map<std::string, Program> programs;
    return resolveImportOrder(entryPath, [&](std::string_view path) -> const Program& {
        return programs.emplace(std::string(path), parse(loadSource(path))).first->second;
    });
}

} // namespace iprop::ips
