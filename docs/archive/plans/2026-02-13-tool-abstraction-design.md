# Tool Abstraction & Cascaded Backend Design

**Date**: 2026-02-13
**Status**: Approved design, pending implementation plan

## Overview

HelixScreen's multi-filament system needs to evolve to support toolchangers, IDEX,
multi-extruder printers, and cascaded configurations (e.g., IDEX with MMU on one head).
This design introduces a proper Tool concept, dynamic multi-extruder temperature handling,
and multi-backend support.

We are in beta — backward compatibility is explicitly NOT a goal. Old subjects and APIs
will be replaced, not shimmed.

## Key Concepts

- **Tool** — A physical print head. Owns hardware: extruder, heater, fan, offsets.
  Single-extruder printers have one implicit tool.
- **Slot** — A filament source. Has color, filament type, position in AMS/MMU/toolchanger.
  A tool knows which slot is feeding it.
- **Backend** — A filament management system (AMS, MMU, toolchanger, AFC, etc.).
  A printer can have multiple backends simultaneously.

**Tools wrap slots.** A tool IS the head; slots are what feed it. For toolchangers,
slot N feeds tool N (1:1). For MMU, all slots feed tool 0. For IDEX+MMU, backend 0's
slots feed tool 0, backend 1's slots feed tool 1.

## Phasing

Three phases, each delivers standalone value:

1. **Multi-extruder temperatures** — Dynamic extruder discovery and temp subjects
2. **Tool abstraction** — ToolInfo/ToolState with per-tool hardware ownership
3. **Multi-backend (cascading)** — Multiple simultaneous backends with namespaced slots

---

## Phase 1: Multi-Extruder Temperature State

### Goal

Refactor `PrinterTemperatureState` from hardcoded single extruder to dynamic N-extruder
support, reusing the `PrinterFanState` pattern.

### Current State (What We're Replacing)

`PrinterTemperatureState` (`include/printer_temperature_state.h`) has hardcoded subjects:

```cpp
// Current — hardcoded single extruder
lv_subject_t extruder_temp_;    // centidegrees (value × 10)
lv_subject_t extruder_target_;  // centidegrees
lv_subject_t bed_temp_;
lv_subject_t bed_target_;
```

Updates look for exactly `status["extruder"]["temperature"]` — no multi-extruder support.
`PrinterDiscovery` already detects all extruders (`heaters()` returns `"extruder"`,
`"extruder1"`, `"heater_bed"`, etc.) but the temperature state ignores everything
beyond the first.

### Target Pattern: PrinterFanState

`PrinterFanState` (`include/printer_fan_state.h`) already solves dynamic hardware:

```cpp
struct FanInfo {
    std::string object_name;     // "fan", "heater_fan hotend_fan"
    std::string display_name;    // "Part Cooling Fan"
    FanType type;
    int speed_percent = 0;
    bool is_controllable = false;
};

// Dynamic subjects in unordered_map with unique_ptr (stable across rehash)
std::unordered_map<std::string, std::unique_ptr<lv_subject_t>> fan_subjects_;
lv_subject_t fans_version_;  // bumped on add/remove, UI observes this
```

### New Data Model

```cpp
struct ExtruderInfo {
    std::string name;           // "extruder", "extruder1", etc.
    std::string display_name;   // "Nozzle", "Nozzle 1" (derived or user-set)
    float temperature = 0.0f;
    float target = 0.0f;
    std::unique_ptr<lv_subject_t> temp_subject;    // centidegrees
    std::unique_ptr<lv_subject_t> target_subject;  // centidegrees
};

// In PrinterTemperatureState:
std::unordered_map<std::string, ExtruderInfo> extruders_;
lv_subject_t extruder_version_;  // bumped when extruders added/removed
// bed_temp_ and bed_target_ remain as-is (single bed assumption is fine)
// chamber_temp_ remains as-is
```

### Discovery Flow

1. `PrinterDiscovery::parse_objects()` already builds `heaters_` vector
2. `PrinterTemperatureState::init_extruders(discovery.heaters())` creates map entries
3. Each detected extruder (`"extruder"`, `"extruder1"`, ...) gets an `ExtruderInfo`
4. Heater bed and chamber sensors handled separately (unchanged)

### Moonraker Subscription

Currently subscribes to `{"extruder": null}`. Must subscribe to all detected extruders:

```json
{
  "extruder": null,
  "extruder1": null,
  "heater_bed": null
}
```

Status updates arrive as `status["extruder"]["temperature"]`,
`status["extruder1"]["temperature"]`, etc. — iterate the map and match keys.

### Removed Subjects

