// ipc: the source compiler CLI. Ordinary compilation and --check share
// the full in-memory source/import/assembly/validation pipeline.
#include "SourceCompilation.hpp"
#include "source_diagnostics.hpp"
#include "language_description.hpp"
#include "ips/Printer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
bool sameFile(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code error;
    return std::filesystem::absolute(first).lexically_normal() ==
               std::filesystem::absolute(second).lexically_normal() ||
           std::filesystem::equivalent(first, second, error);
}
void writeFile(const std::string& path, const char* data, std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out.write(data, static_cast<std::streamsize>(size));
    out.close();
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--describe-language") {
        iprop::tools::writeLanguageDescription(std::cout, IPROP_PRODUCT_VERSION);
        return std::cout ? 0 : 1;
    }
    // Select the renderer before parsing options, including malformed calls
    // in which the JSON flag occurs after the invalid argument.
    bool json = false;
    for (int i=1; i<argc; ++i)
        if (std::string_view(argv[i]) == "--diagnostics=json") json = true;
    const auto fail = [&](std::string message, const char* status = "cli_error") {
        const iprop::CompilationDiagnostic diagnostic{std::move(message), {}, std::nullopt};
        if (json) iprop::tools::writeSourceDiagnosticsJson(std::cout, IPROP_PRODUCT_VERSION, status, {diagnostic});
        else iprop::tools::writeSourceDiagnosticText(std::cerr, diagnostic, {});
        return 1; // Distinct protocol exit classes are defined by task B.4.
    };
    std::string input, output, formattedOutput;
    bool outputRequested = false, check = false;
    iprop::SourceCompilationOptions options;
    for (int i=1; i<argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            if (json) return fail("--help cannot be combined with --diagnostics=json");
            std::cout << "ipc - compile .ips source into an .iprop binary\n\n"
                << "Usage: ipc <file.ips> [-o <out.iprop>] [--debug|--strip] [--no-opt] [--no-compress] [--format <out.ips>]\n"
                << "       ipc <file.ips> --check [--diagnostics=json|text]\n"
                << "       ipc --describe-language  (JSON language contracts)\n"
                << "Imports resolve relative to the entry file. Both modes run the full\n"
                << "compiler and validate the final executable in memory. --check writes\n"
                << "no files and rejects --output/--format. Debug is stripped by default;\n"
                << "--debug retains source maps; the last --strip/--debug flag wins.\n"
                << "Compression is automatic when smaller; --no-compress disables it.\n"
                << "--format writes canonical source beside the compiled result.\n";
            return 0;
        }
        if (arg == "-o" || arg == "--output" || arg == "--format") {
            if (i+1 >= argc) return fail("missing value for " + arg);
            if (std::string_view(argv[i+1]).empty()) return fail("empty value for " + arg);
            if (arg == "--format") formattedOutput = argv[++i];
            else { outputRequested = true; output = argv[++i]; }
        } else if (arg == "--check") check = true;
        else if (arg == "--diagnostics=json") json = true;
        else if (arg == "--diagnostics=text") json = false;
        else if (arg == "--strip" || arg == "--debug") options.stripDebug = arg == "--strip";
        else if (arg == "--compress" || arg == "--no-compress") options.compress = arg == "--compress";
        else if (arg == "--no-opt") options.optimize = false;
        else if (!arg.empty() && arg[0] == '-') return fail("unknown option '" + arg + "'");
        else if (input.empty()) input = arg;
        else return fail("multiple input files");
    }
    if (input.empty()) return fail("no input file");
    if (check && (outputRequested || !formattedOutput.empty()))
        return fail("--check cannot be combined with --output or --format");
    if (output.empty() && !check) {
        const auto dot = input.rfind('.');
        output = (dot == std::string::npos ? input : input.substr(0,dot)) + ".iprop";
    }
    try {
        const std::filesystem::path entryPath(input);
        const auto utf8Name = entryPath.filename().u8string();
        const std::string entryName(utf8Name.begin(), utf8Name.end());
        const auto dir = entryPath.parent_path().empty() ? std::filesystem::path(".") : entryPath.parent_path();
        const auto loadSource = [&](std::string_view path) -> std::string {
            const std::u8string utf8Path(path.begin(),path.end());
            std::ifstream stream(dir / std::filesystem::path(utf8Path), std::ios::binary);
            if (!stream) throw std::runtime_error("no such file");
            std::string source((std::istreambuf_iterator<char>(stream)), {});
            if (stream.bad()) throw std::runtime_error("source read failed");
            return source;
        };
        const auto result = iprop::compileSource(entryName, loadSource, options);
        if (!result.succeeded()) {
            const char* status = result.status == iprop::SourceCompilationStatus::InvalidSource
                ? "invalid_source" : "internal_error";
            if (json) iprop::tools::writeSourceDiagnosticsJson(std::cout, IPROP_PRODUCT_VERSION, status, result.diagnostics);
            else for (const auto& diagnostic : result.diagnostics) {
                const auto found = result.sources.find(diagnostic.path);
                iprop::tools::writeSourceDiagnosticText(std::cerr, diagnostic,
                    found == result.sources.end() ? std::string_view{} : found->second);
            }
            return 1;
        }
        if (!check) {
            std::string canonical;
            if (!formattedOutput.empty()) {
                if (sameFile(formattedOutput,input) || sameFile(formattedOutput,output))
                    return fail("--format must target a distinct source output file");
                canonical = iprop::ips::formatProgram(result.programs.at(entryName));
            }
            writeFile(output,reinterpret_cast<const char*>(result.bytes.data()),result.bytes.size());
            if (!formattedOutput.empty()) {
                // A case-insensitive alias of a new binary exists only after writing it.
                if (sameFile(formattedOutput,input) || sameFile(formattedOutput,output))
                    return fail("--format must target a distinct source output file");
                writeFile(formattedOutput,canonical.data(),canonical.size());
            }
        }
        if (json) iprop::tools::writeSourceDiagnosticsJson(std::cout, IPROP_PRODUCT_VERSION, "ok", {});
        else if (check) std::cout << "checked " << input << '\n';
        else {
            std::cout << "compiled " << input << " -> " << output << '\n';
            if (!formattedOutput.empty()) std::cout << "formatted " << input << " -> " << formattedOutput << '\n';
        }
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        return fail(error.what(), "internal_error");
    }
}
