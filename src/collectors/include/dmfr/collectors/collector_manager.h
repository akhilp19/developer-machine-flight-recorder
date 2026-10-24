#pragma once

#include "dmfr/collectors/build_collector.h"
#include "dmfr/collectors/env_collector.h"
#include "dmfr/collectors/git_collector.h"
#include "dmfr/collectors/system_collector.h"
#include "dmfr/common/config.h"

#include "dmfr/scrubber/scrubber.h"
#include "dmfr/store/store.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace dmfr::collectors {

class CollectorManager {
public:
    CollectorManager(dmfr::store::Store* store, dmfr::scrubber::Scrubber* scrubber);
    ~CollectorManager();

    bool start();
    void stop();

    void apply_config(const dmfr::config::CollectorConfig& config);

    // Route an externally-triggered hook event to the correct collector.
    bool on_hook(const std::string& source, const nlohmann::json& data);

    bool is_running() const { return running_.load(); }

private:
    dmfr::store::Store* store_;
    dmfr::scrubber::Scrubber* scrubber_;

    dmfr::config::CollectorConfig config_;

    GitCollector git_collector_;
    BuildCollector build_collector_;
    SystemCollector system_collector_;
    EnvCollector env_collector_;

    std::atomic<bool> running_{false};
    std::thread thread_;

    void run();
    void insert_event(dmfr::Event event);
};

} // namespace dmfr::collectors
