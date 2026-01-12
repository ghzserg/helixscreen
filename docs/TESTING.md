# Testing Infrastructure

**Status:** Active
**Last Updated:** 2026-01-11

---

## Quick Start

```bash
make test              # Build tests (does not run)
make test-run          # Run unit tests in parallel (~4-8x faster)
make test-fast         # Skip [slow] tests
make test-serial       # Sequential (for debugging)
make test-all          # Everything including [slow]

# Run specific tests
./build/bin/helix-tests "[connection]" "~[.]"
```

**⚠️ Always use `"~[.]"` when running by tag** to exclude hidden tests that may hang.

---

## Test Tag System

Tests are tagged by **feature/importance**, not layer/speed. This enables running all tests for a feature during development and identifying critical tests.

### Importance Tags

| Tag | Count | Purpose |
|-----|-------|---------|
| `[core]` | ~18 | Critical tests - if these fail, the app is fundamentally broken |
| `[slow]` | ~185 | Tests with network/timing - excluded from `test-run` |
| `[eventloop]` | ~54 | Uses `hv::EventLoop` - very slow, always paired with `[slow]` |

### Feature Tags

| Tag | Count | Purpose |
|-----|-------|---------|
| `[connection]` | ~70 | WebSocket connection lifecycle, retry logic |
| `[state]` | ~60 | PrinterState singleton, LVGL subjects, observers |
| `[print]` | ~46 | Print workflow: start, pause, cancel, progress |
| `[api]` | ~79 | Moonraker API infrastructure |
| `[calibration]` | ~27 | Bed mesh, input shaper, QGL, Z-tilt |
| `[printer]` | ~130 | Printer detection, capabilities, hardware |
| `[ams]` | ~67 | AMS/MMU backends |
| `[filament]` | ~32 | Spoolman, filament sensors |
| `[network]` | ~25 | WiFi, Ethernet management |
| `[assets]` | ~28 | Thumbnail extraction |
| `[ui]` | ~138 | Theme, icons, widgets, panels |
| `[gcode]` | ~125 | G-code parsing, streaming, geometry |
| `[config]` | ~64 | Configuration loading, validation |
| `[wizard]` | ~29 | Setup wizard flow |
| `[history]` | ~24 | Print/notification history |
| `[application]` | ~40 | Application lifecycle |

### Sub-Tags

| Tag | Parent | Purpose |
|-----|--------|---------|
| `[afc]` | `[ams]` | AFC (Armored Filament Changer) backend |
| `[valgace]` | `[ams]` | Valgace AMS backend |
| `[ui_theme]` | `[ui]` | Theme colors, fonts |
| `[ui_icon]` | `[ui]` | Icon rendering |
| `[navigation]` | `[ui]` | Panel switching |

### Hidden Tags (Excluded by Default)

- `[.pending]` - Test not yet implemented
- `[.integration]` - Requires full environment
- `[.slow]` - Long-running (deprecated, use `[slow]`)
- `[.disabled]` - Temporarily disabled

Run `./build/bin/helix-tests "[.]" --list-tests` to see all hidden tests.

---

## Core Tests (~18 Must Pass)

These validate fundamental functionality:

**PrinterState** (`test_printer_state.cpp`): Singleton instance, persistence, subject addresses, observer notifications

**Navigation** (`test_navigation.cpp`): Initialization, panel switching, invalid panel handling, all panels accessible

**Config** (`test_config.cpp`): get() for string/int values, missing key handling, defaults

**Print Start** (`test_print_start_collector.cpp`): PRINT_START marker, completion marker, homing/heating phase detection

**UI** (`test_ui_temp_graph.cpp`): Graph create/destroy

---

## Make Targets

### By Speed/Scope

| Target | Behavior |
|--------|----------|
| `make test-run` | Parallel, excludes `[slow]` and hidden |
| `make test-fast` | Same as test-run |
| `make test-all` | Parallel, includes `[slow]` |
| `make test-slow` | Only `[slow]` tagged tests |
| `make test-eventloop` | Only `[eventloop]` tests (5-10 min) |
| `make test-serial` | Sequential for debugging |
| `make test-verbose` | Sequential with timing |

