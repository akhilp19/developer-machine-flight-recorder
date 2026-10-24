#pragma once

#include <filesystem>
#include <string>

namespace dmfr::paths {

std::filesystem::path home_dir();
std::filesystem::path data_dir();

// Override the default data directory for the current process.
// Used when a configuration file specifies a custom data_dir.
void set_data_dir_override(const std::filesystem::path& path);
void clear_data_dir_override();
std::filesystem::path config_dir();
std::filesystem::path log_dir();
std::filesystem::path plugin_dir();
std::filesystem::path socket_path();
std::filesystem::path database_path();
std::filesystem::path archive_dir();
std::filesystem::path pid_path();
std::filesystem::path bin_dir();

} // namespace dmfr::paths
