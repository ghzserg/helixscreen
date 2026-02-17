# AFC Mixed-Topology Support: Box Turtle + OpenAMS + Toolchanger

**Date**: 2026-02-17
**Status**: Design
**Scope**: MAJOR — Discovery, AFC backend, path canvas, AMS panel UI

## Background

User J0eB0l has a 6-tool toolchanger with mixed AFC hardware:
- **1x Box Turtle** (`Turtle_1`): 4 lanes feeding tools T0-T3 directly (1:1 lane-to-tool)
- **2x OpenAMS** (`AMS_1`, `AMS_2`): 4 lanes each, merging into T4 and T5 respectively (4:1 lane-to-tool)
- All controlled by AFC software + Klipper Toolchanger
- Total: 12 lanes, 6 tools, 3 units

### Topology Differences

| Unit Type | Lanes | Hubs | Buffers | Lane:Tool Ratio |
|-----------|-------|------|---------|-----------------|
| Box Turtle | 4 | **None** (`hub: "direct_load"`) | 1 per lane (TN, TN1, TN2, TN3) | 1:1 |
| OpenAMS | 4 per unit | 1 per lane (sensor+cutter) | **None** (`buffer: null`) | 4:1 (shared extruder) |

Key insight: Hub and buffer presence are **independent** and **per-unit** (not global). A unit may have:
- Hub + buffer (possible but not seen yet)
- Hub only (OpenAMS)
- Buffer only (Box Turtle with `direct_load`)
- Neither (possible for simple setups)

Buffer presence on Box Turtle lanes is POSSIBLE but NOT REQUIRED.
OpenAMS hubs are 1:1 per lane (sensor/cutter point), not a multi-lane merger — the "merging" happens at the shared extruder.

---

## Raw Klipper Data (from real hardware)

### AFC Top-Level Object

```json
{
    "AFC": {
        "current_load": null,
        "current_lane": null,
        "current_state": "Idle",
        "units": [
            "OpenAMS AMS_1",
            "OpenAMS AMS_2",
            "Box_Turtle Turtle_1"
        ],
        "lanes": [
            "lane4", "lane5", "lane6", "lane7",
            "lane8", "lane9", "lane10", "lane11",
            "lane0", "lane1", "lane2", "lane3"
        ],
        "maps": [
            "T4", "T5", "T6", "T7",
            "T8", "T9", "T10", "T11",
            "T0", "T1", "T2", "T3"
        ],
        "extruders": [
            "extruder", "extruder1", "extruder2",
            "extruder3", "extruder4", "extruder5"
        ],
        "hubs": [
            "Hub_1", "Hub_2", "Hub_3", "Hub_4",
            "Hub_5", "Hub_6", "Hub_7", "Hub_8"
        ],
        "buffers": ["TN", "TN1", "TN2", "TN3"],
        "spoolman": "http://192.168.0.147:7912",
        "td1_present": true,
        "error_state": false,
        "bypass_state": false,
        "quiet_mode": false
    }
}
```

**Notes:**
- `units` array format: `"{Type} {Name}"` (e.g., `"OpenAMS AMS_1"`, `"Box_Turtle Turtle_1"`)
- `lanes` and `maps` are **parallel arrays** — `lanes[i]` maps to tool `maps[i]`
- Lane ordering: OpenAMS lanes first, then Box Turtle lanes (order of unit definition)

### Unit-Level Objects

These are the most important objects for grouping lanes into units.

**`AFC_BoxTurtle Turtle_1`:**
```json
{
    "lanes": ["lane0", "lane1", "lane2", "lane3"],
    "extruders": ["extruder", "extruder1", "extruder2", "extruder3"],
    "hubs": [],
    "buffers": ["TN", "TN1", "TN2", "TN3"]
}
```

**`AFC_OpenAMS AMS_1`:**
```json
{
    "lanes": ["lane4", "lane5", "lane6", "lane7"],
    "extruders": ["extruder4"],
    "hubs": ["Hub_1", "Hub_2", "Hub_3", "Hub_4"],
    "buffers": []
}
```

