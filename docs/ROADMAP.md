# Developer Machine Flight Recorder — Roadmap

**Version:** 0.4  
**Status:** Phase 3 Complete, Phase 4 Ready  
**Date:** 2026-08-20  
**Research:** `research/INTERVIEW_GUIDE.md`, `research/COMPETITIVE_ANALYSIS.md`, `research/USER_STORIES.md`, `research/PAIN_POINTS.md`  
**Design:** `DESIGN.md`

---

## How to Read This Roadmap

Each phase has a **goal**, a **target outcome**, a set of **deliverables**, and **exit criteria**. Phases are sequential but can overlap at boundaries. The roadmap is designed to validate the core hypothesis as early as possible before committing to larger scope.

---

## Technology Stack

The DMFR agent and CLI will be built in **C++** to maximize control over resource usage and OS-level integration. The following stack is locked for the core product:

| Layer | Technology |
|-------|------------|
| **Agent + CLI** | C++17 or C++20 |
| **Build system** | CMake (primary), with Meson as an experimental alternative |
| **Dependency management** | vcpkg (primary), Conan (optional) |
| **JSON** | nlohmann/json |
| **CLI parsing** | CLI11 |
| **Local data store** | SQLite via modern C++ wrapper, plus Parquet export via Apache Arrow C++ |
| **IPC (agent ↔ CLI)** | Unix domain sockets (POSIX). Windows named pipes are a Phase 1 follow-up. |
| **Crypto / scrubbing** | libsodium or OpenSSL |
| **Testing** | Catch2 |
| **Static analysis** | Clang Static Analyzer, cppcheck, AddressSanitizer / ThreadSanitizer |
| **Packaging** | Homebrew (macOS), .deb/.rpm + AUR (Linux), MSI/WinGet (Windows) |
| **CI/CD** | GitHub Actions with macOS, Linux, and Windows runners |
| **IDE extensions** | TypeScript / Kotlin / Lua wrappers around the CLI (Phase 5) |
| **Optional cloud sync** | Go or C++ backend service + PostgreSQL + S3-compatible object storage (Phase 6) |

Because C++ development is slower and higher-risk than Rust or Go, additional time and safety tooling are built into Phase 1.

---

## Phase 0: Validation and Design — COMPLETE

**Goal:** Confirm the problem is real, understand how developers currently debug environment issues, and lock the v1 scope.

### Deliverables
- [x] Interview guide and research plan (`research/INTERVIEW_GUIDE.md`).
- [ ] 10–15 user interviews with developers across team sizes and tech stacks. *(ready to run; see interview guide)*
- [x] Catalog of 20+ "works on my machine" / environment regression stories (`research/USER_STORIES.md`).
- [x] Competitive analysis of existing tools (`research/COMPETITIVE_ANALYSIS.md`).
- [x] Validated list of pain points ranked by frequency and cost (`research/PAIN_POINTS.md`).
- [x] Finalized PRD v1.0 with narrowed scope for Phase 1 (`PRD.md`).
- [x] Finalized C++ architecture and module boundaries for the agent and CLI (`DESIGN.md`).
- [x] Working CMake project skeleton with unit and integration tests.

### Exit Criteria
- [x] Top 5 pain points identified and ranked.
- [ ] At least 3 distinct developer personas confirm they would use DMFR weekly. *(pending interviews)*
- [x] Clear list of top 3 scenarios to solve first: sudden local build failures, CI/local mismatch, onboarding.
- [x] v1 scope locked: local-only, CLI-first, no network, no IDE integrations, no team sync.
- [x] CMake build matrix defined for macOS, Linux, and Windows.

### Time Estimate
2–3 weeks

**Actual:** Phase 0 design and research artifacts complete. User interviews remain the only pending item and can run in parallel with Phase 2 engineering.

---

## Phase 1: Local Data Collection — COMPLETE

**Goal:** Build a lightweight C++ agent that records the four core data sources locally, without breaking developer workflows.

