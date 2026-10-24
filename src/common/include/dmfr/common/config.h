#pragma once

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace dmfr::config {

struct RetentionConfig {
    bool enabled{true};
    size_t max_events{100'000};
    size_t max_age_hours{24 * 30};
    size_t interval_minutes{60};
};

struct CollectorConfig {
    bool git{true};
    bool build{true};
    bool system{true};
    bool environment{true};
    size_t poll_interval_seconds{30};
};

struct ScrubberConfig {
    std::vector<std::string> denied_env_vars;
    std::vector<std::string> secret_regexes;
    size_t max_string_size{4096};
};

struct Config {
    std::optional<std::filesystem::path> data_dir;
    RetentionConfig retention;
    CollectorConfig collectors;
    ScrubberConfig scrubber;

    static Config defaults();

    std::filesystem::path effective_data_dir() const;
};

// Load configuration from a JSON file. Missing keys are filled with defaults.
// If the file cannot be read, defaults are returned.
Config load(const std::filesystem::path& path);

// Save configuration to a JSON file. Returns false on failure.
bool save(const std::filesystem::path& path, const Config& config);

// Serialize configuration to JSON.
nlohmann::json to_json(const Config& config);

} // namespace dmfr::config
