#include "dmfr/snapshot/snapshot.h"

#include "dmfr/common/event.h"
#include "dmfr/common/paths.h"
#include "dmfr/common/process.h"
#include <future>

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <optional>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
extern char** environ;
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace dmfr::snapshot {

namespace fs = std::filesystem;

namespace {

std::vector<std::string> words(const std::string& command) {
    std::istringstream stream(command);
    std::vector<std::string> argv;
    for (std::string word; stream >> word;) argv.push_back(word);
    return argv;
}
[[maybe_unused]] std::string run_command(const char* cmd) {
    auto result = dmfr::process::capture(words(cmd));
    return result.ok() ? result.output : "";
}
std::string run_in_dir(const fs::path& dir, const std::string& cmd) {
    auto argv = words(cmd);
    // Git's -C option avoids both shell interpretation and process-wide chdir.
    argv.insert(argv.begin() + 1, {"-C", fs::absolute(dir).string()});
    auto result = dmfr::process::capture(argv);
    return result.ok() ? result.output : "";
}
std::string hash_file(const fs::path& path) { return dmfr::process::fingerprint(path); }

std::string format_time(const std::chrono::system_clock::time_point& tp) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm = {};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::chrono::system_clock::time_point parse_time(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) {
        return std::chrono::system_clock::time_point{};
    }
#ifdef _WIN32
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    return std::chrono::system_clock::time_point(std::chrono::seconds(t));
}

std::string get_hostname() {
#if defined(_WIN32)
    char buf[256] = {};
    DWORD size = sizeof(buf);
    if (GetComputerNameA(buf, &size)) {
        return buf;
    }
    return "unknown";
#else
    char buf[256] = {};
    if (::gethostname(buf, sizeof(buf)) == 0) {
        return buf;
    }
    return "unknown";
#endif
}

SystemInfo capture_system() {
    SystemInfo info;
#if defined(__APPLE__)
    info.os_name = "macOS";
    info.os_version = run_command("sw_vers -productVersion");
#elif defined(__linux__)
    info.os_name = "Linux";
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string version = line.substr(12);
            if (!version.empty() && (version.front() == '"' || version.front() == '\'')) {
                version.erase(0, 1);
            }
            if (!version.empty() && (version.back() == '"' || version.back() == '\'')) {
                version.pop_back();
            }
            info.os_version = version;
            break;
        }
    }
    if (info.os_version.empty()) {
        info.os_version = run_command("cat /proc/version");
    }
#elif defined(_WIN32)
    info.os_name = "Windows";
    info.os_version = "unknown";
#else
    info.os_name = "unknown";
    info.os_version = "unknown";
#endif
    info.hostname = get_hostname();
    return info;
}

std::optional<GitInfo> capture_git(const fs::path& project_path) {
    std::error_code ec;
    fs::path git_dir = project_path / ".git";
    if (!fs::exists(git_dir, ec)) {
        return std::nullopt;
    }

    GitInfo info;
    info.repo_path = project_path.string();
    info.branch = run_in_dir(project_path, "git rev-parse --abbrev-ref HEAD");
    info.commit_sha = run_in_dir(project_path, "git rev-parse HEAD");
    std::string dirty_output = run_in_dir(project_path, "git status --porcelain");
    info.dirty = !dirty_output.empty();
    info.remote_url = run_in_dir(project_path, "git config --get remote.origin.url");

    if (info.commit_sha.empty()) {
        return std::nullopt;
    }
    return info;
}

std::vector<ToolVersion> capture_tools(nlohmann::json& warnings) {
    struct Tool {
        const char* name;
        const char* version_cmd;
    };

    static const Tool tools[] = {
        {"node", "node --version"},
        {"npm", "npm --version"},
        {"python", "python --version"},
        {"python3", "python3 --version"},
        {"cargo", "cargo --version"},
        {"rustc", "rustc --version"},
        {"go", "go version"},
        {"git", "git --version"},
        {"cmake", "cmake --version"},
        {"make", "make --version"},
    };

    std::vector<ToolVersion> result;
    // At most four probes run concurrently; never cache environment-dependent versions.
    constexpr size_t count = sizeof(tools) / sizeof(tools[0]);
    for (size_t start = 0; start < count; start += 4) {
        std::vector<std::future<dmfr::process::Result>> probes;
        for (size_t i = start; i < std::min(start + 4, count); ++i) {
            probes.push_back(std::async(std::launch::async, [i] { return dmfr::process::capture(words(tools[i].version_cmd)); }));
        }
        for (size_t j = 0; j < probes.size(); ++j) {
            auto probe = probes[j].get();
            if (probe.ok() && !probe.output.empty()) result.push_back({tools[start + j].name, probe.output,
                dmfr::process::resolve(tools[start + j].name)});
            else if (probe.timed_out || probe.truncated) warnings.push_back({
                {"tool", tools[start + j].name},
                {"reason", probe.timed_out ? "timeout" : "output_limit"}
            });
        }
    }
    return result;
}

