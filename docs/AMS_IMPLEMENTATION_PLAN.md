# Multi-Filament/AMS Support Implementation Plan

**Feature Branch:** `feature/ams-support`
**Worktree:** `/Users/pbrown/Code/Printing/helixscreen-ams-feature`
**Started:** 2025-12-07
**Last Updated:** 2025-12-08

---

## Overview

Add support for both **Happy Hare** and **AFC-Klipper-Add-On** multi-filament systems to HelixScreen with a Bambu-inspired UI.

### Supported Systems

| System | Detection | Moonraker Object |
|--------|-----------|------------------|
| Happy Hare | `mmu` in printer.objects.list | `printer.mmu.*` variables |
| AFC-Klipper-Add-On | `afc` in printer.objects.list | Lane-based, Moonraker DB |

### Design Decisions

| Category | Decision |
|----------|----------|
| **Detection** | Auto-detect from Klipper objects + manual override |
| **Spoolman** | Required integration for material/color info |
| **Multi-unit** | Design for it, implement single first |
| **Navigation** | Dedicated nav icon (panel 6) |
| **Screen Target** | 480x800 primary |
| **Visual Style** | Semi-realistic slots with color swatches |
| **Animations** | Filament path animation during operations (Phase 4) |
| **Print Preview** | Prominent color requirements display |
| **In-Print** | Minimal overlay during tool changes |
| **Error Recovery** | Adaptive wizard (simple default, expandable) |

---

## Architecture

### Backend Layer (Manager → Backend → Platform)

```
AmsState (singleton, reactive subjects)
    └── owns: unique_ptr<AmsBackend>
              ├── AmsBackendHappyHare (Phase 2)
              ├── AmsBackendAfc (Phase 2)
              └── AmsBackendMock (Phase 0) ✅
```

### Reactive State Layer (AmsState)

| Subject | Type | Description |
|---------|------|-------------|
| `ams_type` | int | 0=none, 1=happy_hare, 2=afc |
| `ams_action` | int | AmsAction enum |
| `ams_action_detail` | string | Human-readable status |
| `ams_current_gate` | int | -1 if none |
| `ams_current_tool` | int | Tool number |
| `ams_filament_loaded` | int | 0/1 boolean |
| `ams_gate_count` | int | Number of gates |
| `ams_gates_version` | int | Bump on any gate change |
| `ams_gate_N_color` | int | RGB packed (N=0-15) |
| `ams_gate_N_status` | int | GateStatus enum (N=0-15) |

### UI Component Hierarchy

```
ams_panel.xml (main panel)
├── header_bar "Multi-Filament"
├── slot_grid (row_wrap flex, 8 slots)
│   └── ams_slot.xml × 8 (reusable component)
│       ├── color_swatch (circle with filament color)
│       ├── status_icon (check/error/empty)
│       └── slot_label (number)
├── status_section
│   ├── action_progress (spinner)
│   └── status_label (bound to ams_action_detail)
└── action_buttons
    ├── btn_unload
    └── btn_home

Future Modals (Phase 3+):
├── ams_context_menu.xml (Edit/Load/Unload)
├── ams_edit_modal.xml (Spoolman integration)
├── ams_error_recovery_modal.xml
├── ams_print_preview_overlay.xml
└── ams_in_print_status.xml
```

---

## Phase Progress

### ✅ Phase 0: Foundation (COMPLETE)

**Goal:** Detection, basic state, mock backend

**Files Created:**
- [x] `include/ams_types.h` - Core data structures
  - AmsType, GateStatus, GateInfo, AmsUnit, AmsSystemInfo enums/structs
  - Conversion functions for Happy Hare values
  - String conversion helpers
- [x] `include/ams_error.h` - Error handling
  - AmsResult enum (25+ error codes)
  - AmsError struct with result + message
  - AmsErrorHelper factory class
- [x] `include/ams_backend.h` - Abstract interface
  - Virtual methods: start, stop, load, unload, select, home
  - Event callback system
  - Factory methods: create(AmsType), create_mock(count)
- [x] `include/ams_backend_mock.h` - Mock header
- [x] `src/ams_backend_mock.cpp` - Mock implementation
  - 8 sample filaments (PLA, PETG, ABS, TPU colors)
  - Simulated timing for operations
  - Thread-safe with mutex
