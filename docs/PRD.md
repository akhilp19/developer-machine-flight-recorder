# Developer Machine Flight Recorder (DMFR)

## Product Requirements Document

**Version:** 1.0  
**Status:** Phase 0 Final — v1 Scope Locked  
**Author:** Kimi Code  
**Date:** 2026-08-18  
**Research:** `research/INTERVIEW_GUIDE.md`, `research/COMPETITIVE_ANALYSIS.md`, `research/USER_STORIES.md`, `research/PAIN_POINTS.md`

---

## 1. Executive Summary

The Developer Machine Flight Recorder (DMFR) is a local-first observability tool that continuously records, correlates, and replays the state of a developer's machine. It treats the developer workstation as a system worthy of the same rigorous recording as production infrastructure, making "works on my machine" debugging faster, reproducible, and data-driven.

At its core, DMFR answers one question: **"What changed, and when, that caused this failure?"**

### v1 Scope

The first release focuses on **individual local debugging**: a lightweight agent records Git, build, system, and environment events; a CLI lets the developer query a timeline, diff two points in time, and export a reproduction bundle. Team sync, IDE integrations, and anomaly detection are explicitly out of scope for v1.

---

## 2. Problem Statement

Developers lose hours every week debugging environmental, build, and tooling problems that are actually **forensics problems**:

- Builds that passed yesterday fail today with no code change.
- Tests pass locally but fail in CI.
- A teammate's machine behaves differently with the same repository.
- An IDE plugin, SDK update, or environment variable silently breaks a workflow.
- Onboarding a new developer takes days because the "correct" environment is tribal knowledge.

These problems are especially painful because the information needed to solve them exists, but it is fragmented across Git logs, package manifests, shell history, OS update records, and IDE settings, and much of it is ephemeral. The top-ranked pain points are sudden local build/test failures, CI/local mismatch, and long onboarding times (see `research/PAIN_POINTS.md`).

---

## 3. Vision

> Every developer's machine has a flight recorder: a tamper-resistant, queryable, replayable history of the code, system, build, and environment events that lead to any given failure.

DMFR is **not** a monitoring dashboard. It is a **time machine** for local development.

---

## 4. Target Users

### Primary Users
1. **Individual Developers** — debug their own environment regressions and build failures.
2. **Senior/Platform Engineers** — diagnose team-wide tooling issues and onboard teammates.

### Secondary Users
3. **Engineering Managers** — understand team-wide environment friction and productivity blockers.
4. **DevOps / CI Teams** — correlate local failures with CI failures using shared environment snapshots.
5. **Security / Compliance Teams** — audit what tools and versions were present on a machine at a given time.

---

## 5. Key Value Propositions

| Value | Description |
|-------|-------------|
| **Faster debugging** | Reduce "works on my machine" investigation from hours to minutes. |
| **Reproducibility** | Export a manifest that recreates an environment state at a historical point in time. |
| **Knowledge preservation** | Capture environment and tooling decisions that live only in developers' heads. |
| **Auditability** | Provide a verifiable history of tools, dependencies, and configurations. |

*Proactive prevention and team-wide drift detection are future-phase value props, not v1.*

---

## 6. Product Principles

1. **Local-first by default.** Telemetry starts on the user's machine. Cloud sync is optional and explicit.
2. **Privacy-preserving.** Sensitive data (secrets, tokens, file contents) is never recorded by default.
3. **Lightweight.** The agent must consume negligible CPU, memory, and disk under normal conditions.
4. **Queryable.** The primary interface is search and questions, not dashboards.
5. **Open and extensible.** New data sources and integrations can be added via plugins.
6. **No lock-in.** Data is stored in open, documented formats and can be exported at any time.

---

## 7. Data Sources

DMFR records structured events from four categories. v1 supports the sources listed; future phases expand coverage.

### 7.1 Code History
- Git commits, branch changes, remotes, stashes, tags.
- File system metadata changes in tracked repositories (path, mtime, not full content).
- Working tree status and uncommitted changes.
- Diff metadata (not full content by default).

### 7.2 System Telemetry
- OS version, kernel version, architecture.
- CPU load, memory pressure, disk usage, network state.
- Environment variables (non-sensitive subset) and PATH.
- Shell history (with explicit opt-in per shell).

### 7.3 Build Telemetry (v1: npm, cargo, make, pytest, jest)
- Build tool invocations with command, duration, exit code, and truncated output.
- Dependency resolution events and lockfile changes for supported tools.
- Test runner output and flaky-test signals.
- Compiler warnings and errors.

*Out of v1:* Maven, Bazel, Gradle, custom build tools (plugin-supported).

### 7.4 Environment History
- Installed language runtimes, SDKs, package managers, CLIs (via `--version` style checks).
- Dotfile changes (`.bashrc`, `.zshrc`, `.gitconfig`, etc.).
- Package manifests (`package.json`, `requirements.txt`, `Cargo.lock`, etc.) and their hashes.

*Out of v1:* IDE plugin lists, container/VM state.

---

## 8. Core Features

