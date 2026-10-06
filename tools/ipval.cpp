#include "ipval.hpp"

#include "iprop/Validator.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace iprop {

ValidationReport validateReport(std::span<const u8> bytes) {
    ValidationReport report;
    try {
        const auto loaded = loadFile(bytes);
        validate(loaded);
        report.valid = true;
    } catch (const FormatError& error) {
        report.valid = false;
        report.violations.push_back({error.offset, error.what()});
    }
    return report;
}

namespace {

std::string jsonReport(const std::string& file, const ValidationReport& report) {
    using Json = nlohmann::ordered_json;
    Json violations = Json::array();
    for (const Violation& violation : report.violations)
        violations.push_back(Json::object({{"offset", violation.offset}, {"message", violation.message}}));
    const auto document = Json::object({{"file", file}, {"valid", report.valid},
                                         {"violations", std::move(violations)}});
    // File names and messages may hold invalid UTF-8: replace it, keeping valid JSON.
    return document.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

} // namespace

int ipvalMain(int argc, char* argv[]) {
    std::string input;
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "ipval - validate an .iprop binary\n\n"
                      << "Usage: ipval [--json] <file.iprop>\n"
                      << "Exit codes: 0 = valid, 1 = invalid, 2 = usage/I/O error.\n";
            return 0;
        }
        if (arg == "--json") {
            json = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "ipval: unknown option '" << arg << "'\n";
            return 2;
        } else if (input.empty()) {
            input = arg;
        } else {
            std::cerr << "ipval: multiple input files\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << "ipval: no input file\n";
        return 2;
    }
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        if (json) {
            std::cout << jsonReport(input, {false, {{0, "cannot open file"}}});
        } else {
            std::cerr << "ipval: cannot open '" << input << "'\n";
        }
        return 2;
    }
    const std::vector<u8> bytes((std::istreambuf_iterator<char>(stream)), {});
    const ValidationReport report = validateReport(bytes);

    if (json) {
        std::cout << jsonReport(input, report);
    } else if (report.valid) {
        std::cout << "valid: " << input << "\n";
    } else {
        const Violation& violation = report.violations.front();
        std::cerr << "invalid: " << input << " at byte " << violation.offset
                  << ": " << violation.message << "\n";
    }
    return report.valid ? 0 : 1;
}

} // namespace iprop
