#include "dmfr/common/paths.h"

#include <cstdlib>
#include <optional>

namespace dmfr::paths {

namespace fs = std::filesystem;

namespace {

std::optional<fs::path>& data_dir_override() {
    static std::optional<fs::path> override;
    return override;
}

} // namespace

fs::path home_dir() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home) {
        return fs::current_path();
    }
    return fs::path(home);
}

fs::path data_dir() {
    if (auto& override = data_dir_override(); override.has_value()) {
        return *override;
    }
#ifdef _WIN32
    return home_dir() / "AppData" / "Local" / "dmfr";
#elif __APPLE__
    return home_dir() / "Library" / "Application Support" / "dmfr";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg) {
        return fs::path(xdg) / "dmfr";
    }
    return home_dir() / ".local" / "share" / "dmfr";
#endif
}

void set_data_dir_override(const fs::path& path) {
    data_dir_override() = path;
}

void clear_data_dir_override() {
    data_dir_override().reset();
}

fs::path config_dir() {
#ifdef _WIN32
    return home_dir() / "AppData" / "Roaming" / "dmfr";
#elif __APPLE__
    return home_dir() / "Library" / "Preferences" / "dmfr";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) {
        return fs::path(xdg) / "dmfr";
    }
    return home_dir() / ".config" / "dmfr";
#endif
}

fs::path log_dir() {
    return data_dir() / "logs";
}

fs::path plugin_dir() {
    return data_dir() / "plugins";
}

fs::path socket_path() {
#ifdef _WIN32
    return fs::path("\\\\.\\pipe\\dmfrd");
#else
    return data_dir() / "daemon.sock";
#endif
}

fs::path database_path() {
    return data_dir() / "events.db";
}

fs::path archive_dir() {
    return data_dir() / "archive";
}

fs::path pid_path() {
    return home_dir() / ".dmfr" / "daemon.pid";
}

fs::path bin_dir() {
    return home_dir() / ".dmfr" / "bin";
}

} // namespace dmfr::paths
