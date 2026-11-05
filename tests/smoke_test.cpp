#include <catch2/catch_test_macros.hpp>

#include <string>

#include "iprop/Version.hpp"

// The runtime library reports the project version it was built as.
TEST_CASE("runtime version matches the project version") {
    REQUIRE(iprop::versionMajor == 0);
    REQUIRE(iprop::versionMinor == 9);
    REQUIRE(iprop::versionPatch == 0);
    REQUIRE(std::string(iprop::versionString()) == "0.9.0");
}
