#pragma once

#include "dmfr/common/event.h"

#include <optional>
#include <string>
#include <vector>

namespace dmfr::collectors {

class Collector {
public:
    virtual ~Collector() = default;

    virtual std::string name() const = 0;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_available() const = 0;

    // Poll-based collectors implement this.
    virtual std::vector<Event> poll() { return {}; }

    // Hook-based collectors receive events from external triggers.
    virtual std::optional<Event> on_hook(const std::string& hook_name,
                                         const nlohmann::json& data) {
        (void)hook_name;
        (void)data;
        return std::nullopt;
    }
};

} // namespace dmfr::collectors
