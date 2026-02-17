# Probe Management System Design

**Date**: 2026-02-15
**Status**: Draft

## Problem

Users with bed probes (Cartographer, Beacon, BLTouch, Klicky, Tap, etc.) find the current wizard probe step confusing — it only handles switch sensors assigned as Z_PROBE, not actual dedicated probes. There's no dedicated UI to view probe status, run probe-specific operations, or configure probe settings. Users ask "what about my Cartographer?" and there's no good answer.

## Goals

1. Recognize all common probe types (Cartographer, Beacon, BLTouch, Tap, Klicky, standard, eddy current, smart effector)
2. Show probe status, type, and live readings
3. Provide probe-type-specific controls (BLTouch deploy/stow, Cartographer model select, etc.)
4. Enable safe editing of Klipper config for probe settings that aren't runtime-changeable
5. Surface probes in the first-run wizard alongside other detected hardware

## Non-Goals

- Replacing Mainsail/Fluidd's full config editor
- Supporting every niche probe variant on day one
- Parsing Klipper config for purposes beyond probe/hardware configuration

---

## Design

### 1. Wizard: Detected Hardware Step

**Replaces**: Current step 7 (AMS Identify) and step 10 (Probe Sensor Select)

A single confirmational step that shows all auto-detected hardware add-ons after printer identification:

- **Bed Probe**: Type and name (e.g., "Cartographer (Eddy Current)")
- **Filament System**: AMS/ERCF/AFC type and slot count
- **LEDs**: Detected LED chains
- **Accelerometer**: ADXL345, LIS2DW, etc.
- **Other sensors**: Filament width, humidity, etc.

Each section only appears if that hardware type was detected. If nothing interesting is detected, the step is skipped entirely.

This is read-only — no configuration happens here. Just "we see your hardware" confirmation with a "Not seeing something? Check your Klipper config." help link at the bottom.

### 2. Probe Overlay

A new overlay accessible from the calibration menu, structured in three sections:

#### Top: Probe Identity & Live Status

- Probe type icon, name, and subtype label
- Live triggered indicator (green/red dot)
- Current Z offset and last Z reading (real-time via LVGL subjects)

#### Middle: Probe-Type-Specific Config Panel

Content swaps based on detected probe type:

**BLTouch:**
- Deploy / Stow / Reset buttons (`BLTOUCH_DEBUG COMMAND=pin_down/pin_up/reset`)
- Self-Test button with pass/fail result (`BLTOUCH_DEBUG COMMAND=self_test`)
- Output mode selector: 5V / OD (`SET_BLTOUCH OUTPUT_MODE=...`)
- Config: `stow_on_each_sample`, `probe_with_touch_mode` (via config editor)

**Cartographer:**
- Active model selector dropdown (`CARTOGRAPHER_MODEL_SELECT NAME=...`)
- Coil temperature readout (real-time)
- Calibrate button (`CARTOGRAPHER_CALIBRATE`)
- Touch Calibrate button (`CARTOGRAPHER_TOUCH_CALIBRATE`)

**Beacon:**
- Active model selector dropdown (`BEACON_MODEL_SELECT NAME=...`)
- Temperature compensation status and sensor temp
- Calibrate button (`BEACON_CALIBRATE`)
- Auto-Calibrate button (`BEACON_AUTO_CALIBRATE`)

**Standard Probe / Smart Effector / Tap / Klicky:**
- Basic identification and status display
- Klicky: Deploy/Dock buttons if macros detected (ATTACH_PROBE/DOCK_PROBE)
- For probes without special APIs, the universal actions below do the heavy lifting

#### Bottom: Universal Actions

- **Probe Accuracy Test**: Runs `PROBE_ACCURACY`, displays results inline (standard deviation, range, min/max, sample count)
- **Z-Offset Calibration**: Navigates to existing z-offset calibration overlay
- **Bed Mesh**: Navigates to existing bed mesh overlay

### 3. Expanded Probe Detection