### By Feature

| Target | Tags |
|--------|------|
| `make test-core` | `[core]` |
| `make test-connection` | `[connection]` |
| `make test-state` | `[state]` |
| `make test-print` | `[print]` |
| `make test-gcode` | `[gcode]` |
| `make test-moonraker` | `[api]` |
| `make test-ui` | `[ui]` |
| `make test-network` | `[network]` |
| `make test-ams` | `[ams]` |
| `make test-calibration` | `[calibration]` |
| `make test-filament` | `[filament]` |
| `make test-security` | `[security]` |

### Sanitizers

| Target | Purpose |
|--------|---------|
| `make test-asan` | AddressSanitizer (memory leaks, use-after-free, overflows) |
| `make test-tsan` | ThreadSanitizer (data races, deadlocks) |
| `make test-asan-one TEST="[tag]"` | Run specific test with ASAN |
| `make test-tsan-one TEST="[tag]"` | Run specific test with TSAN |

Sanitizers add ~2-5x overhead. Use for debugging, not regular runs.

---

## Parallel Execution

Tests run in parallel by default using Catch2's sharding. Each shard runs in a separate process with its own LVGL instance.

```bash
# What make test-run does internally:
for i in $(seq 0 $((NPROCS-1))); do
    ./build/bin/helix-tests "~[.] ~[slow]" --shard-count $NPROCS --shard-index $i &
done
wait
```

| Machine | Serial | Parallel | Speedup |
|---------|--------|----------|---------|
| 4 cores | ~100s | ~30s | ~3.5x |
| 8 cores | ~100s | ~18s | ~6x |
| 14 cores | ~100s | ~12s | ~9x |

Use `make test-serial` when debugging failures or reading output.

---

## Excluded Tests Breakdown

The default `make test-run` uses filter `~[.] ~[slow]` to exclude tests that would slow down fast iteration. Here's what's excluded:

### Test Count Summary

| Category | Count | % of Total |
|----------|------:|------------|
| **Total tests** | ~1,441 | 100% |
| **Fast tests** (default run) | ~1,263 | 87.6% |
| **Slow tests** `[slow]` | ~185 | 12.8% |
| **Hidden tests** `[.]` | ~57 | 4.0% |

*Note: Some overlap exists between [slow] and [.]*

### Hidden Tests `[.]` (~57 tests)

Hidden tests never run automatically. They require explicit invocation.

| Category | Count | Purpose |
|----------|------:|---------|
| `[.][application][integration]` | ~15 | Full app integration tests |
| `[.][xml_required]` | ~25 | UI tests needing XML components |
| `[.][ui_integration]` | ~6 | Full LVGL UI integration |
| `[.][disabled]` | ~4 | Known broken (macOS WiFi, etc.) |
| `[.][stress]` | ~2 | Stress/threading tests |

### Slow Tests `[slow]` (~185 tests)

Slow tests are excluded from `test-run` but can be run with `make test-slow`.

| File | Count | Why Slow |
|------|------:|----------|
| `test_print_history_api.cpp` | 18 | History database operations |
| `test_moonraker_client_subscription_cancel.cpp` | 17 | WebSocket event loops |
| `test_moonraker_client_security.cpp` | 14 | Security test fixtures |
| `test_moonraker_client_robustness.cpp` | 14 | Concurrent access tests |
| `test_notification_history.cpp` | 13 | History/persistence |
| `test_moonraker_mock_behavior.cpp` | 12 | Mock client simulation |
| `test_gcode_streaming_controller.cpp` | 12 | Layer processing loops |
| `test_moonraker_events.cpp` | 11 | Event dispatch timing |
| `test_printer_hardware.cpp` | 10 | Hardware detection |
| `test_spoolman.cpp` | 9 | Spoolman API calls |
| Other (16 files) | ~55 | Various timing/network tests |

**When to add `[slow]`:**
- Test creates `hv::EventLoop` (network operations) - also add `[eventloop]`
- Test uses `std::this_thread::sleep_for()` for timing
- Test uses fixtures with network clients (e.g., `MoonrakerClientSecurityFixture`)
- Test takes >500ms to complete