**`AFC_OpenAMS AMS_2`:**
```json
{
    "lanes": ["lane8", "lane9", "lane10", "lane11"],
    "extruders": ["extruder5"],
    "hubs": ["Hub_5", "Hub_6", "Hub_7", "Hub_8"],
    "buffers": []
}
```

**Key observations:**
- Unit object type encodes hardware: `AFC_BoxTurtle` vs `AFC_OpenAMS`
- `extruders` array length tells you lane:tool ratio (4 = 1:1, 1 = 4:1 shared)
- `hubs: []` = no hub hardware, `buffers: []` = no buffer hardware
- These objects give us everything needed to build `AmsUnit` structs

### Per-Lane Data

**Box Turtle lane (AFC_stepper):**
```json
{
    "AFC_stepper lane1": {
        "name": "lane1",
        "unit": "Turtle_1",
        "hub": "direct_load",
        "extruder": "extruder1",
        "buffer": "TN1",
        "buffer_status": "Advancing",
        "lane": 2,
        "map": "T1",
        "load": true,
        "prep": true,
        "tool_loaded": true,
        "loaded_to_hub": true,
        "material": "PLA",
        "spool_id": 6,
        "color": "#EAECEB",
        "weight": 581.55,
        "extruder_temp": 220,
        "runout_lane": null,
        "filament_status": "In Tool",
        "filament_status_led": "#0000ff",
        "status": "Loaded",
        "dist_hub": 2120.0,
        "td1_td": "",
        "td1_color": "",
        "td1_scan_time": ""
    }
}
```

**OpenAMS lane (AFC_lane):**
```json
{
    "AFC_lane lane4": {
        "name": "lane4",
        "unit": "AMS_1",
        "hub": "Hub_1",
        "extruder": "extruder4",
        "buffer": null,
        "buffer_status": null,
        "lane": 1,
        "map": "T4",
        "load": true,
        "prep": true,
        "tool_loaded": false,
        "loaded_to_hub": false,
        "material": "PLA",
        "spool_id": 13,
        "color": "#000000",
        "weight": 295.25,
        "extruder_temp": 240,
        "runout_lane": null,
        "filament_status": "Ready",
        "filament_status_led": "#00cc00",
        "status": "Loaded",
        "dist_hub": 60,
        "td1_td": "",
        "td1_color": "",
        "td1_scan_time": ""
    }
}
```

**Differences between lane types:**

| Field | Box Turtle (`AFC_stepper`) | OpenAMS (`AFC_lane`) |
|-------|---------------------------|----------------------|
| `hub` | `"direct_load"` | `"Hub_1"` (named hub) |
| `buffer` | `"TN1"` (named buffer) | `null` |
| `buffer_status` | `"Advancing"` / `"Unknown"` | `null` |
| `dist_hub` | ~2100 (long bowden) | 60 (short path) |
| `extruder` | unique per lane | shared across unit |

**Shared fields (identical schema):** name, unit, lane, map, load, prep, tool_loaded, loaded_to_hub, material, spool_id, color, weight, extruder_temp, runout_lane, filament_status, filament_status_led, status, td1_*

### Hub Objects (`AFC_hub`)

```json
{
    "AFC_hub Hub_1": {
        "state": false,
        "cut": false,
        "cut_cmd": null,
        "cut_dist": 50,
        "cut_clear": 120,
        "cut_min_length": 200,
        "cut_servo_pass_angle": 0,
        "cut_servo_clip_angle": 160,
        "cut_servo_prep_angle": 75,
        "lanes": ["lane4"],
        "afc_bowden_length": 2031.0
    }
}
```

