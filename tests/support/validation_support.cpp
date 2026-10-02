#include "validation_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "iprop/Format.hpp"
#include "iprop/Validator.hpp"

namespace validation_test {
using namespace iprop;
using namespace test_support;

namespace {

// Entry size of a §6.1 function entry: 18 fixed bytes + one per type byte.
std::size_t entrySize(const FuncSpec& f) {
    return 18 + f.paramTypes.size() + f.resultTypes.size() + f.localTypes.size();
}

} // namespace

// Relative offset (to the code section start) of function fn's first
// instruction. Layout (spec §6): count, the WHOLE function table, then the
// concatenated streams — so the table contributes all entry sizes, and only
// the earlier streams' instruction bytes come before fn's stream.
std::size_t streamRel(const std::vector<FuncSpec>& funcs, std::size_t fn) {
    std::size_t rel = 4;
    for (const auto& f : funcs) {
        rel += entrySize(f);
    }
    for (std::size_t i = 0; i < fn; ++i) {
        rel += funcs[i].instructions.size();
    }
    return rel;
}

// Builds a file and expects validate() to throw a located FormatError at
// codeSectionOffset + rel, mentioning `messagePart`.
void expectValidateError(const std::vector<u8>& fileBytes, std::size_t rel,
                         const char* messagePart) {
    const auto layout = parseLayout(fileBytes);
    const std::size_t base = layout.find(SectionId::Code)->offset;
    bool threw = false;
    try {
        const auto loaded = loadFile(fileBytes);
        validate(loaded);
    } catch (const FormatError& error) {
        threw = true;
        CHECK(error.offset == base + rel);
        CHECK(std::string(error.what()).find(messagePart) != std::string::npos);
    }
    REQUIRE(threw);
}

} // namespace validation_test
