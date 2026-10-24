#pragma once

#include "dmfr/collectors/collector.h"

#include <filesystem>

namespace dmfr::collectors {

class GitCollector : public Collector {
public:
    explicit GitCollector(std::filesystem::path repo_path);

    std::string name() const override { return "git"; }
    bool initialize() override;
    void shutdown() override;
    bool is_available() const override;
    std::optional<Event> on_hook(const std::string& hook_name,
                                 const nlohmann::json& data) override;

    static bool install_hooks(const std::filesystem::path& repo_path);

private:
    std::filesystem::path repo_path_;
    bool available_{false};

    Event make_commit_event(const nlohmann::json& data);
    Event make_status_event();
};

} // namespace dmfr::collectors