### Deliverables
- [x] Cross-platform CMake build matrix (macOS first, Linux second, Windows third).
- [x] C++ agent skeleton with separate `dmfr` CLI and `dmfrd` daemon binaries.
- [x] IPC layer: daemon ↔ CLI communication via Unix domain sockets (POSIX implemented; Windows stub).
- [x] Git collector: commits, branch changes, working tree status via Git hooks.
- [x] Build telemetry collector: POSIX shell wrappers for `npm`, `cargo`, `make`, `pytest`, `jest`.
- [x] System telemetry collector: OS version, CPU/memory snapshots, environment variables, PATH.
- [x] Environment history collector: installed language runtimes, package manager versions, dotfile changes.
- [x] Secret scrubber with known patterns and deny-lists; key-based redaction and path masking.
- [x] Local append-only event store (SQLite) with JSON and Parquet export (Parquet gated on optional Apache Arrow).
- [x] CLI commands: `dmfr init`, `dmfr start`, `dmfr stop`, `dmfr status`, `dmfr query`, `dmfr export`, `dmfr hook`, `dmfr install-wrappers`, `dmfr retention`.
- [x] Unit and integration tests with Catch2.
- [x] CI pipeline skeleton with AddressSanitizer, ThreadSanitizer, and static analysis on every PR.

### Exit Criteria
- [x] Agent builds and passes tests on macOS (full IPC), Linux (compile-tested), and Windows (compile-tested via CI matrix).
- [ ] Agent runs continuously for 7 days on 5+ developers' machines without complaints. *(requires beta users)*
- [x] CPU overhead <1% idle, memory <100 MB in local smoke tests.
- [x] Exported data can be inspected and verified by a human.
- [x] No secrets or file contents leak in recorded events (verified via scrubber unit tests and manual inspection).
- [x] Untrusted input paths have dedicated fuzz targets in `tests/fuzz/` (JSON payloads, IPC framing, store query strings).
- [ ] CI passes on macOS, Linux, and Windows for every merge. *(macOS verified locally; Linux/Windows need runner validation)*

### Time Estimate
8–10 weeks

**Actual:** Phase 1 implemented in working form. Core data collection is functional on macOS and verified end-to-end. Parquet export via `--format parquet` and retention policy via `dmfr retention` are both wired and smoke-tested. Windows IPC and wrapper scripts, plus the 7-day beta soak test, are the remaining follow-ups.

**Phase 1 limitations captured:**
- POSIX IPC only; Windows IPC is a stub.
- Build wrappers are POSIX shell scripts; Windows needs `.bat`/`.ps1` equivalents.
- macOS system telemetry is complete; Linux uses `/proc` fallbacks; Windows is not implemented.
- Parquet export requires Apache Arrow/Parquet C++ to be available at build time (falls back to JSON export otherwise).

---

## Phase 2: Query and Timeline

**Goal:** Make the recorded data useful for debugging via search, filtering, and a timeline view.

### Deliverables
- [x] Structured query parameters (`--from`, `--to`, `--source`, `--type`, `--limit`, `--project`) work in `dmfr query`.
- [x] Configuration file support (`dmfr config init/show`) for retention rules, collector toggles, scrubbing deny-lists, and data-directory overrides.
- [x] Scheduled retention enforcement inside `dmfrd`, driven by `config.json` (`dmfr retention` remains available for manual runs).
- [x] Richer CLI commands: `dmfr query`, `dmfr timeline`, `dmfr events`, `dmfr show <event-id>`.
- [x] SQLite schema optimization for time-range queries (indexes already present; `LIMIT`/`OFFSET` and sort order supported by `Store::query`).
- [x] Terminal-friendly timeline visualization, grouped by time/session/project.
- [x] Event detail view showing full payload, surrounding context events, and sensitivity.
- [x] Event grouping/correlation context: `Store::get_event_context` returns events within a configurable window, optionally filtered to the same project/session.
- [x] Export parity: `dmfr export --format json` and `--format parquet` use the same IPC query path and include the same fields.
- [x] Query performance regression tests/benchmarks for 7-day ranges with 10k+ events.
- [x] Shell autocompletion scripts for bash/zsh.
- [x] User-facing docs: README quickstart, configuration reference, and command examples.