- [x] `src/ams_backend.cpp` - Factory implementation
  - Uses RuntimeConfig::should_mock_ams()
- [x] `include/ams_state.h` - Reactive state header
- [x] `src/ams_state.cpp` - Reactive state implementation
  - Singleton with LVGL subjects
  - Per-gate subjects (16 max)
  - Observer callback routing

**Files Modified:**
- [x] `include/runtime_config.h` - Added:
  - `bool use_real_ams = false`
  - `bool should_mock_ams() const`
- [x] `include/printer_capabilities.h` - Added:
  - `bool has_mmu() const`
  - `AmsType get_mmu_type() const`
  - Private members: `has_mmu_`, `mmu_type_`
- [x] `src/printer_capabilities.cpp` - Added:
  - Detection for "mmu" → HAPPY_HARE
  - Detection for "afc" → AFC

**Verification:**
- [x] Build succeeds
- [x] Critical-reviewer passed

---

### ✅ Phase 1: Core UI (COMPLETE)

**Goal:** Static visualization panel

**Files Created:**
- [x] `include/ui_panel_ams.h` - Panel class header
  - Inherits PanelBase
  - ObserverGuard for RAII cleanup
  - MAX_VISIBLE_SLOTS = 8
- [x] `src/ui_panel_ams.cpp` - Panel implementation
  - Two-phase init (init_subjects → setup)
  - Observer callbacks with null checks
  - Slot click handlers with bounds validation
  - Action button handlers
- [x] `ui_xml/ams_panel.xml` - Main panel layout
  - Overlay pattern (right_mid, 83% width)
  - Slot grid with row_wrap flex
  - Status section with spinner + label
  - Action buttons (Unload, Home)
- [x] `ui_xml/ams_slot.xml` - Reusable slot component
  - API prop: slot_number
  - color_swatch, status_icon, slot_label
  - Responsive sizing (min/max constraints)

**Files Modified:**
- [x] `src/main.cpp` - Component registration:
  - `lv_xml_register_component_from_file("A:ui_xml/ams_slot.xml")`
  - `lv_xml_register_component_from_file("A:ui_xml/ams_panel.xml")`

**Critical Fixes Applied:**
- [x] Observer callbacks check `panel_ != nullptr`
- [x] `handle_slot_tap()` validates against gate_count

**Verification:**
- [x] Build succeeds
- [x] Critical-reviewer passed (after fixes)

---

### ✅ Phase 2: Basic Operations (COMPLETE)

**Goal:** Real backend implementations, load/unload/select

**Files Created:**
- [x] `include/ams_backend_happy_hare.h`
- [x] `src/ams_backend_happy_hare.cpp`
  - Commands: `T{n}`, `MMU_LOAD`, `MMU_UNLOAD`, `MMU_SELECT`, `MMU_RECOVER`, `MMU_HOME`
  - Parses `printer.mmu.*` variables via status update callbacks
- [x] `include/ams_backend_afc.h`
- [x] `src/ams_backend_afc.cpp`
  - Lane-based commands: `AFC_LOAD`, `AFC_UNLOAD`, `AFC_HOME`
  - Moonraker database for lane_data
- [x] `ui_xml/ams_context_menu.xml`
  - Load, Unload, Edit options
  - Positioned near tapped slot

**Files Modified:**
- [x] `src/ui_panel_ams.cpp` - Context menu on slot tap
- [x] `src/ams_backend.cpp` - Factory creates real backends
- [x] `include/ams_backend.h` - Factory overload with API/client params
- [x] `src/main.cpp` - Context menu component registration

**Happy Hare Variables Parsed:**
```
printer.mmu.gate (current gate)
printer.mmu.tool (current tool)
printer.mmu.filament (loaded state)
printer.mmu.gate_status (array: -1=unknown, 0=empty, 1=available, 2=from_buffer)
printer.mmu.gate_color_rgb (array of RGB values)
printer.mmu.gate_material (array of material strings)
printer.mmu.action (current operation)
printer.mmu.ttg_map (tool-to-gate mapping)
printer.mmu.endless_spool_groups
```

**Additional Fixes (2025-12-08):**
- [x] **Deadlock in mock backend** - `start()` and `set_gate_info()` were calling
      `emit_event()` while holding the mutex. Since `emit_event()` also locks the
      mutex and `std::mutex` is non-recursive, this caused deadlock. Fixed by
      releasing the lock before emitting.
