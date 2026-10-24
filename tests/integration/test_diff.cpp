#include "dmfr/common/event.h"
#include "dmfr/diff/diff.h"
#include "dmfr/snapshot/snapshot.h"

#include <catch2/catch_all.hpp>

TEST_CASE("Diff detects system and tool changes", "[diff]") {
    auto before = dmfr::snapshot::SnapshotManifest::create("proj");
    before.system.os_name = "macOS";
    before.system.os_version = "14.0";
    before.tools.push_back({"node", "v18.0.0"});
    before.env_vars["KEY1"] = "value1";

    auto after = before;
    after.snapshot_id = dmfr::generate_uuid();
    after.system.os_version = "14.1";
    after.tools[0].version = "v20.0.0";
    after.env_vars["KEY2"] = "value2";

    auto report = dmfr::diff::compare(before, after);

    REQUIRE(report["change_count"] > 0);
    REQUIRE(report["system"]["os_version"]["status"] == "changed");
    REQUIRE(report["tools"]["node"]["status"] == "changed");
    REQUIRE(report["env_vars"]["KEY2"]["status"] == "added");
}

TEST_CASE("Diff detects added and removed path entries", "[diff]") {
    auto before = dmfr::snapshot::SnapshotManifest::create("proj");
    before.path_entries = {"/usr/bin", "/bin"};

    auto after = before;
    after.snapshot_id = dmfr::generate_uuid();
    after.path_entries = {"/usr/bin", "/local/bin"};

    auto report = dmfr::diff::compare(before, after);

    REQUIRE(report["path_entries"]["added"].size() == 1);
    REQUIRE(report["path_entries"]["removed"].size() == 1);
}

TEST_CASE("Diff detects manifest file hash changes", "[diff]") {
    auto before = dmfr::snapshot::SnapshotManifest::create("proj");
    before.manifests.push_back({"package.json", "/proj/package.json", "abc123"});

    auto after = before;
    after.snapshot_id = dmfr::generate_uuid();
    after.manifests[0].hash = "def456";

    auto report = dmfr::diff::compare(before, after);

    REQUIRE(report["manifests"].contains("/proj/package.json"));
    REQUIRE(report["manifests"]["/proj/package.json"]["status"] == "changed");
}

TEST_CASE("Diff detects PATH precedence and duplicate changes", "[diff]") {
    auto before = dmfr::snapshot::SnapshotManifest::create("proj");
    before.path_entries = {"/usr/bin", "/opt/bin"};
    auto after = before;
    after.path_entries = {"/opt/bin", "/usr/bin"};
    auto report = dmfr::diff::compare(before, after);
    REQUIRE(report["path_entries"]["order_changed"] == true);
    REQUIRE(report["path_entries"]["before"] == before.path_entries);
    REQUIRE(report["change_count"] == 1);
    after.path_entries = {"/usr/bin", "/opt/bin", "/usr/bin"};
    REQUIRE(dmfr::diff::compare(before, after)["change_count"] == 1);
    REQUIRE(dmfr::diff::compare(before, before)["change_count"] == 0);
}

TEST_CASE("Diff matches legacy manifest paths across checkout locations", "[diff]") {
    auto before = dmfr::snapshot::SnapshotManifest::create("proj");
    before.git = dmfr::snapshot::GitInfo{};
    before.git->repo_path = "/alice/proj";
    before.manifests = {{"package.json", "/alice/proj/web/package.json", "abc"}};
    auto after = before;
    after.git->repo_path = "/bob/proj";
    after.manifests[0].path = "/bob/proj/web/package.json";
    REQUIRE(dmfr::diff::compare(before, after)["manifests"].empty());
    after.manifests[0].hash = "def";
    auto report = dmfr::diff::compare(before, after);
    REQUIRE(report["manifests"]["web/package.json"]["status"] == "changed");
    after.manifests[0].path = "web/package.json";
    REQUIRE(dmfr::diff::compare(before, after)["manifests"].size() == 1);
}
