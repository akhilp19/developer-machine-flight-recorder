#include "dmfr/build/build.h"
#include "dmfr/diff/diff.h"
#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <fstream>
#include <set>

namespace {
struct Workspace {
    std::filesystem::path original = std::filesystem::current_path();
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        dmfr::snapshot::SnapshotManifest::create().snapshot_id;
    Workspace() { std::filesystem::create_directories(root); }
    ~Workspace() {
        std::filesystem::current_path(original);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

dmfr::Event insert_build(dmfr::store::Store& store, const std::string& project,
                         int code, int age, const std::string& arg = "test") {
    auto snapshot = dmfr::snapshot::SnapshotManifest::create(project);
    snapshot.env_vars["PUBLIC"] = code ? "after" : "before";
    REQUIRE(store.insert_snapshot(snapshot));
    auto event = dmfr::Event::create(dmfr::Source::Build, "invocation", project,
        {{"command", "make"}, {"args", {arg}}, {"exit_code", code},
         {"snapshot_id", snapshot.snapshot_id}});
    event.timestamp -= std::chrono::seconds(age);
    REQUIRE(store.insert(event));
    return event;
}
}

TEST_CASE("Build diagnosis selects a matching completed baseline across pages", "[build]") {
    Workspace workspace;
    dmfr::store::Store store(workspace.root / "events.db");
    REQUIRE(store.open());
    dmfr::scrubber::Scrubber scrubber;
    REQUIRE(dmfr::build::diagnose(store, scrubber, "proj")["status"] == "no_failed_build");
    auto baseline = insert_build(store, "proj", 0, 300);
    // Neither another project nor another command argument is a baseline.
    insert_build(store, "other", 0, 200);
    for (int i = 0; i < 105; ++i) insert_build(store, "proj", 0, 190 - i, "clean");
    auto failure = insert_build(store, "proj", 2, 1);
    auto report = dmfr::build::diagnose(store, scrubber, "proj");
    REQUIRE(report["status"] == "compared");
    REQUIRE(report["successful_event_id"] == baseline.event_id);
    REQUIRE(report["failed_event_id"] == failure.event_id);
    REQUIRE(report["diff"]["env_vars"]["PUBLIC"]["status"] == "changed");
    REQUIRE(dmfr::build::diagnose(store, scrubber, "ignored", failure.event_id)["status"] == "compared");
    REQUIRE(dmfr::build::diagnose(store, scrubber, "proj", baseline.event_id)["status"] == "invalid_failed_build");
}

TEST_CASE("Diagnosis explains missing and ambiguous evidence", "[build]") {
    Workspace workspace;
    dmfr::store::Store store(workspace.root / "events.db");
    REQUIRE(store.open());
    dmfr::scrubber::Scrubber scrubber;
    auto failure = insert_build(store, "proj", 1, 1);
    REQUIRE(dmfr::build::diagnose(store, scrubber, "proj")["status"] == "no_matching_success");
    auto legacy = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj",
        {{"command", "make"}, {"args", {"test"}}, {"exit_code", 0}});
    legacy.timestamp -= std::chrono::seconds(100);
    REQUIRE(store.insert(legacy));
    REQUIRE(dmfr::build::diagnose(store, scrubber, "proj")["status"] == "missing_snapshot");
    insert_build(store, "redacted", 0, 100, "<REDACTED>");
    insert_build(store, "redacted", 1, 1, "<REDACTED>");
    REQUIRE(dmfr::build::diagnose(store, scrubber, "redacted")["status"] == "command_arguments_redacted");
    auto overlap = insert_build(store, "overlap", 0, 100);
    // Insert a more recent success which was still running when the failure began.
    auto concurrent = dmfr::Event::create(dmfr::Source::Build, "invocation", "overlap",
        {{"command", "make"}, {"args", {"test"}}, {"exit_code", 0}, {"completed_time", 3000}});
    concurrent.timestamp -= std::chrono::seconds(50);
    REQUIRE(store.insert(concurrent));
    auto failed = dmfr::Event::create(dmfr::Source::Build, "invocation", "overlap",
        {{"command", "make"}, {"args", {"test"}}, {"exit_code", 1}, {"start_time", 2000},
         {"snapshot_id", failure.payload["snapshot_id"]}});
    REQUIRE(store.insert(failed));
    REQUIRE(dmfr::build::diagnose(store, scrubber, "overlap")["successful_event_id"] == overlap.event_id);
}

#ifndef _WIN32
TEST_CASE("Build recording captures caller state before execution and preserves status", "[build]") {
    Workspace workspace;
    std::filesystem::current_path(workspace.root);
    dmfr::store::Store store(workspace.root / "events.db");
    REQUIRE(store.open());
    dmfr::scrubber::Scrubber scrubber;
    std::ofstream(workspace.root / "package-lock.json") << "before";
    ::setenv("DMFR_TEST_CONTEXT", "caller-context", 1);
    ::setenv("DMFR_TEST_TOKEN", "secret-value", 1);
    struct Restore {
        ~Restore() { ::unsetenv("DMFR_TEST_CONTEXT"); ::unsetenv("DMFR_TEST_TOKEN"); }
    } restore;
    const std::vector<std::string> command = {"/bin/sh", "-c",
        "printf after > package-lock.json; test \"$DMFR_TEST_CONTEXT\" = caller-context; exit 7"};
    REQUIRE(dmfr::build::record(command, "shell-build", store, scrubber) == 7);
    dmfr::store::Query query;
    query.source = dmfr::Source::Build;
    auto events = store.query(query);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].payload["exit_code"] == 7);
    REQUIRE(events[0].payload["args"][0] == "-c");
    REQUIRE(events[0].payload["executable"] == "/bin/sh");
    auto snapshot = store.get_snapshot_by_id(events[0].payload["snapshot_id"]);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->env_vars.at("DMFR_TEST_CONTEXT") == "caller-context");
    REQUIRE(snapshot->env_vars.at("DMFR_TEST_TOKEN") == "<REDACTED>");
    REQUIRE(snapshot->manifests.size() == 1);
    auto after = dmfr::snapshot::capture(workspace.root);
    REQUIRE(snapshot->manifests[0].path == "package-lock.json");
    REQUIRE(snapshot->manifests[0].hash != after.manifests[0].hash);
    REQUIRE(dmfr::build::record({"dmfr-nonexistent-command"}, "missing", store, scrubber) == 127);
}
#endif

