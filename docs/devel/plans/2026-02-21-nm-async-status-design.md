# NetworkManager Backend: Async Status Polling

**Date**: 2026-02-21
**Status**: Implemented
**Problem**: `WifiBackendNetworkManager::get_status()` makes 2-4 synchronous `popen("nmcli ...")` calls on the main LVGL thread, blocking UI rendering for 100-1000ms per call. Called every 5s from home panel and repeatedly from network settings overlay.

## Design

### Background Status Thread

Add a `status_thread_` to `WifiBackendNetworkManager` that periodically polls nmcli and caches the result. `get_status()` returns cached data under a mutex — zero blocking on the main thread.

### Changes (all internal to `WifiBackendNetworkManager`)

**New members:**
- `std::thread status_thread_` — background polling thread
- `std::mutex status_mutex_` — protects cached status
- `ConnectionStatus cached_status_` — latest status snapshot
- `std::condition_variable status_cv_` — for shutdown + forced refresh
- `std::atomic<bool> status_running_{false}` — thread lifecycle
- `bool supports_5ghz_cached_` — computed once at `start()`, never changes
- `bool supports_5ghz_resolved_{false}` — flag for one-time init

**Modified methods:**
- `get_status()` → returns `cached_status_` under lock (instant)
- `supports_5ghz()` → returns `supports_5ghz_cached_` (instant)
- `start()` → launches `status_thread_`, computes `supports_5ghz_cached_`
- `stop()` → signals `status_cv_`, joins `status_thread_`

**New method:**
- `status_thread_func()` — polls nmcli every 5 seconds, updates cache
- `refresh_status_now()` — signals `status_cv_` to wake thread for immediate refresh (called after CONNECTED/DISCONNECTED events)

### Polling Interval

- Normal: 5 seconds (matches home panel's existing timer)
- On event (connect/disconnect): immediate refresh via condition_variable notify

### Thread Safety

- `cached_status_` protected by `status_mutex_` (same pattern as `networks_mutex_` for scan results)
- `get_status()` takes shared lock, background thread takes unique lock (or just use regular mutex — contention is negligible)

### No Interface Changes

- `WifiBackend::get_status()` signature unchanged
- `WiFiManager` callers unchanged
- wpa_supplicant and mock backends unchanged

### Error Handling

- If nmcli fails, keep last known status (don't clear to "disconnected")
- Log warnings on repeated failures
- Thread exits cleanly on `stop()` via condition_variable
