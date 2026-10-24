#include "dmfr/common/event.h"

#include <catch2/catch_all.hpp>

TEST_CASE("Event creation sets required fields", "[event]") {
    auto event = dmfr::Event::create(
        dmfr::Source::Git,
        "commit",
        "/home/user/project",
        nlohmann::json{{"sha", "abc123"}});

    REQUIRE(event.source == dmfr::Source::Git);
    REQUIRE(event.type == "commit");
    REQUIRE(event.project_id == "/home/user/project");
    REQUIRE(event.payload["sha"] == "abc123");
    REQUIRE_FALSE(event.event_id.empty());
}

TEST_CASE("Source conversion works", "[event]") {
    REQUIRE(dmfr::to_string(dmfr::Source::Git) == "git");
    REQUIRE(dmfr::to_string(dmfr::Source::Build) == "build");
    REQUIRE(dmfr::to_string(dmfr::Source::System) == "system");
}