std::vector<std::string> capture_path() {
    const char* path_env = std::getenv("PATH");
    if (!path_env) {
        return {};
    }
    std::vector<std::string> entries;
    std::string path(path_env);
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        entries.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return entries;
}

std::map<std::string, std::string> capture_env() {
    std::map<std::string, std::string> env;
#if defined(__unix__) || defined(__APPLE__)
    if (environ) {
        for (char** p = environ; *p; ++p) {
            std::string entry(*p);
            size_t pos = entry.find('=');
            if (pos != std::string::npos) {
                env[entry.substr(0, pos)] = entry.substr(pos + 1);
            }
        }
    }
#endif
    return env;
}

std::vector<ManifestFile> capture_manifests(const fs::path& project_path) {
    static const std::vector<const char*> manifest_names = {
        "package.json",
        "Cargo.toml",
        "requirements.txt",
        "go.mod",
        "pom.xml",
        "CMakeLists.txt",
        "package-lock.json", "npm-shrinkwrap.json", "yarn.lock", "pnpm-lock.yaml",
        "Cargo.lock", "poetry.lock", "Pipfile.lock", "uv.lock", "go.sum",
        "Gemfile.lock", "composer.lock", "vcpkg.json", "vcpkg-configuration.json",
        "conan.lock",
    };

    std::vector<ManifestFile> result;
    std::error_code ec;
    if (!fs::exists(project_path, ec)) {
        return result;
    }

    std::vector<fs::path> roots;
    roots.push_back(project_path);
    if (fs::is_directory(project_path, ec)) {
        for (const auto& entry : fs::directory_iterator(project_path, ec)) {
            if (entry.is_directory(ec)) {
                roots.push_back(entry.path());
            }
        }
    }

    for (const auto& root : roots) {
        for (const auto* name : manifest_names) {
            fs::path path = root / name;
            std::string h = hash_file(path);
            if (!h.empty()) {
                result.push_back({name, path.lexically_relative(project_path).generic_string(), h});
            }
        }
    }
    return result;
}

std::vector<ManifestFile> capture_dotfiles() {
    static const std::array<const char*, 5> dotfile_names = {
        ".bashrc",
        ".zshrc",
        ".bash_profile",
        ".zprofile",
        ".gitconfig",
    };

    std::vector<ManifestFile> result;
    fs::path home = dmfr::paths::home_dir();
    for (const auto* name : dotfile_names) {
        fs::path path = home / name;
        std::string h = hash_file(path);
        if (!h.empty()) {
            result.push_back({name, path.string(), h});
        }
    }
    return result;
}

} // namespace

nlohmann::json SystemInfo::to_json() const {
    return nlohmann::json{
        {"os_name", os_name},
        {"os_version", os_version},
        {"hostname", hostname},
    };
}

SystemInfo SystemInfo::from_json(const nlohmann::json& j) {
    SystemInfo info;
    if (j.contains("os_name")) j.at("os_name").get_to(info.os_name);
    if (j.contains("os_version")) j.at("os_version").get_to(info.os_version);
    if (j.contains("hostname")) j.at("hostname").get_to(info.hostname);
    return info;
}

nlohmann::json GitInfo::to_json() const {
    return nlohmann::json{
        {"repo_path", repo_path},
        {"branch", branch},
        {"commit_sha", commit_sha},
        {"dirty", dirty},
        {"remote_url", remote_url},
    };
}

GitInfo GitInfo::from_json(const nlohmann::json& j) {
    GitInfo info;
    if (j.contains("repo_path")) j.at("repo_path").get_to(info.repo_path);
    if (j.contains("branch")) j.at("branch").get_to(info.branch);
    if (j.contains("commit_sha")) j.at("commit_sha").get_to(info.commit_sha);
    if (j.contains("dirty")) j.at("dirty").get_to(info.dirty);
    if (j.contains("remote_url")) j.at("remote_url").get_to(info.remote_url);
    return info;
}

nlohmann::json ToolVersion::to_json() const {
    return nlohmann::json{
        {"name", name},
        {"version", version},
        {"executable", executable},
    };
}

