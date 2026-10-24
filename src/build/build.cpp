#include "dmfr/build/build.h"
#include "dmfr/diff/diff.h"
#include "dmfr/common/paths.h"
#include "dmfr/common/process.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>


namespace dmfr::build {
namespace {
#if defined(__unix__) || defined(__APPLE__)
// Prevent version probes and nested tool calls from re-entering our wrappers.
struct BuildPath {
    std::optional<std::string> original;
    BuildPath() {
        const char* path = std::getenv("PATH");
        if (!path) return;
        original = path;
        const auto wrapper = std::filesystem::weakly_canonical(paths::bin_dir());
        std::string clean;
        bool first = true;
        size_t start = 0;
        for (;;) {
            auto end = original->find(':', start);
            auto entry = original->substr(start, end == std::string::npos ? end : end - start);
            auto directory = entry.empty() ? std::filesystem::current_path() : std::filesystem::path(entry);
            if (std::filesystem::weakly_canonical(directory) != wrapper) {
                if (!first) clean += ':';
                clean += entry;
                first = false;
            }
            if (end == std::string::npos) break;
            start = end + 1;
        }
        ::setenv("PATH", clean.c_str(), 1);
    }
    ~BuildPath() { if (original) ::setenv("PATH", original->c_str(), 1); }
};

#endif

bool valid_build(const Event& event) {
    return event.source == Source::Build && event.type == "invocation" &&
        event.payload.is_object() && event.payload.contains("exit_code") &&
        event.payload["exit_code"].is_number_integer() &&
        event.payload.contains("command") && event.payload["command"].is_string() &&
        event.payload.contains("args") && event.payload["args"].is_array();
}
}

int record(const std::vector<std::string>& argv, const std::string& tool,
           store::Store& store, scrubber::Scrubber& scrubber) {
#if defined(__unix__) || defined(__APPLE__)
    if (argv.empty()) throw std::invalid_argument("record requires a command");
    BuildPath build_path;
    const auto cwd = std::filesystem::weakly_canonical(std::filesystem::current_path());
    const auto executable = process::resolve(argv.front());
    std::string snapshot_id;
    bool recording = true;
    try {
        auto manifest = snapshot::capture(cwd);
        manifest.notes["capture_context"] = "build_process_before_execution";
        scrubber.scrub(manifest);
        if (store.insert_snapshot(manifest)) snapshot_id = manifest.snapshot_id;
        else std::cerr << "dmfr: snapshot could not be stored; build will still run\n";
    } catch (const std::exception&) {
        recording = false;
        std::cerr << "dmfr: capture failed; running without telemetry\n";
    }
    const auto start = std::chrono::system_clock::now();
    const auto clock_start = std::chrono::steady_clock::now();
    const int exit_code = process::foreground(argv);
    if (!recording) return exit_code;
    try {
        auto event = Event::create(Source::Build, "invocation", cwd.string(), {
            {"command", tool.empty() ? std::filesystem::path(argv.front()).filename().string() : tool},
            {"command_identity", tool.empty() ? (argv.front().find('/') == std::string::npos ? argv.front() :
                std::filesystem::absolute(argv.front()).lexically_normal().string()) : tool},
            {"args", std::vector<std::string>(argv.begin() + 1, argv.end())},
            {"executable", executable}, {"cwd", cwd.string()},
            {"start_time", std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count()},
            {"duration_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - clock_start).count()},
            {"completed_time", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
            {"exit_code", exit_code}, {"capture_context", "build_process_before_execution"},
            {"snapshot_id", snapshot_id},
            {"output_capture", "passthrough"}
        });
        // Start time, rather than completion order, defines the baseline ordering.
        event.timestamp = start;
        scrubber.scrub(event);
        if (!store.insert(event)) std::cerr << "dmfr: build event could not be stored\n";
    } catch (const std::exception&) {
        std::cerr << "dmfr: result could not be recorded; preserving build exit status\n";
    }
    return exit_code;
#else
    (void)argv; (void)tool; (void)store; (void)scrubber;
    throw std::runtime_error("build recording currently requires macOS or Linux");
#endif
}

int unrecorded(const std::vector<std::string>& argv) {
#if defined(__unix__) || defined(__APPLE__)
    BuildPath path;
#endif
    return process::foreground(argv);
}

nlohmann::json diagnose(store::Store& store, scrubber::Scrubber& scrubber,
                        const std::string& project, const std::string& event_id) {
    nlohmann::json report = {
        {"status", "no_failed_build"},
        {"interpretation", "Observed differences are candidate explanations, not proof of causation."}
    };
    std::optional<Event> failure;
    if (!event_id.empty()) {
        failure = store.get_event_by_id(event_id);
        if (!failure || !valid_build(*failure) || failure->payload["exit_code"] == 0) {
            report["status"] = "invalid_failed_build";
            return report;
        }
    }
    store::Query query;
    query.source = Source::Build;
    query.type = "invocation";
    query.project_id = failure ? failure->project_id : project;
    query.limit = 100;
    std::optional<Event> success;
    bool baseline_query = false;
    for (;;) {
        if (failure && !baseline_query) {
            query.command_identity = failure->payload.value("command_identity", failure->payload["command"].get<std::string>());
            query.args_json = failure->payload["args"].dump();
            query.exit_code = 0;
            query.before_time = failure->timestamp;
            query.before_id = ""; // only strictly earlier timestamps are baselines
            baseline_query = true;
        }
        auto events = store.query(query);
        if (events.empty()) break;
        for (const auto& event : events) {
            if (!valid_build(event)) continue;
            if (!failure) {
                if (event.payload["exit_code"] != 0) {
                    failure = event;
                    break;
                }
                continue;
            }
            if (event.timestamp >= failure->timestamp) continue;
            // A concurrently running build is not a known success at failure start.
            if (event.payload.contains("completed_time") && failure->payload.contains("start_time") &&
                event.payload["completed_time"].is_number_integer() &&
                failure->payload["start_time"].is_number_integer() &&
                event.payload["completed_time"].get<long long>() > failure->payload["start_time"].get<long long>()) continue;
            if (event.payload["exit_code"] == 0 &&
                event.payload["command"] == failure->payload["command"] &&
                event.payload["args"] == failure->payload["args"]) {
                success = event;
                break;
            }
        }
        if (failure && !baseline_query) continue;
        if (success || events.size() < query.limit) break;
        query.before_time = events.back().timestamp;
        query.before_id = events.back().event_id;
    }
    if (!failure) return report;
    report["failed_event_id"] = failure->event_id;
    const auto argument_evidence = failure->payload["args"].dump() +
        failure->payload.value("command_identity", failure->payload["command"].get<std::string>());
    if (argument_evidence.find("<REDACTED>") != std::string::npos ||
        argument_evidence.find("<TRUNCATED>") != std::string::npos) {
        report["status"] = "command_arguments_redacted";
        report["message"] = "Scrubbed command identity or arguments cannot establish an exact command match.";
        return report;
    }
    if (!success) {
        report["status"] = "no_matching_success";
        return report;
    }
    report["successful_event_id"] = success->event_id;
    auto load = [&store](const Event& event) -> std::optional<snapshot::SnapshotManifest> {
        if (!event.payload.contains("snapshot_id") || !event.payload["snapshot_id"].is_string() ||
            event.payload["snapshot_id"].get<std::string>().empty()) return std::nullopt;
        return store.get_snapshot_by_id(event.payload["snapshot_id"].get<std::string>());
    };
    auto before = load(*success), after = load(*failure);
    if (!before || !after) {
        report["status"] = "missing_snapshot";
        report["message"] = "Record both builds with dmfr record or the updated wrappers.";
        return report;
    }
    scrubber.scrub(*before);
    scrubber.scrub(*after);
    report["status"] = "compared";
    report["diff"] = diff::compare(*before, *after);
    report["probe_warnings"] = {
        {"before", before->notes.is_object() ? before->notes.value("probe_warnings", nlohmann::json::array()) : nlohmann::json::array()},
        {"after", after->notes.is_object() ? after->notes.value("probe_warnings", nlohmann::json::array()) : nlohmann::json::array()}
    };
    auto safe_success = *success, safe_failure = *failure;
    // Re-apply current policy, including to older previously-scrubbed events.
    safe_success.sensitivity = safe_failure.sensitivity = Sensitivity::Public;
    scrubber.scrub(safe_success);
    scrubber.scrub(safe_failure);
    report["command"] = safe_failure.payload["command"];
    report["args"] = safe_failure.payload["args"];
    report["build_executable"] = {
        {"before", safe_success.payload.value("executable", "")},
        {"after", safe_failure.payload.value("executable", "")}
    };
    report["coverage"] = "Pre-build process environment, selected tool versions, and manifest/lockfile fingerprints. No executable environment is reconstructed.";
    return report;
}
}
