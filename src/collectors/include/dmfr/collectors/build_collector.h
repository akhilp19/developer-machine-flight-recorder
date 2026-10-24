#pragma once

#include "dmfr/collectors/collector.h"

namespace dmfr::collectors {

class BuildCollector : public Collector {
public:
    BuildCollector() = default;

    std::string name() const override { return "build"; }
    bool initialize() override;
    void shutdown() override;
    bool is_available() const override;

    std::optional<Event> on_hook(const std::string& hook_name,
                                 const nlohmann::json& data) override;

    static Event make_build_event(const nlohmann::json& data);
};

} // namespace dmfr::collectors
