# Phase 6: Real AFC Backend Integration Plan

> **STATUS: SUPERSEDED** — Completed 2026-02-07 as part of the Comprehensive AFC Support work.
> See commits `103bf15e` through `4478332f` on main. All sensor parsing, device actions,
> error recovery, and mock mode are implemented. Multi-unit support (Phase 6 of the new plan)
> is deferred pending multi-unit hardware for testing.

## Goal
Parse real sensor data from BoxTurtle/AFC Lite via Moonraker to drive the filament path visualization.

## Current State
- Path visualization widget works with mock data (segments animate during load/unload)
- AFC backend exists but sensor states (`prep_sensor_`, `hub_sensor_`, `toolhead_sensor_`) are never populated
- Thread-safety issues have been resolved
- Test hardware at 192.168.1.112 runs **AFC v1.0.0** (current release is **v1.0.32**)

## AFC Version Differences

| Feature | v1.0.0 (installed) | v1.0.32 (latest) |
|---------|-------------------|------------------|
| Version in `afc-install` namespace | ✅ `{"version": "1.0.0"}` | ✅ `{"version": "1.0.32"}` |
| `AFC_stepper lane*` objects | ✅ Full sensor data | ✅ Same |
| `lane_data` in Moonraker DB | ❌ Not available | ✅ Added for third-party access |
| TD1 sensor support | ❌ | ✅ |
| Auto-level during home | ❌ | ✅ |

**Key insight:** The core data we need (`AFC_stepper lane*` with `prep`, `load`, `loaded_to_hub`, `tool_loaded`) is available in **all versions**. The `lane_data` database is a convenience feature added in v1.0.32 but not required for our implementation.

## Real AFC Data Structure (from 192.168.1.112)

### Primary Data Sources
1. **`AFC_stepper lane{N}`** - Per-lane status with sensor states:
   - `prep` (bool) - Prep sensor triggered
   - `load` (bool) - Load sensor triggered
   - `loaded_to_hub` (bool) - Filament reached hub
   - `tool_loaded` (bool) - Filament loaded to toolhead
   - `status` ("Loaded"/"None"/"Ready") - Lane status
   - `color` ("#RRGGBB") - Filament color from Spoolman
   - `material`, `spool_id`, `weight` - Spoolman integration

2. **`AFC_hub Turtle_1`** - Hub state:
   - `state` (bool) - Hub sensor triggered

3. **`AFC_extruder extruder`** - Toolhead sensors:
   - `tool_start_status` (bool) - Toolhead entry sensor
   - `tool_end_status` (bool) - Toolhead exit/nozzle sensor
   - `lane_loaded` - Currently loaded lane name

4. **`AFC`** - Global state:
   - `current_lane` - Active lane name (or null)
   - `current_state` - "Idle", "Loading", "Unloading", etc.
   - `error_state` (bool)
   - `lanes[]` - List of lane names

### Sensor → PathSegment Mapping

```
No sensors            → NONE
prep only             → SPOOL (filament present but not advanced)
prep + load           → PREP (past prep sensor)
prep + load + hub     → LANE → HUB (approaching or at hub)
+ tool_start          → OUTPUT → TOOLHEAD (in bowden)
+ tool_end            → NOZZLE (fully loaded)
```

## Implementation Tasks

### Task 0: Detect AFC Version and Capabilities
**File:** `src/ams_backend_afc.cpp`

Query the `afc-install` database namespace on connect to determine version and capabilities:

```cpp
void AmsBackendAfc::detect_afc_version() {
    // Query: server.database.get_item namespace="afc-install"
    client_->send_jsonrpc("server.database.get_item",
        {{"namespace", "afc-install"}},
        [this](const json& response) {
            if (response.contains("value") && response["value"].contains("version")) {
                afc_version_ = response["value"]["version"].get<std::string>();
                parse_version_capabilities();
                spdlog::info("[AMS AFC] Detected AFC version: {}", afc_version_);
            }
        },
        [this](const MoonrakerError& err) {
            spdlog::warn("[AMS AFC] Could not detect AFC version: {}", err.message);
            afc_version_ = "unknown";
        }
    );
}

void AmsBackendAfc::parse_version_capabilities() {
    // Parse semantic version and set capability flags
    // v1.0.32+ has lane_data database
    // v1.0.32+ has TD1 sensor support
    has_lane_data_db_ = version_at_least("1.0.32");
    has_td1_support_ = version_at_least("1.0.32");
}
```

Store version info in `AmsSystemInfo` for UI display:
```cpp
struct AfcCapabilities {
    std::string version;          // "1.0.0", "1.0.32", etc.
    bool has_lane_data_db;        // lane_data in Moonraker DB
    bool has_td1_support;         // TD1 inline scanner
    bool has_auto_level;          // Auto-level during home
};
```

### Task 1: Subscribe to AFC Objects
**File:** `src/ams_backend_afc.cpp`

Modify `start()` to subscribe to all AFC-related objects:
```cpp
// Objects to subscribe:
"AFC",
"AFC_stepper lane1", "AFC_stepper lane2", ... (dynamic from AFC.lanes)
"AFC_hub Turtle_1",  // or dynamic from AFC.hubs
"AFC_extruder extruder",
"AFC_buffer Turtle_1"
```

