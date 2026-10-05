#include "semantic_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include "ips/Parser.hpp"
#include "ips/SourceError.hpp"

namespace semantic_test {
using namespace iprop::ips;

// Checks `source` and expects a SourceError mentioning `messagePart`.
void expectCheckError(const std::string& source,
                      const std::map<std::string, PartInfo>& parts,
                      const char* messagePart) {
    bool threw = false;
    try {
        checkAsset(parse(source), parts);
    } catch (const SourceError& error) {
        threw = true;
        INFO(error.message);
        REQUIRE(std::string(error.message).find(messagePart) !=
                std::string::npos);
    }
    REQUIRE(threw);
}

void expectCheckError(const std::string& source, const char* messagePart) {
    expectCheckError(source, {}, messagePart);
}

std::string wrap(const std::string& body) {
    return "asset t { size 8x8 draw { " + body + " } }";
}

} // namespace semantic_test
