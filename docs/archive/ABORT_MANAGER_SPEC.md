# Smart Abort Manager Specification

## Overview

Replace the current "Cancel Print" behavior with a smart escalation system that progressively tries softer abort methods before resorting to M112 emergency stop.

## Problem

- **CANCEL_PRINT** only works if the G-code queue is responsive
- During blocking macros (M109 heat wait, BED_MESH_CALIBRATE), CANCEL_PRINT queues behind the blocked command
- Users can wait 5-10+ minutes before their cancel takes effect
- Current workaround: M112 emergency stop → kills Klipper → requires FIRMWARE_RESTART → loses state

## Solution: Progressive Escalation

```
User presses Cancel
        │
        ▼
┌─────────────────────┐
│ TRY_HEATER_INTERRUPT│  If Kalico status unknown or detected:
│ (1s timeout)        │  Send HEATER_INTERRUPT to interrupt M109/M190 waits
└─────────────────────┘
        │
   ┌────┴────┐
   │         │
Success    Error/Timeout
(Kalico!)   (Not Kalico)
   │         │
   │    remember kalico_status_
   │         │
   ▼         ▼
┌─────────────────────┐
│ PROBE_QUEUE         │  Send M115 via G-code queue
│ (2s timeout)        │  Tests if queue is responsive
└─────────────────────┘
        │
   ┌────┴────┐
   │         │
Response   Timeout
 <2s         │
   │         ▼
   │   ┌─────────────────────┐
   │   │ SENT_ESTOP          │  Queue blocked - send M112
   │   └─────────────────────┘
   │              │
   ▼              ▼
┌─────────────────────┐
│ SENT_CANCEL         │  Queue responsive - send CANCEL_PRINT
│ (3s timeout)        │
└─────────────────────┘
   │
   ┌────┴────┐
Success    Timeout
   │         │
   │         ▼
   │   ┌─────────────────────┐
   │   │ SENT_ESTOP          │  Cancel failed - escalate
   │   │ then SENT_RESTART   │
   │   │ then WAITING_RECONNECT
   │   └─────────────────────┘
   │              │
   ▼              ▼
┌─────────────────────┐
│ COMPLETE            │  Show result, dismiss overlay
│ (modal stays until  │
│  printer ready)     │
└─────────────────────┘
```

## Key Design Decisions

