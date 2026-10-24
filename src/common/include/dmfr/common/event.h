#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dmfr {

enum class Source : uint8_t {
    Unknown = 0,
    Git = 1,
    Build = 2,
    System = 3,
    Environment = 4,
    Plugin = 5
};

enum class Sensitivity : uint8_t {
    Public = 0,
    Private = 1,
    Scrubbed = 2
};

std::string to_string(Source source);
std::string to_string(Sensitivity sensitivity);

// Generate a random UUID-like string.
std::string generate_uuid();

struct Event {
    std::string event_id;
    std::chrono::system_clock::time_point timestamp;
    Source source{Source::Unknown};
    std::string type;
    std::string project_id;
    std::string session_id;
    nlohmann::json payload;
    std::vector<std::string> correlation_keys;
    Sensitivity sensitivity{Sensitivity::Public};

    static Event create(Source source,
                        std::string type,
                        std::string project_id,
                        nlohmann::json payload);
};

} // namespace dmfr
