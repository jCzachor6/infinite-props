// ipc: the source compiler CLI. Ordinary compilation and --check share
// the full in-memory source/import/assembly/validation pipeline.
#include "SourceCompilation.hpp"
#include "source_diagnostics.hpp"
#include "language_description.hpp"
#include "ips/Imports.hpp"
#include "ips/Printer.hpp"
#include "iprop/Utf8.hpp"
#include "ips/Version.hpp"
#include "ips/ExportSchema.hpp"

#include <filesystem>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::filesystem::path utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
std::string pathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
class OutputIOError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
void writeFile(const std::filesystem::path& path, const char* data, std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw OutputIOError("cannot write '" + pathText(path) + "'");
    out.write(data, static_cast<std::streamsize>(size));
    out.close();
    if (!out) throw OutputIOError("cannot write '" + pathText(path) + "'");
}
std::filesystem::path physicalPath(const std::filesystem::path& path) {
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error) throw OutputIOError("cannot resolve '" + pathText(path) + "'");
    return resolved;
}
bool sameFile(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code error;
    return std::filesystem::equivalent(first, second, error) ||
           physicalPath(first) == physicalPath(second);
}
void writeSourceAtomically(const std::filesystem::path& destination, std::string_view source) {
    // The exclusive directory owns the temporary file, on the destination's
    // filesystem. A failed write/rename leaves the previous source intact.
    struct Temporary {
        std::filesystem::path directory;
        ~Temporary() noexcept {
            try {
                std::error_code ignored;
                std::filesystem::remove_all(directory, ignored);
            } catch (...) { /* Cleanup must not replace an active exception. */ }
        }
    };
    const auto nonce = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned attempt=0; attempt<100; ++attempt) {
        auto directory = destination.parent_path() /
            utf8Path(".ipc-format-" + std::to_string(nonce) + "-" + std::to_string(attempt));
        std::error_code error;
        if (!std::filesystem::create_directory(directory, error)) {
            if (error) throw OutputIOError("cannot prepare source output '" + pathText(destination) + "'");
            continue;
        }
        Temporary temporary{std::move(directory)};
        const auto file = temporary.directory / "source.ips";
        writeFile(file, source.data(), source.size());
#ifdef _WIN32
        if (!MoveFileExW(file.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw OutputIOError("cannot replace source output '" + pathText(destination) + "'");
#else
        std::filesystem::rename(file, destination, error);
        if (error) throw OutputIOError("cannot replace source output '" + pathText(destination) + "'");
#endif
        return;
    }
    throw OutputIOError("cannot prepare unique source output '" + pathText(destination) + "'");
}
} // namespace

