#include "dmfr/common/config.h"

#include "dmfr/common/paths.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace dmfr::config {

namespace {

nlohmann::json retention_to_json(const RetentionConfig& r) {
    return nlohmann::json{
        {"enabled", r.enabled},
        {"max_events", r.max_events},
        {"max_age_hours", r.max_age_hours},
        {"interval_minutes", r.interval_minutes},
    };
}

void retention_from_json(const nlohmann::json& j, RetentionConfig& r) {
    if (j.contains("enabled")) j.at("enabled").get_to(r.enabled);
    if (j.contains("max_events")) j.at("max_events").get_to(r.max_events);
    if (j.contains("max_age_hours")) j.at("max_age_hours").get_to(r.max_age_hours);
    if (j.contains("interval_minutes")) j.at("interval_minutes").get_to(r.interval_minutes);
}

nlohmann::json collectors_to_json(const CollectorConfig& c) {
    return nlohmann::json{
        {"git", c.git},
        {"build", c.build},
        {"system", c.system},
        {"environment", c.environment},
        {"poll_interval_seconds", c.poll_interval_seconds},
    };
}

void collectors_from_json(const nlohmann::json& j, CollectorConfig& c) {
    if (j.contains("git")) j.at("git").get_to(c.git);
    if (j.contains("build")) j.at("build").get_to(c.build);
    if (j.contains("system")) j.at("system").get_to(c.system);
    if (j.contains("environment")) j.at("environment").get_to(c.environment);
    if (j.contains("poll_interval_seconds")) j.at("poll_interval_seconds").get_to(c.poll_interval_seconds);
}

nlohmann::json scrubber_to_json(const ScrubberConfig& s) {
    return nlohmann::json{
        {"denied_env_vars", s.denied_env_vars},
        {"secret_regexes", s.secret_regexes},
        {"max_string_size", s.max_string_size},
    };
}

void scrubber_from_json(const nlohmann::json& j, ScrubberConfig& s) {
    if (j.contains("denied_env_vars")) j.at("denied_env_vars").get_to(s.denied_env_vars);
    if (j.contains("secret_regexes")) j.at("secret_regexes").get_to(s.secret_regexes);
    if (j.contains("max_string_size")) j.at("max_string_size").get_to(s.max_string_size);
}

nlohmann::json config_to_json(const Config& c) {
    nlohmann::json j;
    if (c.data_dir) {
        j["data_dir"] = c.data_dir->string();
    }
    j["retention"] = retention_to_json(c.retention);
    j["collectors"] = collectors_to_json(c.collectors);
    j["scrubber"] = scrubber_to_json(c.scrubber);
    return j;
}

void config_from_json(const nlohmann::json& j, Config& c) {
    if (j.contains("data_dir")) {
        std::string path_str;
        j.at("data_dir").get_to(path_str);
        c.data_dir = std::filesystem::path(path_str);
    }
    if (j.contains("retention")) retention_from_json(j.at("retention"), c.retention);
    if (j.contains("collectors")) collectors_from_json(j.at("collectors"), c.collectors);
    if (j.contains("scrubber")) scrubber_from_json(j.at("scrubber"), c.scrubber);
}

} // namespace

Config Config::defaults() {
    return Config{};
}

std::filesystem::path Config::effective_data_dir() const {
    if (data_dir && !data_dir->empty()) {
        return *data_dir;
    }
    return paths::data_dir();
}

Config load(const std::filesystem::path& path) {
    Config config = Config::defaults();

    std::ifstream in(path);
    if (!in) {
        return config;
    }

    try {
        nlohmann::json j;
        in >> j;
        config_from_json(j, config);
    } catch (const std::exception&) {
        // Keep defaults on parse error.
    }

    return config;
}

bool save(const std::filesystem::path& path, const Config& config) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        return false;
    }

    out << config_to_json(config).dump(4);
    return out.good();
}

nlohmann::json to_json(const Config& config) {
    return config_to_json(config);
}

} // namespace dmfr::config
