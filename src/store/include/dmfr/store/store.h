#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "dmfr/common/event.h"
#include "dmfr/snapshot/snapshot.h"

namespace dmfr::store {

enum class Order { Ascending, Descending };

struct SnapshotQuery {
    std::optional<std::chrono::system_clock::time_point> from;
    std::optional<std::chrono::system_clock::time_point> to;
    std::optional<std::string> project_id;
    size_t limit{100};
    Order order{Order::Descending};
};

struct Query {
    std::optional<std::chrono::system_clock::time_point> from;
    std::optional<std::chrono::system_clock::time_point> to;
    std::optional<Source> source;
    std::optional<std::string> project_id;
    std::optional<std::string> type;
    size_t limit{100};
    size_t offset{0};
    // Descending keyset cursor and indexed build filters.
    std::optional<std::chrono::system_clock::time_point> before_time;
    std::string before_id;
    std::optional<std::string> command_identity;
    std::optional<std::string> args_json;
    std::optional<int> exit_code;
    Order order{Order::Descending};
};

class Store {
public:
    explicit Store(std::filesystem::path db_path);
    ~Store();

    bool open();
    void close();
    bool is_open() const;

    bool insert(const Event& event);
    std::vector<Event> query(const Query& query) const;

    std::optional<Event> get_event_by_id(const std::string& event_id) const;

    // Return surrounding events within +/- window_minutes of the given event,
    // optionally filtered to the same project_id/session_id.
    std::vector<Event> get_event_context(const Event& event,
                                         int window_minutes,
                                         size_t limit,
                                         bool same_project,
                                         bool same_session) const;

    bool export_parquet(const std::filesystem::path& output_dir,
                        const Query& query) const;

    // Snapshot storage.
    bool insert_snapshot(const dmfr::snapshot::SnapshotManifest& manifest);
    std::optional<dmfr::snapshot::SnapshotManifest> get_snapshot_by_id(const std::string& snapshot_id) const;
    std::vector<dmfr::snapshot::SnapshotManifest> query_snapshots(const SnapshotQuery& query) const;

    bool run_retention_policy(size_t max_events, std::chrono::hours max_age);

private:
    std::filesystem::path db_path_;
    void* db_{nullptr};

    Event row_to_event(void* stmt) const;
};

} // namespace dmfr::store