### Exit Criteria
- Developer can answer "what changed in the last 4 hours?" in under 60 seconds using `dmfr timeline`.
- `dmfr timeline` renders a 24-hour window in under 2 seconds on a typical developer machine.
- Query latency meets NFR-04 (<2 seconds for a 7-day range) on a store with at least 10k events.
- Retention policy runs automatically without manual intervention, respects a configured disk budget, and does not corrupt or lose non-targeted events.
- Event detail view correctly reconstructs the surrounding git commit, build, and system snapshot context.
- `dmfr export` produces logically identical output across JSON and Parquet formats for the same query.
- No sensitive data appears in exported files when scrubbing rules are enabled.

### Limitations and Follow-ups
- Natural-language query assistant is explicitly out of scope for Phase 2; it is planned for Phase 5.
- Cross-machine diff, snapshot import/export, and reproduction bundles remain Phase 3 work.

### Time Estimate
4–6 weeks

---

## Phase 3: Diff, Snapshots, and Reproduction Bundles

**Goal:** Enable comparison and sharing of environment state so users can reproduce and explain failures.

### Deliverables
- [x] Snapshot manifest schema and data model:
  - OS version and kernel/build info.
  - Language runtime versions (Node, Python, Rust, Go, Java, etc.).
  - Package-manager lockfile presence and key dependency versions.
  - Environment variables (scrubbed) and PATH.
  - Git state: branch, commit SHA, dirty flag, remote URL.
  - Build-tool versions (CMake, make, npm, cargo, etc.).
  - Optional free-text notes captured at snapshot time.
- [x] `dmfr snapshot` CLI command and `snapshot` IPC command to capture and store a manifest now.
- [x] Store support for snapshots:
  - `Store::insert_snapshot`
  - `Store::get_snapshot_by_id`
  - `Store::query_snapshots` (by time range, project_id).
- [x] Diff engine (`src/diff/diff.cpp`):
  - Compare two manifests key-by-key.
  - Produce added/removed/changed sections for: env vars, PATH entries, language versions, git state, dependencies.
- [x] `dmfr diff --from T1 --to T2` local diff between two timestamps.
- [x] `dmfr diff --snapshot <id>` or `dmfr diff --file <snapshot.json>` to compare current machine/project against an exported snapshot.
- [x] `dmfr bundle` reproduction bundle export:
  - Output directory containing:
    - `manifest.json` (snapshot at the failure time).
    - `events.jsonl` (events in the selected window).
    - `README.md` with commands and environment summary.
  - Option to scrub/mask secrets and paths before bundling.
- [x] `dmfr import-snapshot <file>` to load an exported manifest into the local store for onboarding or incident review.
- [x] Privacy controls for snapshots and bundles:
  - `--scrub` flag defaulting to true; deny-list env vars and path masking applied to snapshots and bundles.
- [x] Tests:
  - Unit tests for the diff engine with controlled manifests.
  - Integration tests for `snapshot`, `diff`, and `import-snapshot`.
- [x] Docs: README quickstart updated with snapshot, diff, bundle, and import examples.

### Exit Criteria
- [x] A developer can generate and share a reproduction bundle in under 2 minutes.
- [x] A developer can run `dmfr diff --from <before> --to <after>` and see the exact environment changes that coincide with a new failure.
- [x] `dmfr bundle` produces a portable artifact that another developer can inspect without running DMFR.
- [x] `dmfr import-snapshot` loads a snapshot that can be used as a baseline for `dmfr diff`.
- [x] Diff reports correctly identify environment changes between two points.
- [x] Snapshot capture and diff complete in under 5 seconds on a typical machine.
- [x] No secrets or local file paths leak in exported snapshots or bundles when default scrubbing is enabled.
- [ ] New hire can import a snapshot and run tests within 30 minutes. *(requires onboarding workflow validation with real users)*

### Limitations and Follow-ups
- Snapshots are point-in-time captures, not live CI reproduction environments.
- Automatic snapshot-on-failure and CI comparison are Phase 4/7 features.
- Cloud sharing of snapshots is Phase 6; Phase 3 bundles are file-based only.

