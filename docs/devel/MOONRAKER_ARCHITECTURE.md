# Moonraker Layer Architecture

This document describes the architecture of the Moonraker integration layer.

> **Last Updated:** 2026-02-10

## Overview

The Moonraker integration is split into three distinct layers with clean separation of concerns:

```
┌─────────────────────────────────────────────────────────────────┐
│                         UI Panels                                │
│  (BedMeshPanel, Wizards, etc.)                                  │
└─────────────────────────────┬───────────────────────────────────┘
                              │ uses
┌─────────────────────────────▼───────────────────────────────────┐
│                      MoonrakerAPI                                │
│  (Domain Logic Layer)                                           │
│  ├─ PrinterDiscovery (SINGLE SOURCE OF TRUTH)           │
│  │   ├─ heaters, fans, sensors, leds                            │
│  │   ├─ macros, hostname, printer_info                          │
│  │   └─ capability queries (has_qgl, has_probe, etc.)           │
│  ├─ Bed mesh data (active_bed_mesh_, bed_mesh_profiles_)        │
│  ├─ Hardware guessing (guess_bed_heater, guess_hotend_sensor)   │
│  ├─ G-code execution (execute_gcode)                            │
│  ├─ Temperature control (set_temperature)                       │
│  └─ Object exclusion (get_excluded_objects, get_available_objects)│
└─────────────────────────────┬───────────────────────────────────┘
                              │ uses (JSON-RPC calls)
                              │ receives (discovery callbacks)
┌─────────────────────────────▼───────────────────────────────────┐
│                    MoonrakerClient                               │
│  (Transport Layer)                                               │
│  ├─ WebSocket connection/reconnection                           │
│  ├─ JSON-RPC 2.0 request/response handling                      │
│  ├─ Event emission (CONNECTION_FAILED, KLIPPY_DISCONNECTED...)  │
│  ├─ Printer state subscriptions                                 │
│  └─ Dispatches discovery data via callbacks (NO storage)        │
└─────────────────────────────────────────────────────────────────┘
```

**Key Architectural Principle:** MoonrakerClient is pure transport. It does NOT store hardware data. All discovered hardware information flows via callbacks to MoonrakerAPI, which owns the `PrinterDiscovery` instance as the single source of truth.

**Abstraction Boundary (enforced Feb 2026):** UI code should ONLY talk to `MoonrakerAPI`, never `MoonrakerClient` directly. The API provides proxy methods for connection state, subscriptions, database operations, and plugin RPCs. The dead `IMoonrakerDomainService` interface has been deleted; shared data types live in `moonraker_types.h`.

## Layer Responsibilities

### MoonrakerClient (Transport Layer)

**Location:** `include/moonraker_client.h`, `src/api/moonraker_client.cpp` (+ mock split files)

**Responsibilities:**
- WebSocket connection management (connect, disconnect, reconnect)
- JSON-RPC 2.0 protocol handling
- Request timeout management
- Event emission for transport events
- Printer state subscriptions (`register_notify_update()`)
- **Dispatches discovery data via callbacks** (heaters, fans, sensors, LEDs, macros, hostname, printer info, bed mesh)

**Does NOT do:**
- UI notifications (replaced with event emission)
- Business logic decisions
- Hardware "guessing" logic
- **Store hardware discovery data** (moved to MoonrakerAPI)

### MoonrakerAPI (Domain Logic Layer)

**Location:** `include/moonraker_api.h`, `src/api/moonraker_api.cpp`

**Responsibilities:**
- **Owns `PrinterDiscovery`** - single source of truth for all hardware info
- **Owns bed mesh data** (`active_bed_mesh_`, `bed_mesh_profiles_`)
- Hardware guessing (`guess_bed_heater()`, `guess_hotend_sensor()`, etc.)
- G-code command execution
- Temperature control
- Object exclusion state queries
- Print control (pause, resume, cancel)

### PrinterDiscovery (Hardware Data)

**Location:** `include/printer_discovery.h`, `src/printer/printer_discovery.cpp`

**Responsibilities:**
- Store discovered hardware: heaters, fans, sensors, LEDs
- Store macros and capability flags (has_qgl, has_probe, etc.)
- Store printer metadata: hostname, klipper_version, mcu_version
- Provide unified query interface for all hardware capabilities

