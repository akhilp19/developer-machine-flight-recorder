# DMFR Phase 0 — Catalog of User Stories

**Date:** 2026-08-18  
**Goal:** Collect 20+ realistic scenarios that DMFR could solve. These stories come from common developer experiences and the themes surfaced in interviews (once conducted).

---

## How to Use This Document

Each story has:
- **ID** for reference in PRD and roadmap.
- **User** — persona.
- **Scenario** — what happened.
- **Current pain** — how they debug it today.
- **DMFR value** — what DMFR would reveal or automate.
- **Priority guess** — high/medium/low based on frequency and cost.

---

## 1. Sudden Local Build Failures

### US-001 — Build broke overnight
- **User:** Backend engineer on a Node.js monorepo.
- **Scenario:** `npm run build` passed at 5 PM yesterday. At 9 AM today it fails with a TypeScript error in a file no one changed.
- **Current pain:** Manually checks Git log, `package-lock.json`, runs `npm ci`, compares `node --version` with teammates.
- **DMFR value:** Timeline shows a global `npm` update at 7 PM and a `typescript` patch bump in a transitive dependency.
- **Priority:** High

### US-002 — Missing native dependency
- **User:** ML engineer using Python + C extensions.
- **Scenario:** A `pip install` succeeds but importing a package fails with a missing `.so` error.
- **Current pain:** Reinstalls Python, checks `LD_LIBRARY_PATH`, compares with Docker image.
- **DMFR value:** Diff of environment variables and installed system libraries between the last known-good state and now.
- **Priority:** High

### US-003 — Rust toolchain panic
- **User:** Systems engineer.
- **Scenario:** `cargo build` starts failing after a `rustup update`.
- **Current pain:** Bisects Rust versions manually; reads release notes.
- **DMFR value:** Records `rustup` activity and correlates it with the first failed build.
- **Priority:** Medium

### US-004 — Xcode update breaks iOS build
- **User:** Mobile developer.
- **Scenario:** macOS auto-updates Xcode overnight; the iOS simulator no longer matches the project.
- **Current pain:** Checks `xcode-select -p`, downloads older Xcode.
- **DMFR value:** System telemetry records the macOS update and Xcode path change.
- **Priority:** Medium

### US-005 — Makefile silently picks up wrong tool
- **User:** Embedded engineer.
- **Scenario:** A Makefile uses `which python` and now finds Python 3.12 instead of 3.10, breaking the build script.
- **Current pain:** Adds explicit Python version to Makefile after hours of debugging.
- **DMFR value:** Highlights PATH and `python` symlink changes before the first failure.
- **Priority:** Medium

---

## 2. "Works on My Machine"

### US-006 — CI fails, local passes
- **User:** Full-stack developer.
- **Scenario:** Tests pass locally but fail in CI with a database connection error.
- **Current pain:** Compares env vars, checks `.env` files, asks CI team for logs.
- **DMFR value:** Snapshot comparison between local environment and CI environment at build time.
- **Priority:** High

### US-007 — Teammate cannot reproduce
- **User:** Senior engineer helping a junior.
- **Scenario:** Junior reports a failing test; senior's machine passes it.
- **Current pain:** Screen share to compare Node versions, installed packages, IDE settings.
- **DMFR value:** One-click environment diff between the two machines.
- **Priority:** High

### US-008 — Onboarding new hire
- **User:** New hire.
- **Scenario:** Spends three days installing tools and still cannot run the test suite.
- **Current pain:** Reads outdated README; asks in Slack; tries random Stack Overflow fixes.
- **DMFR value:** Import a known-good environment snapshot from a senior teammate.
- **Priority:** High

### US-009 — Contractor laptop differs
- **User:** Contractor joining a project.
- **Scenario:** Corporate laptop has different security tools and env vars than the team's machines.
- **Current pain:** Cannot reproduce a build failure seen by the team.
- **DMFR value:** Identifies the specific system-level difference blocking the build.
- **Priority:** Medium

### US-010 — Remote dev machine drift
- **User:** Developer using a remote Linux workstation.
- **Scenario:** A shared remote dev box gets updated and breaks their workflow.
- **Current pain:** No visibility into what changed on the remote machine.
- **DMFR value:** Records environment changes on the remote machine and correlates with failures.
- **Priority:** Medium

---

## 3. Flaky and Mysterious Tests

### US-011 — Test flakiness after dependency update
- **User:** Backend engineer.
- **Scenario:** A test becomes flaky after `npm update`.
- **Current pain:** Runs the test 50 times; guesses at timing issues.
- **DMFR value:** Links the first flaky run to the dependency update and shows system load at the time.
- **Priority:** High

### US-012 — Clock/timezone-sensitive failure
- **User:** SaaS engineer.
- **Scenario:** Tests fail only when run after midnight UTC due to a date formatting bug.
- **Current pain:** Reproduces by changing system clock.
- **DMFR value:** System telemetry shows the time zone and clock state during failed runs.
- **Priority:** Medium

