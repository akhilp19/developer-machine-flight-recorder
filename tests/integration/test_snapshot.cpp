#include "dmfr/snapshot/snapshot.h"
#include "dmfr/store/store.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("SnapshotManifest serializes and deserializes", "[snapshot]") {
    auto manifest = dmfr::snapshot::SnapshotManifest::create("proj");
    manifest.system.os_name = "macOS";
    manifest.system.os_version = "14.0";
    manifest.system.hostname = "testhost";
    manifest.tools.push_back({"node", "v20.0.0"});
    manifest.path_entries = {"/usr/bin", "/bin"};
    manifest.env_vars["PATH"] = "/usr/bin";
    manifest.manifests.push_back({"package.json", "/proj/package.json", "abc123"});
    manifest.dotfiles.push_back({".bashrc", "/home/user/.bashrc", "def456"});

    auto data = manifest.serialize();
    auto restored = dmfr::snapshot::SnapshotManifest::deserialize(data);

    REQUIRE(restored.has_value());
    REQUIRE(restored->snapshot_id == manifest.snapshot_id);
    REQUIRE(restored->system.os_name == "macOS");
    REQUIRE(restored->tools.size() == 1);
    REQUIRE(restored->tools[0].name == "node");
    REQUIRE(restored->manifests[0].hash == "abc123");
}

TEST_CASE("Store can insert and retrieve snapshots", "[snapshot][store]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_snapshot_store.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE(store.open());

    auto manifest = dmfr::snapshot::SnapshotManifest::create("proj");
    manifest.system.os_name = "Linux";
    manifest.system.os_version = "22.04";

    REQUIRE(store.insert_snapshot(manifest));

    auto found = store.get_snapshot_by_id(manifest.snapshot_id);
    REQUIRE(found.has_value());
    REQUIRE(found->snapshot_id == manifest.snapshot_id);
    REQUIRE(found->system.os_name == "Linux");

    dmfr::store::SnapshotQuery query;
    query.limit = 10;
    auto results = store.query_snapshots(query);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].snapshot_id == manifest.snapshot_id);

    store.close();
    std::filesystem::remove(tmp);
}

TEST_CASE("Capture produces a valid snapshot", "[snapshot]") {
    auto manifest = dmfr::snapshot::capture(std::filesystem::current_path());
    REQUIRE(!manifest.snapshot_id.empty());
    REQUIRE(!manifest.system.os_name.empty());
    REQUIRE(!manifest.system.hostname.empty());

    // PATH is almost always present.
    REQUIRE(!manifest.path_entries.empty());
}

#ifndef _WIN32
TEST_CASE("Snapshot capture treats shell syntax in project paths literally", "[snapshot]") {
    const auto root = std::filesystem::temp_directory_path() /
        dmfr::snapshot::SnapshotManifest::create().snapshot_id;
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{root};
    // The vulnerable double-quoted command would execute this substitution.
    const auto project = root / "repo'$(touch DMFR_INJECTION_MARKER)";
    std::filesystem::create_directories(project / ".git" / "objects");
    std::filesystem::create_directories(project / ".git" / "refs" / "heads");
    std::ofstream(project / "package.json") << "{}";
    std::ofstream(project / ".git" / "HEAD") << "ref: refs/heads/main\n";
    std::ofstream(project / ".git" / "refs" / "heads" / "main")
        << "0123456789012345678901234567890123456789\n";
    const auto manifest = dmfr::snapshot::capture(project);
    REQUIRE_FALSE(std::filesystem::exists(root / "DMFR_INJECTION_MARKER"));
    REQUIRE_FALSE(std::filesystem::exists(std::filesystem::current_path() / "DMFR_INJECTION_MARKER"));
    REQUIRE(manifest.git.has_value());
    REQUIRE(manifest.git->branch == "main");
    REQUIRE(manifest.manifests.size() == 1);
    REQUIRE(manifest.manifests[0].path == "package.json");
}
#endif