**Accessed via:** `MoonrakerAPI::hardware_discovery()`

**Note:** `PrinterCapabilities` class has been DELETED. All its functionality is now in `PrinterDiscovery`.

**Key Pattern:** All async methods use callback pattern:
```cpp
void method_name(
    std::function<void(ResultType)> on_success,
    ErrorCallback on_error
);
```

### Event System

**Location:** `include/moonraker_events.h`

The event system decouples the transport layer from the UI:

```cpp
enum class MoonrakerEventType {
    CONNECTION_FAILED,    // Max reconnect attempts exceeded
    CONNECTION_LOST,      // WebSocket closed unexpectedly
    RECONNECTING,         // Attempting to reconnect
    RECONNECTED,          // Successfully reconnected
    MESSAGE_OVERSIZED,    // Received message exceeds size limit
    RPC_ERROR,            // JSON-RPC request failed
    KLIPPY_DISCONNECTED,  // Klipper firmware disconnected
    KLIPPY_READY,         // Klipper firmware ready
    DISCOVERY_FAILED,     // Printer discovery failed
    REQUEST_TIMEOUT       // JSON-RPC request timed out
};

struct MoonrakerEvent {
    MoonrakerEventType type;
    std::string message;
    std::string details;
    bool is_error;
};
```

**Usage:**
```cpp
// Register handler in main.cpp
moonraker_client->register_event_handler([](const MoonrakerEvent& evt) {
    if (evt.is_error) {
        ui_notification_error(title, evt.message.c_str(), true);
    } else {
        ui_notification_warning(evt.message.c_str());
    }
});
```

## Mock Architecture

For testing, parallel mock implementations exist:

```
┌─────────────────────────────────────────────────────────────────┐
│                      MockPrinterState                            │
│  (Shared State - tests/mocks/mock_printer_state.h)              │
│  ├─ Temperatures (atomic<double>)                               │
│  ├─ Print state                                                 │
│  ├─ Excluded objects (mutex-protected set)                      │
│  └─ Available objects                                           │
└─────────────────────┬─────────────────┬─────────────────────────┘
                      │                 │
┌─────────────────────▼───────┐  ┌─────▼─────────────────────────┐
│   MoonrakerClientMock       │  │    MoonrakerAPIMock           │
│   (Transport Mock)          │  │    (Domain Mock)              │
│   ├─ Simulated temps        │  │    ├─ Local file downloads    │
│   ├─ EXCLUDE_OBJECT parsing │  │    ├─ Mock uploads            │
│   └─ Synthetic bed mesh     │  │    └─ Shared state access     │
└─────────────────────────────┘  └─────────────────────────────────┘
```

### MockPrinterState

Thread-safe shared state between mock implementations:

```cpp
auto state = std::make_shared<MockPrinterState>();

MoonrakerClientMock client(PrinterType::VORON_24);
client.set_mock_state(state);

MoonrakerAPIMock api(client, printer_state);
api.set_mock_state(state);

// Now excluded objects sync between mocks
client.gcode_script("EXCLUDE_OBJECT NAME=Part_1", ...);
// api.get_excluded_objects_from_mock() returns {"Part_1"}
```

## Removed/Migrated Methods

The following methods have been removed from `MoonrakerClient` and are now in `MoonrakerAPI`:

| Removed from MoonrakerClient | Now in MoonrakerAPI |
|------------------------------|---------------------|
| `get_heaters()` | `hardware_discovery().heaters()` |
| `get_fans()` | `hardware_discovery().fans()` |
| `get_sensors()` | `hardware_discovery().sensors()` |
| `get_leds()` | `hardware_discovery().leds()` |
| `get_hostname()` | `hardware_discovery().hostname()` |
| `get_active_bed_mesh()` | `get_active_bed_mesh()` |
| `get_bed_mesh_profiles()` | `get_bed_mesh_profiles()` |
| `has_bed_mesh()` | `has_bed_mesh()` |
| `guess_bed_heater()` | `guess_bed_heater()` |
| `guess_hotend_heater()` | `guess_hotend_heater()` |
| `guess_bed_sensor()` | `guess_bed_sensor()` |
| `guess_hotend_sensor()` | `guess_hotend_sensor()` |

### Deleted Classes