Kill `extruder_temp_` and `extruder_target_` singleton subjects. All consumers updated
to use `get_extruder_temp_subject("extruder")` or iterate extruders via the version
subject pattern.

### TempControlPanel Changes

Current: two hardcoded `HeaterConfig` structs (nozzle + bed).

New: dynamic heater list built from discovered extruders + bed. Segmented control at top
when multiple heaters exist (segments: "Nozzle 0", "Nozzle 1", "Bed"). Single-extruder
printers: no segmented control, looks exactly like today.

### Home Panel / Print Status Changes

Currently show one nozzle temp. With multiple extruders detected, show active
extruder temp prominently. Details deferred to Phase 2 (tool abstraction determines
which extruder is "active").

---

## Phase 2: Tool Abstraction ✅ Complete

### Goal

Introduce `ToolInfo` — a physical print head that owns hardware references. Separate
"what hardware is printing" (tool) from "what filament is loaded" (slot).

### klipper-toolchanger Reference

This design maps directly to `viesturz/klipper-toolchanger`'s Klipper objects:

**Klipper `[toolchanger]` status object:**
```json
{
  "status": "ready",
  "tool": "T0",
  "tool_number": 0,
  "tool_numbers": [0, 1, 2],
  "tool_names": ["T0", "T1", "T2"]
}
```
States: `uninitialized`, `initializing`, `ready`, `changing`, `error`

**Klipper `[tool T0]` status object:**
```json
{
  "name": "T0",
  "tool_number": 0,
  "toolchanger": "toolchanger",
  "active": true,
  "mounted": true,
  "detect_state": "present",
  "extruder": "extruder",
  "fan": "part_fan_T0",
  "gcode_x_offset": 0.0,
  "gcode_y_offset": 0.0,
  "gcode_z_offset": 0.0
}
```
Detection states: `present`, `absent`, `unavailable`

**GCode commands:**
- `SELECT_TOOL TOOL=T0 [RESTORE_AXIS=xy]` — mount and activate tool
- `UNSELECT_TOOL [SAVE_POSITION=1]` — park current tool
- `INITIALIZE_TOOLCHANGER [RECOVER=1]` — reset to ready state
- `SET_TOOL_TEMPERATURE TOOL=T0 TARGET=200` — heat specific tool
- `VERIFY_TOOL_DETECTED TOOL=T0` — validate detection pin

**Fan remapping:** `[multi_fan]` intercepts M106/M107 and routes to active tool's fan.
`transfer_fan_speed: true` in `[toolchanger]` config transfers fan speed during tool
changes. HelixScreen does NOT need to manage this — Klipper handles it transparently.

### Data Model

```cpp
struct ToolInfo {
    int index;                              // 0, 1, 2...
    std::string name;                       // "T0", "T1" or user-defined
    std::optional<std::string> extruder_name;  // "extruder", "extruder1"
    std::optional<std::string> heater_name;    // standalone heater_generic (nozzle-swap systems)
    std::optional<std::string> fan_name;       // per-tool fan (Klipper handles routing)
    float gcode_x_offset = 0;
    float gcode_y_offset = 0;
    float gcode_z_offset = 0;
    bool active = false;                    // currently selected tool?
    bool mounted = false;                   // detection pin says present?
    DetectState detect_state = DetectState::UNAVAILABLE;  // present/absent/unavailable
};
```

Hardware references are all optional and independent to handle different configurations:

| Configuration | extruder_name | heater_name | fan_name |
|---|---|---|---|
| Normal toolchanger | per-tool (`"extruder"`, `"extruder1"`) | empty | per-tool |
| Nozzle-swap system | shared (same motor) | per-tool (`heater_generic`) | optional per-tool |
| Single extruder | `"extruder"` | empty | `"fan"` |
| IDEX | per-tool | empty | per-tool |

Helper for temperature display:
```cpp
std::string ToolInfo::effective_heater() const {
    if (heater_name) return *heater_name;
    if (extruder_name) return *extruder_name;
    return "extruder";  // fallback
}
```

### ToolState Class

```cpp
class ToolState {
public:
    static ToolState& instance();

    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    // Discovery — called from init_subsystems_from_hardware()
    void init_tools(const PrinterDiscovery& hardware);

    // State updates from Moonraker status
    void update_from_status(const nlohmann::json& status);

    // Accessors
    const std::vector<ToolInfo>& tools() const;
    const ToolInfo* active_tool() const;
    int active_tool_index() const;
    int tool_count() const;

    // Subjects
    lv_subject_t* get_active_tool_subject();     // int: index of active tool
    lv_subject_t* get_tool_count_subject();      // int: for UI to react to discovery
    lv_subject_t* get_tools_version_subject();   // int: bumped on state changes

private:
    std::vector<ToolInfo> tools_;
    int active_tool_index_ = 0;
    lv_subject_t active_tool_subject_;
    lv_subject_t tool_count_subject_;
    lv_subject_t tools_version_subject_;
};
```