**Key observations:**
- `state`: bool — hub sensor triggered (filament detected)
- `lanes`: which lanes feed this hub (1:1 for OpenAMS)
- `afc_bowden_length`: distance from hub to toolhead
- Cut parameters: for filament cutting at hub (OpenAMS feature)
- Box Turtle has **no hub objects** (hubs array is empty on unit)

### Buffer Objects (`AFC_buffer`)

```json
{
    "AFC_buffer TN": {
        "state": "Advancing",
        "lanes": ["lane0"],
        "enabled": false,
        "rotation_distance": null,
        "active_lane": null,
        "fault_detection_enabled": true,
        "error_sensitivity": 7.0,
        "fault_timer": "Stopped",
        "distance_to_fault": -10707.42,
        "filament_error_pos": 40.0,
        "current_pos": 10747.42
    }
}
```

**Key observations:**
- `state`: string — "Advancing", "Trailing", "Unknown", etc.
- `lanes`: which lane this buffer serves (1:1 for Box Turtle)
- Fault detection: distance-based error tracking
- OpenAMS has **no buffer objects** (buffers array is empty on unit)

### Klipper Object Name Patterns

Two different Klipper object prefixes for lanes:
- **Box Turtle**: `AFC_stepper lane{N}` (with `tmc2209 AFC_stepper lane{N}`)
- **OpenAMS**: `AFC_lane lane{N}` (no stepper driver — no motor on OpenAMS lanes)

Unit-level objects:
- `AFC_BoxTurtle {name}` (e.g., `AFC_BoxTurtle Turtle_1`)
- `AFC_OpenAMS {name}` (e.g., `AFC_OpenAMS AMS_1`)

Other AFC objects:
- `AFC_hub {name}` — hub sensor/cutter objects
- `AFC_buffer {name}` — buffer tracking objects
- `AFC_extruder {name}` — AFC wrapper around Klipper extruder
- `AFC_Toolchanger {name}` — toolchanger integration
- `AFC_led {name}` — LED control objects
- `AFC` — top-level state object

### Filament Sensor Objects

```
filament_switch_sensor virtual_bypass    — virtual bypass sensor
filament_switch_sensor quiet_mode        — quiet mode toggle
filament_switch_sensor extruder4_tool_start  — OpenAMS tool start sensor
filament_switch_sensor extruder5_tool_start  — OpenAMS tool start sensor
_filament_switch_sensor lane{N}_prep     — per-lane prep sensor (BT)
_filament_switch_sensor lane{N}_load     — per-lane load sensor (BT)
_filament_switch_sensor TN{N}_expanded   — buffer expanded sensor (BT)
_filament_switch_sensor TN{N}_compressed — buffer compressed sensor (BT)
_filament_switch_sensor extruder4_tool_start   — prefixed version (internal?)
_filament_switch_sensor AMS_extruder4          — AMS extruder sensor
```

Note: underscore-prefixed `_filament_switch_sensor` are internal AFC sensors. Non-prefixed `filament_switch_sensor` are Klipper-visible.

---

## Topology Diagrams

### Box Turtle (direct_load topology)

```
lane0 ──[prep]──[load]──[buffer TN ]──────────── extruder  (T0)
lane1 ──[prep]──[load]──[buffer TN1]──────────── extruder1 (T1)
lane2 ──[prep]──[load]──[buffer TN2]──────────── extruder2 (T2)
lane3 ──[prep]──[load]──[buffer TN3]──────────── extruder3 (T3)
```

- Each lane is independent with its own tool
- Buffer sits between lane and tool, manages retraction/advancement
- `hub: "direct_load"` — no hub/merger, filament goes straight to tool
- `dist_hub` ~2100mm — long bowden from buffer to toolhead

### OpenAMS (hub topology, shared extruder)