Add to `ProbeSensorManager` and `ProbeSensorType` enum:

| Type | Klipper Object | Detection Method |
|------|---------------|------------------|
| Standard | `probe` | Object list |
| BLTouch | `bltouch` | Object list |
| Smart Effector | `smart_effector` | Object list |
| Eddy Current | `probe_eddy_current <name>` | Object list prefix match |
| Cartographer | `cartographer` or eddy with cartographer macros | Object list + macro heuristics |
| Beacon | `beacon` or eddy with beacon macros | Object list + macro heuristics |
| Tap / Voron Tap | `probe` + macro heuristics | `TAP_VERSION` or tap-related macros |
| Klicky | No dedicated object | Macro heuristics: `ATTACH_PROBE`, `DOCK_PROBE` |

For Cartographer and Beacon, detection may need to combine object list (they register as `probe_eddy_current`) with macro/command inspection to distinguish them from generic eddy probes.

### 4. Klipper Config Editor Infrastructure

A reusable `KlipperConfigEditor` class for safe, targeted editing of Klipper config files.

#### Architecture

```
KlipperConfigEditor
├── resolve_includes()
│   ├── Parse [include ...] directives from printer.cfg
│   ├── Support glob patterns ([include hardware/*.cfg])
│   ├── Read included files via Moonraker file API
│   ├── Recursive with cycle detection
│   └── Build: section_name → { file_path, line_range }
│
├── find_section()        // "where is [probe]?" → "hardware/probe.cfg:12-24"
├── get_key_line()        // Within that file, which line has 'samples ='?
│
├── set_value()           // Edit the correct file
├── add_key()             // Add new key to a section
├── remove_key()          // Comment out a key (safer than deleting)
│
└── Safety:
    ├── backup_all()      // Backup every file we touch
    ├── restore_all()     // Restore all backups on failure
    └── validate_structure()  // Re-parse after edit
```

#### Reading Config Values

Use Moonraker's existing `query_configfile()` (already implemented) to read parsed values as structured JSON. No parsing needed for reads.

#### Writing Config Values

Targeted text manipulation — NOT a full INI rewrite. This preserves comments, formatting, and structure.

**Strategy**: Use GCode + `SAVE_CONFIG` for runtime-changeable values, config file editing only for values that require it:

| Field | Runtime? | Method |
|-------|----------|--------|
| z_offset | Yes | `SAVE_CONFIG` (existing z-offset calibration) |
| BLTouch output_mode | Yes | `SET_BLTOUCH` + `SAVE_CONFIG` |
| x_offset, y_offset | No | Config edit |
| samples | No | Config edit |
| speed | No | Config edit |
| sample_retract_dist | No | Config edit |
| samples_tolerance | No | Config edit |

#### Include Resolution

Many printers split config across multiple files:

```
printer.cfg
├── [include hardware/probe.cfg]
├── [include hardware/steppers.cfg]
├── [include macros/*.cfg]
└── [printer], [extruder], etc.
```

The editor must:

1. Parse `[include ...]` directives from printer.cfg
2. Resolve glob patterns using Moonraker's file list API
3. Recursively read included files (cap at 5 levels)
4. Build a section → file mapping
5. Edit the correct file, not just printer.cfg

#### Klipper Config Format Rules

Ported from Kalico's `configfile.py` (`/Users/pbrown/Code/Printing/kalico/klippy/configfile.py`):

| Rule | Detail |
|------|--------|
| Delimiters | Both `:` and `=` — preserve whichever the file uses |
| Multi-line values | Continuation lines are indented (spaces or tabs) |
| Empty lines in multi-line | Preserved (`empty_lines_in_values=True`) |
| Comments | `#` and `;` inline — always preserve, never strip |
| SAVE_CONFIG section | After `#*# <--- SAVE_CONFIG --->` marker, `#*# ` prefix on every line |
| Case | Section names case-sensitive, option names lowercase |
| Duplicate sections | Last wins (merged), but we should never create dupes |
| Includes | `[include file.cfg]` with glob support |
| Variable interpolation | `${section:option}` — not needed for editing, ignore |