### Single-Extruder Printers

One implicit `ToolInfo{0, "T0", "extruder", nullopt, "fan"}` created automatically.
`active_tool_index_` always 0. No UI changes from a basic single-extruder setup.

### Toolchanger Printers

`PrinterDiscovery` already detects `tool T0`, `tool T1`, etc. via `tool_names()`.
Each creates a `ToolInfo` populated from the Klipper tool status object (extruder,
fan, offsets, detect_state). Active tool tracked from `toolchanger.tool_number`.

### Moonraker Subscription (Toolchanger)

Subscribe to toolchanger + all tool objects:
```json
{
  "toolchanger": null,
  "tool T0": null,
  "tool T1": null,
  "extruder": null,
  "extruder1": null
}
```

Our existing `AmsBackendToolChanger` already subscribes to these objects. `ToolState`
can either share the subscription or receive forwarded data from the backend.

### Existing AmsBackendToolChanger Integration

`AmsBackendToolChanger` (`src/printer/ams_backend_toolchanger.cpp`) already:
- Subscribes to `toolchanger.*` and `tool T*.*` via `MoonrakerClient`
- Parses `tool_names`, `tool_numbers`, `active`, `mounted`, `gcode_*_offset`
- Sends `SELECT_TOOL TOOL=T{n}`, `UNSELECT_TOOL`, `INITIALIZE_TOOLCHANGER`
- Maps toolchanger status to `AmsAction` enum
- Maintains `tool_mounted_` vector

`ToolState` should be populated FROM the backend's parsed data, not duplicate parsing.
The backend already does the work — `ToolState` is the UI-facing subject layer.

### Home Panel UI

Active tool's temp shown prominently. Small "T0"/"T1" badge next to the temp readout
when `tool_count > 1`. If space permits, secondary readout for other tool(s) in a
subdued style. Space-constrained displays: active tool temp only, no badge.

### Offsets

Read-only display in a tool info view accessible from controls panel. No calibration UI
in this phase.

### Fan Routing

Klipper's `multi_fan` handles fan remapping on tool change. We just read `fan` subject
as today. The `transfer_fan_speed` config in `[toolchanger]` ensures fan speed carries
across tool changes. No HelixScreen code changes needed.

---

## Phase 3: Multi-Backend (Cascading) ✅ Complete

### Goal

Replace single `backend_` with a collection so a printer can have multiple filament
management systems simultaneously (e.g., IDEX with MMU on one head, or toolchanger
with AFC on one tool).

### klipper-toolchanger Cascading Reference

Cascaded toolchangers use the `parent_tool` config parameter:

```ini
[toolchanger]
# Primary toolchanger — selects between heads
name: toolchanger_carriage

[tool left_head]
toolchanger: toolchanger_carriage
tool_number: 0
extruder: extruder

[tool right_head]
toolchanger: toolchanger_carriage
tool_number: 1
extruder: extruder1

[toolchanger mmu]
# MMU attached to left head only
parent_tool: toolchanger_carriage.left_head

[tool mmu_slot_0]
toolchanger: mmu
tool_number: 0

[tool mmu_slot_1]
toolchanger: mmu
tool_number: 1
```

Each toolchanger maintains independent status. `SELECT_TOOL` on the child automatically
selects the parent tool first. In Moonraker, both `toolchanger` and `toolchanger mmu`
appear as separate status objects.

### Current State (What We're Replacing)

`AmsState` singleton holds:
```cpp
std::unique_ptr<AmsBackend> backend_;  // single backend
```

All subjects are flat: `ams_slot_0_color`, `ams_slot_1_color`, etc. with
`static constexpr int MAX_SLOTS = 16`.

Per-slot subjects are created dynamically via `get_slot_color_subject(int slot_index)`
and `get_slot_status_subject(int slot_index)` but indexed globally.

### New Data Model

```cpp
// In AmsState:
std::vector<std::unique_ptr<AmsBackend>> backends_;
```

Each backend gets an index (0, 1, ...) assigned at registration order.

### Slot Namespacing

Per-backend slot indexing. Old flat subjects (`ams_slot_0_color`) are removed entirely.
New accessors:

```cpp
lv_subject_t* get_slot_color_subject(int backend_index, int slot_index);
lv_subject_t* get_slot_status_subject(int backend_index, int slot_index);
```

