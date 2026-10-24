#pragma once

#include <string>
#include <regex>
#include <vector>

#include "dmfr/common/config.h"
#include "dmfr/common/event.h"
#include "dmfr/snapshot/snapshot.h"

namespace dmfr::scrubber {

class Scrubber {
public:
    Scrubber();

    // Redacts secrets in-place. Returns true if any redaction occurred.
    bool scrub(Event& event);

    // Scrub a snapshot manifest (env vars and paths).
    bool scrub(dmfr::snapshot::SnapshotManifest& manifest);

    // Configuration
    void add_denied_env_var(std::string pattern);
    void add_secret_regex(std::string regex);
    void set_max_string_size(size_t size);
    void apply_config(const dmfr::config::ScrubberConfig& config);

private:
    std::vector<std::string> denied_env_vars_;
    std::vector<std::regex> secret_regexes_;
    size_t max_string_size_{4096};

    bool scrub_payload(nlohmann::json& payload, const std::string& key_hint = "");
    bool scrub_string(std::string& value);
    bool is_denied_env_var(const std::string& key) const;
    bool is_path_key(const std::string& key) const;
    std::string mask_path(const std::string& value) const;
};

} // namespace dmfr::scrubber
