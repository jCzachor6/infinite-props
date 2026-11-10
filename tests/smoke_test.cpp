#include <catch2/catch_test_macros.hpp>

#include <string>

#include "iprop/Version.hpp"

// The runtime library reports the project version it was built as.
TEST_CASE("runtime version matches the project version") {
    REQUIRE(iprop::versionMajor == 1);
    REQUIRE(iprop::versionMinor == 0);
    REQUIRE(iprop::versionPatch == 0);
    REQUIRE(std::string(iprop::versionString()) == "1.0.0");
}