#### Safety Protocol

1. **Backup** every file we're about to edit (`file.cfg` → `file.cfg.helix_backup`) via Moonraker file API
2. **Edit** with targeted text manipulation (find section → find key → replace value)
3. **Validate structure** — re-parse our edit to verify it's still valid
4. **Write** modified file via Moonraker file API
5. **FIRMWARE_RESTART** via Moonraker
6. **Monitor** — wait for Klipper to reconnect (~15s timeout)
   - Success: delete all `.helix_backup` files, show confirmation
   - Failure: auto-restore ALL backups, FIRMWARE_RESTART again, show error

#### Edge Cases

| Case | Strategy |
|------|----------|
| Section split across files | Error — don't attempt, tell user |
| Include with glob (`*.cfg`) | Resolve via Moonraker file list |
| Relative include paths | Relative to the file containing the `[include]` |
| Missing included file | Skip with warning |
| Section in SAVE_CONFIG area | Edit `#*#` section of printer.cfg (always in main file) |
| Deeply nested includes (>5) | Cap recursion, warn user |

---

## Implementation Phases

### Phase 1: Probe Detection & Status
- Expand `ProbeSensorType` enum with Cartographer, Beacon, Tap, Klicky
- Detection logic in `ProbeSensorManager` (object list + macro heuristics)
- Probe overlay with identity/status header and universal actions
- PROBE_ACCURACY test with inline results

### Phase 2: Type-Specific Panels
- BLTouch controls (deploy/stow/self-test/output mode via GCode)
- Cartographer panel (model select, calibrate, coil temp)
- Beacon panel (model select, temp comp, calibrate)
- Klicky deploy/dock via macro buttons

### Phase 3: Klipper Config Editor
- `KlipperConfigEditor` class with include resolution
- Targeted text manipulation (set_value, add_key, remove_key)
- Backup/restore safety net
- Post-edit health monitoring with auto-revert

### Phase 4: Config-Based Probe Settings
- Wire probe overlay to config editor for non-runtime settings
- x/y offset, samples, speed, retract distance, tolerance
- Settings show current values from `query_configfile()`, edit via config editor

### Phase 5: Wizard Detected Hardware Step
- New consolidated hardware detection step
- Replace AMS Identify (step 7) and Probe Sensor Select (step 10)
- Show probe, AMS, LEDs, accelerometer, other sensors
- Skip logic: skip if no interesting hardware detected

---

## Files Affected

**New files:**
- `src/ui/overlays/probe_overlay.cpp` / `.h` — Probe management overlay
- `src/system/klipper_config_editor.cpp` / `.h` — Config editor infrastructure
- `ui_xml/probe_overlay.xml` — Main probe overlay layout
- `ui_xml/components/probe_bltouch_panel.xml` — BLTouch-specific controls
- `ui_xml/components/probe_cartographer_panel.xml` — Cartographer-specific controls
- `ui_xml/components/probe_beacon_panel.xml` — Beacon-specific controls
- `ui_xml/components/probe_generic_panel.xml` — Fallback for simple probes
- `ui_xml/wizard_detected_hardware.xml` — New wizard step

**Modified files:**
- `include/probe_sensor_types.h` — New probe type enum values
- `src/sensors/probe_sensor_manager.cpp` — Expanded detection logic
- `src/ui/ui_wizard.cpp` — Replace AMS + probe steps with detected hardware step
- `src/ui/ui_wizard_probe_sensor_select.cpp` — Refactor or remove
- `include/moonraker_api.h` / `src/api/moonraker_api_config.cpp` — May need new queries for probe-specific objects

**Test files:**
- `tests/test_klipper_config_editor.cpp` — Unit tests for config parser/editor
- `tests/test_probe_detection.cpp` — Probe type detection tests