### Time Estimate
4–6 weeks

---

## Phase 4: Correlation and Root Cause

**Goal:** Automatically surface likely causes by linking events across data sources.

### Deliverables
- [ ] Correlation engine with time-window and project-context rules.
- [ ] Automatic linking of dependency updates to build failures.
- [ ] "Explain this failure" CLI command for build/test failures.
- [ ] Confidence-scored candidate causes.

### Exit Criteria
- Correlation suggestions are correct at least 70% of the time on historical failures.
- "Explain this failure" surfaces at least one plausible cause for the top 5 failure types.

### Time Estimate
4–6 weeks

---

### Implemented build comparison workflow (2026-10-22)

- `dmfr record -- <command>` captures the caller's scrubbed environment and a
  pre-build snapshot, records the resolved executable, and links the build result.
- Updated POSIX wrappers use recording automatically; output stays attached to
  the terminal and version probes bypass recording.
- `dmfr diagnose` compares a failed build with the prior matching success in the
  same working directory, excluding overlapping runs that were not yet complete.
- Snapshots include common lockfile fingerprints and resolved tool paths.
- Reports distinguish candidate changes from proven causes and explain missing
  or redacted evidence. Bundles are documented as diagnostic artifacts.

This is a deterministic comparison workflow, not a completed causal inference or
replay engine. Real-user diagnosis-time and long-running overhead validation remain
outstanding; Windows execution recording is not implemented.

## Phase 5: Developer Experience and Integrations

**Goal:** Embed DMFR into the places developers already work.

### Deliverables
- [ ] Shell integration: `dmfr explain` bound to common terminal workflows.
- [ ] Git hook integration for automatic per-commit snapshots.
- [ ] IDE extension for VS Code: inline failure explanation and environment diff panel.
- [ ] IDE extension for JetBrains (optional, depending on user base).
- [ ] Plugin SDK and documentation for custom collectors.
- [ ] Improved natural-language query assistant.

### Exit Criteria
- 50%+ of beta users use an IDE or shell integration at least once per week.
- Plugin SDK supports at least one community-built collector.
- Natural-language query resolves top 10 common questions correctly.

### Time Estimate
6–8 weeks

---

## Phase 6: Team and Optional Cloud Sync

**Goal:** Enable teams to share anonymized, privacy-preserving telemetry for broader insights.

### Deliverables
- [ ] End-to-end encrypted sync of selected summaries to team workspace.
- [ ] Team dashboard: build failure patterns, common environment drift, flaky test signals.
- [ ] Granular privacy controls per user and per organization.
- [ ] Shared "known-good" environment snapshots for onboarding.
- [ ] Audit logs for compliance and security reviews.

### Exit Criteria
- Team features are opt-in and clearly communicated as value-add, not surveillance.
- At least one team of 5+ uses shared snapshots for onboarding or incident review.
- Privacy controls pass a third-party review or internal security audit.

### Time Estimate
8–10 weeks

---

## Phase 7: Intelligence and Automation

**Goal:** Move from reactive forensics to proactive prevention.

### Deliverables
- [ ] Anomaly detection: build-time spikes, new warnings, environment drift.
- [ ] Predictive alerts: "This change pattern has caused failures for other projects."
- [ ] Automated environment health scoring per project.
- [ ] Integration with CI systems to compare local and CI environments.
- [ ] Recommendation engine for dependency updates and tool migrations.

### Exit Criteria
- Anomaly detection catches at least one real regression per month per active team.
- Users trust and act on recommendations at least 30% of the time.
- CI/local environment comparison correctly identifies mismatch causes.

### Time Estimate
10–12 weeks

---

## Phase 8: Scale, Ecosystem, and Maturity

**Goal:** Make DMFR a durable, broadly adopted platform.

### Deliverables
- [ ] Public REST/GraphQL API for third-party integrations.
- [ ] Marketplace for community plugins and connectors.
- [ ] Enterprise features: SSO, SCIM, audit trails, on-premise deployment.
- [ ] Official support for containerized and remote dev environments.
- [ ] Comprehensive public documentation, case studies, and security whitepapers.
- [ ] Stable 1.0 release with documented support lifecycle.