**When to add `[eventloop]`:**
- Test creates `hv::EventLoop` for WebSocket operations
- Test requires real network connection/disconnection cycles
- ALWAYS add `[slow]` alongside `[eventloop]` - eventloop tests are inherently slow

### Disabled Tests (#if 0)

These tests are completely disabled due to known issues:

| File | Line | Reason |
|------|------|--------|
| `test_moonraker_client_robustness.cpp` | 555 | `send_jsonrpc` returns -1 instead of 0 when disconnected |
| `test_moonraker_client_security.cpp` | 690 | Segmentation fault (object lifetime issues) |

### Running Excluded Tests

```bash
# Run slow tests only
make test-slow

# Run all tests (slow + fast, but not hidden)
make test-all

# Run specific hidden tests
./build/bin/helix-tests "[.][application][integration]"

# List all hidden tests
./build/bin/helix-tests "[.]" --list-tests

# List all slow tests
./build/bin/helix-tests "[slow]" --list-tests
```

---

## Test Timing Categories

Tests fall into three timing categories based on their execution characteristics. Understanding these helps plan CI/CD pipelines and local development workflows.

### Fast Tests (~1,400 tests, ~27s parallel)

The majority of tests complete quickly and are suitable for rapid iteration during development.

```bash
make test-run    # Default: runs fast tests in parallel shards
```

**Characteristics:**
- No network operations or event loops
- Pure logic, parsing, state management
- Typical test: <100ms

### Slow Non-EventLoop Tests (~52 tests)

Tests marked `[slow]` that do NOT use `hv::EventLoop`. These are slow due to deliberate delays, database operations, or simulation work.

```bash
make test-slow   # Run only [slow] tagged tests
```

**Why slow:**
- `std::this_thread::sleep_for()` for timing tests
- Database/history operations (SQLite)
- Mock print simulation with phase transitions

| File | Count | Reason |
|------|------:|--------|
| `test_print_history_api.cpp` | 18 | SQLite operations |
| `test_notification_history.cpp` | 13 | History persistence |
| `test_moonraker_mock_behavior.cpp` | 12 | Mock simulation delays |
| `test_gcode_streaming_controller.cpp` | 12 | Layer processing |

### EventLoop Tests (~54 tests, 5-10 min total)

Tests using `hv::EventLoop` for real network operations. These are the slowest tests and are tagged with BOTH `[eventloop]` AND `[slow]`.

```bash
# Run eventloop tests specifically
./build/bin/helix-tests "[eventloop]" "~[.]"

# These are already excluded by make test-run (via ~[slow])
```

**Why very slow:**
- Real WebSocket connection/disconnection cycles
- Network timeout waiting (1-5 seconds per test)
- Event loop startup/shutdown overhead
- Thread synchronization

| File | Count | Tests |
|------|------:|-------|
| `test_moonraker_client_subscription_cancel.cpp` | 17 | Subscription lifecycle |
| `test_moonraker_client_robustness.cpp` | 14 | Edge cases, concurrent access |
| `test_moonraker_client_security.cpp` | 14 | Security validation |
| `test_print_preparation_manager.cpp` | 6 | Print preparation retry |
| `test_moonraker_api_security.cpp` | 2 | API lifecycle |
| `test_moonraker_connection_retry.cpp` | 1 | Connection retry logic |

**Important:** All `[eventloop]` tests MUST also be tagged `[slow]` to ensure they are excluded from `make test-run`.

### Test Execution Matrix

| Command | Fast | Slow (non-eventloop) | EventLoop | Hidden |
|---------|:----:|:--------------------:|:---------:|:------:|
| `make test-run` | Yes | No | No | No |
| `make test-fast` | Yes | No | No | No |
| `make test-slow` | No | Yes | Yes | No |
| `make test-all` | Yes | Yes | Yes | No |
| `[eventloop]` | No | No | Yes | No |

---

## Test Organization

