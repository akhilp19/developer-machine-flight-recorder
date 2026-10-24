# Developer Machine Flight Recorder — System Design

**Version:** 0.1  
**Status:** Phase 0 Draft  
**Date:** 2026-08-18  
**Scope:** Core agent, CLI, local store, and IPC for macOS, Linux, and Windows.

---

## 1. Goals

This document defines the system design for DMFR's C++ implementation. It covers the architecture, module boundaries, data flow, storage, IPC, security, build, and packaging needed to satisfy the PRD and Phase 0 exit criteria.

The design prioritizes:

1. **Minimal resource footprint** — the agent must be invisible.
2. **Correctness and safety** — untrusted inputs are handled defensively.
3. **Cross-platform parity** — macOS, Linux, and Windows from the start.
4. **Privacy by default** — no secrets or file contents leak.
5. **Build simplicity** — a single CMake project with vcpkg dependencies.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      Developer Workstation                       │
│                                                                  │
│   ┌────────────┐  ┌────────────┐  ┌───────────┐  ┌──────────┐ │
│   │ Git hooks  │  │ fs watcher │  │  shell    │  │  build   │  │
│   │            │  │            │  │  wrapper  │  │ wrappers │  │
│   └──────┬─────┘  └──────┬─────┘  └─────┬─────┘  └────┬─────┘  │
│          │               │              │              │        │
│          └───────────────┴───────┬──────┴──────────────┘        │
│                                  ▼                               │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  dmfrd (daemon process)                                  │   │
│   │  ┌─────────────┐ ┌─────────────┐ ┌──────────────────┐  │   │
│   │  │ Collectors  │ │  Scrubber   │ │   Local Store    │  │   │
│   │  │  (adapters) │ │  (privacy)  │ │   (SQLite +      │  │   │
│   │  │             │ │             │ │    Parquet)      │  │   │
│   │  └─────────────┘ └─────────────┘ └──────────────────┘  │   │
│   └────────────────────────┬────────────────────────────────┘   │
│                            │ IPC                                 │
│                            ▼                                    │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  dmfr CLI (client process)                               │   │
│   │  - query, status, start, stop, export, config            │   │
│   └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### Two-Process Model

- **dmfrd** — long-running daemon. Owns collectors, the scrubber, and the local store. Runs with user privileges only.
- **dmfr** — short-lived CLI. Connects to the daemon over IPC, executes commands, prints results, and exits.

### Why Not a Single-Process Hybrid?

A single binary that forks into a daemon is possible, but a clean two-process model is safer and simpler:

- CLI crashes cannot corrupt the daemon's event log.
- CLI commands can be written, tested, and reasoned about independently.
- The daemon can run with tighter resource scheduling.
- IDE and shell integrations only need to call the CLI, not manage daemon state.

---

## 3. Module Breakdown

| Module | Responsibility | Language | Location |
|--------|----------------|----------|----------|
| `common` | Shared types, serialization, error handling, platform macros | C++ | `src/common/` |
| `ipc` | Daemon ↔ CLI communication, socket abstraction | C++ | `src/ipc/` |
| `daemon` | Daemon main loop, lifecycle, signal handling | C++ | `src/daemon/` |
| `collectors` | Git, build, system, environment collectors | C++ | `src/collectors/` |
| `scrubber` | Secret detection and redaction | C++ | `src/scrubber/` |
| `store` | SQLite schema, writes, queries, Parquet export | C++ | `src/store/` |
| `query` | Query parsing and execution over the store | C++ | `src/query/` |
| `cli` | Command-line interface | C++ | `src/cli/` |
| `plugins` | Plugin SDK and loader | C++ | `src/plugins/` |
| `tests` | Unit, integration, fuzz, and property tests | C++ | `tests/` |

---

## 4. Data Flow

```
┌───────────┐
│  Source   │  git, shell, build, fs, system
└─────┬─────┘
      │
      ▼
┌─────────────┐
│  Collector  │  normalizes raw event into Event struct
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Scrubber   │  redacts secrets, masks private env vars
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Store      │  append event to SQLite; rotate to Parquet
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Query/CLI  │  reads from store and returns results
└─────────────┘
```

