#include "dmfr/scrubber/scrubber.h"

#include <catch2/catch_all.hpp>
#include <regex>

TEST_CASE("Scrubber redacts denied env vars", "[scrubber]") {
    dmfr::scrubber::Scrubber scrubber;

    nlohmann::json payload{
        {"GITHUB_TOKEN", "super-secret-token"},
        {"PUBLIC_VAR", "hello"}
    };

    auto event = dmfr::Event::create(
        dmfr::Source::Environment,
        "env_change",
        "/project",
        std::move(payload));

    REQUIRE(scrubber.scrub(event));
    REQUIRE(event.payload["GITHUB_TOKEN"] == "<REDACTED>");
    REQUIRE(event.payload["PUBLIC_VAR"] == "hello");
    REQUIRE(event.sensitivity == dmfr::Sensitivity::Scrubbed);
}

TEST_CASE("Scrubber truncates long strings", "[scrubber]") {
    dmfr::scrubber::Scrubber scrubber;
    scrubber.set_max_string_size(8);

    std::string value(100, 'a');
    nlohmann::json payload{{"data", value}};

    auto event = dmfr::Event::create(
        dmfr::Source::Build,
        "output",
        "/project",
        std::move(payload));

    REQUIRE(scrubber.scrub(event));
    auto result = event.payload["data"].get<std::string>();
    REQUIRE(result.size() < 100);
    REQUIRE(result.find("<TRUNCATED>") != std::string::npos);
}

TEST_CASE("Configured regexes redact event and snapshot strings", "[scrubber]") {
    dmfr::scrubber::Scrubber scrubber;
    dmfr::config::ScrubberConfig config;
    config.secret_regexes = {"credential-[0-9]+"};
    scrubber.apply_config(config);
    auto event = dmfr::Event::create(dmfr::Source::Build, "output", "proj",
        nlohmann::json{{"message", "value credential-123 end"}});
    REQUIRE(scrubber.scrub(event));
    REQUIRE(event.payload["message"] == "value <REDACTED> end");
    auto manifest = dmfr::snapshot::SnapshotManifest::create("proj");
    manifest.env_vars["GITHUB_TOKEN"] = "private";
    manifest.env_vars["PUBLIC"] = "credential-456";
    manifest.git = dmfr::snapshot::GitInfo{};
    manifest.git->remote_url = "https://credential-789@example.com/repo";
    manifest.notes = {{"text", "credential-321"}};
    REQUIRE(scrubber.scrub(manifest));
    REQUIRE(manifest.env_vars["GITHUB_TOKEN"] == "<REDACTED>");
    REQUIRE(manifest.env_vars["PUBLIC"] == "<REDACTED>");
    REQUIRE(manifest.git->remote_url == "https://<REDACTED>@example.com/repo");
    REQUIRE(manifest.notes["text"] == "<REDACTED>");
    REQUIRE_THROWS_AS(scrubber.add_secret_regex("["), std::regex_error);
}

TEST_CASE("Build arguments redact split flags and assignments by default", "[scrubber]") {
    dmfr::scrubber::Scrubber scrubber;
    auto event = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", {
        {"args", {"--password=dummy1", "--api-key", "dummy2", "GITHUB_TOKEN=dummy3", "--jobs", "8", "test"}}
    });
    REQUIRE(scrubber.scrub(event));
    REQUIRE(event.payload["args"] == nlohmann::json::array({
        "--password=<REDACTED>", "--api-key", "<REDACTED>", "GITHUB_TOKEN=<REDACTED>", "--jobs", "8", "test"}));
}