ToolVersion ToolVersion::from_json(const nlohmann::json& j) {
    ToolVersion tv;
    if (j.contains("name")) j.at("name").get_to(tv.name);
    if (j.contains("version")) j.at("version").get_to(tv.version);
    if (j.contains("executable")) j.at("executable").get_to(tv.executable);
    return tv;
}

nlohmann::json ManifestFile::to_json() const {
    return nlohmann::json{
        {"name", name},
        {"path", path},
        {"hash", hash},
    };
}

ManifestFile ManifestFile::from_json(const nlohmann::json& j) {
    ManifestFile mf;
    if (j.contains("name")) j.at("name").get_to(mf.name);
    if (j.contains("path")) j.at("path").get_to(mf.path);
    if (j.contains("hash")) j.at("hash").get_to(mf.hash);
    return mf;
}

SnapshotManifest SnapshotManifest::create(std::string project_id) {
    SnapshotManifest manifest;
    manifest.snapshot_id = dmfr::generate_uuid();
    manifest.timestamp = std::chrono::system_clock::now();
    manifest.project_id = std::move(project_id);
    return manifest;
}

nlohmann::json SnapshotManifest::to_json() const {
    nlohmann::json j;
    j["snapshot_id"] = snapshot_id;
    j["timestamp"] = format_time(timestamp);
    j["project_id"] = project_id;
    j["system"] = system.to_json();
    if (git) {
        j["git"] = git->to_json();
    } else {
        j["git"] = nullptr;
    }

    j["tools"] = nlohmann::json::array();
    for (const auto& tool : tools) {
        j["tools"].push_back(tool.to_json());
    }

    j["path_entries"] = path_entries;
    j["env_vars"] = env_vars;

    j["manifests"] = nlohmann::json::array();
    for (const auto& m : manifests) {
        j["manifests"].push_back(m.to_json());
    }

    j["dotfiles"] = nlohmann::json::array();
    for (const auto& d : dotfiles) {
        j["dotfiles"].push_back(d.to_json());
    }

    j["notes"] = notes;
    return j;
}

SnapshotManifest SnapshotManifest::from_json(const nlohmann::json& j) {
    SnapshotManifest manifest;
    if (j.contains("snapshot_id")) j.at("snapshot_id").get_to(manifest.snapshot_id);
    if (j.contains("timestamp")) manifest.timestamp = parse_time(j.at("timestamp").get<std::string>());
    if (j.contains("project_id")) j.at("project_id").get_to(manifest.project_id);
    if (j.contains("system")) manifest.system = SystemInfo::from_json(j.at("system"));
    if (j.contains("git") && !j.at("git").is_null()) {
        manifest.git = GitInfo::from_json(j.at("git"));
    }
    if (j.contains("tools") && j.at("tools").is_array()) {
        for (const auto& item : j.at("tools")) {
            manifest.tools.push_back(ToolVersion::from_json(item));
        }
    }
    if (j.contains("path_entries")) j.at("path_entries").get_to(manifest.path_entries);
    if (j.contains("env_vars")) j.at("env_vars").get_to(manifest.env_vars);
    if (j.contains("manifests") && j.at("manifests").is_array()) {
        for (const auto& item : j.at("manifests")) {
            manifest.manifests.push_back(ManifestFile::from_json(item));
        }
    }
    if (j.contains("dotfiles") && j.at("dotfiles").is_array()) {
        for (const auto& item : j.at("dotfiles")) {
            manifest.dotfiles.push_back(ManifestFile::from_json(item));
        }
    }
    if (j.contains("notes")) manifest.notes = j.at("notes");
    return manifest;
}

std::string SnapshotManifest::serialize() const {
    return to_json().dump(4);
}

std::optional<SnapshotManifest> SnapshotManifest::deserialize(const std::string& data) {
    try {
        return from_json(nlohmann::json::parse(data));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

SnapshotManifest capture(const fs::path& project_path) {
    auto manifest = SnapshotManifest::create(project_path.filename().string());
    manifest.system = capture_system();
    manifest.git = capture_git(project_path);
    manifest.notes["probe_warnings"] = nlohmann::json::array();
    manifest.tools = capture_tools(manifest.notes["probe_warnings"]);
    manifest.path_entries = capture_path();
    manifest.env_vars = capture_env();
    manifest.manifests = capture_manifests(project_path);
    manifest.dotfiles = capture_dotfiles();
    return manifest;
}

} // namespace dmfr::snapshot
