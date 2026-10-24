#include "dmfr/collectors/collector_manager.h"

#include "dmfr/common/paths.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

namespace dmfr::collectors {

CollectorManager::CollectorManager(dmfr::store::Store* store,
                                   dmfr::scrubber::Scrubber* scrubber)
    : store_(store), scrubber_(scrubber),
      git_collector_(paths::home_dir()) {}

CollectorManager::~CollectorManager() {
    stop();
}

void CollectorManager::apply_config(const dmfr::config::CollectorConfig& config) {
    config_ = config;
}

bool CollectorManager::start() {
    if (running_.exchange(true)) {
        return true;
    }

    if (config_.git) {
        git_collector_.initialize();
    }
    if (config_.build) {
        build_collector_.initialize();
    }
    if (config_.system) {
        system_collector_.initialize();
    }
    if (config_.environment) {
        env_collector_.initialize();
    }

    thread_ = std::thread(&CollectorManager::run, this);
    return true;
}

void CollectorManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    if (config_.git) {
        git_collector_.shutdown();
    }
    if (config_.build) {
        build_collector_.shutdown();
    }
    if (config_.system) {
        system_collector_.shutdown();
    }
    if (config_.environment) {
        env_collector_.shutdown();
    }
}

void CollectorManager::run() {
    using namespace std::chrono;
    auto next_poll = steady_clock::now();
    const auto poll_interval = seconds(static_cast<long long>(config_.poll_interval_seconds));

    while (running_.load()) {
        auto now = steady_clock::now();
        if (now >= next_poll) {
            if (config_.system) {
                auto system_events = system_collector_.poll();
                for (auto& event : system_events) {
                    insert_event(std::move(event));
                }
            }

            if (config_.environment) {
                auto env_events = env_collector_.poll();
                for (auto& event : env_events) {
                    insert_event(std::move(event));
                }
            }

            next_poll = now + poll_interval;
        }

        std::this_thread::sleep_for(milliseconds(100));
    }
}

bool CollectorManager::on_hook(const std::string& source, const nlohmann::json& data) {
    if (source == "git" && !config_.git) {
        return false;
    }
    if (source == "build" && !config_.build) {
        return false;
    }

    std::optional<dmfr::Event> event;
    if (source == "git") {
        auto repo = data.value("repo", "");
        GitCollector collector(repo);
        collector.initialize();
        event = collector.on_hook(data.value("type", ""), data.value("data", data));
    } else if (source == "build") {
        event = build_collector_.on_hook(data.value("type", ""), data.value("data", data));
    }

    if (!event) {
        return false;
    }

    insert_event(std::move(*event));
    return true;
}

void CollectorManager::insert_event(dmfr::Event event) {
    if (scrubber_) {
        scrubber_->scrub(event);
    }
    if (store_) {
        if (!store_->insert(event)) {
            spdlog::warn("Failed to insert event from collector into store");
        }
    }
}

} // namespace dmfr::collectors