### Event Lifecycle

1. **Capture** — collector observes an event via OS API, hook, or polling.
2. **Normalize** — collector converts raw data into a strongly typed `dmfr::Event`.
3. **Sanitize** — scrubber inspects payload strings and redacts secrets.
4. **Persist** — store appends the event to a write-ahead buffer and SQLite.
5. **Index** — background task updates indexes for time-range and type queries.
6. **Export** — old events are compressed and archived as Parquet files.

---

## 5. Core Data Structures

### Event

```cpp
namespace dmfr {

enum class Source : uint8_t {
  Git,
  Build,
  System,
  Environment,
  Plugin
};

struct Event {
  std::string event_id;        // UUIDv4
  std::chrono::system_clock::time_point timestamp;
  Source source;
  std::string type;            // e.g., "commit", "build_run", "file_change"
  std::string project_id;      // hash of repo root or workspace
  std::string session_id;      // terminal session or process group
  nlohmann::json payload;
  std::vector<std::string> correlation_keys;
  Sensitivity sensitivity;
};

} // namespace dmfr
```

### Snapshot Manifest

```cpp
struct Snapshot {
  std::string snapshot_id;
  std::chrono::system_clock::time_point timestamp;
  std::string project_id;
  nlohmann::json git;
  nlohmann::json environment;
  nlohmann::json dependencies;
};
```

---

## 6. Storage Layer

### SQLite Schema

```sql
CREATE TABLE events (
  event_id TEXT PRIMARY KEY,
  timestamp REAL NOT NULL,          -- Unix seconds with fractional
  source TEXT NOT NULL,
  type TEXT NOT NULL,
  project_id TEXT,
  session_id TEXT,
  payload BLOB NOT NULL,            -- compressed JSON
  sensitivity TEXT NOT NULL
);

CREATE INDEX idx_events_time ON events(timestamp);
CREATE INDEX idx_events_source_time ON events(source, timestamp);
CREATE INDEX idx_events_project_time ON events(project_id, timestamp);
CREATE INDEX idx_events_session_time ON events(session_id, timestamp);

CREATE TABLE correlation (
  event_id TEXT REFERENCES events(event_id),
  key TEXT NOT NULL,
  value TEXT NOT NULL
);

CREATE INDEX idx_corr_event ON correlation(event_id);
CREATE INDEX idx_corr_key_value ON correlation(key, value);

CREATE TABLE snapshots (
  snapshot_id TEXT PRIMARY KEY,
  timestamp REAL NOT NULL,
  project_id TEXT,
  manifest BLOB NOT NULL
);
```

### Retention Strategy

- Recent events (default 7 days) kept in hot SQLite database.
- Older events compressed and archived as Parquet files in `~/.dmfr/archive/`.
- Configurable retention: time-based, size-based, or count-based.
- Export command can materialize any time range as Parquet or JSON.

### Parquet Export

- Use Apache Arrow C++ for Parquet writes.
- Export includes the same columns as the `events` table.
- Files are partitioned by date: `archive/2026-08-18/events.parquet`.

---

## 7. Collectors

### Git Collector

- Triggered by Git hooks installed into tracked repositories on `dmfr init`.
- Records commits, branch changes, working tree status, remotes, tags.
- Does not record full diff contents by default.

### Build Telemetry Collector

- Lightweight wrappers for common build tools (`npm`, `cargo`, `make`, `pytest`, `jest`).
- Captures command, duration, exit code, and truncated stdout/stderr.
- Wrappers are installed by modifying the user's shell PATH or aliases.

### System Telemetry Collector

- Polls at low frequency (every 30 seconds) or on significant change.
- Records OS version, CPU load, memory pressure, disk usage, network state.
- Processes sampled with configurable allow-list.

### Environment History Collector

- Detects changes to dotfiles (`~/.bashrc`, `~/.gitconfig`, etc.) via hash comparison.
- Records installed tool versions by invoking `--version` or equivalent.
- Watches package manifest files in tracked projects.

---

## 8. Scrubber

### Threat Model