### US-013 — Race condition under load
- **User:** Systems engineer.
- **Scenario:** A test fails only when the machine is under memory pressure.
- **Current pain:** Runs tests while manually loading the CPU.
- **DMFR value:** Correlates test failures with CPU/memory telemetry.
- **Priority:** Medium

---

## 4. Environment Drift and Tooling Changes

### US-014 — Dotfile change breaks shell
- **User:** Platform engineer.
- **Scenario:** Edits `.zshrc` to add an alias; the next day shell startup fails on a work machine.
- **Current pain:** Comments out sections of `.zshrc` one by one.
- **DMFR value:** Dotfile history shows the exact change and when it happened.
- **Priority:** Medium

### US-015 — IDE plugin update
- **User:** Frontend engineer.
- **Scenario:** VS Code extension updates and changes formatting rules, causing pre-commit failures.
- **Current pain:** Disables extensions one by one.
- **DMFR value:** Records IDE/plugin version changes and correlates with build failures.
- **Priority:** Medium

### US-016 — System package update
- **User:** Data engineer.
- **Scenario:** Homebrew updates `openssl` and breaks Python's `cryptography` package.
- **Current pain:** Reinstalls Python packages; pins OpenSSL version.
- **DMFR value:** System telemetry shows `openssl` update and the first Python import failure.
- **Priority:** High

### US-017 — PATH pollution
- **User:** DevOps engineer.
- **Scenario:** A new tool installer prepends to PATH and shadows the correct `kubectl` binary.
- **Current pain:** `which kubectl` shows wrong path; manually reorders PATH.
- **DMFR value:** Records PATH changes and flags the shadowed binary.
- **Priority:** Medium

---

## 5. Security and Compliance

### US-018 — Audit which tools were installed
- **User:** Security engineer.
- **Scenario:** Need to verify what tools and versions were on a developer's machine during an incident.
- **Current pain:** Asks developer manually; relies on memory.
- **DMFR value:** Queryable environment history at any timestamp.
- **Priority:** Medium

### US-019 — Detect secret leakage in local logs
- **User:** Security-conscious developer.
- **Scenario:** Worries that local build logs might contain API keys.
- **Current pain:** Manually scans log files.
- **DMFR value:** Built-in scrubber redacts secrets before storage.
- **Priority:** High

### US-020 — Prove compliance for onboarding
- **User:** Engineering manager.
- **Scenario:** Needs to show auditors that new hires use approved tool versions.
- **Current pain:** Manual surveys and screenshots.
- **DMFR value:** Snapshot reports of standard environment configurations.
- **Priority:** Low

---

## 6. Performance and Productivity

### US-021 — Build got slower
- **User:** Full-stack engineer.
- **Scenario:** Webpack build time doubles over a week.
- **Current pain:** Guesses at dependency bloat; profiles manually.
- **DMFR value:** Build telemetry shows duration trend and correlates with dependency updates.
- **Priority:** Medium

### US-022 — IDE indexing slow after update
- **User:** Mobile developer.
- **Scenario:** Android Studio indexing takes 30 minutes after an SDK update.
- **Current pain:** Clears caches; restarts IDE.
- **DMFR value:** System telemetry shows disk I/O and process activity during indexing.
- **Priority:** Low

### US-023 — Long test feedback loop
- **User:** Backend engineer.
- **Scenario:** Local test suite takes 20 minutes; wants to know when it regressed.
- **Current pain:** Manual timing; no historical data.
- **DMFR value:** Tracks per-suite duration over time.
- **Priority:** Medium

---

## 7. Cross-Functional Scenarios

### US-024 — Designer running code
- **User:** Designer who codes occasionally.
- **Scenario:** Needs to run a prototype; environment setup is opaque.
- **Current pain:** Asks engineer for help every time.
- **DMFR value:** Simple command to see what changed and import a working snapshot.
- **Priority:** Medium

### US-025 — Platform team debugging multiple reports
- **User:** Platform engineer.
- **Scenario:** Three teammates report the same build failure after a tooling change.
- **Current pain:** Interviews each person; manually compares their setups.
- **DMFR value:** Aggregated view of environment drift across the team (with opt-in sharing).
- **Priority:** High

---

## Summary by Priority

| Priority | Count | Top Themes |
|----------|-------|-----------|
| High | 11 | Sudden build failures, CI/local mismatch, onboarding, flaky tests, secret safety, team-wide tooling issues |
| Medium | 12 | Dotfiles, IDE plugins, PATH, remote machines, timezone/perf issues |
| Low | 2 | Compliance, IDE indexing |

**Highest-value categories to build first:**
1. Local build failure forensics.
2. Environment diff between two machines/times.
3. Secret scrubbing and privacy controls.
4. Team snapshot sharing for onboarding.
