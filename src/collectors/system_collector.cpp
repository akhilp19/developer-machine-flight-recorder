#include "dmfr/collectors/system_collector.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(__APPLE__)
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/host_info.h>
#include <mach/vm_statistics.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
extern char** environ;
#endif

namespace dmfr::collectors {

namespace {

std::string run_command(const char* cmd) {
    std::array<char, 256> buffer{};
    std::string result;
    FILE* pipe = ::popen(cmd, "r");
    if (!pipe) {
        return "";
    }
    while (::fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    ::pclose(pipe);
    // Trim trailing newline/whitespace.
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

} // namespace

SystemCollector::SystemCollector()
    : last_sample_(std::chrono::steady_clock::now()),
      last_env_sample_(std::chrono::steady_clock::now()) {}

bool SystemCollector::initialize() {
    initialized_ = true;
    return true;
}

void SystemCollector::shutdown() {
    initialized_ = false;
}

bool SystemCollector::is_available() const {
    return initialized_;
}

std::vector<Event> SystemCollector::poll() {
    std::vector<Event> events;
    auto now = std::chrono::steady_clock::now();

    if (!os_version_emitted_) {
        auto os_events = sample_os();
        events.insert(events.end(), os_events.begin(), os_events.end());
        os_version_emitted_ = true;
    }

    // Sample CPU/memory every 30 seconds.
    if (now - last_sample_ >= std::chrono::seconds(30)) {
        auto sample = sample_cpu_memory();
        events.insert(events.end(), sample.begin(), sample.end());
        last_sample_ = now;
    }

    // Sample environment every 30 seconds if changed.
    if (now - last_env_sample_ >= std::chrono::seconds(30)) {
        auto env = sample_env();
        events.insert(events.end(), env.begin(), env.end());
        last_env_sample_ = now;
    }

    return events;
}

std::vector<Event> SystemCollector::sample_os() {
    nlohmann::json payload;
    std::string name;
    std::string version;

#if defined(__APPLE__)
    name = "macOS";
    version = run_command("sw_vers -productVersion");
#elif defined(__linux__)
    name = "Linux";
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            version = line.substr(12);
            if (!version.empty() && (version.front() == '"' || version.front() == '\'')) {
                version.erase(0, 1);
            }
            if (!version.empty() && (version.back() == '"' || version.back() == '\'')) {
                version.pop_back();
            }
            break;
        }
    }
    if (version.empty()) {
        version = read_file_first_line("/proc/version");
    }
#elif defined(_WIN32)
    name = "Windows";
    version = "unknown";
#else
    name = "unknown";
    version = "unknown";
#endif

    payload["os"] = name;
    payload["version"] = version;

    return {Event::create(Source::System, "os_version", "", std::move(payload))};
}

std::vector<Event> SystemCollector::sample_cpu_memory() {
    nlohmann::json payload;

#if defined(__APPLE__)
    // CPU load.
    host_cpu_load_info_data_t cpu_info{};
    mach_msg_type_number_t cpu_count = HOST_CPU_LOAD_INFO_COUNT;
    kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                       reinterpret_cast<host_info_t>(&cpu_info), &cpu_count);
    if (kr == KERN_SUCCESS) {
        uint64_t total = cpu_info.cpu_ticks[CPU_STATE_USER] +
                         cpu_info.cpu_ticks[CPU_STATE_SYSTEM] +
                         cpu_info.cpu_ticks[CPU_STATE_IDLE] +
                         cpu_info.cpu_ticks[CPU_STATE_NICE];
        if (total > 0) {
            payload["cpu_user"] = static_cast<double>(cpu_info.cpu_ticks[CPU_STATE_USER]) / static_cast<double>(total);
            payload["cpu_system"] = static_cast<double>(cpu_info.cpu_ticks[CPU_STATE_SYSTEM]) / static_cast<double>(total);
            payload["cpu_idle"] = static_cast<double>(cpu_info.cpu_ticks[CPU_STATE_IDLE]) / static_cast<double>(total);
        }
    }

    // Memory pressure.
    vm_statistics64_data_t vm_info{};
    mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
    kr = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                           reinterpret_cast<host_info64_t>(&vm_info), &vm_count);
    if (kr == KERN_SUCCESS) {
        uint64_t used = vm_info.active_count + vm_info.inactive_count + vm_info.wire_count;
        uint64_t free = vm_info.free_count;
        uint64_t total = used + free;
        payload["memory_used_pages"] = used;
        payload["memory_free_pages"] = free;
        payload["memory_total_pages"] = total;
        if (total > 0) {
            payload["memory_pressure"] = static_cast<double>(used) / static_cast<double>(total);
        }
    }
#elif defined(__linux__)
    std::string load = read_file_first_line("/proc/loadavg");
    if (!load.empty()) {
        std::istringstream iss(load);
        double one_min;
        if (iss >> one_min) {
            payload["loadavg_1m"] = one_min;
        }
    }

    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::istringstream iss(line.substr(9));
            iss >> total_kb;
        } else if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream iss(line.substr(13));
            iss >> available_kb;
        }
    }
    if (total_kb > 0) {
        payload["memory_total_kb"] = total_kb;
        payload["memory_available_kb"] = available_kb;
        payload["memory_pressure"] = static_cast<double>(total_kb - available_kb) / static_cast<double>(total_kb);
    }
#elif defined(_WIN32)
    payload["note"] = "system sampling not implemented on Windows";
#else
    payload["note"] = "system sampling not implemented on this platform";
#endif

    return {Event::create(Source::System, "system_sample", "", std::move(payload))};
}

std::vector<Event> SystemCollector::sample_env() {
    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    std::string env = path + "\n";
#if defined(__unix__) || defined(__APPLE__)
    if (environ) {
        for (char** p = environ; *p; ++p) {
            env += *p;
            env += '\n';
        }
    }
#endif

    std::string path_hash = hash_string(path);
    std::string env_hash = hash_string(env);

    if (path_hash == last_path_hash_ && env_hash == last_env_hash_) {
        return {};
    }

    last_path_hash_ = path_hash;
    last_env_hash_ = env_hash;

    nlohmann::json payload;
    payload["path"] = path;
    payload["path_hash"] = path_hash;
    payload["env_hash"] = env_hash;

    return {Event::create(Source::System, "env_snapshot", "", std::move(payload))};
}

std::string SystemCollector::hash_string(const std::string& value) {
    // Simple FNV-1a hash.
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

} // namespace dmfr::collectors
