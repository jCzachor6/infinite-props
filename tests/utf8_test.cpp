#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "iprop/Utf8.hpp"

using namespace iprop;

namespace {

bool valid(const std::string& s) {
    return isValidUtf8({reinterpret_cast<const u8*>(s.data()), s.size()});
}

} // namespace

TEST_CASE("ascii and empty strings are valid") {
    REQUIRE(valid(""));
    REQUIRE(valid("rock"));
    REQUIRE(valid("hello world 123"));
}

TEST_CASE("valid multi-byte sequences") {
    REQUIRE(valid("\xC2\xA2"));                         // U+00A2 (2-byte)
    REQUIRE(valid("żółć"));                             // Polish (2-byte)
    REQUIRE(valid("\xE2\x82\xAC"));                     // U+20AC euro (3-byte)
    REQUIRE(valid("\xF0\x9F\x9A\x80"));                 // U+1F680 rocket (4-byte)
    REQUIRE(valid("\xF4\x8F\xBF\xBF"));                 // U+10FFFF (max)
    REQUIRE(valid("a\xC2\xA2\xE2\x82\xAC\xF0\x9F\x9A\x80" "b")); // mixed (split to keep escapes separate)
}

TEST_CASE("invalid single bytes") {
    REQUIRE_FALSE(valid("\x80")); // lone continuation
    REQUIRE_FALSE(valid("\xFF"));
    REQUIRE_FALSE(valid("\xC0")); // truncated lead
    REQUIRE_FALSE(valid("\xF8")); // 5-byte lead (obsolete)
}

TEST_CASE("overlong encodings are rejected") {
    REQUIRE_FALSE(valid("\xC0\xAF"));                                     // '/' as 2-byte
    REQUIRE_FALSE(valid("\xE0\x80\xAF"));                                 // 2-byte value as 3-byte
    REQUIRE_FALSE(valid("\xF0\x80\x80\xAF"));                             // 3-byte value as 4-byte
}

TEST_CASE("surrogates and out-of-range values are rejected") {
    REQUIRE_FALSE(valid("\xED\xA0\x80"));     // U+D800 surrogate
    REQUIRE_FALSE(valid("\xED\xBF\xBF"));     // U+DFFF surrogate
    REQUIRE_FALSE(valid("\xF4\x90\x80\x80")); // U+110000, above the range
}

TEST_CASE("truncated sequences are rejected") {
    REQUIRE_FALSE(valid("\xC2"));                 // 2-byte lead alone
    REQUIRE_FALSE(valid("\xE2\x82"));             // 3-byte lead + 1 continuation
    REQUIRE_FALSE(valid("\xF0\x9F\x9A"));         // 4-byte lead + 2 continuations
    REQUIRE_FALSE(valid("a\xC2"));                // truncated at the end
}

TEST_CASE("bad continuation bytes are rejected") {
    REQUIRE_FALSE(valid("\xC2\x41")); // 'A' is not a continuation
    REQUIRE_FALSE(valid("\xE2\x82\x41"));
    REQUIRE_FALSE(valid("\xE2\x41\xAC"));
    REQUIRE_FALSE(valid("\xF0\x9F\x9A\x41"));
}
