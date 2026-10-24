#include "dmfr/diff/diff.h"

#include <algorithm>
#include <set>

namespace dmfr::diff {

namespace {

nlohmann::json make_change(const std::string& status,
                           const nlohmann::json& before,
                           const nlohmann::json& after) {
    nlohmann::json j;
    j["status"] = status;
    if (!before.is_null()) {
        j["before"] = before;
    }
    if (!after.is_null()) {
        j["after"] = after;
    }
    return j;
}

std::map<std::string, const dmfr::snapshot::ManifestFile*> index_manifest_files(
    const std::vector<dmfr::snapshot::ManifestFile>& files) {
    std::map<std::string, const dmfr::snapshot::ManifestFile*> index;
    for (const auto& f : files) {
        index[f.path] = &f;
    }
    return index;
}

} // namespace

nlohmann::json compare_string_maps(const std::map<std::string, std::string>& before,
                                   const std::map<std::string, std::string>& after) {
    nlohmann::json result = nlohmann::json::object();

    for (const auto& [key, value] : before) {
        auto it = after.find(key);
        if (it == after.end()) {
            result[key] = make_change("removed", value, nullptr);
        } else if (it->second != value) {
            result[key] = make_change("changed", value, it->second);
        }
    }

    for (const auto& [key, value] : after) {
        if (before.find(key) == before.end()) {
            result[key] = make_change("added", nullptr, value);
        }
    }

    return result;
}

nlohmann::json compare_string_sets(const std::vector<std::string>& before,
                                   const std::vector<std::string>& after) {
    std::set<std::string> before_set(before.begin(), before.end());
    std::set<std::string> after_set(after.begin(), after.end());

    nlohmann::json result = nlohmann::json::object();
    result["added"] = nlohmann::json::array();
    result["removed"] = nlohmann::json::array();

    for (const auto& s : after_set) {
        if (before_set.find(s) == before_set.end()) {
            result["added"].push_back(s);
        }
    }

    for (const auto& s : before_set) {
        if (after_set.find(s) == after_set.end()) {
            result["removed"].push_back(s);
        }
    }

    return result;
}

nlohmann::json compare_manifest_files(const std::vector<dmfr::snapshot::ManifestFile>& before,
                                      const std::vector<dmfr::snapshot::ManifestFile>& after) {
    auto before_index = index_manifest_files(before);
    auto after_index = index_manifest_files(after);

    nlohmann::json result = nlohmann::json::object();

    for (const auto& [path, file_ptr] : before_index) {
        auto it = after_index.find(path);
        if (it == after_index.end()) {
            result[path] = make_change("removed", file_ptr->to_json(), nullptr);
        } else if (it->second->hash != file_ptr->hash) {
            result[path] = make_change("changed", file_ptr->to_json(), it->second->to_json());
        }
    }

    for (const auto& [path, file_ptr] : after_index) {
        if (before_index.find(path) == before_index.end()) {
            result[path] = make_change("added", nullptr, file_ptr->to_json());
        }
    }

    return result;
}

nlohmann::json compare_tools(const std::vector<dmfr::snapshot::ToolVersion>& before,
                             const std::vector<dmfr::snapshot::ToolVersion>& after) {
    std::map<std::string, std::string> before_map;
    std::map<std::string, std::string> after_map;
    for (const auto& t : before) before_map[t.name] = t.version;
    for (const auto& t : after) after_map[t.name] = t.version;
    return compare_string_maps(before_map, after_map);
}

nlohmann::json compare(const dmfr::snapshot::SnapshotManifest& before,
                       const dmfr::snapshot::SnapshotManifest& after) {
    nlohmann::json report;
    report["before_snapshot_id"] = before.snapshot_id;
    report["after_snapshot_id"] = after.snapshot_id;
    report["before_timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                     before.timestamp.time_since_epoch())
                                     .count();
    report["after_timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                    after.timestamp.time_since_epoch())
                                    .count();
    report["project_id"] = before.project_id.empty() ? after.project_id : before.project_id;

    nlohmann::json system_diff = nlohmann::json::object();
    if (before.system.os_name != after.system.os_name) {
        system_diff["os_name"] = make_change("changed", before.system.os_name, after.system.os_name);
    }
    if (before.system.os_version != after.system.os_version) {
        system_diff["os_version"] = make_change("changed", before.system.os_version, after.system.os_version);
    }
    if (before.system.hostname != after.system.hostname) {
        system_diff["hostname"] = make_change("changed", before.system.hostname, after.system.hostname);
    }
    report["system"] = system_diff;

    nlohmann::json git_diff = nlohmann::json::object();
    if (before.git && after.git) {
        if (before.git->branch != after.git->branch) {
            git_diff["branch"] = make_change("changed", before.git->branch, after.git->branch);
        }
        if (before.git->commit_sha != after.git->commit_sha) {
            git_diff["commit_sha"] = make_change("changed", before.git->commit_sha, after.git->commit_sha);
        }
        if (before.git->dirty != after.git->dirty) {
            git_diff["dirty"] = make_change("changed", before.git->dirty, after.git->dirty);
        }
        if (before.git->remote_url != after.git->remote_url) {
            git_diff["remote_url"] = make_change("changed", before.git->remote_url, after.git->remote_url);
        }
    } else if (before.git && !after.git) {
        git_diff["status"] = make_change("removed", before.git->to_json(), nullptr);
    } else if (!before.git && after.git) {
        git_diff["status"] = make_change("added", nullptr, after.git->to_json());
    }
    report["git"] = git_diff;

    report["tools"] = compare_tools(before.tools, after.tools);
    std::map<std::string, std::string> before_executables, after_executables;
    for (const auto& tool : before.tools) before_executables[tool.name] = tool.executable;
    for (const auto& tool : after.tools) after_executables[tool.name] = tool.executable;
    report["executables"] = compare_string_maps(before_executables, after_executables);
    report["path_entries"] = compare_string_sets(before.path_entries, after.path_entries);
    report["path_entries"]["order_changed"] = before.path_entries != after.path_entries;
    if (before.path_entries != after.path_entries) {
        report["path_entries"]["before"] = before.path_entries;
        report["path_entries"]["after"] = after.path_entries;
    }
    report["env_vars"] = compare_string_maps(before.env_vars, after.env_vars);
    // Normalize older snapshots that stored absolute project paths.
    auto relative_manifests = [](const dmfr::snapshot::SnapshotManifest& snapshot) {
        auto files = snapshot.manifests;
        if (snapshot.git && !snapshot.git->repo_path.empty()) {
            for (auto& file : files) {
                std::filesystem::path path(file.path);
                const std::filesystem::path root(snapshot.git->repo_path);
                auto relative = path.lexically_relative(root);
                if (!relative.empty() && *relative.begin() != "..") {
                    file.path = relative.generic_string();
                }
            }
        }
        return files;
    };
    report["manifests"] = compare_manifest_files(relative_manifests(before), relative_manifests(after));
    report["dotfiles"] = compare_manifest_files(before.dotfiles, after.dotfiles);

    size_t change_count = 0;
    for (const auto& section : {&system_diff, &git_diff, &report["tools"], &report["executables"], &report["env_vars"], &report["manifests"], &report["dotfiles"]}) {
        change_count += section->size();
    }
    change_count += report["path_entries"]["added"].size();
    change_count += report["path_entries"]["removed"].size();
    if (before.path_entries != after.path_entries &&
        report["path_entries"]["added"].empty() && report["path_entries"]["removed"].empty()) {
        ++change_count;
    }
    report["change_count"] = change_count;

    return report;
}

} // namespace dmfr::diff