- [x] **Shutdown crash** - `AmsState::~AmsState()` called `stop()` which logged
      via spdlog, but during static destruction the logger may already be destroyed.
      Removed logging from `stop()` to prevent SIGSEGV.
- [x] **CLI access** - Added `-p ams` flag to main.cpp to open AMS panel directly
      for testing. Also added backend creation in `AmsPanel::init_subjects()`.

**Verification:**
- [x] Build succeeds
- [x] Context menu appears on slot tap
- [x] **Panel displays with mock data** - 4 colored slots (Red/Blue/Green/Yellow)
- [x] **Clean shutdown** - No crash on exit
- [ ] Live testing with Happy Hare printer (deferred)
- [ ] Live testing with AFC printer (deferred)

**TODO - Wizard Integration:**
- [ ] Add AMS detection step to connection wizard
- [ ] Show detected AMS type (Happy Hare / AFC / None)
- [ ] Allow manual override in settings

---

### 🔲 Phase 3: Spoolman Integration (NOT STARTED)

**Goal:** Edit modal with Spoolman data

**Files to Create:**
- [ ] `include/spoolman_client.h`
- [ ] `src/spoolman_client.cpp`
  - HTTP client for Spoolman API
  - Spool list, vendor list, filament list
- [ ] `ui_xml/ams_edit_modal.xml`
  - Spoolman spool dropdown
  - Manual color picker
  - Material type selector
  - Weight/remaining display

**Files to Modify:**
- [ ] `src/moonraker_client.cpp` - Spoolman detection
- [ ] `include/ams_state.h` - Spoolman fields per gate

**Verification:**
- [ ] Edit modal opens from context menu
- [ ] Spoolman spools populate dropdown
- [ ] Slot mapping persists to backend

---

### 🔲 Phase 4: Rich Feedback (NOT STARTED)

**Goal:** Filament path animations

**Files to Create:**
- [ ] `include/ui_ams_path_animator.h`
- [ ] `src/ui_ams_path_animator.cpp`
  - Canvas-based path drawing
  - Animated colored segment
- [ ] `ui_xml/ams_operation_overlay.xml`

**Animation States:**
- IDLE: Gray paths shown
- LOADING: Colored segment slot→extruder
- UNLOADING: Colored segment extruder→slot
- CHANGING: Sequential unload/load

**Verification:**
- [ ] Smooth path animations during load
- [ ] Progress visible during operations
- [ ] No UI freeze

---

### 🔲 Phase 5: Print Integration (NOT STARTED)

**Goal:** Color preview and in-print status

**Files to Create:**
- [ ] `include/gcode_color_extractor.h`
- [ ] `src/gcode_color_extractor.cpp`
  - Parse M600/tool change commands
  - Extract color requirements from G-code
- [ ] `ui_xml/ams_print_preview_overlay.xml`
- [ ] `ui_xml/ams_in_print_status.xml`

**Files to Modify:**
- [ ] `ui_xml/print_file_detail.xml` - Color swatches
- [ ] `src/ui_panel_print_status.cpp` - AMS section

**Verification:**
- [ ] Print detail shows required colors
- [ ] Warning for missing/mismatched colors
- [ ] Minimal overlay during tool changes

---

### 🔲 Phase 6: Error Recovery (NOT STARTED)

**Goal:** Adaptive recovery wizard

**Files to Create:**
- [ ] `ui_xml/ams_error_recovery_modal.xml`
- [ ] `include/ui_wizard_ams_recovery.h`
- [ ] `src/ui_wizard_ams_recovery.cpp`
- [ ] Visual diagram assets (filament path, jam locations)

**Wizard Flow:**
1. Error identification with diagram
2. Physical intervention instructions
3. Retry operation
4. Success/failure handling

**Error Types:**
- Filament jam (in selector, in tube, at extruder)
- Gate blocked
- Sensor error
- Encoder error
- Homing failed

**Verification:**
- [ ] Error triggers wizard
- [ ] Diagrams show problem location
- [ ] Recovery commands work
- [ ] Print resumes after recovery

---

### 🔲 Phase 7: Advanced Features (NOT STARTED)

**Goal:** Mapping, endless spool, calibration

**Files to Create:**
- [ ] `ui_xml/ams_advanced_panel.xml`