```
tests/
├── catch_amalgamated.hpp/.cpp  # Catch2 v3 amalgamated
├── test_main.cpp               # Test runner entry
├── ui_test_utils.h/.cpp        # UI testing utilities
├── unit/                       # Unit tests (real LVGL)
│   ├── test_config.cpp
│   ├── test_gcode_parser.cpp
│   └── ...
├── integration/                # Integration tests (mocks)
│   └── test_mock_example.cpp
└── mocks/                      # Mock implementations
    ├── mock_lvgl.cpp
    └── mock_moonraker_client.cpp

experimental/src/              # Standalone test binaries
```

---

## Writing Tests

### Catch2 v3 Basics

```cpp
#include "your_module.h"
#include "../catch_amalgamated.hpp"

using Catch::Approx;

TEST_CASE("Component - Feature", "[component][feature]") {
    SECTION("Scenario one") {
        REQUIRE(result == expected);
    }
    SECTION("Scenario two") {
        REQUIRE(value == Approx(3.14).epsilon(0.01));
    }
}
```

**Assertions:** `REQUIRE()` (stops on failure), `CHECK()` (continues), `REQUIRE_FALSE()`

**Skipping:** `if (!condition) { SKIP("Reason"); }`

**Logging:** `INFO("Parsed " << count << " items");`

### Adding New Tests

1. Create file in `tests/unit/test_<module>.cpp`
2. **Always add a feature tag** - What functional area?
3. **Add `[core]` if critical** - Would the app break without this?
4. **Add `[slow]` if >500ms** - Keeps fast iteration fast

```cpp
// Good: Feature + importance
TEST_CASE("PrinterState observer cleanup", "[core][state]")

// Good: Feature + speed
TEST_CASE("Connection retry 5s timeout", "[connection][slow]")

// Bad: No feature context
TEST_CASE("Some test", "[unit]")
```

The Makefile auto-discovers test files in `tests/unit/` and `tests/integration/`.

---

## Mocking Infrastructure

### MoonrakerClientMock

```cpp
#include "tests/mocks/moonraker_client_mock.h"

MoonrakerClientMock client;
client.connect(url, on_connected, on_disconnected);
client.trigger_connected();   // Fire callback
client.get_rpc_methods();     // Verify calls made
client.reset();               // Reset for next test
```

### Available Mocks

- **MoonrakerClientMock:** WebSocket simulation
- **MockLVGL:** Minimal LVGL stubs for integration tests
- **MockPrintFiles:** Filesystem operations

---

## UI Testing Utilities

```cpp
#include "../ui_test_utils.h"

void setup_lvgl_for_testing();
lv_display_t* create_test_display(int width, int height);
void simulate_click(lv_obj_t* obj);
void simulate_swipe(lv_obj_t* obj, lv_dir_t direction);
```

---

## Gotchas

### LVGL Observer Auto-Notification

`lv_subject_add_observer()` immediately fires the callback with current value:

```cpp
lv_subject_add_observer(subject, callback, &count);
REQUIRE(count == 1);  // Fired immediately!

state.set_value(new_value);
REQUIRE(count == 2);  // Fired again on change
```

### Hidden Tests Hang

Always use `"~[.]"` when running by tag:

```bash
# ✅ Correct
./build/bin/helix-tests "[application]" "~[.]"

# ❌ May hang on hidden tests
./build/bin/helix-tests "[application]"
```

### Common Issues

| Issue | Solution |
|-------|----------|
| Catch2 header not found | Use `#include "../catch_amalgamated.hpp"` |
| Approx not found | Add `using Catch::Approx;` |
| Test won't link | Check .o files in Makefile test link command |
| LVGL undefined in integration | Use mocks, not real LVGL |

---

## Debugging

```bash
# Run specific test case
./build/bin/helix-tests "Test case name"

# List all tests matching tag
./build/bin/helix-tests --list-tests "[connection]"

# Verbose output
./build/bin/helix-tests -s -v high

# In debugger
lldb build/bin/helix-tests
(lldb) run "[gcode]"
```

---

## Related Documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md):** Thread safety patterns
- **[BUILD_SYSTEM.md](BUILD_SYSTEM.md):** Build configuration
- **[DEVELOPMENT.md#contributing](DEVELOPMENT.md#contributing):** Code standards
