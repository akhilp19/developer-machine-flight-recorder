#pragma once

#include "dmfr/collectors/collector.h"

#include <chrono>
#include <string>

namespace dmfr::collectors {

class SystemCollector : public Collector {
public:
    SystemCollector();

    std::string name() const override { return "system"; }
    bool initialize() override;
    void shutdown() override;
    bool is_available() const override;

    std::vector<Event> poll() override;

private:
    bool initialized_{false};
    bool os_version_emitted_{false};
    std::chrono::steady_clock::time_point last_sample_;
    std::chrono::steady_clock::time_point last_env_sample_;
    std::string last_path_hash_;
    std::string last_env_hash_;

    std::vector<Event> sample_os();
    std::vector<Event> sample_cpu_memory();
    std::vector<Event> sample_env();

    static std::string hash_string(const std::string& value);
};

} // namespace dmfr::collectors