Per-backend indexing matches user mental model: "slot 2 on my MMU", not "global slot 6".

### Backend Discovery

`PrinterDiscovery` detects AMS type from Moonraker objects. For cascaded setups, it
needs to detect MULTIPLE systems:

- Happy Hare objects (`mmu`, `mmu_encoder`) → `AmsBackendHappyHare`
- AFC objects (`AFC`, `AFC_stepper`) → `AmsBackendAFC`
- Toolchanger objects (`toolchanger`, `tool T*`) → `AmsBackendToolChanger`
- ValgACE objects → `AmsBackendValgACE`

Currently `PrinterDiscovery::mmu_type()` returns a single `AmsType`. This becomes a
list of detected systems, each with enough info to instantiate its backend.

### Tool-to-Backend Mapping

Each tool knows which backend (if any) feeds it:

| Config | Tool 0 fed by | Tool 1 fed by |
|---|---|---|
| Toolchanger only | backend 0, slot 0 | backend 0, slot 1 |
| Single head + MMU | backend 0, any slot | N/A |
| IDEX + MMU on head 0 | backend 1 (MMU), any slot | direct drive (no backend) |
| IDEX + MMU on both | backend 1 (MMU-left), any slot | backend 2 (MMU-right), any slot |

This mapping lives in `ToolInfo`:
```cpp
struct ToolInfo {
    // ... existing fields ...
    int backend_index = -1;     // which backend feeds this tool (-1 = direct drive)
    int backend_slot = -1;      // fixed slot in that backend (-1 = any/dynamic)
};
```

### AMS Panel UI

Backend selector (segmented control) when multiple backends detected. Each segment
shows that backend's slots. Single-backend printers see no change.

### Event Routing

Backend callbacks include `this` pointer. `AmsState` identifies which backend fired
by matching against `backends_` vector. Event handlers receive backend index for
context.

### System-Level Subjects

Current subjects like `ams_type_subject`, `ams_action_subject` describe a single
backend. With multiple backends, these either:

- Track the "primary" backend (index 0) for simple UIs
- Get per-backend variants via accessors: `get_ams_action_subject(int backend_index)`

Decision deferred to implementation — depends on how much UI complexity we want.

---

## Explicit Non-Goals

- Calibration UI (offsets are read-only)
- Fan routing UI or fan-per-tool display (Klipper handles it)
- New GCode commands beyond what backends already send
- Backward compatibility for old subject names
- Probe management or crash detection UI
- Tool parameter editing (SET_TOOL_PARAMETER)

---

## Key File Impact Summary

### Phase 1 (Multi-Extruder Temps)

| File | Change |
|---|---|
| `include/printer_temperature_state.h` | Replace single extruder subjects with `unordered_map<string, ExtruderInfo>` |
| `src/printer/printer_temperature_state.cpp` | Dynamic init, multi-extruder status parsing |
| `src/ui/ui_panel_temp_control.h/cpp` | Dynamic heater list, segmented control |
| `src/ui/panels/home_panel.*` | Update extruder temp binding |
| `src/ui/overlays/print_status_overlay.*` | Update extruder temp binding |
| Moonraker subscription setup | Subscribe to all detected extruders |

### Phase 2 (Tool Abstraction)

| File | Change |
|---|---|
| New: `include/tool_state.h` | ToolInfo struct, ToolState singleton |
| New: `src/printer/tool_state.cpp` | Implementation |
| `src/printer/ams_backend_toolchanger.cpp` | Feed parsed tool data into ToolState |
| `src/ui/panels/home_panel.*` | Active tool badge, multi-temp display |
| `src/ui/overlays/print_status_overlay.*` | Active tool indicator |
| `src/application/init_subsystems.*` | Init ToolState from discovery |

### Phase 3 (Multi-Backend)

| File | Change |
|---|---|
| `include/ams_state.h/cpp` | `vector<unique_ptr<AmsBackend>>`, per-backend subjects |
| `include/printer_discovery.h/cpp` | Detect multiple AMS systems |
| `src/ui/overlays/ams_panel.*` | Backend selector segmented control |
| All AMS UI consumers | Update to pass backend index |

---

## References

- [viesturz/klipper-toolchanger](https://github.com/viesturz/klipper-toolchanger) — Klipper toolchanging extension
- [klipper-toolchanger docs](https://github.com/viesturz/klipper-toolchanger/blob/main/toolchanger.md) — Configuration and GCode reference
- [DeepWiki analysis](https://deepwiki.com/viesturz/klipper-toolchanger) — Architecture deep dive
- `docs/devel/FILAMENT_MANAGEMENT.md` — Existing HelixScreen AMS system docs
