// ipc: the .ips compiler CLI — source → .iprop binary.
//
//   ipc rock.ips -o rock.iprop
//
// The pipeline: lex/parse the entry and its imported parts (import paths
// resolve relative to the entry file), resolve the import DAG, run semantic
// checks, lower to bytecode and emit a validated .iprop file.

#include "Compiler.hpp"

#include "iprop/Debug.hpp"

#include "ips/Imports.hpp"
#include "ips/Parser.hpp"
#include "ips/Semantics.hpp"
#include "ips/SourceError.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void reportError(const std::string& path, const std::string& source,
                 const iprop::ips::SourceError& error) {
    std::cerr << "ipc: " << path << ":"
              << iprop::ips::formatError(source, error);
}

int fail(const std::string& message) {
    std::cerr << "ipc: " << message << "\n";
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string input;
    std::string output;
    bool strip = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "ipc - compile .ips source into an .iprop binary\n\n"
                      << "Usage: ipc <file.ips> [-o <out.iprop>] [--strip]\n"
                      << "Imports resolve relative to the entry file; the\n"
                      << "output passes the runtime validator. --strip removes\n"
                      << "the debug section (identical execution).\n";
            return 0;
        }
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                return fail("missing value for " + arg);
            }
            output = argv[++i];
        } else if (arg == "--strip") {
            strip = true;
        } else if (!arg.empty() && arg[0] == '-') {
            return fail("unknown option '" + arg + "'");
        } else if (input.empty()) {
            input = arg;
        } else {
            return fail("multiple input files");
        }
    }
    if (input.empty()) {
        return fail("no input file");
    }
    if (output.empty()) {
        const auto dot = input.rfind('.');
        output = (dot == std::string::npos ? input : input.substr(0, dot)) + ".iprop";
    }

    std::ifstream stream(input);
    if (!stream) {
        return fail("cannot open '" + input + "'");
    }
    std::string entrySource((std::istreambuf_iterator<char>(stream)), {});

    const std::filesystem::path entryPath(input);
    const auto dir = entryPath.parent_path().empty()
                         ? std::filesystem::path(".")
                         : entryPath.parent_path();

    const auto loadSource = [&](std::string_view path) -> std::string {
        const std::filesystem::path resolved =
            dir / std::filesystem::path(std::string(path));
        std::ifstream file(resolved);
        if (!file) {
            throw std::runtime_error("no such file");
        }
        return std::string((std::istreambuf_iterator<char>(file)), {});
    };

    try {
        using namespace iprop::ips;

        const std::vector<std::string> order =
            resolveImports(entryPath.filename().string(), loadSource);

        std::map<std::string, std::string> sources;
        std::map<std::string, Program> programs;
        for (const auto& path : order) {
            std::string source = loadSource(path);
            sources.emplace(path, source);
            try {
                programs.emplace(path, parse(source));
            } catch (const SourceError& error) {
                reportError(path, source, error);
                return 1;
            }
        }

        const Program& entry = programs.at(entryPath.filename().string());
        std::map<std::string, const Program*> partPrograms;
        std::map<std::string, PartInfo> partInfos;
        for (const auto& import : entry.imports) {
            const Program& part = programs.at(import.path);
            partPrograms.emplace(import.alias, &part);
            partInfos.emplace(import.alias, partInfoOf(part.asset));
        }

        // Check each part standalone, then the entry with the part surfaces.
        for (const auto& import : entry.imports) {
            const std::string& path = import.path;
            try {
                checkAsset(programs.at(path), {});
            } catch (const SourceError& error) {
                reportError(path, sources.at(path), error);
                return 1;
            }
        }
        try {
            checkAsset(entry, partInfos);
        } catch (const SourceError& error) {
            reportError(entryPath.filename().string(), entrySource, error);
            return 1;
        }

        std::vector<iprop::u8> bytes =
            iprop::compileProgram(entry, partPrograms);
        if (strip) {
            bytes = iprop::stripDebug(bytes);
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            return fail("cannot write '" + output + "'");
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    } catch (const iprop::ips::SourceError& error) {
        // Import-DAG and compiler errors carry their own locations.
        std::cerr << "ipc: " << error.message << "\n";
        return 1;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    std::cout << "compiled " << input << " -> " << output << "\n";
    return 0;
}
