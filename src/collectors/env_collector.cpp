#include "dmfr/collectors/env_collector.h"

#include "dmfr/common/paths.h"
#include "dmfr/common/process.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace dmfr::collectors {

namespace {

std::string run_command_impl(const char* cmd) {
    std::istringstream stream(cmd);
    std::vector<std::string> argv;
    for (std::string word; stream >> word;) argv.push_back(word);
    auto result = dmfr::process::capture(argv);
    return result.ok() ? result.output : "";
}

} // namespace

EnvCollector::EnvCollector()
    : last_tool_poll_(std::chrono::steady_clock::time_point::min()) {}

bool EnvCollector::initialize() {
    initialized_ = true;
    return true;
}

void EnvCollector::shutdown() {
    initialized_ = false;
}

bool EnvCollector::is_available() const {
    return initialized_;
}

std::vector<Event> EnvCollector::poll() {
    std::vector<Event> events;

    auto tool_events = poll_tool_versions();
    events.insert(events.end(), tool_events.begin(), tool_events.end());

    auto dotfile_events = poll_dotfiles();
    events.insert(events.end(), dotfile_events.begin(), dotfile_events.end());

    auto manifest_events = poll_manifests();
    events.insert(events.end(), manifest_events.begin(), manifest_events.end());

    return events;
}

std::vector<Event> EnvCollector::poll_tool_versions() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_tool_poll_ < std::chrono::minutes(5)) {
        return {};
    }
    last_tool_poll_ = now;

    struct Tool {
        const char* name;
        const char* version_cmd;
    };

    static const Tool tools[] = {
        {"node", "node --version"},
        {"python", "python --version"},
        {"cargo", "cargo --version"},
        {"go", "go version"},
        {"rustc", "rustc --version"},
        {"git", "git --version"},
    };

    std::vector<Event> events;
    for (const auto& tool : tools) {
        std::string output = run_command_impl(tool.version_cmd);
        if (!output.empty()) {
            nlohmann::json payload;
            payload["tool"] = tool.name;
            payload["version"] = output;
            events.push_back(Event::create(Source::Environment, "tool_version", "", std::move(payload)));
        }
    }

    return events;
}

std::vector<Event> EnvCollector::poll_dotfiles() {
    static const std::array<const char*, 3> dotfiles = {
        ".bashrc",
        ".zshrc",
        ".gitconfig",
    };

    std::vector<Event> events;
    fs::path home = paths::home_dir();
    for (const auto* name : dotfiles) {
        fs::path path = home / name;
        std::string hash = hash_file(path);
        if (hash.empty()) {
            continue;
        }

        auto it = dotfile_hashes_.find(name);
        if (it != dotfile_hashes_.end() && it->second == hash) {
            continue;
        }

        dotfile_hashes_[name] = hash;
        nlohmann::json payload;
        payload["path"] = path.string();
        payload["hash"] = hash;
        payload["name"] = name;
        events.push_back(Event::create(Source::Environment, "dotfile_change", "", std::move(payload)));
    }

    return events;
}

std::vector<Event> EnvCollector::poll_manifests() {
    static const std::array<const char*, 3> manifests = {
        "package.json",
        "Cargo.toml",
        "requirements.txt",
    };

    std::vector<Event> events;

    // Discover tracked project roots: current directory and one level deep under home.
    std::vector<fs::path> roots;
    roots.push_back(fs::current_path());
    fs::path home = paths::home_dir();
    std::error_code ec;
    if (fs::exists(home, ec)) {
        for (const auto& entry : fs::directory_iterator(home, ec)) {
            if (entry.is_directory(ec)) {
                roots.push_back(entry.path());
            }
        }
    }

    for (const auto& root : roots) {
        for (const auto* name : manifests) {
            fs::path path = root / name;
            std::string hash = hash_file(path);
            if (hash.empty()) {
                continue;
            }

            std::string key = path.string();
            auto it = manifest_hashes_.find(key);
            if (it != manifest_hashes_.end() && it->second == hash) {
                continue;
            }

            manifest_hashes_[key] = hash;
            nlohmann::json payload;
            payload["path"] = path.string();
            payload["hash"] = hash;
            payload["manifest"] = name;
            events.push_back(Event::create(Source::Environment, "manifest_change", root.string(), std::move(payload)));
        }
    }

    return events;
}

std::string EnvCollector::hash_file(const fs::path& path) {
    return dmfr::process::fingerprint(path);
}

std::string EnvCollector::run_command(const char* cmd) {
    return run_command_impl(cmd);
}

} // namespace dmfr::collectors
