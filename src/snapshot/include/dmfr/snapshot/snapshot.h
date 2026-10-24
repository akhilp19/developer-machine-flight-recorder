#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace dmfr::snapshot {

struct SystemInfo {
    std::string os_name;
    std::string os_version;
    std::string hostname;

    nlohmann::json to_json() const;
    static SystemInfo from_json(const nlohmann::json& j);
};

struct GitInfo {
    std::string repo_path;
    std::string branch;
    std::string commit_sha;
    bool dirty{false};
    std::string remote_url;

    nlohmann::json to_json() const;
    static GitInfo from_json(const nlohmann::json& j);
};

struct ToolVersion {
    std::string name;
    std::string version;
    std::string executable{}; // resolved from the capturing process PATH

    nlohmann::json to_json() const;
    static ToolVersion from_json(const nlohmann::json& j);
};

struct ManifestFile {
    std::string name;      // e.g. "package.json"
    std::string path;    // project-relative for manifests; absolute for dotfiles
    std::string hash;    // fnv-1a hex hash of contents

    nlohmann::json to_json() const;
    static ManifestFile from_json(const nlohmann::json& j);
};

struct SnapshotManifest {
    std::string snapshot_id;
    std::chrono::system_clock::time_point timestamp;
    std::string project_id; // may be empty

    SystemInfo system;
    std::optional<GitInfo> git;
    std::vector<ToolVersion> tools;
    std::vector<std::string> path_entries;
    std::map<std::string, std::string> env_vars;
    std::vector<ManifestFile> manifests;
    std::vector<ManifestFile> dotfiles;
    nlohmann::json notes = nlohmann::json::object();

    static SnapshotManifest create(std::string project_id = "");

    nlohmann::json to_json() const;
    static SnapshotManifest from_json(const nlohmann::json& j);

    std::string serialize() const;
    static std::optional<SnapshotManifest> deserialize(const std::string& data);
};

// Capture a snapshot of the current machine state for the given project.
SnapshotManifest capture(const std::filesystem::path& project_path = std::filesystem::current_path());

} // namespace dmfr::snapshot