| Deleted Class | Replacement |
|---------------|-------------|
| `PrinterCapabilities` | `PrinterDiscovery` (accessed via `MoonrakerAPI::hardware_discovery()`) |

**Migration:** Replace `PrinterCapabilities` usage with `api->hardware_discovery().has_qgl()`, `api->hardware_discovery().macros()`, etc.

## Key Differences: API vs Client

| Aspect | MoonrakerAPI | MoonrakerClient |
|--------|--------------|-----------------|
| **Purpose** | Domain logic + data ownership | Transport only |
| **Hardware data** | Owns `PrinterDiscovery` | Dispatches via callbacks |
| **Bed mesh** | Owns `active_bed_mesh_`, `bed_mesh_profiles_` | Dispatches via callbacks |
| Return types | Pointers (nullable) | N/A for hardware data |
| G-code | `execute_gcode()` (async) | `gcode_script()` (sync-ish) |
| Connection state | `is_connected()`, `get_connection_state()` | Internal state |
| Subscriptions | `subscribe_notifications()`, `register_method_callback()` | Direct registration |
| Database | `database_get_item()`, `database_post_item()` | Raw `send_jsonrpc()` |
| Plugin RPCs | `get_phase_tracking_status()`, `set_phase_tracking_enabled()` | Raw `send_jsonrpc()` |
| Thread safety | Delegates to client | Internal mutexes |
| UI coupling | None | None (events only) |

## Testing

### Running Moonraker Tests

```bash
# All moonraker-related tests
./build/bin/helix-tests "[moonraker]"

# Just event tests
./build/bin/helix-tests "[events]"

# Integration tests
./build/bin/helix-tests "[integration]"

# Domain method parity tests
./build/bin/helix-tests "[domain]"

# Shared state tests
./build/bin/helix-tests "[shared_state]"
```

### Test Coverage

| Test File | Focus |
|-----------|-------|
| `test_moonraker_events.cpp` | Event emission, handler registration |
| `test_moonraker_api_domain.cpp` | Domain method parity (API vs Client) |
| `test_moonraker_mock_behavior.cpp` | Mock client behavior |
| `test_mock_shared_state.cpp` | MockPrinterState thread safety |
| `test_moonraker_full_stack.cpp` | Integration across all layers |

## File Reference

### Headers

| File | Purpose |
|------|---------|
| `include/moonraker_client.h` | Transport layer (WebSocket, JSON-RPC) |
| `include/moonraker_api.h` | Domain logic layer |
| `include/moonraker_events.h` | Event types and callbacks |
| `include/moonraker_types.h` | Shared data types (BedMeshProfile, GcodeStoreEntry, etc.) |
| `include/moonraker_client_mock.h` | Transport layer mock |
| `include/moonraker_api_mock.h` | Domain layer mock |

### Sources

| File | Purpose |
|------|---------|
| `src/api/moonraker_client.cpp` | Transport implementation |
| `src/api/moonraker_api.cpp` | Domain logic (+ `moonraker_api_*.cpp` splits) |
| `src/api/moonraker_client_mock.cpp` | Mock transport (+ `moonraker_client_mock_*.cpp` splits) |
| `src/api/moonraker_api_mock.cpp` | Mock domain (local file access) |
| `src/application/moonraker_manager.cpp` | Lifecycle wiring, profile loading, observer setup |

### Test Files

| File | Purpose |
|------|---------|
| `tests/mocks/mock_printer_state.h` | Shared mock state |
| `tests/unit/test_moonraker_*.cpp` | Unit tests |
| `tests/unit/test_mock_shared_state.cpp` | Shared state tests |
| `tests/unit/test_moonraker_full_stack.cpp` | Integration tests |

## Migration Guide

### Migrating from MoonrakerClient to MoonrakerAPI

**Before (OLD - no longer works):**
```cpp
MoonrakerClient* client = get_moonraker_client();
std::string bed_heater = client->guess_bed_heater();  // REMOVED
const std::vector<std::string>& heaters = client->get_heaters();  // REMOVED
const BedMeshProfile& mesh = client->get_active_bed_mesh();  // REMOVED
```

**After (CURRENT):**
```cpp
MoonrakerAPI* api = get_moonraker_api();
std::string bed_heater = api->guess_bed_heater();
const std::vector<std::string>& heaters = api->hardware_discovery().heaters();
const BedMeshProfile* mesh = api->get_active_bed_mesh();  // Note: pointer!
if (mesh) {
    // Use mesh data
}
```

