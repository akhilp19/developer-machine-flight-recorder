#include "dmfr/common/config.h"
#include "dmfr/common/paths.h"
#include "dmfr/collectors/collector_manager.h"
#include "dmfr/diff/diff.h"
#include "dmfr/ipc/server.h"
#include "dmfr/snapshot/snapshot.h"
#include "dmfr/scrubber/scrubber.h"
#include "dmfr/store/store.h"

#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

#include <nlohmann/json.hpp>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif


namespace fs = std::filesystem;

static volatile std::sig_atomic_t g_running = 1;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = 0;
    }
}

namespace {

bool write_pid_file(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream ofs(path);
    if (!ofs) {
        return false;
    }
    ofs << ::getpid() << "\n";
    return ofs.good();
}

void remove_pid_file(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

std::optional<dmfr::Source> parse_source(const std::string& str) {
    if (str == "git") return dmfr::Source::Git;
    if (str == "build") return dmfr::Source::Build;
    if (str == "system") return dmfr::Source::System;
    if (str == "environment") return dmfr::Source::Environment;
    if (str == "plugin") return dmfr::Source::Plugin;
    if (str == "unknown") return dmfr::Source::Unknown;
    return std::nullopt;
}

nlohmann::json event_to_json(const dmfr::Event& event) {
    using namespace std::chrono;
    nlohmann::json j;
    j["event_id"] = event.event_id;
    j["timestamp"] = duration_cast<seconds>(event.timestamp.time_since_epoch()).count();
    j["source"] = dmfr::to_string(event.source);
    j["type"] = event.type;
    j["project_id"] = event.project_id;
    j["session_id"] = event.session_id;
    j["payload"] = event.payload;
    j["sensitivity"] = dmfr::to_string(event.sensitivity);
    j["correlation_keys"] = event.correlation_keys;
    return j;
}

std::chrono::system_clock::time_point unix_to_time_point(long long seconds) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

dmfr::store::Query build_query(const nlohmann::json& req, dmfr::store::Order order) {
    dmfr::store::Query query;
    query.order = order;

    if (req.contains("from") && req["from"].is_number()) {
        query.from = unix_to_time_point(req["from"].get<long long>());
    }
    if (req.contains("to") && req["to"].is_number()) {
        query.to = unix_to_time_point(req["to"].get<long long>());
    }
    if (req.contains("source") && req["source"].is_string()) {
        const auto source = parse_source(req["source"].get<std::string>());
        if (source) {
            query.source = *source;
        }
    }
    if (req.contains("project_id") && req["project_id"].is_string()) {
        query.project_id = req["project_id"].get<std::string>();
    }
    if (req.contains("type") && req["type"].is_string()) {
        query.type = req["type"].get<std::string>();
    }
    if (req.contains("limit") && req["limit"].is_number_integer()) {
        query.limit = req["limit"].get<size_t>();
    }
    if (req.contains("offset") && req["offset"].is_number_integer()) {
        query.offset = req["offset"].get<size_t>();
    }

    return query;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    spdlog::info("DMFR daemon starting");

    const auto config = dmfr::config::load(dmfr::paths::config_dir() / "config.json");
    dmfr::paths::set_data_dir_override(config.effective_data_dir());

    const fs::path data_dir = dmfr::paths::data_dir();
    if (!fs::exists(data_dir)) {
        std::error_code ec;
        fs::create_directories(data_dir, ec);
        if (ec) {
            spdlog::error("Failed to create data directory: {}", ec.message());
            return 1;
        }
    }

    const fs::path pid_path = dmfr::paths::pid_path();
    if (!write_pid_file(pid_path)) {
        spdlog::error("Failed to write PID file to {}", pid_path.string());
        return 1;
    }

    dmfr::store::Store store(dmfr::paths::database_path());
    if (!store.open()) {
        spdlog::error("Failed to open local store");
        remove_pid_file(pid_path);
        return 1;
    }

    dmfr::scrubber::Scrubber scrubber;
    scrubber.apply_config(config.scrubber);

    dmfr::collectors::CollectorManager collector_manager(&store, &scrubber);
    collector_manager.apply_config(config.collectors);

    dmfr::ipc::Server server(dmfr::paths::socket_path().string());
    server.set_handler([&store, &scrubber, &collector_manager](const nlohmann::json& req) -> nlohmann::json {
        nlohmann::json response;
        response["ok"] = true;

        auto cmd_it = req.find("cmd");
        if (cmd_it == req.end() || !cmd_it->is_string()) {
            response["ok"] = false;
            response["error"] = "missing or invalid 'cmd' field";
            return response;
        }

        const std::string cmd = cmd_it->get<std::string>();

        if (cmd == "status") {
            response["status"] = "running";
            response["pid"] = ::getpid();
            response["collectors"] = collector_manager.is_running();

            dmfr::store::Query count_query;
            count_query.limit = std::numeric_limits<size_t>::max();
            const auto all_events = store.query(count_query);
            response["events"] = all_events.size();
            return response;
        }

        if (cmd == "shutdown") {
            g_running = 0;
            return response;
        }

        if (cmd == "query") {
            auto query = build_query(req, dmfr::store::Order::Descending);
            const auto events = store.query(query);
            nlohmann::json array = nlohmann::json::array();
            for (const auto& event : events) {
                array.push_back(event_to_json(event));
            }
            response["events"] = array;
            return response;
        }

        if (cmd == "timeline") {
            auto query = build_query(req, dmfr::store::Order::Ascending);
            const auto events = store.query(query);
            nlohmann::json array = nlohmann::json::array();
            for (const auto& event : events) {
                array.push_back(event_to_json(event));
            }
            response["events"] = array;
            return response;
        }

        if (cmd == "events") {
            auto query = build_query(req, dmfr::store::Order::Descending);
            const auto events = store.query(query);
            nlohmann::json array = nlohmann::json::array();
            for (const auto& event : events) {
                array.push_back(event_to_json(event));
            }
            response["events"] = array;
            response["offset"] = query.offset;
            response["limit"] = query.limit;
            return response;
        }

        if (cmd == "show") {
            if (!req.contains("event_id") || !req["event_id"].is_string()) {
                response["ok"] = false;
                response["error"] = "missing or invalid 'event_id' field";
                return response;
            }

            auto event = store.get_event_by_id(req["event_id"].get<std::string>());
            if (!event) {
                response["ok"] = false;
                response["error"] = "event not found";
                return response;
            }

            response["event"] = event_to_json(*event);

            int window_minutes = req.value("window_minutes", 10);
            size_t context_limit = req.value("context_limit", 50);
            bool same_project = req.value("same_project", false);
            bool same_session = req.value("same_session", false);

            auto context = store.get_event_context(*event,
                                                   window_minutes,
                                                   context_limit,
                                                   same_project,
                                                   same_session);
            nlohmann::json context_array = nlohmann::json::array();
            for (const auto& ctx_event : context) {
                context_array.push_back(event_to_json(ctx_event));
            }
            response["context"] = context_array;
            return response;
        }

        if (cmd == "export_parquet") {
            if (!req.contains("output_dir") || !req["output_dir"].is_string()) {
                response["ok"] = false;
                response["error"] = "missing or invalid 'output_dir' field";
                return response;
            }

            auto query = build_query(req, dmfr::store::Order::Descending);
            std::filesystem::path output_dir(req["output_dir"].get<std::string>());
            if (!store.export_parquet(output_dir, query)) {
                response["ok"] = false;
                response["error"] = "parquet export failed (is Arrow available?)";
            }
            return response;
        }

        if (cmd == "snapshot") {
            if (req.contains("list") && req["list"].get<bool>()) {
                dmfr::store::SnapshotQuery sq;
                sq.limit = req.value("limit", 100);
                if (req.contains("project_id") && req["project_id"].is_string()) {
                    sq.project_id = req["project_id"].get<std::string>();
                }

                auto snapshots = store.query_snapshots(sq);
                nlohmann::json array = nlohmann::json::array();
                for (const auto& snap : snapshots) {
                    nlohmann::json summary;
                    summary["snapshot_id"] = snap.snapshot_id;
                    summary["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                                 snap.timestamp.time_since_epoch())
                                                 .count();
                    summary["project_id"] = snap.project_id;
                    summary["os"] = snap.system.os_name;
                    if (snap.git) {
                        summary["git_commit"] = snap.git->commit_sha;
                        summary["dirty"] = snap.git->dirty;
                    }
                    array.push_back(summary);
                }
                response["snapshots"] = array;
                return response;
            }

            std::filesystem::path project_path = req.contains("project_path") && req["project_path"].is_string()
                ? std::filesystem::path(req["project_path"].get<std::string>())
                : std::filesystem::current_path();

            auto manifest = dmfr::snapshot::capture(project_path);
            scrubber.scrub(manifest);
            if (!store.insert_snapshot(manifest)) {
                response["ok"] = false;
                response["error"] = "failed to store snapshot";
                return response;
            }

            response["snapshot_id"] = manifest.snapshot_id;
            response["manifest"] = manifest.to_json();
            return response;
        }

        if (cmd == "diff") {
            auto get_project_path = [&req]() {
                return (req.contains("project_path") && req["project_path"].is_string())
                    ? std::filesystem::path(req["project_path"].get<std::string>())
                    : std::filesystem::current_path();
            };

            auto load_snapshot = [&](const std::string& id) -> std::optional<dmfr::snapshot::SnapshotManifest> {
                return store.get_snapshot_by_id(id);
            };

            std::optional<dmfr::snapshot::SnapshotManifest> before;
            std::optional<dmfr::snapshot::SnapshotManifest> after;

            if (req.contains("snapshot_id") && req["snapshot_id"].is_string()) {
                before = load_snapshot(req["snapshot_id"].get<std::string>());
                if (!before) {
                    response["ok"] = false;
                    response["error"] = "snapshot not found";
                    return response;
                }
                after = dmfr::snapshot::capture(get_project_path());
            } else if (req.contains("from_id") && req["from_id"].is_string() &&
                       req.contains("to_id") && req["to_id"].is_string()) {
                before = load_snapshot(req["from_id"].get<std::string>());
                after = load_snapshot(req["to_id"].get<std::string>());
                if (!before || !after) {
                    response["ok"] = false;
                    response["error"] = "one or both snapshot ids not found";
                    return response;
                }
            } else if (req.contains("file") && req["file"].is_string()) {
                std::ifstream in(req["file"].get<std::string>());
                if (!in) {
                    response["ok"] = false;
                    response["error"] = "failed to open snapshot file";
                    return response;
                }
                try {
                    nlohmann::json j;
                    in >> j;
                    before = dmfr::snapshot::SnapshotManifest::from_json(j);
                } catch (const std::exception& e) {
                    response["ok"] = false;
                    response["error"] = std::string("invalid snapshot file: ") + e.what();
                    return response;
                }
                after = dmfr::snapshot::capture(get_project_path());
            } else if (req.contains("from") && req.contains("to")) {
                dmfr::store::SnapshotQuery sq;
                sq.from = unix_to_time_point(req["from"].get<long long>());
                sq.to = unix_to_time_point(req["to"].get<long long>());
                sq.limit = req.value("limit", 100);
                if (req.contains("project_id") && req["project_id"].is_string()) {
                    sq.project_id = req["project_id"].get<std::string>();
                }
                sq.order = dmfr::store::Order::Ascending;
                auto snapshots = store.query_snapshots(sq);
                if (snapshots.size() < 2) {
                    response["ok"] = false;
                    response["error"] = "need at least two snapshots in the selected time range";
                    return response;
                }
                before = snapshots.front();
                after = snapshots.back();
            } else {
                response["ok"] = false;
                response["error"] = "diff requires --snapshot, --from-id/--to-id, --file, or --from/--to";
                return response;
            }

            scrubber.scrub(*before);
            scrubber.scrub(*after);
            response["report"] = dmfr::diff::compare(*before, *after);
            return response;
        }

        if (cmd == "bundle") {
            if (!req.contains("output_dir") || !req["output_dir"].is_string()) {
                response["ok"] = false;
                response["error"] = "missing or invalid 'output_dir' field";
                return response;
            }

            std::filesystem::path output_dir(req["output_dir"].get<std::string>());
            std::filesystem::path project_path = (req.contains("project_path") && req["project_path"].is_string())
                ? std::filesystem::path(req["project_path"].get<std::string>())
                : std::filesystem::current_path();

            bool do_scrub = req.value("scrub", true);

            auto manifest = dmfr::snapshot::capture(project_path);
            if (do_scrub) {
                scrubber.scrub(manifest);
            }

            dmfr::store::Query event_query;
            event_query.limit = req.value("limit", 1000);
            if (req.contains("from") && req["from"].is_number()) {
                event_query.from = unix_to_time_point(req["from"].get<long long>());
            }
            if (req.contains("to") && req["to"].is_number()) {
                event_query.to = unix_to_time_point(req["to"].get<long long>());
            }
            if (req.contains("project_id") && req["project_id"].is_string()) {
                event_query.project_id = req["project_id"].get<std::string>();
            }

            auto events = store.query(event_query);

            std::error_code ec;
            std::filesystem::create_directories(output_dir, ec);
            if (ec) {
                response["ok"] = false;
                response["error"] = "failed to create bundle directory";
                return response;
            }

            {
                std::ofstream manifest_out(output_dir / "manifest.json", std::ios::out | std::ios::trunc);
                if (!manifest_out) {
                    response["ok"] = false;
                    response["error"] = "failed to write manifest.json";
                    return response;
                }
                manifest_out << manifest.to_json().dump(2);
            }

            {
                std::ofstream events_out(output_dir / "events.jsonl", std::ios::out | std::ios::trunc);
                if (!events_out) {
                    response["ok"] = false;
                    response["error"] = "failed to write events.jsonl";
                    return response;
                }
                for (const auto& event : events) {
                    events_out << event_to_json(event).dump() << "\n";
                }
            }

            {
                std::ofstream readme_out(output_dir / "README.md", std::ios::out | std::ios::trunc);
                if (readme_out) {
                    readme_out << "# DMFR Diagnostic Bundle\n\n";
                    readme_out << "This bundle contains diagnostic evidence, not a runnable environment. It does not include source, installed dependencies, or replay tooling.\n\n";
                    readme_out << "- Snapshot: `manifest.json`\n";
                    readme_out << "- Events: `events.jsonl`\n";
                    readme_out << "\nUse `dmfr import-snapshot manifest.json` to load the snapshot.\n";
                }
            }

            response["bundle_dir"] = output_dir.string();
            response["event_count"] = events.size();
            return response;
        }

        if (cmd == "import_snapshot") {
            if (!req.contains("file") || !req["file"].is_string()) {
                response["ok"] = false;
                response["error"] = "missing or invalid 'file' field";
                return response;
            }

            std::string file_path = req["file"].get<std::string>();
            std::ifstream in(file_path);
            if (!in) {
                response["ok"] = false;
                response["error"] = "failed to open snapshot file";
                return response;
            }

            try {
                nlohmann::json j;
                in >> j;
                auto manifest = dmfr::snapshot::SnapshotManifest::from_json(j);
                scrubber.scrub(manifest);
                if (!store.insert_snapshot(manifest)) {
                    response["ok"] = false;
                    response["error"] = "failed to insert snapshot into store";
                    return response;
                }
                response["snapshot_id"] = manifest.snapshot_id;
            } catch (const std::exception& e) {
                response["ok"] = false;
                response["error"] = std::string("invalid snapshot file: ") + e.what();
            }
            return response;
        }

        if (cmd == "retention") {
            size_t max_events = req.value("max_events", 100000);
            long long max_age_hours = req.value("max_age_hours", 24 * 30);
            if (max_age_hours < 0) {
                max_age_hours = 24 * 30;
            }

            if (!store.run_retention_policy(max_events, std::chrono::hours(max_age_hours))) {
                response["ok"] = false;
                response["error"] = "failed to run retention policy";
            }
            return response;
        }

        if (cmd == "insert") {
            if (!req.contains("source") || !req["source"].is_string()) {
                response["ok"] = false;
                response["error"] = "missing or invalid 'source' field";
                return response;
            }

            const std::string source = req["source"].get<std::string>();
            nlohmann::json data = req.contains("data") ? req["data"] : req;

            if (collector_manager.on_hook(source, data)) {
                return response;
            }

            // Fallback: build a generic event from the request.
            dmfr::Event event;
            event.event_id = data.value("event_id", "");
            if (event.event_id.empty()) {
                event = dmfr::Event::create(dmfr::Source::Unknown, "hook", "", nlohmann::json::object());
            } else {
                event.timestamp = std::chrono::system_clock::now();
                auto src = parse_source(source);
                event.source = src.value_or(dmfr::Source::Unknown);
                event.type = data.value("type", std::string("hook"));
                event.project_id = data.value("project_id", std::string(""));
                event.session_id = data.value("session_id", std::string(""));
                event.payload = data.value("payload", data);
            }

            scrubber.scrub(event);
            if (!store.insert(event)) {
                response["ok"] = false;
                response["error"] = "failed to insert event";
            }
            return response;
        }

        response["ok"] = false;
        response["error"] = "unknown command: " + cmd;
        return response;
    });

    if (!server.start()) {
        spdlog::error("Failed to start IPC server");
        store.close();
        remove_pid_file(pid_path);
        return 1;
    }

    if (!collector_manager.start()) {
        spdlog::error("Failed to start collector manager");
        server.stop();
        store.close();
        remove_pid_file(pid_path);
        return 1;
    }

    spdlog::info("DMFR daemon ready");

    using namespace std::chrono;
    std::optional<steady_clock::time_point> next_retention;
    if (config.retention.enabled && config.retention.interval_minutes > 0) {
        next_retention = steady_clock::now() + minutes(config.retention.interval_minutes);
    }

    while (g_running && server.is_running()) {
        if (next_retention) {
            auto now = steady_clock::now();
            if (now >= *next_retention) {
                spdlog::info("Running scheduled retention policy");
                if (!store.run_retention_policy(
                        config.retention.max_events,
                        hours(static_cast<long long>(config.retention.max_age_hours)))) {
                    spdlog::error("Scheduled retention policy failed");
                }
                *next_retention = now + minutes(config.retention.interval_minutes);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("DMFR daemon shutting down");
    collector_manager.stop();
    server.stop();
    store.close();
    remove_pid_file(pid_path);

    return 0;
}