1. **M115 G-code as canary** - Goes through queue, WILL block if queue stuck (that's our detection)
2. **Kalico HEATER_INTERRUPT** - Built-in G-code (NOT a macro!). Detect via runtime probe, not version string
3. **Replace Cancel Print** - Not E-Stop. E-Stop stays as immediate M112 for true emergencies
4. **Modal until ready** - Overlay stays visible until printer is in known good state (prevents user confusion)

## Kalico Detection Strategy

**HEATER_INTERRUPT is a built-in G-code command in Kalico**, not a macro. It cannot be detected via `printer.objects.list` macro enumeration. The `printer.info` `software_version` field also doesn't reliably indicate Kalico.

**Runtime capability probe approach:**
1. On **first abort attempt only** (NEVER at connection time!), send `HEATER_INTERRUPT` G-code
2. If Klipper returns success (empty response) → Kalico detected, remember this
3. If Klipper returns "Unknown command" error → Not Kalico, skip in future aborts
4. Cache result in `AbortManager::kalico_status_` for subsequent abort attempts

**CRITICAL: Do NOT probe at connection time!** The printer is independent of HelixScreen - users may have started a heat-up from another interface (web, console). Sending HEATER_INTERRUPT at startup would unexpectedly abort their operation.

## Files to Create

### `include/abort_manager.h`
```cpp
class AbortManager {
public:
    static AbortManager& instance();
    void init(MoonrakerAPI* api, PrinterState* state);
    void init_subjects();
    void deinit_subjects();
    void start_abort();  // Main entry point
    bool is_aborting() const;

private:
    enum class State {
        IDLE,
        TRY_HEATER_INTERRUPT,  // Kalico soft interrupt (first attempt)
        PROBE_QUEUE,           // M115 canary to test queue responsiveness
        SENT_CANCEL,           // CANCEL_PRINT sent
        SENT_ESTOP,            // M112 sent
        SENT_RESTART,          // FIRMWARE_RESTART sent
        WAITING_RECONNECT      // Waiting for klippy_state == READY
    };
    std::atomic<State> state_{State::IDLE};

    // Kalico detection (runtime probe)
    enum class KalicoStatus { UNKNOWN, DETECTED, NOT_PRESENT };
    KalicoStatus kalico_status_ = KalicoStatus::UNKNOWN;

    // Timeouts
    static constexpr uint32_t HEATER_INTERRUPT_TIMEOUT_MS = 1000;  // Quick probe
    static constexpr uint32_t PROBE_TIMEOUT_MS = 2000;
    static constexpr uint32_t CANCEL_TIMEOUT_MS = 3000;
    static constexpr uint32_t RECONNECT_TIMEOUT_MS = 15000;

    // State machine transitions
    void try_heater_interrupt();   // Kalico: try soft interrupt first
    void start_probe();            // M115 canary
    void send_cancel_print();
    void escalate_to_estop();
    void send_firmware_restart();
    void complete_abort(const char* message);

    // Kalico detection callbacks
    void on_heater_interrupt_success();
    void on_heater_interrupt_error(const MoonrakerError& err);
};
```

### `src/abort/abort_manager.cpp`
Implementation of state machine with:
- LVGL timer-based timeouts
- `ui_async_call()` for thread-safe UI updates
- Observer on `klippy_state` for reconnection detection
- `BusyOverlay` or custom modal for progress display

### `ui_xml/abort_progress_modal.xml`
Modal overlay showing:
- Spinner
- Progress message ("Stopping...", "Emergency Stop...", "Restarting firmware...", "Waiting for printer...")
- Final message ("Stopped - Home before printing" or "Print cancelled")

## Files to Modify

### `src/ui/ui_panel_print_status.cpp`
Change `handle_cancel_button()` to call `AbortManager::instance().start_abort()` instead of direct `CANCEL_PRINT`.

### `src/application/application.cpp`
Initialize AbortManager during startup:
```cpp
AbortManager::instance().init_subjects();
// ... later ...
AbortManager::instance().init(&api_, &printer_state_);
```

## Threading Considerations

- All MoonrakerClient callbacks run on libhv WebSocket thread
- Use `ui_async_call()` to marshal state transitions and UI updates to LVGL thread
- Use `std::atomic<State>` for state variable (safe cross-thread reads)
- LVGL timers run on main thread (no sync needed for timeout callbacks)

## Implementation Approach (per /implement skill)

This is **MAJOR work** (new feature, multiple files, architectural). Follow the protocol:

### Phase 1: Write Failing Tests FIRST
- Create `tests/unit/test_abort_manager.cpp`
- Test state machine transitions
- Test timeout behavior
- Test Kalico detection caching
- Tests must FAIL before implementation

### Phase 2: Implement AbortManager Core
- Delegate to `general-purpose` agent
- Create header and implementation files
- State machine with timer-based timeouts

### Phase 3: UI Integration
- Delegate to `general-purpose` agent
- Wire Cancel button to AbortManager
- Create/adapt abort progress modal

### Phase 4: Code Review
- Mandatory review of all changes
- Check for threading issues, edge cases
- Verify test coverage

### Phase 5: Integration Testing
- Manual testing with mock and real printers
- Test escalation paths

## Test Scenarios

1. **Soft cancel works** - Cancel during normal printing completes quickly
2. **Queue probe works** - M115 returns response within 2s normally
3. **Escalation works** - Cancel during M109 wait escalates to M112 after timeout
4. **Recovery works** - After M112, FIRMWARE_RESTART brings printer back online
5. **Kalico path** - If HEATER_INTERRUPT available, it's tried first
6. **UI stays until ready** - Modal doesn't dismiss until klippy_state == READY
7. **Kalico caching** - Second abort skips probe if status already known
8. **No connection-time probe** - HEATER_INTERRUPT never sent at startup

## Critical Files Reference

| File | Purpose |
|------|---------|
| `include/ui_emergency_stop.h` | Pattern: singleton with subjects, observers |
| `src/ui/ui_busy_overlay.cpp` | Pattern: LVGL timer-based overlays |
| `include/moonraker_client.h` | `send_jsonrpc()` with timeout callbacks |
| `include/ui_update_queue.h` | `ui_async_call()` thread marshaling |
| `src/ui/ui_panel_print_status.cpp:1018` | Current `handle_cancel_button()` |

## Sources

- [Kalico Documentation - Kalico Additions](https://docs.kalico.gg/Kalico_Additions.html) - HEATER_INTERRUPT command info
- [Danger Klipper G-Codes](https://dangerklipper.io/G-Codes.html) - Built-in commands
- [Moonraker Printer Admin API](https://moonraker.readthedocs.io/en/latest/external_api/printer/) - printer.info endpoint