```
lane4  ──[prep]──[load]──[Hub_1 sensor+cutter]──┐
lane5  ──[prep]──[load]──[Hub_2 sensor+cutter]──┤── extruder4 (T4)
lane6  ──[prep]──[load]──[Hub_3 sensor+cutter]──┤
lane7  ──[prep]──[load]──[Hub_4 sensor+cutter]──┘

lane8  ──[prep]──[load]──[Hub_5 sensor+cutter]──┐
lane9  ──[prep]──[load]──[Hub_6 sensor+cutter]──┤── extruder5 (T5)
lane10 ──[prep]──[load]──[Hub_7 sensor+cutter]──┤
lane11 ──[prep]──[load]──[Hub_8 sensor+cutter]──┘
```

- 4 lanes share 1 extruder — only 1 lane active at a time
- Each lane has its own hub (sensor + optional cutter)
- No buffer hardware
- `dist_hub` ~60mm — short path from spool to hub sensor
- `afc_bowden_length` ~2030mm — long bowden from hub to toolhead

### Combined System View

```
┌─ Box Turtle (Turtle_1) ─────────────────────────────────┐
│ lane0 ──[buf TN ]──→ T0 (extruder)                      │
│ lane1 ──[buf TN1]──→ T1 (extruder1)                     │
│ lane2 ──[buf TN2]──→ T2 (extruder2)                     │
│ lane3 ──[buf TN3]──→ T3 (extruder3)                     │
└──────────────────────────────────────────────────────────┘

┌─ OpenAMS (AMS_1) ───────────────────────────────────────┐
│ lane4  ──[Hub_1]──┐                                      │
│ lane5  ──[Hub_2]──┤──→ T4 (extruder4)                   │
│ lane6  ──[Hub_3]──┤                                      │
│ lane7  ──[Hub_4]──┘                                      │
└──────────────────────────────────────────────────────────┘

┌─ OpenAMS (AMS_2) ───────────────────────────────────────┐
│ lane8  ──[Hub_5]──┐                                      │
│ lane9  ──[Hub_6]──┤──→ T5 (extruder5)                   │
│ lane10 ──[Hub_7]──┤                                      │
│ lane11 ──[Hub_8]──┘                                      │
└──────────────────────────────────────────────────────────┘
```

---

## Current Code Analysis

### What Already Works

1. **Multi-backend framework**: `AmsState` supports N backends with per-backend LVGL subjects
2. **AmsUnit struct**: Has `has_hub_sensor`, `buffer_health` per unit — independent flags
3. **SlotInfo**: Complete per-slot data (color, material, spool_id, weight, sensors, error state)
4. **Tool mapping**: `ToolInfo.backend_index` + `backend_slot` for routing tool changes
5. **AFC backend**: Parses lane state, sensor data, Moonraker DB for colors/materials
6. **Path canvas**: Hub and linear topology rendering
7. **Backend selector UI**: Shows when `backend_count > 1`
8. **MAX_SLOTS = 16** per backend — sufficient for all known configs

### What Needs Work

#### 1. Discovery — Detect Both Lane Object Types
**Current**: Only discovers `AFC_stepper lane*` objects
**Needed**: Also discover `AFC_lane lane*` objects (OpenAMS uses `AFC_lane`)
**Also**: Discover unit-level objects (`AFC_BoxTurtle`, `AFC_OpenAMS`) for topology data

#### 2. AFC Backend — Unit Inference from Unit Objects
**Current**: Flat list of lane names, unit grouping unclear
**Needed**: Query unit-level objects (`AFC_BoxTurtle Turtle_1`, `AFC_OpenAMS AMS_1`) to get:
- `lanes[]` — which lanes belong to this unit
- `extruders[]` — 1:1 or shared (determines lane:tool ratio)
- `hubs[]` — empty = no hub (direct_load)
- `buffers[]` — empty = no buffer
**Also**: Parse `AFC.units` array for `"{Type} {Name}"` format to know object prefix

#### 3. AFC Backend — Per-Lane Hub/Buffer Detection
**Current**: May assume all lanes have hubs (AFC = HUB topology)
**Needed**: Check per-lane `hub` field:
- `"direct_load"` → no hub, direct to tool
- Any other string → has named hub
**And** per-lane `buffer` field:
- `null` → no buffer
- String → has named buffer, subscribe to `AFC_buffer {name}`