### v1 Features

#### 8.1 Event Recording Agent
- Lightweight background agent (`dmfrd`) on macOS, Linux, and Windows.
- Collects events via Git hooks, shell wrappers, build-tool wrappers, and polling.
- Buffers events locally with configurable retention policies.
- Minimal resource footprint: <1% CPU, <100 MB RAM, configurable disk budget.

#### 8.2 Local Time-Series Store
- Stores events in SQLite with WAL mode and time-range indexes.
- Default retention: 30 days, configurable.
- Supports time-range queries, source/type filters, and project filters.
- Exports to JSON and Parquet.

#### 8.3 Query Interface
- CLI for power users: `dmfr query`, `dmfr timeline`, `dmfr events`.
- Structured query parameters (`--from`, `--to`, `--source`, `--project`, `--type`).
- Terminal-friendly timeline and table output.

#### 8.4 Environment Diff and Reproduction Bundle
- Diff two timestamps: `dmfr diff --from T1 --to T2`.
- Export a manifest + commands + environment summary as a reproduction bundle.
- Manifest can be shared with teammates to compare environments.

#### 8.5 Secret Scrubber
- Redacts known secret env vars and value patterns before storage.
- Truncates long strings to prevent accidental secret leakage.
- Configurable deny-lists.

### Post-v1 Features (Roadmap)

#### 8.6 Correlation Engine
- Automatically links related events across sources with confidence scores.
- Detects anomalies: unusual build times, new warnings, environment drift.

#### 8.7 IDE / Editor Integrations
- Inline hints when a recent environment change likely caused an error.
- One-click "explain this failure" from terminal output.

#### 8.8 Team / Cloud Features (Optional)
- Encrypted sync of selected summaries to a team workspace.
- Shared dashboards for build failure patterns and environment drift.

---

## 9. User Stories

### v1 Critical Scenarios

1. **As a developer,** after my build suddenly fails, I want to see what changed in my environment in the last 24 hours so that I can identify the cause quickly. *(PP-01)*

2. **As a senior engineer,** when a junior reports a test failure I cannot reproduce, I want to compare our environment snapshots so that I can spot the difference. *(PP-02 / PP-04)*

3. **As a new hire,** when onboarding, I want to import a known-good environment snapshot from a senior teammate so that I can be productive faster. *(PP-03)*

4. **As a security-conscious developer,** I want to know that no secrets or tokens are recorded by DMFR so that I can trust the tool. *(PP-07)*

5. **As a platform engineer,** when a teammate's build breaks after a system update, I want to see the exact environment and system changes that occurred. *(PP-06)*

See `research/USER_STORIES.md` for the full catalog of 25 stories.

---

## 10. Functional Requirements (v1)

| ID | Requirement | v1 Priority |
|----|-------------|-------------|
| FR-01 | Agent runs unobtrusively in the background on macOS, Linux, Windows. | Must |
| FR-02 | Records Git events, file-system metadata, and working-tree status. | Must |
| FR-03 | Records build-tool invocations for npm, cargo, make, pytest, jest. | Must |
| FR-04 | Records environment changes: installed tools, dotfiles, env vars, PATH. | Must |
| FR-05 | Stores data locally in SQLite with JSON/Parquet export. | Must |
| FR-06 | Provides CLI query interface for time-range and event-type search. | Must |
| FR-07 | Generates environment diff reports between two timestamps. | Must |
| FR-08 | Exports a reproduction bundle (manifest + commands + env summary). | Must |
| FR-09 | Scrubs secrets, tokens, and sensitive env vars by default. | Must |
| FR-10 | Daemon/CLI two-process model with IPC on domain sockets / named pipes. | Must |
| FR-11 | Cross-platform CMake build with vcpkg dependencies. | Must |
| FR-12 | Supports plugin architecture for custom data sources. | Should |
| FR-13 | Correlates events across data sources automatically. | Won't (v1) |
| FR-14 | Provides IDE extensions for inline failure explanation. | Won't (v1) |
| FR-15 | Supports optional encrypted team sync with user consent. | Won't (v1) |
| FR-16 | Detects anomalies (build time spikes, new warnings, drift). | Won't (v1) |

---

## 11. Non-Functional Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| NFR-01 | Agent CPU overhead | <0.5% idle, <5% during bursts |
| NFR-02 | Agent memory footprint | <80 MB resident |
| NFR-03 | Local storage budget | Default 5 GB, user-configurable |
| NFR-04 | Query latency for local data | <2 seconds for 7-day range |
| NFR-05 | Startup time (agent) | <1 second |
| NFR-06 | Data retention | Default 30 days, configurable |
| NFR-07 | Offline operation | Full functionality without internet |
| NFR-08 | Privacy | No secrets, credentials, or personal file contents recorded |
| NFR-09 | Cross-platform support | macOS, Linux, Windows (Server later) |
| NFR-10 | Data export | Full export to JSON / Parquet within 60 seconds |

---