### Task 2: Parse AFC_stepper Data for Lane Info
**File:** `src/ams_backend_afc.cpp`

Replace `query_lane_data()` / `parse_lane_data()` with parsing from `AFC_stepper` objects:

```cpp
void parse_afc_stepper(const std::string& lane_name, const json& data) {
    int gate_index = lane_name_to_index(lane_name);
    GateInfo& gate = get_gate_info_mut(gate_index);

    // Filament info
    gate.color_rgb = parse_color(data["color"]);  // "#RRGGBB" format
    gate.material = data["material"];
    gate.spoolman_id = data["spool_id"];
    gate.remaining_weight_g = data["weight"];

    // Status from sensors
    bool prep = data["prep"];
    bool load = data["load"];
    bool loaded_to_hub = data["loaded_to_hub"];
    bool tool_loaded = data["tool_loaded"];
    std::string status = data["status"];

    if (status == "Loaded" || tool_loaded) {
        gate.status = GateStatus::LOADED;
    } else if (prep || load) {
        gate.status = GateStatus::AVAILABLE;
    } else {
        gate.status = GateStatus::EMPTY;
    }

    // Store per-lane sensor states for active lane path inference
    lane_sensors_[gate_index] = {prep, load, loaded_to_hub};
}
```

### Task 3: Parse Hub and Toolhead Sensors
**File:** `src/ams_backend_afc.cpp`

```cpp
void parse_afc_hub(const json& data) {
    hub_sensor_ = data["state"].get<bool>();
}

void parse_afc_extruder(const json& data) {
    tool_start_sensor_ = data["tool_start_status"].get<bool>();
    tool_end_sensor_ = data["tool_end_status"].get<bool>();

    // Track which lane is loaded
    if (data.contains("lane_loaded") && !data["lane_loaded"].is_null()) {
        current_lane_name_ = data["lane_loaded"].get<std::string>();
    }
}
```

### Task 4: Implement get_filament_segment() with Real Sensors
**File:** `src/ams_backend_afc.cpp`

Replace the placeholder implementation:

```cpp
PathSegment AmsBackendAfc::get_filament_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // If no lane is active, return NONE
    if (current_lane_name_.empty() || system_info_.current_gate < 0) {
        return PathSegment::NONE;
    }

    int gate = system_info_.current_gate;
    const auto& lane = lane_sensors_[gate];

    // Check sensors from nozzle back to spool
    if (tool_end_sensor_) {
        return PathSegment::NOZZLE;
    }
    if (tool_start_sensor_) {
        return PathSegment::TOOLHEAD;
    }
    if (hub_sensor_) {
        return PathSegment::OUTPUT;  // Past hub, in bowden
    }
    if (lane.loaded_to_hub) {
        return PathSegment::HUB;  // At hub
    }
    if (lane.load) {
        return PathSegment::LANE;  // Past load sensor
    }
    if (lane.prep) {
        return PathSegment::PREP;  // At prep sensor
    }

    return PathSegment::SPOOL;  // Filament present but not advanced
}
```

### Task 5: Implement infer_error_segment()
**File:** `src/ams_backend_afc.cpp`

```cpp
PathSegment AmsBackendAfc::infer_error_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!system_info_.error_state) {
        return PathSegment::NONE;
    }

    // Error is at the furthest sensor that was triggered
    // before the operation failed
    return get_filament_segment();
}
```

### Task 6: Parse Global AFC State
**File:** `src/ams_backend_afc.cpp`

```cpp
void parse_afc_state(const json& data) {
    if (data.contains("current_state")) {
        std::string state = data["current_state"];
        system_info_.action = ams_action_from_string(state);
    }

    if (data.contains("current_lane") && !data["current_lane"].is_null()) {
        std::string lane = data["current_lane"];
        system_info_.current_gate = lane_name_to_index(lane);
    }

    if (data.contains("error_state")) {
        error_state_ = data["error_state"].get<bool>();
        if (error_state_) {
            error_segment_ = get_filament_segment();
        } else {
            error_segment_ = PathSegment::NONE;
        }
    }

    // Update lane list dynamically
    if (data.contains("lanes") && data["lanes"].is_array()) {
        update_lane_list(data["lanes"]);
    }
}
```

### Task 7: Update handle_status_update() Router
**File:** `src/ams_backend_afc.cpp`

Modify to route updates to appropriate parsers:

```cpp
void AmsBackendAfc::handle_status_update(const json& notification) {
    if (!notification.contains("params")) return;
    const auto& params = notification["params"][0];

    bool state_changed = false;

    // Route to specific parsers
    if (params.contains("AFC")) {
        parse_afc_state(params["AFC"]);
        state_changed = true;
    }

    // Parse each AFC_stepper lane object
    for (const auto& lane : lane_names_) {
        std::string key = "AFC_stepper " + lane;
        if (params.contains(key)) {
            parse_afc_stepper(lane, params[key]);
            state_changed = true;
        }
    }

    // Parse hub
    for (const auto& hub : hub_names_) {
        std::string key = "AFC_hub " + hub;
        if (params.contains(key)) {
            parse_afc_hub(params[key]);
            state_changed = true;
        }
    }

    // Parse extruder
    if (params.contains("AFC_extruder extruder")) {
        parse_afc_extruder(params["AFC_extruder extruder"]);
        state_changed = true;
    }

    if (state_changed) {
        emit_event(EVENT_STATE_CHANGED);
    }
}
```