### Migrating from PrinterCapabilities

**Before (OLD - class deleted):**
```cpp
PrinterCapabilities caps;
caps.parse_objects(printer_objects);
bool has_qgl = caps.has_qgl();
const auto& macros = caps.macros();
```

**After (CURRENT):**
```cpp
MoonrakerAPI* api = get_moonraker_api();
bool has_qgl = api->hardware_discovery().has_qgl();
const auto& macros = api->hardware_discovery().macros();
```

### Migrating from get_client() / get_moonraker_client()

**Before (OLD - violates abstraction boundary):**
```cpp
// Connection state
api->get_client().get_connection_state();
api->get_client().get_last_url();

// Subscriptions
api->get_client().register_method_callback("notify_gcode_response", "panel", cb);
api->get_client().register_notify_update(cb);

// Database
client->send_jsonrpc("server.database.get_item", params, on_result, on_error);

// G-code
client->gcode_script("TURN_OFF_HEATERS");

// Disconnect modal
client->suppress_disconnect_modal(15000);
```

**After (CURRENT):**
```cpp
// Connection state — use API proxies
api->get_connection_state();
api->get_websocket_url();

// Subscriptions — use API proxies
api->register_method_callback("notify_gcode_response", "panel", cb);
api->subscribe_notifications(cb);

// Database — use typed wrappers
api->database_get_item("helix", "spoolman_enabled", on_success, on_error);

// G-code — use validated path
api->execute_gcode("TURN_OFF_HEATERS", nullptr, nullptr);

// Disconnect modal — use API proxy
api->suppress_disconnect_modal(15000);
```

### Key Changes

1. **Hardware data:** All hardware queries go through `api->hardware_discovery()`
2. **Bed mesh:** Owned by MoonrakerAPI, accessed via `api->get_active_bed_mesh()`
3. **Null checks:** `MoonrakerAPI` returns pointers for bed mesh, not references
4. **PrinterCapabilities deleted:** Use `PrinterDiscovery` via `api->hardware_discovery()`
5. **Global accessor:** Use `get_moonraker_api()` for domain operations
6. **IMoonrakerDomainService deleted:** `BedMeshProfile` and `GcodeStoreEntry` now in `moonraker_types.h`
7. **UI abstraction boundary:** UI code uses API proxy methods, never `get_client()` or `get_moonraker_client()`

## Multi-Tool & Multi-Extruder Subscriptions

When `PrinterDiscovery` detects multiple extruders or a tool changer, additional Moonraker subscription keys are registered:

### Extruder Subscriptions

For each discovered extruder (`"extruder"`, `"extruder1"`, etc.), the status subscription includes:

```
extruder: temperature, target
extruder1: temperature, target
...
```

`PrinterTemperatureState::update_from_status()` iterates its `extruders_` map and updates per-extruder subjects for any matching keys in the status JSON.

### Toolchanger Subscriptions

When a tool changer is detected, the subscription includes:

```
toolchanger: tool_number
tool T0: active, mounted, detect_state, gcode_x_offset, gcode_y_offset, gcode_z_offset, extruder, fan
tool T1: active, mounted, detect_state, ...
```

`ToolState::update_from_status()` parses these keys and bumps the `tools_version` subject on changes.

### Multi-Backend AMS

Each AMS backend manages its own Moonraker subscriptions independently. When `AmsState::init_backends_from_hardware()` creates multiple backends, each backend registers its own WebSocket subscriptions for its object types (e.g., `mmu` for Happy Hare, `AFC` for AFC, `tool T*` for tool changers). Backend state changes are synced to per-backend slot subjects via `AmsState::sync_backend()`.

**Full docs:** [TOOL_ABSTRACTION.md](TOOL_ABSTRACTION.md), [MULTI_EXTRUDER_TEMPERATURE.md](MULTI_EXTRUDER_TEMPERATURE.md), [FILAMENT_MANAGEMENT.md](FILAMENT_MANAGEMENT.md)

---

## See Also

- `docs/TESTING_MOONRAKER_API.md` - Manual testing procedures
- `docs/TESTING.md` - General testing guide
- `include/moonraker_api.h` - Full API documentation (Doxygen)
