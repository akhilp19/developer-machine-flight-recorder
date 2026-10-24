# Developer Machine Flight Recorder (DMFR)

A local-first debugging tool that records development activity and compares machine environments around build failures.

## Status

Phase 3 complete; initial build comparison workflow implemented. Snapshots, diffs, diagnostic bundles, and comparisons against the last successful build are available. Broader correlation and real-user validation remain planned.

## Quick Start

```bash
# Configure with vcpkg
cmake --preset=macos-debug
# or
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Optional: enable Parquet export if Apache Arrow is installed
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/Cellar/apache-arrow/25.0.1_3

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Initialize configuration and start the daemon
./build/bin/dmfr config init
./build/bin/dmfr start

# Query and inspect events
./build/bin/dmfr query --limit 10
./build/bin/dmfr timeline --limit 50
./build/bin/dmfr events --limit 20 --offset 0
./build/bin/dmfr show <event-id> --window 60

# Snapshots and diff
./build/bin/dmfr snapshot
./build/bin/dmfr snapshot --list --limit 10
./build/bin/dmfr diff --from-id <id1> --to-id <id2>
./build/bin/dmfr diff --snapshot <id>
./build/bin/dmfr bundle /tmp/dmfr-bundle --limit 1000
./build/bin/dmfr import-snapshot /tmp/dmfr-bundle/manifest.json

# Export events
./build/bin/dmfr export /tmp/events.json --limit 1000
./build/bin/dmfr export /tmp/events --format parquet --limit 1000

# Run retention manually
./build/bin/dmfr retention --max-events 100000 --max-age-days 30

# Stop the daemon
./build/bin/dmfr stop
```

## Compare a failed build with the last success

On macOS and Linux, run builds through the recorder:

```bash
dmfr record -- make test
# After a later failure of the same command:
dmfr diagnose
# Or select a particular failure:
dmfr diagnose --event <failed-build-event-id>
```

The recorder captures the calling terminal's environment, PATH, resolved tool
paths, Git state, and manifest/lockfile fingerprints **before** the build runs.
It stores a scrubbed snapshot and associates it with the build event. It runs
arguments directly without a shell, preserves terminal input/output, and returns
the command's exit status. Build output passes through and is not saved.
Recording and diagnosis work against the local store without a running daemon.

Reinstall wrappers with `dmfr install-wrappers` to enable this automatically for
`npm`, `cargo`, `make`, `pytest`, and `jest`. The wrappers remove their own directory
from PATH before capture and execution to avoid recursion. The recorder also removes its installed wrapper directory from PATH, including
when invoked directly, so version probes and nested tool calls cannot recurse.
Standalone `--version`/`-V` wrapper calls pass through without recording a build.

Diagnosis selects the latest failure in the current working directory (override
with `--project <directory>`), then the previous successful run with the same
command identity and argument vector. Successful runs still in progress when the
failure started are excluded. Reports include observed differences and executable
paths; they identify candidate explanations, not proven causes. Exact matching is
unavailable when arguments have been redacted or truncated. Old events without
linked snapshots produce an explicit missing-evidence result.

Manifest capture covers the project directory and its immediate subdirectories,
including common npm, Yarn, pnpm, Cargo, Python, Go, Ruby, Composer, vcpkg, and Conan
lockfiles. It records file fingerprints, not a parsed dependency graph.

`dmfr bundle` exports **diagnostic evidence**, not an executable reproduction.
It contains a current snapshot and selected events; it does not package source,
installed dependencies, historical build snapshots, or a replay environment.

## Recording reliability and overhead

Recognized credential arguments such as `--password=value`, `--token value`, and
`API_KEY=value` are redacted before persistence. This does not parse arbitrary
shell program text or discover every possible secret; custom patterns cover
application-specific formats. Arguments passed to the executed command are unchanged.
Previously stored events are not rewritten.

The recorder forwards SIGINT, SIGQUIT, and SIGTERM while waiting for the foreground
child and records its resulting exit status, including terminal Ctrl+C. Initialization
or snapshot failures let the command run without telemetry; result-persistence
failures preserve its exit status. SIGKILL and machine crashes cannot be finalized.

Each external snapshot probe has a 500 ms timeout and a 64 KiB output cap. Up to
four version probes run concurrently, and timed-out or oversized version results
are reported in `notes.probe_warnings` and diagnosis output. Versions are not cached,
so virtual environments and version-manager changes remain visible. File fingerprints
use a fixed 64 KiB buffer. Actual startup overhead depends on the project and tools.

Diagnosis uses indexed command identity and arguments with cursor pagination.
Explicit script paths distinguish different scripts sharing a filename; commands
invoked by tool name remain comparable when PATH resolves to a different executable.
Older events fall back to their recorded command name and have weaker identity.

Retention applies the configured age and count limits to events and unreferenced
snapshots atomically. Snapshots referenced by retained events are preserved; those
protected snapshots may exceed the standalone snapshot count limit. Recording checks
retention on the configured interval even without a daemon. Database file space is
reused after deletion; retention does not run a blocking VACUUM on every build.

## Configuration

DMFR reads a JSON configuration file from the platform config directory:

- macOS: `~/Library/Preferences/dmfr/config.json`
- Linux: `~/.config/dmfr/config.json`
- Windows: `~/AppData/Roaming/dmfr/config.json`

Run `dmfr config init` to create the file with defaults, then edit:

```json
{
    "data_dir": "/custom/path/to/dmfr/data",
    "retention": {
        "enabled": true,
        "max_events": 100000,
        "max_age_hours": 720,
        "interval_minutes": 60
    },
    "collectors": {
        "git": true,
        "build": true,
        "system": true,
        "environment": true,
        "poll_interval_seconds": 30
    },
    "scrubber": {
        "denied_env_vars": [],
        "secret_regexes": [],
        "max_string_size": 4096
    }
}
```

Snapshot creation and import apply the configured scrubbing rules before storage.
Diff responses also scrub both snapshots. Custom `secret_regexes` use C++ ECMAScript
regular expressions; invalid patterns are rejected when configuration is applied.
Existing stored snapshots are not rewritten automatically.

New snapshots store dependency manifest paths relative to the project directory,
so different checkout locations can be compared. Legacy absolute manifest paths
are normalized when the snapshot includes a Git repository path. PATH diffs include
`order_changed` and the original sequences when precedence or duplicates change.

## Shell completion

Source the completion scripts in your shell profile:

```bash
# Bash
source scripts/completion/dmfr.bash

# Zsh
fpath+=(/path/to/scripts/completion)
autoload -U compinit && compinit
```

## Project Structure

```
src/
  common/        shared types, errors, paths
  ipc/           daemon ↔ CLI inter-process communication
  daemon/        background agent
  collectors/    event collectors (git, build, system, environment)
  scrubber/      secret detection and redaction
  store/         SQLite local store and Parquet export
  query/         query parsing and execution
  snapshot/      environment snapshot capture
  diff/          snapshot comparison engine
  build/         build execution, linked snapshots, and failure comparison
  cli/           command-line interface
  plugins/       plugin SDK
tests/
  unit/          unit tests
  integration/   integration tests
  fuzz/          fuzz tests
```

## Documentation

- [PRD.md](docs/PRD.md) — product requirements
- [ROADMAP.md](docs/ROADMAP.md) — phased development plan
- [DESIGN.md](docs/DESIGN.md) — system design

## License

TBD
