#include <catch2/catch_test_macros.hpp>

#include <string>

#include "iprop/Version.hpp"

// Smoke test: proves the build, CTest wiring and the runtime library link
// work end to end. Real tests arrive with the interpreter phases.
TEST_CASE("runtime version matches the project version") {
    REQUIRE(iprop::versionMajor == 0);
    REQUIRE(iprop::versionMinor == 7);
    REQUIRE(iprop::versionPatch == 2);
    REQUIRE(std::string(iprop::versionString()) == "0.7.2");
}

TEST_CASE("catch2 is wired up") {
    REQUIRE(1 + 1 == 2);
}