## 12. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Developer Workstation                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────┐  │
│  │ Git Hook │  │ FS Watch │  │ Shell    │  │ Build Tool │  │
│  │          │  │          │  │ Wrapper  │  │ Wrappers   │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └─────┬──────┘  │
│       │             │             │               │        │
│       └─────────────┴──────┬──────┴───────────────┘        │
│                            ▼                                │
│                    ┌─────────────────┐                      │
│                    │  DMFR Agent     │                      │
│                    │  - collectors   │                      │
│                    │  - buffer       │                      │
│                    │  - scrubber     │                      │
│                    └────────┬────────┘                      │
│                             ▼                               │
│                    ┌─────────────────┐                      │
│                    │  Local Store    │                      │
│                    │  SQLite + WAL   │                      │
│                    │  + Parquet exp. │                      │
│                    └────────┬────────┘                      │
│                             ▼                               │
│  ┌─────────────┐  ┌─────────────────┐  ┌──────────────┐  │
│  │ CLI Tool    │  │ Query Engine    │  │ Diff/Export  │  │
│  │             │  │                 │  │              │  │
│  └─────────────┘  └─────────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Components

- **Collectors** — source-specific adapters that turn raw events into structured records.
- **Scrubber** — redacts secrets and sensitive fields before storage.
- **Local Store** — SQLite database with append-only event tables and time-range indexes.
- **Query Engine** — parses structured queries into SQL execution plans.
- **Diff/Export** — generates environment diffs and reproduction bundles.
- **IPC** — domain sockets on macOS/Linux, named pipes on Windows, connecting daemon and CLI.

See `DESIGN.md` for the complete C++ system design.

---

## 13. Data Model

### Event Record

```json
{
  "event_id": "uuid",
  "timestamp": "2026-08-18T14:23:11.003Z",
  "source": "git|system|build|env",
  "type": "commit|file_change|build_run|tool_install",
  "project_id": "hash-of-repo-root",
  "session_id": "terminal-session-id",
  "payload": { ... },
  "correlation_keys": ["branch", "command", "tool"],
  "sensitivity": "public|private|scrubbed"
}
```

### Snapshot Manifest

```json
{
  "snapshot_id": "uuid",
  "timestamp": "...",
  "project_id": "...",
  "git": { "commit", "branch", "status" },
  "environment": { "tools", "env_vars", "dotfiles" },
  "dependencies": { "manifests", "lockfiles" }
}
```

---

## 14. Privacy and Security

- **Default-deny recording:** Only non-sensitive metadata is captured.
- **Secret scrubbing:** Environment variables matching known secret patterns are redacted before storage.
- **File content exclusion:** Full file contents are not recorded unless explicitly requested for specific paths.
- **User ownership:** All local data belongs to the user and is stored under their home directory.
- **No network by default:** v1 has no cloud or network calls.
- **Audit log:** The agent itself logs what categories it records so users can inspect and disable them.

---

## 15. Success Metrics

### v1 Outcomes
- Median time to identify the cause of a sudden local build failure.
- Fraction of users who can generate a reproduction bundle in under 2 minutes.
- User-reported confidence that DMFR does not record secrets.
- Percentage of beta users running `dmfr query` at least once per week.

### Future Metrics
- Number of active agents installed.
- Reduction in "works on my machine" escalation time.
- Number of regressions caught by anomaly detection.

### Quality
- Agent resource overhead stays within NFR targets.
- False-positive rate of correlation suggestions (post-v1).
- Data export completeness and correctness.

---

## 16. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Users fear surveillance. | Position as personal tool; default local-only; transparent scrubbing; publish privacy whitepaper. |
| Performance overhead. | Aggressive sampling, quotas, and user-tunable budgets; C++ implementation for tight control. |
| Data explosion. | Compression, retention policies, and intelligent summarization. |
| Platform fragmentation. | CMake + vcpkg; native abstractions per OS; CI on macOS/Linux/Windows. |
| Wrong root-cause suggestions. | Post-v1: confidence scoring; never auto-remediate without user approval. |
| C++ memory safety issues. | RAII, fuzz tests, sanitizers, static analysis in CI. |

---

## 17. Open Questions

1. ✅ Resolved: Agent is a daemon + CLI pair; implementation language is C++17/20 with CMake.
2. ✅ Resolved: v1 build tools are npm, cargo, make, pytest, jest.
3. ✅ Resolved: Local store is SQLite; Parquet export is optional via Arrow.
4. ❓ What is the exact licensing and business model for v1?
5. ❓ How do we handle containerized / remote development environments in v2?
6. ❓ What is the minimum viable replay: manifest export, or full environment reproduction?

---

## 18. Glossary

- **Agent** — The background process that collects DMFR telemetry.
- **Correlation** — Linking events from different sources by time or context.
- **Flight recorder** — Analogy to an aircraft black box: continuous, tamper-resistant recording.
- **Local-first** — Data is primarily stored on the user's machine, not in the cloud.
- **Replay** — Reconstructing the state of the environment at a historical point in time.
- **Snapshot** — A manifest describing the state of code, tools, and environment at a moment.
