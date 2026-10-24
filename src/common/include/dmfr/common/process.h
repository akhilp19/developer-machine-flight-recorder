#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace dmfr::process {
std::string resolve(const std::string& command);
struct Result {
    std::string output;
    int exit_code{127};
    bool timed_out{false};
    bool truncated{false};
    bool ok() const { return exit_code == 0 && !timed_out && !truncated; }
};
// Probes run in their own process group; time/output limits include descendants.
Result capture(const std::vector<std::string>& argv,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
               size_t max_output = 65536);
// Foreground children retain terminal access; SIGINT/SIGQUIT/SIGTERM are forwarded.
int foreground(const std::vector<std::string>& argv);
std::string fingerprint(const std::filesystem::path& file);
}