### Exit Criteria
- Public API has active usage from at least 3 external tools.
- Enterprise customers can deploy fully on-premise or in private cloud.
- 1.0 release is stable enough for production use across thousands of machines.

### Time Estimate
12–16 weeks

---

## Cumulative Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| 0. Validation | 2–3 weeks | ~3 weeks |
| 1. Local Collection | 8–10 weeks | ~12 weeks |
| 2. Query and Timeline | 4–6 weeks | ~17 weeks |
| 3. Diff and Reproduction | 4–6 weeks | ~22 weeks |
| 4. Correlation | 4–6 weeks | ~27 weeks |
| 5. Integrations | 6–8 weeks | ~34 weeks |
| 6. Team Sync | 8–10 weeks | ~43 weeks |
| 7. Intelligence | 10–12 weeks | ~54 weeks |
| 8. Scale and Maturity | 12–16 weeks | ~67 weeks |

**Total estimated time to 1.0:** approximately **15–17 months** from Phase 0 kickoff.

---

## Key Milestones

| Milestone | Phase | Description |
|-----------|-------|-------------|
| **M0: Problem Validated** | End of Phase 0 | Clear evidence developers need this. |
| **M1: Agent Running** | End of Phase 1 | Production-ready local data collection. |
| **M2: Debugging Faster** | End of Phase 2 | Users query and inspect history efficiently. |
| **M3: Reproducible Failures** | End of Phase 3 | Users share and import environment snapshots. |
| **M4: Root Cause Found** | End of Phase 4 | Automatic correlation proves value. |
| **M5: Embedded in Workflow** | End of Phase 5 | IDE and shell integrations drive habit. |
| **M6: Team Value** | End of Phase 6 | Shared telemetry improves team outcomes. |
| **M7: Proactive Prevention** | End of Phase 7 | Intelligence features reduce failures. |
| **M8: Platform** | End of Phase 8 | Stable, extensible, enterprise-ready product. |

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-08-18 | Local-first default | Builds trust, avoids surveillance concerns, works offline. |
| 2026-08-18 | Open data format | Reduces lock-in, enables community integrations. |
| 2026-08-18 | CLI before GUI | Target audience is developers; CLI validates core faster. |
| 2026-08-18 | Team sync is Phase 6 | Personal value must be proven before asking for shared data. |
| 2026-08-18 | C++17/20 + CMake + vcpkg | Maximizes performance and OS integration; accepts slower iteration in exchange for resource control. |
| 2026-08-18 | SQLite local store with Parquet export | Simple, portable, well-supported in C++; Parquet enables open data export. |
| 2026-08-18 | Separate `dmfr` CLI and `dmfrd` daemon binaries | Cleaner IPC model; CLI crashes cannot corrupt daemon state. See `DESIGN.md`. |
| 2026-08-18 | v1 build tools: npm, cargo, make, pytest, jest | Highest-frequency tools among target users; plugin SDK expands coverage later. |
| 2026-08-18 | No network calls in v1 | Removes privacy and security risk; proves local value before any cloud feature. |
| 2026-08-18 | Diff/reproduction bundles before correlation | Users can immediately compare environments and share state; correlation builds on top. |
| 2026-08-19 | POSIX IPC first, Windows IPC stubbed | macOS/Linux are the primary v1 targets; Windows IPC and wrappers will follow before 1.0. |
| 2026-08-19 | Key-based scrubbing, not heuristic value scanning | Over-aggressive alphanumeric heuristic redacted SHAs and file paths; key deny-list + explicit regex is safer. |

---

## Open Decisions

1. What is the exact licensing and business model for v1?
2. Do we open-source the agent from day one, or keep it proprietary initially?
3. Which cloud backend stack is used for optional team sync?
4. What is the pricing model for team/enterprise features?
5. Do we migrate the local store to DuckDB in Phase 2, or stay on SQLite through v1?
