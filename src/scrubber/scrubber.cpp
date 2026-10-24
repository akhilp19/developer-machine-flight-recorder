#include "dmfr/scrubber/scrubber.h"

#include "dmfr/common/paths.h"
#include "dmfr/snapshot/snapshot.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace dmfr::scrubber {

namespace {

std::vector<std::string> default_denied_env_vars() {
    return {
        "PASSWORD", "PASS", "TOKEN", "SECRET", "KEY", "API_KEY",
        "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY",
        "GITHUB_TOKEN", "GITLAB_TOKEN",
        "NPM_TOKEN", "PYPI_TOKEN",
        "SSH_PRIVATE_KEY", "PRIVATE_KEY"
    };
}

} // namespace

Scrubber::Scrubber() : denied_env_vars_(default_denied_env_vars()) {}

void Scrubber::add_denied_env_var(std::string pattern) {
    denied_env_vars_.push_back(std::move(pattern));
}

void Scrubber::add_secret_regex(std::string regex) {
    secret_regexes_.emplace_back(regex);
}

void Scrubber::set_max_string_size(size_t size) {
    max_string_size_ = size;
}

void Scrubber::apply_config(const dmfr::config::ScrubberConfig& config) {
    denied_env_vars_ = default_denied_env_vars();
    for (const auto& pattern : config.denied_env_vars) {
        if (std::find(denied_env_vars_.begin(), denied_env_vars_.end(), pattern) ==
            denied_env_vars_.end()) {
            denied_env_vars_.push_back(pattern);
        }
    }
    std::vector<std::regex> compiled;
    for (const auto& pattern : config.secret_regexes) compiled.emplace_back(pattern);
    secret_regexes_ = std::move(compiled);
    max_string_size_ = config.max_string_size;
}

bool Scrubber::scrub(dmfr::snapshot::SnapshotManifest& manifest) {
    auto payload = manifest.to_json();
    bool changed = scrub_payload(payload);
    manifest = dmfr::snapshot::SnapshotManifest::from_json(payload);
    return changed;
}

bool Scrubber::scrub(Event& event) {
    bool changed = false;

    if (event.sensitivity == Sensitivity::Private ||
        event.sensitivity == Sensitivity::Scrubbed) {
        // Already private or scrubbed; leave as-is.
        return false;
    }

    if (scrub_payload(event.payload)) {
        changed = true;
    }

    if (changed) {
        event.sensitivity = Sensitivity::Scrubbed;
    }

    return changed;
}

bool Scrubber::scrub_payload(nlohmann::json& payload, const std::string& key_hint) {
    bool changed = false;
    if (payload.is_string()) {
        std::string value = payload.get<std::string>();
        if (is_path_key(key_hint)) {
            std::string masked = mask_path(value);
            if (masked != value) {
                value = masked;
                payload = masked;
                changed = true;
            }
        }
        if (scrub_string(value)) {
            payload = value;
            changed = true;
        }
    } else if (payload.is_object()) {
        for (auto& [key, value] : payload.items()) {
            if (is_denied_env_var(key) && value.is_string()) {
                value = "<REDACTED>";
                changed = true;
            } else if (scrub_payload(value, key)) {
                changed = true;
            }
        }
    } else if (payload.is_array()) {
        bool redact_next = false;
        for (auto& element : payload) {
            if (key_hint == "args" && element.is_string()) {
                auto arg = element.get<std::string>();
                if (redact_next) {
                    element = "<REDACTED>";
                    redact_next = false;
                    changed = true;
                    continue;
                }
                const auto equals = arg.find('=');
                auto name = arg.substr(0, equals);
                std::replace(name.begin(), name.end(), '-', '_');
                if (is_denied_env_var(name)) {
                    if (equals != std::string::npos) {
                        element = arg.substr(0, equals + 1) + "<REDACTED>";
                        changed = true;
                        continue;
                    }
                    if (!arg.empty() && arg[0] == '-') redact_next = true;
                }
            }
            if (scrub_payload(element, key_hint)) {
                changed = true;
            }
        }
    }
    return changed;
}

bool Scrubber::scrub_string(std::string& value) {
    bool changed = false;
    for (const auto& pattern : secret_regexes_) {
        auto redacted = std::regex_replace(value, pattern, "<REDACTED>");
        if (redacted != value) {
            value = std::move(redacted);
            changed = true;
        }
    }
    if (value.size() > max_string_size_) {
        value.resize(max_string_size_);
        value += "...<TRUNCATED>";
        return true;
    }

    return changed;
}

bool Scrubber::is_path_key(const std::string& key) const {
    std::string upper;
    upper.reserve(key.size());
    for (char c : key) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return upper.find("PATH") != std::string::npos ||
           upper.find("CWD") != std::string::npos ||
           upper.find("REPO") != std::string::npos ||
           upper == "EXECUTABLE" ||
           upper == "COMMAND_IDENTITY" ||
           upper == "FILE" ||
           upper == "DIR";
}

std::string Scrubber::mask_path(const std::string& value) const {
    std::string home = dmfr::paths::home_dir().string();
    std::string masked = value;
    size_t pos = masked.find(home);
    while (pos != std::string::npos) {
        masked.replace(pos, home.size(), "~");
        pos = masked.find(home);
    }
    return masked;
}

bool Scrubber::is_denied_env_var(const std::string& key) const {
    std::string upper;
    upper.reserve(key.size());
    for (char c : key) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    for (const auto& pattern : denied_env_vars_) {
        if (upper.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace dmfr::scrubber
