#include "dmfr/store/store.h"

#include "dmfr/common/errors.h"

#include <fmt/format.h>
#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

#ifdef DMFR_HAS_ARROW
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>
#endif

namespace dmfr::store {

Store::Store(std::filesystem::path db_path)
    : db_path_(std::move(db_path)), db_(nullptr) {}

Store::~Store() {
    close();
}

bool Store::open() {
    std::error_code ec;
    std::filesystem::create_directories(db_path_.parent_path(), ec);
    if (ec) {
        return false;
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path_.string().c_str(), &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 500);

    const char* init_sql = R"sql(
        PRAGMA journal_mode = WAL;

        CREATE TABLE IF NOT EXISTS events (
            event_id TEXT PRIMARY KEY,
            timestamp REAL NOT NULL,
            source TEXT NOT NULL,
            type TEXT NOT NULL,
            project_id TEXT,
            session_id TEXT,
            payload BLOB NOT NULL,
            sensitivity TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_events_time ON events(timestamp);
        CREATE INDEX IF NOT EXISTS idx_events_source_time ON events(source, timestamp);
        CREATE INDEX IF NOT EXISTS idx_events_project_time ON events(project_id, timestamp);
        CREATE INDEX IF NOT EXISTS idx_events_session_time ON events(session_id, timestamp);

        CREATE INDEX IF NOT EXISTS idx_build_identity ON events(
            project_id, source, type,
            coalesce(json_extract(CAST(payload AS TEXT), '$.command_identity'),
                     json_extract(CAST(payload AS TEXT), '$.command')),
            timestamp DESC, event_id DESC
        );
        CREATE INDEX IF NOT EXISTS idx_event_project_cursor ON events(project_id, source, type, timestamp DESC, event_id DESC);

        CREATE TABLE IF NOT EXISTS correlation (
            event_id TEXT REFERENCES events(event_id),
            key TEXT NOT NULL,
            value TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_corr_event ON correlation(event_id);
        CREATE INDEX IF NOT EXISTS idx_corr_key_value ON correlation(key, value);

        CREATE TABLE IF NOT EXISTS snapshots (
            snapshot_id TEXT PRIMARY KEY,
            timestamp REAL NOT NULL,
            project_id TEXT,
            manifest BLOB NOT NULL
        );
    )sql";

    char* err_msg = nullptr;
    rc = sqlite3_exec(db, init_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }

    db_ = db;
    return true;
}

void Store::close() {
    if (db_) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

bool Store::is_open() const {
    return db_ != nullptr;
}

namespace {

double time_to_unix(const std::chrono::system_clock::time_point& tp) {
    auto duration = tp.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return static_cast<double>(micros.count()) / 1'000'000.0;
}

std::chrono::system_clock::time_point unix_to_time(double seconds) {
    auto micros = std::chrono::microseconds(static_cast<int64_t>(std::llround(seconds * 1'000'000)));
    return std::chrono::system_clock::time_point(micros);
}

} // namespace

bool Store::insert(const Event& event) {
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return false;
    }

    const char* sql = R"sql(
        INSERT INTO events (event_id, timestamp, source, type, project_id, session_id, payload, sensitivity)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )sql";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    std::string payload_str = event.payload.dump();

    sqlite3_bind_text(stmt, 1, event.event_id.c_str(), static_cast<int>(event.event_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, time_to_unix(event.timestamp));
    sqlite3_bind_text(stmt, 3, dmfr::to_string(event.source).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, event.type.c_str(), static_cast<int>(event.type.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, event.project_id.c_str(), static_cast<int>(event.project_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, event.session_id.c_str(), static_cast<int>(event.session_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 7, payload_str.data(), static_cast<int>(payload_str.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, dmfr::to_string(event.sensitivity).c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

Event Store::row_to_event(void* stmt_void) const {
    sqlite3_stmt* stmt = static_cast<sqlite3_stmt*>(stmt_void);
    Event event;
    event.event_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    event.timestamp = unix_to_time(sqlite3_column_double(stmt, 1));

    std::string source_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (source_str == "git") event.source = Source::Git;
    else if (source_str == "build") event.source = Source::Build;
    else if (source_str == "system") event.source = Source::System;
    else if (source_str == "environment") event.source = Source::Environment;
    else if (source_str == "plugin") event.source = Source::Plugin;
    else event.source = Source::Unknown;

    event.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    event.project_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    event.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

    const void* payload_blob = sqlite3_column_blob(stmt, 6);
    int payload_len = sqlite3_column_bytes(stmt, 6);
    std::string payload_str(static_cast<const char*>(payload_blob), static_cast<size_t>(payload_len));
    try {
        event.payload = nlohmann::json::parse(payload_str);
    } catch (...) {
        event.payload = nlohmann::json{};
    }

    std::string sens_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    if (sens_str == "public") event.sensitivity = Sensitivity::Public;
    else if (sens_str == "private") event.sensitivity = Sensitivity::Private;
    else if (sens_str == "scrubbed") event.sensitivity = Sensitivity::Scrubbed;

    return event;
}

std::vector<Event> Store::query(const Query& query) const {
    std::vector<Event> results;
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return results;
    }

    std::string sql = "SELECT event_id, timestamp, source, type, project_id, session_id, payload, sensitivity FROM events WHERE 1=1";

    if (query.from) {
        sql += " AND timestamp >= ?";
    }
    if (query.to) {
        sql += " AND timestamp <= ?";
    }
    if (query.source) {
        sql += " AND source = ?";
    }
    if (query.project_id) {
        sql += " AND project_id = ?";
    }
    if (query.type) {
        sql += " AND type = ?";
    }
    if (query.before_time) sql += " AND (timestamp, event_id) < (?, ?)";
    if (query.command_identity) sql += " AND coalesce(json_extract(CAST(payload AS TEXT), '$.command_identity'), json_extract(CAST(payload AS TEXT), '$.command')) = ?";
    if (query.args_json) sql += " AND json_extract(CAST(payload AS TEXT), '$.args') = ?";
    if (query.exit_code) sql += " AND json_extract(CAST(payload AS TEXT), '$.exit_code') = ?";
    sql += query.order == Order::Ascending ? " ORDER BY timestamp ASC, event_id ASC" : " ORDER BY timestamp DESC, event_id DESC";
    sql += " LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return results;
    }

    int idx = 1;
    if (query.from) {
        sqlite3_bind_double(stmt, idx++, time_to_unix(*query.from));
    }
    if (query.to) {
        sqlite3_bind_double(stmt, idx++, time_to_unix(*query.to));
    }
    if (query.source) {
        sqlite3_bind_text(stmt, idx++, dmfr::to_string(*query.source).c_str(), -1, SQLITE_TRANSIENT);
    }
    if (query.project_id) {
        sqlite3_bind_text(stmt, idx++, query.project_id->c_str(), static_cast<int>(query.project_id->size()), SQLITE_TRANSIENT);
    }
    if (query.type) {
        sqlite3_bind_text(stmt, idx++, query.type->c_str(), static_cast<int>(query.type->size()), SQLITE_TRANSIENT);
    }
    if (query.before_time) {
        sqlite3_bind_double(stmt, idx++, time_to_unix(*query.before_time));
        sqlite3_bind_text(stmt, idx++, query.before_id.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (query.command_identity) sqlite3_bind_text(stmt, idx++, query.command_identity->c_str(), -1, SQLITE_TRANSIENT);
    if (query.args_json) sqlite3_bind_text(stmt, idx++, query.args_json->c_str(), -1, SQLITE_TRANSIENT);
    if (query.exit_code) sqlite3_bind_int(stmt, idx++, *query.exit_code);
    sqlite3_bind_int64(stmt, idx++, static_cast<sqlite3_int64>(query.limit));
    sqlite3_bind_int64(stmt, idx++, static_cast<sqlite3_int64>(query.offset));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(row_to_event(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::optional<Event> Store::get_event_by_id(const std::string& event_id) const {
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return std::nullopt;
    }

    const char* sql = "SELECT event_id, timestamp, source, type, project_id, session_id, payload, sensitivity FROM events WHERE event_id = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, event_id.c_str(), static_cast<int>(event_id.size()), SQLITE_TRANSIENT);

    std::optional<Event> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = row_to_event(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Event> Store::get_event_context(const Event& event,
                                          int window_minutes,
                                          size_t limit,
                                          bool same_project,
                                          bool same_session) const {
    std::vector<Event> results;
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db || window_minutes <= 0) {
        return results;
    }

    using namespace std::chrono;
    auto window = minutes(window_minutes);
    double from_ts = time_to_unix(event.timestamp - window);
    double to_ts = time_to_unix(event.timestamp + window);

    std::string sql = "SELECT event_id, timestamp, source, type, project_id, session_id, payload, sensitivity FROM events WHERE timestamp >= ? AND timestamp <= ? AND event_id != ?";
    if (same_project && !event.project_id.empty()) {
        sql += " AND project_id = ?";
    }
    if (same_session && !event.session_id.empty()) {
        sql += " AND session_id = ?";
    }
    sql += " ORDER BY timestamp ASC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    int idx = 1;
    sqlite3_bind_double(stmt, idx++, from_ts);
    sqlite3_bind_double(stmt, idx++, to_ts);
    sqlite3_bind_text(stmt, idx++, event.event_id.c_str(), static_cast<int>(event.event_id.size()), SQLITE_TRANSIENT);
    if (same_project && !event.project_id.empty()) {
        sqlite3_bind_text(stmt, idx++, event.project_id.c_str(), static_cast<int>(event.project_id.size()), SQLITE_TRANSIENT);
    }
    if (same_session && !event.session_id.empty()) {
        sqlite3_bind_text(stmt, idx++, event.session_id.c_str(), static_cast<int>(event.session_id.size()), SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt, idx++, static_cast<sqlite3_int64>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(row_to_event(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Store::insert_snapshot(const dmfr::snapshot::SnapshotManifest& manifest) {
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return false;
    }

    const char* sql = R"sql(
        INSERT INTO snapshots (snapshot_id, timestamp, project_id, manifest)
        VALUES (?, ?, ?, ?)
    )sql";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    std::string manifest_str = manifest.serialize();
    double ts = time_to_unix(manifest.timestamp);

    sqlite3_bind_text(stmt, 1, manifest.snapshot_id.c_str(), static_cast<int>(manifest.snapshot_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, ts);
    sqlite3_bind_text(stmt, 3, manifest.project_id.c_str(), static_cast<int>(manifest.project_id.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 4, manifest_str.data(), static_cast<int>(manifest_str.size()), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<dmfr::snapshot::SnapshotManifest> Store::get_snapshot_by_id(const std::string& snapshot_id) const {
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return std::nullopt;
    }

    const char* sql = "SELECT manifest FROM snapshots WHERE snapshot_id = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, snapshot_id.c_str(), static_cast<int>(snapshot_id.size()), SQLITE_TRANSIENT);

    std::optional<dmfr::snapshot::SnapshotManifest> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int len = sqlite3_column_bytes(stmt, 0);
        std::string data(static_cast<const char*>(blob), static_cast<size_t>(len));
        result = dmfr::snapshot::SnapshotManifest::deserialize(data);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<dmfr::snapshot::SnapshotManifest> Store::query_snapshots(const SnapshotQuery& query) const {
    std::vector<dmfr::snapshot::SnapshotManifest> results;
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return results;
    }

    std::string sql = "SELECT manifest FROM snapshots WHERE 1=1";
    if (query.from) {
        sql += " AND timestamp >= ?";
    }
    if (query.to) {
        sql += " AND timestamp <= ?";
    }
    if (query.project_id) {
        sql += " AND project_id = ?";
    }
    sql += query.order == Order::Ascending ? " ORDER BY timestamp ASC" : " ORDER BY timestamp DESC";
    sql += " LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    int idx = 1;
    if (query.from) {
        sqlite3_bind_double(stmt, idx++, time_to_unix(*query.from));
    }
    if (query.to) {
        sqlite3_bind_double(stmt, idx++, time_to_unix(*query.to));
    }
    if (query.project_id) {
        sqlite3_bind_text(stmt, idx++, query.project_id->c_str(), static_cast<int>(query.project_id->size()), SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt, idx++, static_cast<sqlite3_int64>(query.limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int len = sqlite3_column_bytes(stmt, 0);
        std::string data(static_cast<const char*>(blob), static_cast<size_t>(len));
        if (auto manifest = dmfr::snapshot::SnapshotManifest::deserialize(data)) {
            results.push_back(std::move(*manifest));
        }
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Store::export_parquet(const std::filesystem::path& output_dir,
                           const Query& query) const {
#ifndef DMFR_HAS_ARROW
    (void)output_dir;
    (void)query;
    return false;
#else
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        return false;
    }

    const auto events = this->query(query);
    if (events.empty()) {
        return true;
    }

    std::vector<std::string> event_ids;
    std::vector<int64_t> timestamps;
    std::vector<std::string> sources;
    std::vector<std::string> types;
    std::vector<std::string> project_ids;
    std::vector<std::string> session_ids;
    std::vector<std::string> payloads;
    std::vector<std::string> sensitivities;
    std::vector<std::string> correlation_keys;

    event_ids.reserve(events.size());
    timestamps.reserve(events.size());
    sources.reserve(events.size());
    types.reserve(events.size());
    project_ids.reserve(events.size());
    session_ids.reserve(events.size());
    payloads.reserve(events.size());
    sensitivities.reserve(events.size());
    correlation_keys.reserve(events.size());

    for (const auto& event : events) {
        event_ids.push_back(event.event_id);
        timestamps.push_back(std::chrono::duration_cast<std::chrono::seconds>(
                                 event.timestamp.time_since_epoch())
                                 .count());
        sources.push_back(dmfr::to_string(event.source));
        types.push_back(event.type);
        project_ids.push_back(event.project_id);
        session_ids.push_back(event.session_id);
        payloads.push_back(event.payload.dump());
        sensitivities.push_back(dmfr::to_string(event.sensitivity));

        std::ostringstream ck;
        for (size_t i = 0; i < event.correlation_keys.size(); ++i) {
            if (i > 0) ck << ",";
            ck << event.correlation_keys[i];
        }
        correlation_keys.push_back(ck.str());
    }

    arrow::StringBuilder id_builder, source_builder, type_builder, project_id_builder,
        session_id_builder, payload_builder, sensitivity_builder, correlation_keys_builder;
    arrow::TimestampBuilder ts_builder(arrow::timestamp(arrow::TimeUnit::SECOND),
                                       arrow::default_memory_pool());

    auto append_strings = [](arrow::StringBuilder& builder,
                            const std::vector<std::string>& values) -> bool {
        for (const auto& value : values) {
            if (!builder.Append(value).ok()) {
                return false;
            }
        }
        return true;
    };

    if (!append_strings(id_builder, event_ids) ||
        !append_strings(source_builder, sources) ||
        !append_strings(type_builder, types) ||
        !append_strings(project_id_builder, project_ids) ||
        !append_strings(session_id_builder, session_ids) ||
        !append_strings(payload_builder, payloads) ||
        !append_strings(sensitivity_builder, sensitivities) ||
        !append_strings(correlation_keys_builder, correlation_keys)) {
        return false;
    }

    for (int64_t ts : timestamps) {
        if (!ts_builder.Append(ts).ok()) {
            return false;
        }
    }

    std::shared_ptr<arrow::Array> id_array, source_array, type_array, project_id_array,
        session_id_array, payload_array, sensitivity_array, correlation_keys_array, ts_array;

    if (!id_builder.Finish(&id_array).ok() ||
        !source_builder.Finish(&source_array).ok() ||
        !type_builder.Finish(&type_array).ok() ||
        !project_id_builder.Finish(&project_id_array).ok() ||
        !session_id_builder.Finish(&session_id_array).ok() ||
        !payload_builder.Finish(&payload_array).ok() ||
        !sensitivity_builder.Finish(&sensitivity_array).ok() ||
        !correlation_keys_builder.Finish(&correlation_keys_array).ok() ||
        !ts_builder.Finish(&ts_array).ok()) {
        return false;
    }

    auto schema = arrow::schema({
        arrow::field("event_id", arrow::utf8()),
        arrow::field("timestamp", arrow::timestamp(arrow::TimeUnit::SECOND)),
        arrow::field("source", arrow::utf8()),
        arrow::field("type", arrow::utf8()),
        arrow::field("project_id", arrow::utf8()),
        arrow::field("session_id", arrow::utf8()),
        arrow::field("payload", arrow::utf8()),
        arrow::field("sensitivity", arrow::utf8()),
        arrow::field("correlation_keys", arrow::utf8()),
    });

    auto table = arrow::Table::Make(schema,
                                    {id_array, ts_array, source_array, type_array,
                                     project_id_array, session_id_array, payload_array,
                                     sensitivity_array, correlation_keys_array});
    if (!table) {
        return false;
    }

    auto filename = output_dir / "events.parquet";
    std::shared_ptr<arrow::io::FileOutputStream> outfile;
    if (!arrow::io::FileOutputStream::Open(filename.string()).Value(&outfile).ok()) {
        return false;
    }

    auto status = parquet::arrow::WriteTable(*table,
                                             arrow::default_memory_pool(),
                                             outfile,
                                             static_cast<int>(events.size()));
    return status.ok();
#endif
}

bool Store::run_retention_policy(size_t max_events, std::chrono::hours max_age) {
    sqlite3* db = static_cast<sqlite3*>(db_);
    if (!db) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    auto cutoff = now - max_age;
    double cutoff_unix = time_to_unix(cutoff);

    // Build a subquery that selects events to delete: either too old or beyond the count limit.
    std::string select_old = fmt::format(
        "SELECT event_id FROM events WHERE timestamp < {:.6f}",
        cutoff_unix);

    std::string subquery = select_old;
    if (max_events > 0) {
        subquery += fmt::format(
            " UNION SELECT event_id FROM (SELECT event_id FROM events ORDER BY timestamp DESC LIMIT -1 OFFSET {})",
            max_events);
    }

    std::string delete_corr = "DELETE FROM correlation WHERE event_id IN (" + subquery + ")";
    std::string delete_events = "DELETE FROM events WHERE event_id IN (" + subquery + ")";

    // Keep snapshots referenced by retained events, even when older than cutoff.
    std::string snapshot_candidates = fmt::format("timestamp < {:.6f}", cutoff_unix);
    if (max_events > 0) snapshot_candidates += fmt::format(
        " OR snapshot_id NOT IN (SELECT snapshot_id FROM snapshots ORDER BY timestamp DESC, snapshot_id DESC LIMIT {})", max_events);
    const std::string delete_snapshots =
        "DELETE FROM snapshots WHERE (" + snapshot_candidates + ") AND snapshot_id NOT IN ("
        "SELECT json_extract(CAST(payload AS TEXT), '$.snapshot_id') FROM events "
        "WHERE json_type(CAST(payload AS TEXT), '$.snapshot_id') = 'text')";
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    for (const auto& sql : {delete_corr, delete_events, delete_snapshots}) {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    return true;
}

} // namespace dmfr::store
