#include "ips/Imports.hpp"

#include <set>
#include <utility>

#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"

namespace iprop::ips {

std::vector<std::string> resolveImports(
    std::string_view entryPath,
    const std::function<std::string(std::string_view path)>& loadSource) {
    std::vector<std::string> order;
    std::set<std::string> done;
    std::vector<std::string> chain;
    std::set<std::string> inChain;
    SourceLocation chainImportLocation;

    std::function<void(const std::string&)> visit =
        [&](const std::string& path) {
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
                throw SourceError{chainImportLocation, std::move(message)};
            }
            inChain.insert(path);
            chain.push_back(path);

            std::string source;
            try {
                source = loadSource(path);
            } catch (const std::exception& e) {
                throw SourceError{chainImportLocation,
                                  std::string("cannot load '") + path +
                                      "': " + e.what()};
            }
            const Program program = parse(source);
            for (const auto& import : program.imports) {
                chainImportLocation = import.location;
                visit(import.path);
            }
            chain.pop_back();
            inChain.erase(path);
            done.insert(path);
            order.push_back(path);
        };

    visit(std::string(entryPath));
    return order;
}

} // namespace iprop::ips