TEST_CASE("Tool executable changes survive snapshot round trips", "[build][diff]") {
    auto before = dmfr::snapshot::SnapshotManifest::create("proj");
    before.tools.push_back({"python", "3.12", "/old/bin/python"});
    auto after = before;
    after.tools[0].executable = "/new/bin/python";
    auto restored = dmfr::snapshot::SnapshotManifest::deserialize(after.serialize());
    REQUIRE(restored.has_value());
    auto report = dmfr::diff::compare(before, *restored);
    REQUIRE(report["tools"].empty());
    REQUIRE(report["executables"]["python"]["status"] == "changed");
    REQUIRE(report["change_count"] == 1);
    auto legacy = dmfr::snapshot::ToolVersion::from_json({{"name", "python"}, {"version", "3.12"}});
    REQUIRE(legacy.executable.empty());
}

TEST_CASE("Snapshot retention preserves live links and bounds unreferenced history", "[store][retention]") {
    Workspace workspace;
    dmfr::store::Store store(workspace.root / "events.db");
    REQUIRE(store.open());
    auto linked = dmfr::snapshot::SnapshotManifest::create("proj");
    linked.timestamp -= std::chrono::hours(100);
    REQUIRE(store.insert_snapshot(linked));
    auto event = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", {{"snapshot_id", linked.snapshot_id}});
    REQUIRE(store.insert(event));
    auto expired = linked;
    expired.snapshot_id = dmfr::snapshot::SnapshotManifest::create().snapshot_id;
    REQUIRE(store.insert_snapshot(expired));
    for (int i = 0; i < 5; ++i) REQUIRE(store.insert_snapshot(dmfr::snapshot::SnapshotManifest::create("proj")));
    REQUIRE(store.run_retention_policy(2, std::chrono::hours(24)));
    REQUIRE(store.get_snapshot_by_id(linked.snapshot_id).has_value());
    REQUIRE_FALSE(store.get_snapshot_by_id(expired.snapshot_id).has_value());
    dmfr::store::SnapshotQuery query;
    query.limit = 100;
    REQUIRE(store.query_snapshots(query).size() == 3); // two recent, one retained link
    REQUIRE(store.run_retention_policy(2, std::chrono::hours(0)));
    REQUIRE(store.query_snapshots(query).empty());
}

TEST_CASE("History keyset pagination preserves tied timestamps", "[store][build]") {
    Workspace workspace;
    dmfr::store::Store store(workspace.root / "events.db");
    REQUIRE(store.open());
    auto timestamp = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", {}).timestamp;
    for (int i = 0; i < 210; ++i) {
        auto event = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", {});
        event.timestamp = timestamp;
        REQUIRE(store.insert(event));
    }
    dmfr::store::Query query;
    query.project_id = "proj";
    query.limit = 50;
    std::set<std::string> ids;
    for (;;) {
        auto page = store.query(query);
        if (page.empty()) break;
        for (const auto& event : page) REQUIRE(ids.insert(event.event_id).second);
        query.before_time = page.back().timestamp;
        query.before_id = page.back().event_id;
    }
    REQUIRE(ids.size() == 210);
}