#### 4. AFC Backend — Hub Object Subscriptions
**Current**: Subscribes to `AFC_hub` objects by name
**Needed**: Only subscribe to hubs that exist on units that have them
**Data per hub**: `state` (bool sensor), `lanes[]`, `afc_bowden_length`, cut params

#### 5. AFC Backend — Buffer Object Subscriptions
**Current**: May not subscribe to individual `AFC_buffer` objects
**Needed**: Subscribe to `AFC_buffer {name}` for each buffer in unit's buffers list
**Data per buffer**: `state` (string), `lanes[]`, fault detection, position

#### 6. Path Canvas — Per-Unit Topology
**Current**: Single global `topology` (0=LINEAR, 1=HUB) per widget
**Needed**: Set topology based on viewed unit's characteristics:
- Unit has hubs + shared extruder → HUB topology (merger visualization)
- Unit has no hubs + buffers + 1:1 extruders → DIRECT/PARALLEL topology
- Possibly a new topology type for direct_load (parallel lanes with buffers)

#### 7. AMS Panel — Unit Selection Within Single Backend
**Current**: Backend selector for switching between backends
**Needed**: May need unit selector within a single backend, since all 3 units are in one AFC backend
**Or**: One "backend" per unit? This is a design decision.

#### 8. Tool Mapping — Shared vs Dedicated Extruders
**Current**: Each tool maps to one slot
**Needed**: Handle the OpenAMS case where 4 lanes share 1 tool:
- When T4 is selected, show which of the 4 AMS_1 lanes is active
- Lane selection within a tool context (which filament for this tool?)
- Runout/endless spool within a single tool's lane pool

---

## Design Decisions Needed

### D1: One Backend vs One Backend Per Unit?

**Option A**: Single AFC backend, multiple AmsUnits within it
- Matches how AFC actually works (one `AFC` Klipper object controls everything)
- Simpler event routing
- Unit selector in UI instead of backend selector

**Option B**: One AFC backend per unit (Turtle_1, AMS_1, AMS_2)
- Cleaner separation, each backend has uniform topology
- Backend selector already works in UI
- But: AFC commands are unit-agnostic (single `AFC` state machine)

**Recommendation**: Option A (single backend, multi-unit). The AFC state machine is unified — splitting into multiple backends would create artificial boundaries that don't match the actual control flow.

### D2: Path Canvas Topology Per-Unit

The path canvas currently uses a single topology enum. Options:
- Add `DIRECT` topology type (parallel lanes, no merger, optional buffers)
- Set topology dynamically when switching unit view
- Each unit card in overview renders with its own topology

### D3: OpenAMS Lane-to-Tool Mapping Display

OpenAMS has 4 lanes → 1 tool. How to display:
- Show all 4 lanes with the shared tool label?
- Show active lane highlighted, others dimmed?
- Show in the same slot grid UI, grouped by unit?

### D4: Mock Backend Updates

The mock backend needs a mixed-topology mode to test this setup without hardware.

---

## Open Questions

1. **Can a Box Turtle lane have NO buffer?** (confirmed: yes, buffers are optional)
2. **Can an OpenAMS lane have a buffer?** (unlikely but not confirmed)
3. **What does `loaded_to_hub` mean for `direct_load` lanes?** (seen as `true` when loaded — may just mean "past the load sensor")
4. **What is `td1_present` / `td1_td` / `td1_color` / `td1_scan_time`?** (likely TagDock/filament identification sensor)
5. **What are the `fps` and `fps2` objects?** (filament position sensors for OpenAMS?)
6. **Are there other unit types beyond Box_Turtle and OpenAMS?** (Night Owl mentioned in mock — another AFC variant?)
7. **How does AFC handle tool changes between units?** (e.g., T1 → T4 requires BT unload + OpenAMS load)