int runCompiler(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--describe-language") {
        iprop::tools::writeLanguageDescription(std::cout, IPROP_PRODUCT_VERSION);
        return std::cout ? 0 : 1;
    }
    // Select the renderer before parsing options, including malformed calls
    // in which the JSON flag occurs after the invalid argument.
    bool json = false;
    for (int i=1; i<argc; ++i)
        if (std::string_view(argv[i]) == "--diagnostics=json") json = true;
    const auto fail = [&](std::string message, iprop::ips::DiagnosticStage stage=iprop::ips::DiagnosticStage::CLI) {
        const iprop::CompilationDiagnostic diagnostic{std::move(message), {}, std::nullopt,
            iprop::ips::defaultDiagnosticCode(stage),stage};
        const auto status=iprop::ips::diagnosticStatusFor({diagnostic});
        if (json) iprop::tools::writeSourceDiagnosticsJson(std::cout, IPROP_PRODUCT_VERSION, status, {diagnostic});
        else iprop::tools::writeSourceDiagnosticText(std::cerr, diagnostic, {});
        return iprop::ips::diagnosticExitCode(status);
    };
    for (int i=1;i<argc;++i) {
        const std::string_view argument(argv[i]);
        if (!iprop::isValidUtf8({reinterpret_cast<const iprop::u8*>(argument.data()),argument.size()}))
            return fail("CLI arguments must be valid UTF-8");
    }
    std::string input, output, formattedOutput;
    bool outputRequested = false, check = false, formatOnly = false;
    iprop::SourceCompilationOptions options;
    for (int i=1; i<argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            if (json) return fail("--help cannot be combined with --diagnostics=json");
            std::cout << "ipc - compile .ips source into an .iprop binary\n\n"
                << "Usage: ipc <file.ips> [-o <out.iprop>] [--debug|--strip] [--no-opt] [--no-compress] [--format <out.ips>]\n"
                << "       ipc <file.ips> --check [--diagnostics=json|text]\n"
                << "       ipc <file.ips> --format <out.ips> --format-only\n"
                << "       ipc --describe-language  (JSON language contracts)\n"
                << "Imports resolve relative to the importing file. Both modes run the full\n"
                << "compiler and validate the final executable in memory. --check writes\n"
                << "no files and rejects --output/--format. Debug is stripped by default;\n"
                << "--debug retains source maps; the last --strip/--debug flag wins.\n"
                << "Compression is automatic when smaller; --no-compress disables it.\n"
                << "--format writes canonical source beside the compiled result;\n"
                << "--format-only validates and writes only that source atomically.\n";
            return 0;
        }
        if (arg == "-o" || arg == "--output" || arg == "--format") {
            if (i+1 >= argc) return fail("missing value for " + arg);
            if (std::string_view(argv[i+1]).empty()) return fail("empty value for " + arg);
            if (arg == "--format") formattedOutput = argv[++i];
            else { outputRequested = true; output = argv[++i]; }
        } else if (arg == "--check") check = true;
        else if (arg == "--format-only") formatOnly = true;
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
    if (check && (outputRequested || !formattedOutput.empty() || formatOnly))
        return fail("--check cannot be combined with --output, --format or --format-only");
    if (formatOnly && (formattedOutput.empty() || outputRequested))
        return fail("--format-only requires --format and cannot be combined with --output");
    if (output.empty() && !check && !formatOnly) {
        const auto dot = input.rfind('.');
        output = (dot == std::string::npos ? input : input.substr(0,dot)) + ".iprop";
    }
    try {
        const auto entryPath = utf8Path(input);
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
        // A file is identified by its physical location relative to the entry
        // directory, so `link/..` leaves a symlinked directory's target as the
        // OS does and each file's imports resolve beside the file itself.
        std::error_code dirError;
        auto physicalDir = std::filesystem::weakly_canonical(dir, dirError);
        if (dirError) physicalDir = dir;
        const auto resolveImport = [&](std::string_view importer, std::string_view path) -> std::string {
            const auto joined = (dir / utf8Path(importer)).parent_path() / utf8Path(path);
            std::error_code error;
            const auto physical = std::filesystem::weakly_canonical(joined, error);
            if (error) return iprop::ips::resolveImportPath(importer, path);
            const auto relative = physical.lexically_relative(physicalDir);
            return pathText(relative.empty() ? physical : relative);
        };
        auto result = iprop::compileSource(entryName, loadSource, options, resolveImport);
        if (!result.succeeded()) {
            const auto status = result.status;
            if (json) iprop::tools::writeSourceDiagnosticsJson(std::cout, IPROP_PRODUCT_VERSION, status, result.diagnostics);
            else for (const auto& diagnostic : result.diagnostics) {
                const auto found = result.sources.find(diagnostic.path);
                iprop::tools::writeSourceDiagnosticText(std::cerr, diagnostic,
                    found == result.sources.end() ? std::string_view{} : found->second);
            }
            return iprop::ips::diagnosticExitCode(status);
        }
        if (!check) {
            std::string canonical;
            std::filesystem::path sourceDestination;
            if (!formattedOutput.empty()) {
                const auto requestedDestination = utf8Path(formattedOutput);
                if (sameFile(requestedDestination,entryPath) ||
                    (!formatOnly && sameFile(requestedDestination,utf8Path(output))))
                    return fail("--format must target a distinct source output file");
                auto& program = result.programs.at(entryName);
                std::map<std::string, const iprop::ips::Program*> parts;
                // Every loaded source is protected, including nested imports;
                // source keys resolve relative to the entry's directory.
                for (const auto& [path, unused] : result.sources) {
                    (void)unused;
                    if (sameFile(requestedDestination,dir / utf8Path(path)))
                        return fail("--format must target a distinct source output file, including imports");
                }
                for (std::size_t i = 0; i < program.imports.size(); ++i) {
                    parts.emplace(program.imports[i].alias, &result.programs.at(result.imports.at(entryName)[i]));
                }
                const auto requestedParent = requestedDestination.parent_path().empty()
                    ? std::filesystem::path(".") : requestedDestination.parent_path();
                // Replace the requested directory entry, not a final symlink's
                // referent. The compiler resolves entry imports from this parent.
                sourceDestination = physicalPath(requestedParent) / requestedDestination.filename();
                // Keep aliases and trivia; only the quoted import path changes
                // when the formatted entry is relocated. Resolve symlinks before
                // deriving a relative path, including symlink/.. components.
                if (sourceDestination.parent_path() != physicalPath(dir)) {
                    for (auto& import : program.imports) {
                        const auto target = physicalPath(dir / utf8Path(import.path));
                        const auto relative = target.lexically_relative(sourceDestination.parent_path());
                        import.path = pathText(relative.empty() ? target : relative);
                    }
                }
                canonical = iprop::ips::formatProgram(program, parts);
            }
            if (!formatOnly)
                writeFile(utf8Path(output),reinterpret_cast<const char*>(result.bytes.data()),result.bytes.size());
            if (!formattedOutput.empty()) {
                // A case-insensitive alias of a new binary exists only after writing it.
                if (sameFile(utf8Path(formattedOutput),entryPath) ||
                    (!formatOnly && sameFile(utf8Path(formattedOutput),utf8Path(output))))
                    return fail("--format must target a distinct source output file");
                writeSourceAtomically(sourceDestination,canonical);
            }
        }
        if (json) iprop::tools::writeSourceDiagnosticsJson(std::cout, IPROP_PRODUCT_VERSION, iprop::ips::DiagnosticStatus::Success, {});
        else if (check) std::cout << "checked " << input << '\n';
        else {
            if (!formatOnly) std::cout << "compiled " << input << " -> " << output << '\n';
            if (!formattedOutput.empty()) std::cout << "formatted " << input << " -> " << formattedOutput << '\n';
        }
        return std::cout ? 0 : 1;
    } catch (const OutputIOError& error) {
        return fail(error.what(), iprop::ips::DiagnosticStage::IO);
    } catch (const std::exception& error) {
        return fail(error.what(), iprop::ips::DiagnosticStage::Internal);
    }
}

