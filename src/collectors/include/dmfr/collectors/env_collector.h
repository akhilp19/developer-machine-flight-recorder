#pragma once

#include "dmfr/collectors/collector.h"

#include <chrono>
#include <map>
#include <string>

namespace dmfr::collectors {

class EnvCollector : public Collector {
public:
    EnvCollector();

    std::string name() const override { return "environment"; }
    bool initialize() override;
    void shutdown() override;
    bool is_available() const override;

    std::vector<Event> poll() override;

private:
    bool initialized_{false};
    std::chrono::steady_clock::time_point last_tool_poll_;
    std::map<std::string, std::string> dotfile_hashes_;
    std::map<std::string, std::string> manifest_hashes_;

    std::vector<Event> poll_tool_versions();
    std::vector<Event> poll_dotfiles();
    std::vector<Event> poll_manifests();

    static std::string hash_file(const std::filesystem::path& path);
    static std::string run_command(const char* cmd);
};

} // namespace dmfr::collectors