The agent ingests untrusted data: file paths, shell commands, environment variables, build output. We must not record secrets.

### Rules

1. **Deny-list of env vars** — `PASSWORD`, `TOKEN`, `SECRET`, `KEY`, `AWS_*`, `GITHUB_*`, etc.
2. **Regex detection** — API keys, JWTs, base64 secrets, private key headers.
3. **Path masking** — home directory and sensitive paths replaced with placeholders.
4. **Truncation** — long strings limited to 4 KB by default.
5. **No file content** — full file contents are never stored unless the user explicitly opts in for specific paths.

### Implementation

- Scrubber runs synchronously before persistence.
- Configurable via `~/.dmfr/scrubber.json`.
- Unit and fuzz tests ensure no secret patterns leak through.

---

## 9. IPC Design

### Transport

- **macOS / Linux:** Unix domain socket at `~/.dmfr/daemon.sock`.
- **Windows:** Named pipe at `\\.\pipe\dmfrd`.

### Protocol

- Request/response over a simple framed protocol.
- Each message: `uint32be length + JSON payload`.
- Supports commands: `status`, `start_collection`, `stop_collection`, `query`, `export`, `shutdown`.

### Security

- Socket/pipe permissions restricted to the owning user.
- No network exposure.
- CLI validates daemon identity via a per-session nonce or process ID check.

---

## 10. CLI Surface

```
dmfr init [path]           # initialize DMFR in a repo or globally
dmfr start                 # start daemon if not running
dmfr stop                  # stop daemon
dmfr status                # show daemon status and recent stats
dmfr query "..."           # run a query against the local store
dmfr timeline              # print recent event timeline
dmfr diff --from T1 --to T2# environment diff between two times
dmfr export --range ...    # export events to Parquet or JSON
dmfr config ...            # view or edit configuration
```

### CLI Implementation

- Built with CLI11.
- Communicates with daemon via IPC for read/write operations.
- Read-only commands may bypass daemon and read SQLite directly when offline.

---

## 11. Plugin Architecture

### Plugin SDK

- C ABI with opaque `dmfr_plugin_t` handle.
- Plugins register collectors and CLI commands.
- Distributed as shared libraries in `~/.dmfr/plugins/`.

### Example Interface

```cpp
extern "C" DMFR_EXPORT int dmfr_plugin_init(dmfr_plugin_api_t* api);
extern "C" DMFR_EXPORT int dmfr_plugin_register_collectors(dmfr_collector_registry_t* registry);
extern "C" DMFR_EXPORT void dmfr_plugin_shutdown();
```

---

## 12. Build System

### Directory Layout

```
dmfr/
├── CMakeLists.txt
├── vcpkg.json
├── src/
│   ├── common/
│   ├── ipc/
│   ├── daemon/
│   ├── collectors/
│   ├── scrubber/
│   ├── store/
│   ├── query/
│   ├── cli/
│   └── plugins/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── fuzz/
├── cmake/
│   └── modules/
└── packaging/
    ├── macos/
    ├── linux/
    └── windows/
```

### Dependencies via vcpkg

```json
{
  "dependencies": [
    "nlohmann-json",
    "sqlite3",
    "cli11",
    "catch2",
    "fmt",
    "spdlog",
    "openssl",
    "arrow"
  ]
}
```

### Build Targets

- `dmfrd` — daemon executable.
- `dmfr` — CLI executable.
- `dmfr_core` — static library used by both.
- `test_unit`, `test_integration`, `test_fuzz` — test executables.

### CMake Presets

- `macos-debug`, `macos-release`
- `linux-debug`, `linux-release`
- `windows-debug`, `windows-release`

---

## 13. Platform Abstraction

### Filesystem

- Use `std::filesystem` for all file operations.
- Provide `dmfr::paths` for platform-specific data directories.

### Processes and Signals

- macOS/Linux: POSIX `sigaction`, `daemonize`, `setpriority`.
- Windows: `CreateProcess`, named pipes, `SetThreadPriority`.

### OS-Specific Telemetry