namespace {
int reportEntryFailure(bool json, bool invalidUtf16 = false) noexcept {
    // Literal C stdio reporting needs no C++ allocations or throwing renderer,
    // including when Windows argument conversion itself runs out of memory.
    const char* status = invalidUtf16 ? "cli_error" : "internal_error";
    const char* code = invalidUtf16 ? "IPS-CLI-001" : "IPS-INTERNAL-001";
    const char* stage = invalidUtf16 ? "cli" : "internal";
    const char* message = invalidUtf16 ? "CLI arguments must be valid UTF-16" : "unexpected compiler failure";
    if (json) std::fprintf(stdout, "{\"schema_version\":%u,\"product_version\":\"" IPROP_PRODUCT_VERSION
        "\",\"language_version\":{\"major\":%u,\"minor\":%u},\"status\":\"%s\","
        "\"diagnostics\":[{\"code\":\"%s\",\"stage\":\"%s\",\"severity\":\"error\","
        "\"message\":\"%s\",\"file\":null,\"location\":null,\"range\":null,"
        "\"source_id\":null,\"expected_type\":null,\"actual_type\":null,\"suggestions\":[],"
        "\"related_locations\":[],\"fix_its\":[]}]}\n",
        iprop::ips::kSourceDiagnosticsSchemaVersion,
        static_cast<unsigned>(iprop::ips::kLanguageVersionMajor),
        static_cast<unsigned>(iprop::ips::kLanguageVersionMinor), status, code, stage, message);
    else std::fprintf(stderr, "ipc: %s\n", message);
    return invalidUtf16 ? 2 : 3;
}
} // namespace

#if defined(_WIN32) && defined(IPROP_WINDOWS_UTF8_ENTRY)
int wmain(int argc, wchar_t* wideArgv[]) {
    bool json = false;
    // Detect the fallback renderer before converting or allocating argv.
    for (int i = 1; i < argc; ++i)
        if (std::wstring_view(wideArgv[i]) == L"--diagnostics=json") json = true;
    try {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            // -1 includes the terminator in both the size query and conversion.
            // Explicit CP_UTF8 avoids the Windows ANSI/process code page.
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                wideArgv[i], -1, nullptr, 0, nullptr, nullptr);
            if (size == 0)
                return reportEntryFailure(json, GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
            std::string argument(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArgv[i], -1,
                    argument.data(), size, nullptr, nullptr) != size)
                return reportEntryFailure(json, GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
            argument.pop_back(); // The converted terminator remains at data()[size()].
            arguments.push_back(std::move(argument));
        }
        // Publish pointers only after their owning strings stop moving. Both
        // vectors stay alive until runCompiler completes, with argv[argc] null.
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (auto& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        return runCompiler(argc, argv.data());
    } catch (...) {
        return reportEntryFailure(json);
    }
}
#else
int main(int argc, char* argv[]) {
    try { return runCompiler(argc,argv); }
    catch (...) {
        bool json=false;
        for (int i=1;i<argc;++i) if (std::string_view(argv[i])=="--diagnostics=json") json=true;
        return reportEntryFailure(json);
    }
}
#endif
