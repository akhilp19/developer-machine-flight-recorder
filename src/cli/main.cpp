#include "dmfr/common/config.h"
#include "dmfr/build/build.h"
#include "dmfr/common/errors.h"
#include "dmfr/common/paths.h"
#include "dmfr/collectors/build_collector.h"
#include "dmfr/collectors/git_collector.h"
#include "dmfr/ipc/client.h"
#include "dmfr/scrubber/scrubber.h"
#include "dmfr/snapshot/snapshot.h"
#include "dmfr/store/store.h"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

dmfr::config::Config g_config;

fs::path config_file_path(const std::string& override) {
    if (!override.empty()) {
        return fs::path(override);
    }
    return dmfr::paths::config_dir() / "config.json";
}

void load_and_apply_config(const std::string& config_path) {
    g_config = dmfr::config::load(config_file_path(config_path));
    dmfr::paths::set_data_dir_override(g_config.effective_data_dir());
}

fs::path find_daemon_binary(const std::string& cli_path) {
    // If the CLI path contains a directory separator, look for dmfrd next to it.
    fs::path cli(cli_path);
    if (cli.has_parent_path() && !cli.parent_path().empty()) {
        fs::path sibling = cli.parent_path() / "dmfrd";
        std::error_code ec;
        if (fs::exists(sibling, ec) && !ec) {
            return sibling;
        }
    }

    // Otherwise search PATH.
    const char* path_env = std::getenv("PATH");
    if (path_env) {
        std::string path_str(path_env);
        size_t start = 0;
        while (start < path_str.size()) {
            size_t end = path_str.find(':', start);
            if (end == std::string::npos) {
                end = path_str.size();
            }
            fs::path candidate = fs::path(path_str.substr(start, end - start)) / "dmfrd";
            std::error_code ec;
            if (fs::exists(candidate, ec) && !ec) {
                return candidate;
            }
            start = end + 1;
        }
    }

    return "dmfrd";
}

bool is_daemon_running() {
    dmfr::ipc::Client client(dmfr::paths::socket_path().string());
    return client.connect();
}

nlohmann::json send_command(const nlohmann::json& request) {
    dmfr::ipc::Client client(dmfr::paths::socket_path().string());
    if (!client.connect()) {
        throw dmfr::DmfrError("daemon is not running");
    }
    auto response = client.send(request);
    if (!response) {
        throw dmfr::DmfrError("no response from daemon");
    }
    return *response;
}

bool spawn_daemon(const std::string& cli_path) {
    fs::path daemon_path = find_daemon_binary(cli_path);
    std::string daemon_str = daemon_path.string();

#if defined(__unix__) || defined(__APPLE__)
    pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        // Child process: start a new session and exec the daemon.
        ::setsid();
        ::execl(daemon_str.c_str(), daemon_str.c_str(), nullptr);
        // If exec fails, exit cleanly.
        _exit(1);
    }
    // Parent: wait for the daemon to create its IPC socket.
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (is_daemon_running()) {
            return true;
        }
    }
    return false;
#else
    // Fallback for non-POSIX platforms.
    std::string command = "\"" + daemon_str + "\" &";
    int rc = std::system(command.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return rc == 0 && is_daemon_running();
#endif
}

bool install_git_hooks(const fs::path& repo_root) {
    return dmfr::collectors::GitCollector::install_hooks(repo_root);
}

std::string make_wrapper_script(const std::string& tool) {
    // The wrapper resolves through PATH with its own directory removed.
    // C++ handles argv/JSON and captures the caller environment before execution.
    const std::string script = R"sh(#!/bin/sh
CMD="@TOOL@"
BIN_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CLEAN_PATH=""
REST="$PATH"
while :; do
    PART=${REST%%:*}
    if [ "$PART" != "$BIN_DIR" ]; then
        CLEAN_PATH="${CLEAN_PATH}${CLEAN_PATH+:}$PART"
    fi
    case "$REST" in *:*) REST=${REST#*:} ;; *) break ;; esac
done
# Remove the initial delimiter introduced by the initialized accumulator.
CLEAN_PATH=${CLEAN_PATH#:}
REAL_BIN=$(PATH="$CLEAN_PATH" command -v "$CMD")
if [ -z "$REAL_BIN" ]; then
    echo "dmfr wrapper: real $CMD not found" >&2
    exit 127
fi
# Version probes are telemetry, not builds.
if [ "$#" -eq 1 ]; then
    case "$1" in --version|-V) PATH="$CLEAN_PATH" exec "$REAL_BIN" "$@" ;; esac
fi
DMFR=$(PATH="$CLEAN_PATH" command -v dmfr)
if [ -z "$DMFR" ]; then
    exec "$REAL_BIN" "$@"
fi
PATH="$CLEAN_PATH" exec "$DMFR" record --tool "$CMD" -- "$REAL_BIN" "$@"
)sh";
    auto result = script;
    result.replace(result.find("@TOOL@"), 6, tool);
    return result;
}

