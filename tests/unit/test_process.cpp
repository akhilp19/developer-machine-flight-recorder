#include "dmfr/common/process.h"
#include <catch2/catch_all.hpp>
#include <fstream>

#ifndef _WIN32
TEST_CASE("Probe execution bounds hangs and descendant pipes", "[process]") {
    const auto start = std::chrono::steady_clock::now();
    auto result = dmfr::process::capture({"/bin/sh", "-c", "sleep 10 & wait"}, std::chrono::milliseconds(100));
    REQUIRE(result.timed_out);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    result = dmfr::process::capture({"/bin/sh", "-c", "sleep 10 & exit 0"}, std::chrono::milliseconds(100));
    REQUIRE(result.timed_out);
}
TEST_CASE("Probe execution bounds output and checks exit codes", "[process]") {
    auto result = dmfr::process::capture({"/bin/sh", "-c", "while :; do printf 12345678; done"}, std::chrono::seconds(1), 100);
    REQUIRE(result.truncated);
    REQUIRE(result.output.size() == 100);
    REQUIRE_FALSE(result.ok());
    result = dmfr::process::capture({"/bin/sh", "-c", "echo bad-version; exit 1"});
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.exit_code == 1);
    result = dmfr::process::capture({"/bin/echo", "literal $(not-executed)"});
    REQUIRE(result.ok());
    REQUIRE(result.output == "literal $(not-executed)");
}
#endif