### Task 8: Add New Member Variables
**File:** `include/ams_backend_afc.h`

```cpp
// Per-lane sensor state
struct LaneSensors {
    bool prep = false;
    bool load = false;
    bool loaded_to_hub = false;
};
std::array<LaneSensors, 16> lane_sensors_;

// Hub and toolhead sensors
bool hub_sensor_ = false;
bool tool_start_sensor_ = false;
bool tool_end_sensor_ = false;

// Global state
bool error_state_ = false;
std::string current_lane_name_;
std::vector<std::string> lane_names_;
std::vector<std::string> hub_names_;
```

## Files to Modify

| File | Changes |
|------|---------|
| `include/ams_backend_afc.h` | Add LaneSensors struct, sensor member variables, AfcCapabilities struct, version tracking |
| `src/ams_backend_afc.cpp` | Implement all parsing functions, version detection, update handle_status_update |
| `include/ams_types.h` | Add AfcCapabilities struct (optional, could be in ams_backend_afc.h) |

## Optional: lane_data Database (v1.0.32+)

If AFC version >= 1.0.32, we can optionally use the `lane_data` database as a fallback/supplement:

```cpp
void AmsBackendAfc::query_lane_data_db() {
    if (!has_lane_data_db_) {
        spdlog::debug("[AMS AFC] lane_data DB not available (requires v1.0.32+)");
        return;
    }

    client_->send_jsonrpc("server.database.get_item",
        {{"namespace", "AFC"}, {"key", "lane_data"}},
        [this](const json& response) {
            // Supplement data from AFC_stepper objects
            parse_lane_data_db(response["value"]);
        },
        [](const MoonrakerError&) {
            // Ignore - we fall back to AFC_stepper objects
        }
    );
}
```

This is **not required** for sensor data (which comes from `AFC_stepper` objects), but could provide additional metadata if available.

## Testing

```bash
# Connect to real BoxTurtle
./build/bin/helix-screen -c 192.168.1.112 -p ams -vv

# Watch for sensor state logs:
# [AMS AFC] Lane lane1: prep=true load=true hub=false tool=false → LANE
# [AMS AFC] Segment: LANE → HUB (hub_sensor triggered)
```

## Version Compatibility Notes

The implementation uses **dynamic discovery** of AFC objects:
- Lane names from `AFC.lanes[]` array
- Hub names from `AFC.hubs[]` array
- Extruder info from `AFC.extruders[]` array

All field parsing uses optional checks (`data.contains()`) with sensible fallbacks, so the code will work with different AFC versions that may have slightly different field sets.

If a field is missing, the code will:
- Use `false` for boolean sensors
- Use `GateStatus::UNKNOWN` for unknown status
- Use empty string for missing material/color
- Skip objects that don't exist

## UI Cleanup: Rename "Home" to "Reset"

The "Home AMS" button is Happy Hare-specific terminology. Both systems need a "reset to known good state" action:

| System | Current Term | Universal Term | Command |
|--------|--------------|----------------|---------|
| Happy Hare | "Home" | **Reset** | `MMU_HOME` |
| AFC | N/A | **Reset** | `AFC_RESET` |

**Changes:**
1. Rename `btn_home` → `btn_reset` in `ams_panel.xml`
2. Change button text from "Home AMS" → "Reset"
3. Rename `AmsAction::HOMING` → `AmsAction::RESETTING` in `ams_types.h`
4. Update backend `home_filament()` → `reset()` method:
   - Happy Hare: calls `MMU_HOME`
   - AFC: calls `AFC_RESET`

**Files to modify (in addition to above):**
- `ui_xml/ams_panel.xml` - Rename button
- `include/ams_types.h` - Rename enum value
- `include/ams_backend.h` - Rename method `home_filament()` → `reset()`
- `src/ams_backend_mock.cpp` - Update mock to use `reset()` and `RESETTING` action
- `src/ams_backend_happy_hare.cpp` - Rename method, still calls `MMU_HOME`
- `src/ams_backend_afc.cpp` - Implement `reset()` calling `AFC_RESET`
- `src/ui_panel_ams.cpp` - Update button handler

## Verification Checklist

- [ ] Lane colors from Spoolman display correctly
- [ ] Lane status (empty/available/loaded) matches physical state
- [ ] Path visualization shows correct segment based on sensor states
- [ ] Segment updates in real-time during load/unload operations
- [ ] Error segment highlights correctly when error_state is true
- [ ] Multiple BoxTurtle units work (if applicable)
- [ ] "Reset" button works (calls `MMU_HOME` for Happy Hare, `AFC_RESET` for AFC)
- [ ] AFC version detected and logged on connect
