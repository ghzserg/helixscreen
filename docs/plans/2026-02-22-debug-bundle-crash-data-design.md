# Debug Bundle: Include Crash Data

**Date:** 2026-02-22
**Status:** Approved

## Problem

Debug bundles currently lack crash diagnostics:
1. `crash.txt` is consumed (deleted) on startup — gone by the time a bundle is generated
2. `collect_log_tail()` checks wrong file paths (doesn't match actual log locations)
3. `crash_report.txt` (human-readable, persists) is never collected
4. No local crash history — once consumed, crash data is gone
5. No device_id for cross-referencing R2 telemetry data

## Design

### 1. Crash History Persistence (`crash_history.json`)

New file: `config/crash_history.json` — array of past crash submissions, capped at 20 entries (FIFO).

```json
[
  {
    "timestamp": "2026-02-20T12:34:56Z",
    "signal": 11,
    "signal_name": "SIGSEGV",
    "app_version": "0.10.12",
    "uptime_sec": 3600,
    "fault_addr": "0x00000000",
    "fault_code_name": "SEGV_MAPERR",
    "github_issue": 142,
    "github_url": "https://github.com/prestonbrown/helixscreen/issues/142",
    "sent_via": "crash_reporter"
  }
]
```

Written by:
- `CrashReporter::try_auto_send()` — captures `issue_number` + `issue_url` from crash worker response
- `TelemetryManager::do_send()` — logs crash events from the batch (no R2 key returned, so logs timestamp + signal info)

### 2. Fix Log Tail Collection

Fix `collect_log_tail()` path search order:
1. `journalctl -u helixscreen --no-pager -n N` (Linux with systemd)
2. `/var/log/helix-screen.log`
3. `$HOME/.local/share/helix-screen/helix.log` (was incorrectly `helix-screen.log`)
4. `/tmp/helixscreen.log` (settings default)
5. Settings `log_path` override (custom path from helixconfig.json)

Same fix in `CrashReporter::get_log_tail()`.

### 3. Include crash_report.txt

Read `config/crash_report.txt` (written by `CrashReporter::save_to_file()`) and include as `crash_report` field in bundle.

### 4. Include Crash History in Bundle

Read `config/crash_history.json` and include as `crash_history` array in bundle.

### 5. Include Hashed Device ID

Include `TelemetryManager`'s double-hashed device_id in bundle for manual R2 cross-referencing.

## Files Modified

- `src/system/debug_bundle_collector.cpp` — new collectors + fix log paths
- `include/system/debug_bundle_collector.h` — new method declarations
- `src/system/crash_reporter.cpp` — write crash history on successful send
- `include/system/crash_reporter.h` — new crash history methods
- `src/system/telemetry_manager.cpp` — write crash history for crash events in batch
- `tests/unit/test_debug_bundle_collector.cpp` — new tests
- `tests/unit/test_crash_reporter.cpp` — new tests