bool install_build_wrappers() {
    fs::path bin_dir = dmfr::paths::bin_dir();
    std::error_code ec;
    fs::create_directories(bin_dir, ec);
    if (ec) {
        return false;
    }

    const std::vector<std::string> tools = {
        "npm",
        "cargo",
        "make",
        "pytest",
        "jest",
    };

    for (const auto& tool : tools) {
        fs::path wrapper = bin_dir / tool;
        std::string script = make_wrapper_script(tool);
        std::ofstream out(wrapper, std::ios::out | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << script;
        out.close();

        fs::permissions(wrapper,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add,
                        ec);
        if (ec) {
            return false;
        }
    }

    return true;
}

std::string format_timestamp(long long seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm = {};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return ss.str();
}

std::string format_date(long long seconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm = {};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

std::string short_id(const std::string& id) {
    return id.size() > 8 ? id.substr(0, 8) : id;
}

std::string event_summary(const nlohmann::json& event) {
    const auto& payload = event.value("payload", nlohmann::json::object());
    if (payload.contains("command") && payload["command"].is_string()) {
        return payload["command"].get<std::string>();
    }
    if (payload.contains("type") && payload["type"].is_string()) {
        return payload["type"].get<std::string>();
    }
    return event.value("type", std::string("unknown"));
}

void print_event_table(const nlohmann::json& events) {
    fmt::print("{:20} {:8} {:10} {:16} {:16} {:10} {}\n",
               "Timestamp", "ID", "Source", "Type", "Project", "Sensitivity", "Summary");
    fmt::print("{:-<100}\n", "");
    for (const auto& event : events) {
        long long ts = event.value("timestamp", 0LL);
        fmt::print("{:20} {:8} {:10} {:16} {:16} {:10} {}\n",
                   format_timestamp(ts),
                   short_id(event.value("event_id", std::string{})),
                   event.value("source", std::string{}),
                   event.value("type", std::string{}),
                   event.value("project_id", std::string{}),
                   event.value("sensitivity", std::string{}),
                   event_summary(event));
    }
}

void print_timeline(const nlohmann::json& events, const std::string& group_by) {
    std::string last_group;
    for (const auto& event : events) {
        long long ts = event.value("timestamp", 0LL);
        std::string group;
        if (group_by == "session") {
            group = event.value("session_id", std::string{"<no session>"});
        } else if (group_by == "project") {
            group = event.value("project_id", std::string{"<no project>"});
        } else {
            group = format_date(ts);
        }

        if (group != last_group) {
            fmt::print("\n=== {} ===\n", group);
            last_group = group;
        }

        fmt::print("  {:16} {:8} {:10} {:16} {}\n",
                   format_timestamp(ts),
                   short_id(event.value("event_id", std::string{})),
                   event.value("source", std::string{}),
                   event.value("type", std::string{}),
                   event_summary(event));
    }
    fmt::print("\n");
}

void print_event_detail(const nlohmann::json& event, const nlohmann::json& context) {
    fmt::print("Event detail\n");
    fmt::print("{:-<60}\n", "");
    fmt::print("{}\n", event.dump(2));

    if (!context.empty()) {
        fmt::print("\nContext ({} events)\n", context.size());
        print_event_table(context);
    }
}

void retain_if_due(dmfr::store::Store& store) {
    const auto& retention = g_config.retention;
    if (!retention.enabled) return;
    const auto marker = dmfr::paths::data_dir() / "retention.last_run";
    std::error_code ec;
    const auto last = fs::last_write_time(marker, ec);
    if (!ec && fs::file_time_type::clock::now() - last <
        std::chrono::minutes(retention.interval_minutes)) return;
    if (store.run_retention_policy(retention.max_events, std::chrono::hours(retention.max_age_hours))) {
        std::ofstream stamp(marker, std::ios::trunc);
        stamp << "retention completed\n";
    }
}

bool insert_event_directly(dmfr::Event event) {
    dmfr::scrubber::Scrubber scrubber;
    scrubber.apply_config(g_config.scrubber);
    scrubber.scrub(event);

    dmfr::store::Store store(dmfr::paths::database_path());
    if (!store.open()) {
        return false;
    }
    bool ok = store.insert(event);
    store.close();
    return ok;
}

bool send_hook_event(const std::string& source, const nlohmann::json& data) {
    dmfr::ipc::Client client(dmfr::paths::socket_path().string());
    if (client.connect()) {
        nlohmann::json request = {{"cmd", "insert"}, {"source", source}, {"data", data}};
        auto response = client.send(request);
        if (response && response->value("ok", false)) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Developer Machine Flight Recorder"};
    app.require_subcommand(1);

    std::string config_path;
    app.add_option("-c,--config", config_path, "Configuration file path");

    // start
    auto* start_cmd = app.add_subcommand("start", "Start the DMFR daemon");

    // stop
    auto* stop_cmd = app.add_subcommand("stop", "Stop the DMFR daemon");

    // status
    auto* status_cmd = app.add_subcommand("status", "Show daemon status");

    // query
    std::string query_string;
    long long from_ts = 0;
    long long to_ts = 0;
    std::string source_filter;
    std::string type_filter;
    std::string query_project;
    size_t query_limit = 100;
    auto* query_cmd = app.add_subcommand("query", "Query the local store");
    query_cmd->add_option("query", query_string, "Query string (currently ignored)");
    query_cmd->add_option("--from", from_ts, "Start Unix timestamp");
    query_cmd->add_option("--to", to_ts, "End Unix timestamp");
    query_cmd->add_option("--source", source_filter, "Filter by source");
    query_cmd->add_option("--type", type_filter, "Filter by event type");
    query_cmd->add_option("--project", query_project, "Filter by project ID");
    query_cmd->add_option("--limit", query_limit, "Maximum number of events");

    // timeline
    long long timeline_from = 0;
    long long timeline_to = 0;
    std::string timeline_source;
    std::string timeline_type;
    std::string timeline_project;
    size_t timeline_limit = 100;
    std::string timeline_group_by = "time";
    auto* timeline_cmd = app.add_subcommand("timeline", "Show events in chronological timeline");
    timeline_cmd->add_option("--from", timeline_from, "Start Unix timestamp");
    timeline_cmd->add_option("--to", timeline_to, "End Unix timestamp");
    timeline_cmd->add_option("--source", timeline_source, "Filter by source");
    timeline_cmd->add_option("--type", timeline_type, "Filter by event type");
    timeline_cmd->add_option("--project", timeline_project, "Filter by project ID");
    timeline_cmd->add_option("--limit", timeline_limit, "Maximum number of events");
    timeline_cmd->add_option("--group-by", timeline_group_by, "Group by: time, session, or project")
        ->default_str("time");

    // events
    long long events_from = 0;
    long long events_to = 0;
    std::string events_source;
    std::string events_type;
    std::string events_project;
    size_t events_limit = 100;
    size_t events_offset = 0;
    auto* events_cmd = app.add_subcommand("events", "List recent events with pagination");
    events_cmd->add_option("--from", events_from, "Start Unix timestamp");
    events_cmd->add_option("--to", events_to, "End Unix timestamp");
    events_cmd->add_option("--source", events_source, "Filter by source");
    events_cmd->add_option("--type", events_type, "Filter by event type");
    events_cmd->add_option("--project", events_project, "Filter by project ID");
    events_cmd->add_option("--limit", events_limit, "Maximum number of events");
    events_cmd->add_option("--offset", events_offset, "Number of events to skip");

    // show
    std::string show_event_id;
    int show_window_minutes = 10;
    size_t show_context_limit = 50;
    bool show_same_project = false;
    bool show_same_session = false;
    auto* show_cmd = app.add_subcommand("show", "Show event detail and surrounding context");
    show_cmd->add_option("event_id", show_event_id, "Event ID to display")->required();
    show_cmd->add_option("--window", show_window_minutes, "Context window in minutes");
    show_cmd->add_option("--context-limit", show_context_limit, "Maximum context events");
    show_cmd->add_flag("--same-project", show_same_project, "Only show events from the same project");
    show_cmd->add_flag("--same-session", show_same_session, "Only show events from the same session");

    std::vector<std::string> record_args;
    std::string record_tool;
    auto* record_cmd = app.add_subcommand("record", "Run a build with a pre-build environment snapshot");
    record_cmd->positionals_at_end();
    record_cmd->add_option("--tool", record_tool, "Logical tool name (used by wrappers)");
    record_cmd->add_option("argv", record_args, "Command and arguments after --")->required();

    std::string diagnose_event, diagnose_project;
    auto* diagnose_cmd = app.add_subcommand("diagnose", "Compare a failed build with its previous matching success");
    diagnose_cmd->add_option("--event", diagnose_event, "Failed build event ID (default latest failure)");
    diagnose_cmd->add_option("--project", diagnose_project, "Working directory (default current directory)");

    // snapshot
    std::string snapshot_project_path;
    bool snapshot_list = false;
    size_t snapshot_limit = 100;
    auto* snapshot_cmd = app.add_subcommand("snapshot", "Capture or list environment snapshots");
    snapshot_cmd->add_option("--project", snapshot_project_path, "Project path to snapshot (defaults to current directory)");
    snapshot_cmd->add_flag("--list", snapshot_list, "List recent snapshots instead of capturing");
    snapshot_cmd->add_option("--limit", snapshot_limit, "Maximum number of snapshots to list");

    // diff
    std::string diff_snapshot_id;
    std::string diff_from_id;
    std::string diff_to_id;
    std::string diff_file;
    long long diff_from_ts = 0;
    long long diff_to_ts = 0;
    std::string diff_project;
    std::string diff_project_path;
    auto* diff_cmd = app.add_subcommand("diff", "Compare environment snapshots");
    diff_cmd->add_option("--snapshot", diff_snapshot_id, "Compare current project against this stored snapshot id");
    diff_cmd->add_option("--from-id", diff_from_id, "First stored snapshot id");
    diff_cmd->add_option("--to-id", diff_to_id, "Second stored snapshot id");
    diff_cmd->add_option("--file", diff_file, "Compare current project against this snapshot file");
    diff_cmd->add_option("--from", diff_from_ts, "Start Unix timestamp to find first snapshot");
    diff_cmd->add_option("--to", diff_to_ts, "End Unix timestamp to find second snapshot");
    diff_cmd->add_option("--project", diff_project, "Project filter when using --from/--to");
    diff_cmd->add_option("--project-path", diff_project_path, "Project path for current capture (defaults to cwd)");

    // bundle
    std::string bundle_output_dir;
    long long bundle_from = 0;
    long long bundle_to = 0;
    std::string bundle_project;
    std::string bundle_project_path;
    bool bundle_scrub = true;
    size_t bundle_limit = 1000;
    auto* bundle_cmd = app.add_subcommand("bundle", "Export a diagnostic bundle (manifest + events)");
    bundle_cmd->add_option("output_dir", bundle_output_dir, "Directory to write the bundle to")->required();
    bundle_cmd->add_option("--from", bundle_from, "Start Unix timestamp");
    bundle_cmd->add_option("--to", bundle_to, "End Unix timestamp");
    bundle_cmd->add_option("--project", bundle_project, "Filter events by project ID");
    bundle_cmd->add_option("--project-path", bundle_project_path, "Project path to snapshot (defaults to cwd)");
    bundle_cmd->add_flag("--scrub", bundle_scrub, "Scrub secrets and paths before writing (default true)");
    bundle_cmd->add_option("--limit", bundle_limit, "Maximum number of events to include");

    // import-snapshot
    std::string import_snapshot_file;
    auto* import_snapshot_cmd = app.add_subcommand("import-snapshot", "Import a snapshot manifest into the local store");
    import_snapshot_cmd->add_option("file", import_snapshot_file, "Path to snapshot JSON file")->required();

    // export
    std::string export_path;
    long long export_from = 0;
    long long export_to = 0;
    std::string export_source;
    std::string export_type;
    std::string export_project;
    std::string export_format = "json";
    size_t export_limit = 1000;
    auto* export_cmd = app.add_subcommand("export", "Export events to file");
    export_cmd->add_option("path", export_path, "Output path")->required();
    export_cmd->add_option("--from", export_from, "Start Unix timestamp");
    export_cmd->add_option("--to", export_to, "End Unix timestamp");
    export_cmd->add_option("--source", export_source, "Filter by source");
    export_cmd->add_option("--type", export_type, "Filter by event type");
    export_cmd->add_option("--project", export_project, "Filter by project ID");
    export_cmd->add_option("--limit", export_limit, "Maximum number of events");
    export_cmd->add_option("--format", export_format, "Export format: json or parquet")->default_str("json");

    // retention
    size_t retention_max_events = 100000;
    size_t retention_max_age_days = 30;
    auto* retention_cmd = app.add_subcommand("retention", "Run retention policy on the local store");
    retention_cmd->add_option("--max-events", retention_max_events, "Maximum number of events to keep");
    retention_cmd->add_option("--max-age-days", retention_max_age_days, "Maximum age of events in days");

    // config
    auto* config_cmd = app.add_subcommand("config", "Manage DMFR configuration");
    auto* config_init_cmd = config_cmd->add_subcommand("init", "Write a default configuration file");
    auto* config_show_cmd = config_cmd->add_subcommand("show", "Print the current configuration");

    // init
    std::string init_path;
    auto* init_cmd = app.add_subcommand("init", "Initialize DMFR in a repository");
    init_cmd->add_option("path", init_path, "Repository path (defaults to current directory)");

    // hook
    std::string hook_source;
    std::string hook_repo;
    std::string hook_type;
    std::string hook_sha;
    std::string hook_branch;
    std::string hook_message;
    std::string hook_json;
    std::string hook_file;
    auto* hook_cmd = app.add_subcommand("hook", "Send a hook event to the daemon");
    hook_cmd->add_option("source", hook_source, "Hook source (git, build)")->required();
    hook_cmd->add_option("--repo", hook_repo, "Repository path");
    hook_cmd->add_option("--type", hook_type, "Hook type");
    hook_cmd->add_option("--sha", hook_sha, "Commit SHA");
    hook_cmd->add_option("--branch", hook_branch, "Branch name");
    hook_cmd->add_option("--message", hook_message, "Commit message");
    hook_cmd->add_option("--json", hook_json, "Raw JSON payload (inline)");
    hook_cmd->add_option("--file", hook_file, "Path to JSON payload file");

    // install-wrappers
    auto* install_wrappers_cmd = app.add_subcommand("install-wrappers", "Install build-tool wrapper scripts");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (record_cmd->parsed()) {
        std::unique_ptr<dmfr::store::Store> store;
        dmfr::scrubber::Scrubber scrubber;
        try {
            load_and_apply_config(config_path);
            store = std::make_unique<dmfr::store::Store>(dmfr::paths::database_path());
            if (!store->open()) throw std::runtime_error("store unavailable");
            scrubber.apply_config(g_config.scrubber);
        } catch (const std::exception&) {
            fmt::print(stderr, "dmfr: recorder initialization failed; running without telemetry\n");
            return dmfr::build::unrecorded(record_args);
        }
        const int status = dmfr::build::record(record_args, record_tool, *store, scrubber);
        // Housekeeping must never change the executed command's exit status.
        try { retain_if_due(*store); } catch (const std::exception&) {}
        return status;
    }

    load_and_apply_config(config_path);

    if (diagnose_cmd->parsed()) {
        try {
            fs::create_directories(dmfr::paths::data_dir());
            dmfr::store::Store store(dmfr::paths::database_path());
            if (!store.open()) throw std::runtime_error("could not open local store");
            dmfr::scrubber::Scrubber scrubber;
            scrubber.apply_config(g_config.scrubber);
            auto project = fs::weakly_canonical(diagnose_project.empty() ? fs::current_path() : fs::path(diagnose_project));
            auto report = dmfr::build::diagnose(store, scrubber, project.string(), diagnose_event);
            fmt::print("{}\n", report.dump(2));
            return report.value("status", "") == "compared" ? 0 : 1;
        } catch (const std::exception& error) {
            fmt::print(stderr, "dmfr: {}\n", error.what());
            return 1;
        }
    }

    if (start_cmd->parsed()) {
        if (is_daemon_running()) {
            fmt::print("DMFR daemon is already running.\n");
            return 0;
        }
        std::string cli_path = (argc > 0 && argv[0]) ? argv[0] : "dmfr";
        if (spawn_daemon(cli_path)) {
            fmt::print("DMFR daemon started.\n");
            return 0;
        }
        fmt::print(stderr, "Failed to start DMFR daemon.\n");
        return 1;
    }

    if (stop_cmd->parsed()) {
        if (!is_daemon_running()) {
            fmt::print("DMFR daemon is not running.\n");
            return 0;
        }
        try {
            send_command(nlohmann::json{{"cmd", "shutdown"}});
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Failed to send shutdown: {}\n", e.what());
            return 1;
        }

        // Wait briefly for the daemon to shut down.
        for (int i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!is_daemon_running()) {
                fmt::print("DMFR daemon stopped.\n");
                return 0;
            }
        }
        fmt::print(stderr, "Daemon did not stop in time.\n");
        return 1;
    }

    if (status_cmd->parsed()) {
        try {
            auto response = send_command(nlohmann::json{{"cmd", "status"}});
            fmt::print("{}\n", response.dump(2));
        } catch (const dmfr::DmfrError&) {
            fmt::print("DMFR daemon is not running.\n");
            return 1;
        }
        return 0;
    }

    if (query_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "query"}};
        if (query_cmd->count("--from") > 0) {
            request["from"] = from_ts;
        }
        if (query_cmd->count("--to") > 0) {
            request["to"] = to_ts;
        }
        if (!source_filter.empty()) {
            request["source"] = source_filter;
        }
        if (!type_filter.empty()) {
            request["type"] = type_filter;
        }
        if (!query_project.empty()) {
            request["project_id"] = query_project;
        }
        request["limit"] = query_limit;

        try {
            auto response = send_command(request);
            if (response.contains("events")) {
                fmt::print("{}\n", response["events"].dump(2));
            } else {
                fmt::print("{}\n", response.dump(2));
            }
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Query failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (timeline_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "timeline"}, {"group_by", timeline_group_by}};
        if (timeline_cmd->count("--from") > 0) {
            request["from"] = timeline_from;
        }
        if (timeline_cmd->count("--to") > 0) {
            request["to"] = timeline_to;
        }
        if (!timeline_source.empty()) {
            request["source"] = timeline_source;
        }
        if (!timeline_type.empty()) {
            request["type"] = timeline_type;
        }
        if (!timeline_project.empty()) {
            request["project_id"] = timeline_project;
        }
        request["limit"] = timeline_limit;

        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Timeline failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            if (response.contains("events")) {
                print_timeline(response["events"], timeline_group_by);
            }
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Timeline failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (events_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "events"}};
        if (events_cmd->count("--from") > 0) {
            request["from"] = events_from;
        }
        if (events_cmd->count("--to") > 0) {
            request["to"] = events_to;
        }
        if (!events_source.empty()) {
            request["source"] = events_source;
        }
        if (!events_type.empty()) {
            request["type"] = events_type;
        }
        if (!events_project.empty()) {
            request["project_id"] = events_project;
        }
        request["limit"] = events_limit;
        request["offset"] = events_offset;

        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Events failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            if (response.contains("events")) {
                print_event_table(response["events"]);
            }
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Events failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (show_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "show"}, {"event_id", show_event_id}};
        request["window_minutes"] = show_window_minutes;
        request["context_limit"] = show_context_limit;
        request["same_project"] = show_same_project;
        request["same_session"] = show_same_session;

        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Show failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            print_event_detail(response.value("event", nlohmann::json::object()),
                               response.value("context", nlohmann::json::array()));
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Show failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (snapshot_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "snapshot"}};
        if (snapshot_list) {
            request["list"] = true;
            request["limit"] = snapshot_limit;
            try {
                auto response = send_command(request);
                if (!response.value("ok", false)) {
                    fmt::print(stderr, "Snapshot list failed: {}\n",
                               response.value("error", "unknown error"));
                    return 1;
                }
                fmt::print("{}\n", response.value("snapshots", nlohmann::json::array()).dump(2));
            } catch (const dmfr::DmfrError& e) {
                fmt::print(stderr, "Snapshot list failed: {}\n", e.what());
                return 1;
            }
            return 0;
        }

        if (!snapshot_project_path.empty()) {
            request["project_path"] = std::filesystem::absolute(snapshot_project_path).string();
        }

        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Snapshot failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            fmt::print("Captured snapshot {}\n", response.value("snapshot_id", std::string{}));
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Snapshot failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (diff_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "diff"}};
        if (!diff_project_path.empty()) {
            request["project_path"] = std::filesystem::absolute(diff_project_path).string();
        }
        if (!diff_project.empty()) {
            request["project_id"] = diff_project;
        }

        if (diff_cmd->count("--snapshot") > 0) {
            request["snapshot_id"] = diff_snapshot_id;
        } else if (diff_cmd->count("--from-id") > 0 || diff_cmd->count("--to-id") > 0) {
            request["from_id"] = diff_from_id;
            request["to_id"] = diff_to_id;
        } else if (diff_cmd->count("--file") > 0) {
            request["file"] = diff_file;
        } else if (diff_cmd->count("--from") > 0 || diff_cmd->count("--to") > 0) {
            request["from"] = diff_from_ts;
            request["to"] = diff_to_ts;
        } else {
            fmt::print(stderr, "Diff requires --snapshot, --from-id/--to-id, --file, or --from/--to\n");
            return 1;
        }

        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Diff failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            fmt::print("{}\n", response.value("report", nlohmann::json::object()).dump(2));
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Diff failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (bundle_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "bundle"}, {"output_dir", bundle_output_dir}};
        if (!bundle_project_path.empty()) {
            request["project_path"] = std::filesystem::absolute(bundle_project_path).string();
        }
        if (!bundle_project.empty()) {
            request["project_id"] = bundle_project;
        }
        if (bundle_cmd->count("--from") > 0) {
            request["from"] = bundle_from;
        }
        if (bundle_cmd->count("--to") > 0) {
            request["to"] = bundle_to;
        }
        request["scrub"] = bundle_scrub;
        request["limit"] = bundle_limit;

        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Bundle failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            fmt::print("Bundle written to {} ({} events)\n",
                       response.value("bundle_dir", std::string{}),
                       response.value("event_count", 0));
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Bundle failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (import_snapshot_cmd->parsed()) {
        nlohmann::json request = {{"cmd", "import_snapshot"}, {"file", import_snapshot_file}};
        try {
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Import failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            fmt::print("Imported snapshot {}\n", response.value("snapshot_id", std::string{}));
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Import failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (export_cmd->parsed()) {
        try {
            if (export_format == "parquet") {
                nlohmann::json request = {{"cmd", "export_parquet"}, {"output_dir", export_path}};
                if (export_cmd->count("--from") > 0) {
                    request["from"] = export_from;
                }
                if (export_cmd->count("--to") > 0) {
                    request["to"] = export_to;
                }
                if (!export_source.empty()) {
                    request["source"] = export_source;
                }
                if (!export_type.empty()) {
                    request["type"] = export_type;
                }
                if (!export_project.empty()) {
                    request["project_id"] = export_project;
                }
                request["limit"] = export_limit;

                auto response = send_command(request);
                if (!response.value("ok", false)) {
                    fmt::print(stderr, "Parquet export failed: {}\n",
                               response.value("error", "unknown error"));
                    return 1;
                }
                fmt::print("Exported events to {}/events.parquet\n", export_path);
            } else {
                nlohmann::json request = {{"cmd", "query"}};
                if (export_cmd->count("--from") > 0) {
                    request["from"] = export_from;
                }
                if (export_cmd->count("--to") > 0) {
                    request["to"] = export_to;
                }
                if (!export_source.empty()) {
                    request["source"] = export_source;
                }
                if (!export_type.empty()) {
                    request["type"] = export_type;
                }
                if (!export_project.empty()) {
                    request["project_id"] = export_project;
                }
                request["limit"] = export_limit;

                auto response = send_command(request);
                std::ofstream out(export_path, std::ios::out | std::ios::trunc);
                if (!out) {
                    fmt::print(stderr, "Failed to open export file: {}\n", export_path);
                    return 1;
                }
                if (response.contains("events")) {
                    out << response["events"].dump(2);
                } else {
                    out << response.dump(2);
                }
                fmt::print("Exported events to {}\n", export_path);
            }
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Export failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (retention_cmd->parsed()) {
        try {
            nlohmann::json request = {
                {"cmd", "retention"},
                {"max_events", retention_max_events},
                {"max_age_hours", static_cast<long long>(retention_max_age_days * 24)}
            };
            auto response = send_command(request);
            if (!response.value("ok", false)) {
                fmt::print(stderr, "Retention failed: {}\n",
                           response.value("error", "unknown error"));
                return 1;
            }
            fmt::print("Retention policy applied.\n");
        } catch (const dmfr::DmfrError& e) {
            fmt::print(stderr, "Retention failed: {}\n", e.what());
            return 1;
        }
        return 0;
    }

    if (config_cmd->parsed()) {
        fs::path path = config_file_path(config_path);
        if (config_init_cmd->parsed()) {
            if (!dmfr::config::save(path, dmfr::config::Config::defaults())) {
                fmt::print(stderr, "Failed to write config to {}\n", path.string());
                return 1;
            }
            fmt::print("Wrote default config to {}\n", path.string());
            return 0;
        }
        if (config_show_cmd->parsed()) {
            fmt::print("{}\n", dmfr::config::to_json(g_config).dump(2));
            return 0;
        }
        fmt::print(stderr, "Unknown config subcommand.\n");
        return 1;
    }

    if (init_cmd->parsed()) {
        fs::path repo_root = init_path.empty() ? fs::current_path() : fs::path(init_path);
        std::error_code ec;
        fs::create_directories(repo_root / ".dmfr", ec);
        if (ec) {
            fmt::print(stderr, "Failed to create .dmfr directory in {}\n", repo_root.string());
            return 1;
        }

        fs::path git_dir = repo_root / ".git";
        if (!fs::exists(git_dir, ec)) {
            fmt::print(stderr, "{} is not a Git repository\n", repo_root.string());
            return 1;
        }

        if (!install_git_hooks(repo_root)) {
            fmt::print(stderr, "Failed to install Git hooks in {}\n", repo_root.string());
            return 1;
        }

        fmt::print("Initialized DMFR in {}\n", repo_root.string());
        return 0;
    }

    if (hook_cmd->parsed()) {
        nlohmann::json data;

        if (!hook_file.empty()) {
            std::ifstream in(hook_file);
            if (!in) {
                fmt::print(stderr, "Failed to open payload file: {}\n", hook_file);
                return 1;
            }
            try {
                in >> data;
            } catch (const std::exception& e) {
                fmt::print(stderr, "Invalid JSON payload file: {}\n", e.what());
                return 1;
            }
        } else if (!hook_json.empty()) {
            try {
                data = nlohmann::json::parse(hook_json);
            } catch (const std::exception& e) {
                fmt::print(stderr, "Invalid JSON payload: {}\n", e.what());
                return 1;
            }
        } else {
            if (!hook_repo.empty()) {
                data["repo"] = hook_repo;
            }
            if (!hook_type.empty()) {
                data["type"] = hook_type;
            }
            if (!hook_sha.empty()) {
                data["sha"] = hook_sha;
            }
            if (!hook_branch.empty()) {
                data["branch"] = hook_branch;
            }
            if (!hook_message.empty()) {
                data["message"] = hook_message;
            }
        }

        // For git hooks, build the event locally if the daemon is not running.
        if (hook_source == "git") {
            fs::path repo = hook_repo.empty() ? fs::current_path() : fs::path(hook_repo);
            dmfr::collectors::GitCollector collector(repo);
            collector.initialize();
            auto event = collector.on_hook(hook_type.empty() ? "post-commit" : hook_type, data);
            if (!event) {
                fmt::print(stderr, "Git hook produced no event\n");
                return 1;
            }

            if (!send_hook_event("git", data) && !insert_event_directly(*event)) {
                fmt::print(stderr, "Failed to send git hook event\n");
                return 1;
            }
            return 0;
        }

        if (hook_source == "build") {
            auto event = dmfr::collectors::BuildCollector::make_build_event(data);
            if (!send_hook_event("build", data) && !insert_event_directly(std::move(event))) {
                fmt::print(stderr, "Failed to send build hook event\n");
                return 1;
            }
            return 0;
        }

        fmt::print(stderr, "Unknown hook source: {}\n", hook_source);
        return 1;
    }

    if (install_wrappers_cmd->parsed()) {
        if (!install_build_wrappers()) {
            fmt::print(stderr, "Failed to install build wrappers\n");
            return 1;
        }
        fmt::print("Installed build tool wrappers in {}\n", dmfr::paths::bin_dir().string());
        fmt::print("Add the following directory to the front of your PATH:\n");
        fmt::print("  export PATH=\"{}:$PATH\"\n", dmfr::paths::bin_dir().string());
        return 0;
    }

    return 0;
}