**Features:**
- [ ] Tool-to-gate mapping UI
- [ ] Endless spool group configuration
- [ ] Calibration shortcuts (gate calibration, encoder calibration)
- [ ] Multi-unit selector (if applicable)

**Verification:**
- [ ] Mapping changes persist
- [ ] Endless spool groups function
- [ ] Calibration accessible

---

### 🔲 Phase 8: Polish (NOT STARTED)

**Goal:** Hardening and documentation

**Deliverables:**
- [ ] Stress testing (rapid operations, disconnect/reconnect)
- [ ] Touch target verification (44px minimum)
- [ ] `docs/AMS_USER_GUIDE.md`
- [ ] Settings panel AMS section
- [ ] Unit test suite for AMS classes

---

## Key Data Structures Reference

```cpp
// ams_types.h
enum class AmsType { NONE = 0, HAPPY_HARE = 1, AFC = 2 };

enum class GateStatus {
    UNKNOWN = 0,   // Not yet queried
    EMPTY = 1,     // No filament detected
    AVAILABLE = 2, // Filament present, ready to load
    LOADED = 3,    // Currently loaded in extruder
    FROM_BUFFER = 4, // Available from buffer (endless spool)
    BLOCKED = 5    // Sensor error or jam
};

enum class AmsAction {
    IDLE = 0, LOADING, UNLOADING, SELECTING, HOMING,
    FORMING_TIP, CUTTING, PAUSED, ERROR
};

struct GateInfo {
    int gate_index = -1;
    GateStatus status = GateStatus::UNKNOWN;
    std::string color_name;
    uint32_t color_rgb = 0x808080;
    std::string material;
    int mapped_tool = -1;
    int spoolman_id = -1;
    float remaining_weight_g = -1.0f;
};

struct AmsSystemInfo {
    AmsType type = AmsType::NONE;
    AmsAction action = AmsAction::IDLE;
    std::string operation_detail;
    int current_gate = -1;
    int current_tool = -1;
    bool filament_loaded = false;
    int total_gates = 0;
    std::vector<AmsUnit> units;
    // Helper: get_gate_global(index) for flat access
};
```

---

## Critical Reference Files

| Purpose | File |
|---------|------|
| Backend pattern | `include/wifi_backend.h` |
| Reactive state | `include/printer_state.h` |
| Panel base | `include/ui_panel_base.h` |
| Modal pattern | `ui_xml/wifi_password_modal.xml` |
| Animation | `include/ui_heating_animator.h` |
| Discovery | `src/moonraker_client.cpp` |

---

## Icons Needed

| Icon | MDI Name | Purpose |
|------|----------|---------|
| `spool` | printer-3d-nozzle-heat | Filament spool |
| `humidity` | water-percent | Humidity sensor |
| `palette` | palette | Color picker |
| `tray` | tray | AMS unit |

Add to `include/ui_icon_codepoints.h` and run `make regen-fonts`.

---

## Testing Commands

```bash
# Build
cd /Users/pbrown/Code/Printing/helixscreen-ams-feature
make -j

# Run AMS panel directly with mock backend (recommended for testing)
./build/bin/helix-screen --test -p ams -s large -vv

# Run normal UI with mock AMS available
./build/bin/helix-screen --test -vv

# With screenshot on startup
HELIX_AUTO_SCREENSHOT=1 HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p ams -vv

# Future: Run with real Happy Hare
./build/bin/helix-screen --real-ams -vv
```

---

## Session Resume Checklist

When resuming work on this feature:

1. **Check current phase** - See "Phase Progress" section above
2. **Switch to worktree**: `cd /Users/pbrown/Code/Printing/helixscreen-ams-feature`
3. **Check branch**: `git branch` should show `feature/ams-support`
4. **Read last completed phase** - Note any partial work
5. **Run build** to verify state: `make -j`
6. **Continue with next unchecked item**

---

## Estimated Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| 0: Foundation | 2-3 days | 3 days ✅ |
| 1: Core UI | 3-4 days | 7 days ✅ |
| 2: Operations | 4-5 days | 12 days |
| 3: Spoolman | 2-3 days | 15 days |
| 4: Animations | 3-4 days | 19 days |
| 5: Print | 4-5 days | 24 days |
| 6: Recovery | 4-5 days | 29 days |
| 7: Advanced | 3-4 days | 33 days |
| 8: Polish | 2-3 days | 36 days |

**Total: ~5-6 weeks of focused development**