- macOS: `sysctl`, `host_statistics`, `I/O Kit`.
- Linux: `/proc`, `sysfs`, `getrlimit`.
- Windows: WMI, `GetSystemTimes`, performance counters.

### Git Hooks

- Install hooks into `.git/hooks/` per repository.
- Hook scripts invoke `dmfr hook <event>` and are language-agnostic shells.

---

## 14. Testing Strategy

### Unit Tests

- Every module has matching `*_test.cpp` files.
- Run with Catch2 or Google Test.
- Target >70% line coverage for core modules.

### Integration Tests

- Spin up daemon and CLI in a temporary directory.
- Simulate Git operations and build commands.
- Verify events are recorded and queries return correct results.

### Fuzz Tests

- Fuzz the scrubber with random strings containing secret-like patterns.
- Fuzz the IPC protocol with malformed frames.
- Fuzz JSON payload parsing.

### Sanitizers

- AddressSanitizer and UndefinedBehaviorSanitizer on every CI run.
- ThreadSanitizer for daemon concurrency tests.
- MemorySanitizer on Linux where feasible.

### Static Analysis

- Clang Static Analyzer, cppcheck, and clang-tidy in CI.
- Treat warnings as errors in release builds.

---

## 15. Packaging and Distribution

### macOS

- Homebrew formula.
- `.pkg` installer as alternative.
- Notarized `.dmg` for desktop GUI (future).

### Linux

- `.deb` and `.rpm` packages.
- AUR PKGBUILD for Arch.
- Static binary as fallback.

### Windows

- MSI installer via WiX.
- WinGet manifest.
- ZIP with static binary.

### Updates

- Daemon checks a signed metadata endpoint for updates.
- Updates are opt-in and never automatic for enterprise users.

---

## 16. Privacy and Security Model

### Data Ownership

- All local data stored under `~/.dmfr/`.
- User can inspect, export, or delete all data at any time.

### Network

- No network calls by default.
- Optional team sync requires explicit opt-in and client-side encryption.

### Privilege

- Daemon runs as the unprivileged user.
- No root/admin elevation required.

### Auditing

- `dmfr audit` prints what categories are being recorded.
- Configuration changes are logged as events themselves.

---

## 17. Performance Budgets

| Resource | Target | Measurement |
|----------|--------|-------------|
| CPU idle | <0.5% | 30-second average on an 8-core laptop |
| CPU during burst | <5% | during active build recording |
| RAM | <80 MB | RSS of `dmfrd` |
| Disk writes | <10 MB/hour | normal activity, excluding build output sampling |
| Startup time | <1 second | from `dmfr start` to daemon ready |
| CLI query latency | <2 seconds | over 7 days of events |

---

## 18. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Memory corruption from untrusted input | RAII, bounds-checked containers, fuzz testing, sanitizers. |
| C++ build complexity slows iteration | Strong module boundaries, vcpkg for deps, CI on all platforms. |
| Platform behavior divergence | Abstract OS interfaces behind interfaces; integration tests per OS. |
| Secret leakage | Multi-layer scrubber, deny-lists, regex, and continuous audit tests. |
| Daemon instability | Separate process model, crash logging, automatic restart via OS service. |
| SQLite contention | Write-ahead logging (WAL) and batched writes. |

---

## 19. Open Design Questions

1. Should the agent run as a single binary with subcommands, or as separate `dmfr` and `dmfrd` binaries?
2. Should IPC use JSON or a compact binary protocol like MessagePack?
3. Should the store migrate to DuckDB in Phase 2, or stick with SQLite?
4. How are collectors versioned and hot-reloaded without restarting the daemon?
5. What is the rollback strategy if a bad release corrupts the local store?

---

## 20. Exit Criteria for Phase 0 System Design

- [ ] This design document reviewed and approved by the team.
- [ ] Module boundaries and public APIs agreed upon.
- [ ] Dependency list and vcpkg manifest finalized.
- [ ] CMake build matrix verified on at least macOS and Linux.
- [ ] Fuzzing and sanitizer strategy approved.
- [ ] Privacy scrubber rules reviewed.
- [ ] Packaging plan for macOS, Linux, and Windows documented.
