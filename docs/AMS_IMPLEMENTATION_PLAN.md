# Multi-Filament/AMS Support Implementation Plan

**Feature Branch:** `feature/ams`
**Repository:** `/Users/pbrown/code/helixscreen-ams`
**Started:** 2025-12-07
**Last Updated:** 2025-12-18 (Marked Phase 3 complete - real API was already implemented)

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
| `ams_current_slot` | int | -1 if none |
| `ams_current_tool` | int | Tool number |
| `ams_filament_loaded` | int | 0/1 boolean |
| `ams_slot_count` | int | Number of slots |
| `ams_slots_version` | int | Bump on any slot change |
| `ams_slot_N_color` | int | RGB packed (N=0-15) |
| `ams_slot_N_status` | int | SlotStatus enum (N=0-15) |

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
    └── btn_reset

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
  - AmsType, SlotStatus, SlotInfo, AmsUnit, AmsSystemInfo enums/structs
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
  - Per-slot subjects (16 max)
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
- [x] `handle_slot_tap()` validates against slot_count

**Verification:**
- [x] Build succeeds
- [x] Critical-reviewer passed (after fixes)

---

### ✅ Phase 2.5: Spool Visualization (COMPLETE)

**Goal:** Bambu-style pseudo-3D filament spool widget

**Files Created:**
- [x] `include/ui_spool_canvas.h` - Spool canvas widget header
- [x] `src/ui_spool_canvas.cpp` - Custom LVGL XML widget implementation
  - Coverage-based anti-aliasing for smooth ellipse edges
  - Physically correct side-view rendering
  - Gradient lighting effects (filament + hub hole)
- [x] `include/ui_ams_slot.h` - C++ AMS slot component
- [x] `src/ui_ams_slot.cpp` - Dynamic data binding for slots
- [x] `ui_xml/test_panel.xml` - Side-by-side comparison test
- [x] `ui_xml/spool_test.xml` - Dedicated spool testing

**Spool Canvas Features:**
| Feature | Implementation |
|---------|----------------|
| 3D perspective | Narrow ellipses (45% horizontal compression) |
| Physical correctness | Front flange solid, filament only visible from side |
| Fill level | 0.0-1.0 controls wound filament radius |
| Filament gradient | sqrt curve for fast light→dark transition |
| Flange gradient | Vertical gradient (bright top → dark bottom) |
| Edge highlights | 2px bright-to-dark gradient on flange left edges |
| Hub hole gradient | Dark top → light bottom (interior shadow) |
| Anti-aliasing | Coverage-based edge smoothing + pole pixels |
| XML attributes | `color`, `fill_level`, `size` |

**Drawing Algorithm (back-to-front):**
1. Back flange (gradient ellipse + left edge highlight)
2. Filament cylinder with gradient (back ellipse + rectangle + front ellipse)
3. Front flange (gradient ellipse + left edge highlight)
4. Hub hole with gradient (shadow effect)

**Enhanced Rendering (2025-12-08):**
- [x] sqrt() curve on gradients for faster light-to-dark transition
- [x] Edge highlights on flanges (2px band along left edge, gradient top→bottom)
- [x] Pole pixel rendering for smoother top/bottom ellipse edges
- [x] Increased lighten/darken amounts for more dramatic 3D effect

**XML Usage:**
```xml
<spool_canvas color="0xFF5733" fill_level="0.75" size="64"/>
```

**Verification:**
- [x] Build succeeds
- [x] Anti-aliased edges render smoothly
- [x] Fill levels display correctly (100%/75%/40%/10%)
- [x] Gradients visible on filament and hub
- [x] Edge highlights give flanges 3D thickness illusion

**Known Limitations:**
- Ellipse poles (top/bottom) appear somewhat flat due to horizontal compression
  at 72px resolution - this is inherent to compressed ellipse geometry

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
- [x] **Deadlock in mock backend** - `start()` and `set_slot_info()` were calling
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

### 🔲 Phase 2.6: Configurable Visualization (DEFERRED)

**Goal:** Allow users to choose between visualization styles

**Configuration Options:**
| Setting | Values | Description |
|---------|--------|-------------|
| `ams_spool_style` | `"3d"` / `"flat"` | Pseudo-3D canvas or flat concentric rings |

**Deferred:** Will implement after path visualization is complete.

---

### ✅ Phase 2.7: Filament Path Data Model (COMPLETE)

**Goal:** Add data model for filament path visualization

**Files Modified:**
- [x] `include/ams_types.h` - Added:
  - `PathTopology` enum (LINEAR for Happy Hare, HUB for AFC)
  - `PathSegment` enum (NONE, SPOOL, PREP, LANE, HUB, OUTPUT, TOOLHEAD, NOZZLE)
  - `path_topology_to_string()`, `path_segment_to_string()` helpers
  - `path_segment_from_happy_hare_pos()` - converts filament_pos to PathSegment
  - `path_segment_from_afc_sensors()` - infers PathSegment from sensor states
- [x] `include/ams_state.h` / `src/ams_state.cpp` - Added path subjects:
  - `ams_path_topology` (int) - PathTopology enum
  - `ams_path_active_slot` (int) - Currently loaded slot
  - `ams_path_filament_segment` (int) - PathSegment where filament is
  - `ams_path_error_segment` (int) - PathSegment with error for highlighting
  - `ams_path_anim_progress` (int) - Animation progress 0-100
  - All registered with XML system for reactive binding
  - `sync_from_backend()` updated to sync path subjects
- [x] `include/ams_backend.h` - Added pure virtual methods:
  - `get_topology()` - Returns PathTopology
  - `get_filament_segment()` - Returns current filament position
  - `infer_error_segment()` - Returns error location for highlighting
- [x] `include/ams_backend_mock.h` / `src/ams_backend_mock.cpp`:
  - Implemented path methods (default HUB topology)
  - Updates `filament_segment_` during load/unload
  - Updates `error_segment_` during `simulate_error()`
- [x] `include/ams_backend_happy_hare.h` / `src/ams_backend_happy_hare.cpp`:
  - LINEAR topology
  - Tracks `filament_pos_` from Moonraker status updates
  - Uses `path_segment_from_happy_hare_pos()` for conversion
- [x] `include/ams_backend_afc.h` / `src/ams_backend_afc.cpp`:
  - HUB topology
  - Tracks sensor states (`prep_sensor_`, `hub_sensor_`, `toolhead_sensor_`)
  - Uses `path_segment_from_afc_sensors()` for position inference

**Verification:**
- [x] Build succeeds
- [x] Path subjects registered (5 path subjects logged)
- [x] `sync_from_backend()` logs segment state

**Next:** Phase 3 - Path Canvas Widget (see `docs/FILAMENT_PATH_VISUALIZATION_PLAN.md`)

---

### ✅ Phase 3: Spoolman Integration (COMPLETE)

**Goal:** Full Spoolman integration with spool assignment to AMS slots

**Status:** Complete - UI, mock backend, real API, and capability detection all implemented.

#### UI Components

**SpoolmanPanel** (`src/ui_panel_spoolman.cpp` - 378 lines):
- [x] Full panel with loading/empty/spool list states
- [x] Scrollable spool list with 3D canvas visualization
- [x] Spool count display and refresh button
- [x] Click-to-select active spool

**Spoolman Picker** (integrated in `src/ui_panel_ams.cpp`):
- [x] Modal dialog for assigning spools to AMS slots
- [x] Scrollable spool list within modal
- [x] Link/unlink functionality
- [x] Completion callbacks with result handling

**XML Layouts:**
- [x] `ui_xml/spoolman_panel.xml` (141 lines) - 3-state responsive layout
- [x] `ui_xml/spoolman_picker_modal.xml` (65 lines) - Slot assignment modal
- [x] `ui_xml/spoolman_spool_row.xml` (122 lines) - Row with 3D spool canvas
- [x] `ui_xml/spoolman_spool_item.xml` (37 lines) - Picker list item
- [x] `ui_xml/ams_edit_modal.xml` - "Sync to Spoolman" button
- [x] `ui_xml/ams_context_menu.xml` - "Assign Spoolman spool" option

**Data Types** (`include/spoolman_types.h` - 112 lines):
- [x] SpoolInfo struct with 20+ fields (vendor, material, color, weights, temps)
- [x] Helper methods: `remaining_percent()`, `is_low()`, `display_name()`

#### Mock Backend (`src/moonraker_api_mock.cpp`)

- [x] `get_spoolman_status()` - Returns mock connected state
- [x] `get_spoolman_spools()` - Returns 8 sample filaments
- [x] `get_spoolman_spool()` - Single spool lookup by ID
- [x] `update_spoolman_spool_weight()` - Update remaining weight
- [x] `set_mock_spoolman_enabled()` / `is_mock_spoolman_enabled()`
- [x] `assign_spool_to_slot()` - Slot-spool mapping (c9d891f)
- [x] `get_spool_for_slot()` - Retrieve spool for slot (c9d891f)
- [x] `consume_filament()` - Track usage (c9d891f)

#### Real Moonraker API (`src/moonraker_api_advanced.cpp`)

- [x] `get_spoolman_status()` → `server.spoolman.status` (lines 713-737)
- [x] `get_spoolman_spools()` → `server.spoolman.proxy` → `/v1/spool` (lines 739-766)
- [x] `get_spoolman_spool(id)` → `server.spoolman.proxy` → `/v1/spool/{id}` (lines 768-795)
- [x] `set_active_spool(id)` → `server.spoolman.post_spool_id` (lines 797-814)
- [x] `update_spoolman_spool_weight()` → `server.spoolman.proxy` PATCH (lines 828-854)
- [x] `update_spoolman_filament_color()` → `server.spoolman.proxy` PATCH (lines 856-880)
- [x] `parse_spool_info()` helper for JSON→SpoolInfo conversion (lines 679-711)

#### Capability Detection (`src/moonraker_client.cpp`)

- [x] Parse "spoolman" from `server.info` components array (lines 962-964)
- [x] Verify connection via `server.spoolman.status` (lines 970-986)
- [x] Set `printer_has_spoolman_` subject via `PrinterState::set_spoolman_available()` (line 980)
- [x] Thread-safe subject update using `lv_async_call` (printer_state.cpp:976-985)

#### AmsState Spoolman Sync (c9d891f)

- [x] Auto-call `set_active_spool()` when slot with `spoolman_id` is loaded
- [x] `HELIX_MOCK_SPOOLMAN=0` env var to disable mock Spoolman
- [x] 12 unit tests for slot-spool mapping

**Verification:**
- [x] Spoolman panel shows spool list in `--test` mode
- [x] Picker modal opens from AMS context menu
- [x] Spool assignment to slots works (mock and real)
- [x] Capability detection works with real Moonraker servers

---

### ✅ Phase 4: Rich Feedback (COMPLETE)

**Goal:** Filament path visualization with animations

**Implementation:** See `docs/FILAMENT_PATH_VISUALIZATION_PLAN.md` for details.

**Files Created:**
- [x] `include/ui_filament_path_canvas.h` - Path canvas widget header
- [x] `src/ui_filament_path_canvas.cpp` - Custom LVGL XML widget
  - Theme-aware schematic path drawing
  - Supports HUB (AFC) and LINEAR (Happy Hare) topologies
  - Slots at top → prep sensors → hub/selector → output → toolhead → nozzle
  - Click callback for slot selection
- [x] Bambu-style isometric 3D extruder visualization

**Animation Features:**
- [x] Segment transition animation with glowing filament tip
- [x] Error pulse animation (opacity modulation)
- [x] Thread-safe: AmsState uses recursive_mutex, lv_async_call for UI updates

**Verification:**
- [x] Smooth path animations during load/unload
- [x] Progress visible during operations
- [x] No UI freeze or deadlocks

---

### ✅ Phase 4.5: Real AFC Backend Integration (COMPLETE)

**Goal:** Parse real sensor data from AFC/BoxTurtle to drive path visualization

**Implementation:** See `docs/archive/AFC_BACKEND_INTEGRATION_PLAN.md` for original plan.

**Part A: UI Cleanup - "Home" → "Reset"**
- [x] Renamed `AmsAction::HOMING` → `AmsAction::RESETTING` in `ams_types.h`
- [x] Renamed `home()` → `reset()` in abstract backend interface
- [x] Updated all backends (Mock, Happy Hare, AFC)
- [x] Renamed `btn_home` → `btn_reset` in `ams_panel.xml` with "refresh" icon

**Part B: AFC Version Detection**
- [x] Added `detect_afc_version()` querying `afc-install` database namespace
- [x] Added `version_at_least()` for semantic version comparison
- [x] Sets `has_lane_data_db_` capability flag for v1.0.32+

**Part C: Real Sensor Data Parsing**
- [x] Added `LaneSensors` struct for per-lane sensor states (prep, load, loaded_to_hub)
- [x] Implemented `parse_afc_stepper()`, `parse_afc_hub()`, `parse_afc_extruder()`
- [x] Implemented `compute_filament_segment_unlocked()` with sensor→segment mapping
- [x] Fixed potential deadlock by using internal unlocked helper

**Unit Tests:**
- [x] Added 31 unit tests in `tests/unit/test_ams_backend_afc.cpp`
- [x] Tests for `version_at_least()` (11 tests)
- [x] Tests for `compute_filament_segment_unlocked()` (20 tests)

**Verification:**
- [x] Build succeeds
- [x] All 31 AFC unit tests pass
- [ ] Live testing with real BoxTurtle at 192.168.1.112

---

### ✅ Phase 4.6: External Spool Bypass Support (COMPLETE)

**Goal:** Add UI and backend support for external spool bypass mode

**Background:**
Both Happy Hare and AFC support bypass mode for feeding filament from an external spool,
bypassing the MMU/hub entirely. This is used for single-color prints, testing, or when
the AMS runs out of a color.

- **Happy Hare**: `printer.mmu.gate = -2` indicates bypass mode
- **AFC**: `printer.AFC.bypass_state` boolean, virtual or hardware bypass sensor

**Part A: Backend Fixes**
- [x] AFC backend: `supports_bypass = true`
- [x] AFC backend: Subscribe to `AFC` object, parse `bypass_state`
- [x] AFC backend: Set `current_slot = -2` when `bypass_state == true`
- [x] Added `bypass_active_` member to AFC backend
- [x] Added `enable_bypass()` / `disable_bypass()` / `is_bypass_active()` to all backends

**Part B: State Layer**
- [x] Added `ams_bypass_active` subject (int, 0/1) to AmsState
- [x] Sync from `current_slot == -2` in `sync_from_backend()`

**Part C: Path Canvas Bypass Visualization**
- [x] Added `set_bypass_active(bool)` and `set_bypass_callback()` APIs
- [x] Draw bypass entry point on right side of canvas with "Bypass" label
- [x] Draw direct bypass→OUTPUT→toolhead→nozzle path
- [x] Highlight bypass path when active

**Part D: UI Panel Integration**
- [x] Added "Enable Bypass" button to `ams_panel.xml`
- [x] Button visibility controlled by `supports_bypass` capability
- [x] Button text toggles between "Enable Bypass" / "Disable Bypass"
- [x] "Currently Loaded" shows "External" / "Bypass" when slot is -2
- [x] Simplified button styling (removed flex, using `align="center"`)

**Part E: All Backends**
- [x] Mock backend: Full bypass toggle implementation
- [x] AFC backend: `enable_bypass()` sends `SET_BYPASS ENABLE=1`
- [x] Happy Hare backend: `enable_bypass()` sends `MMU_SELECT_BYPASS`

**Unit Tests:**
- [x] Added bypass tests in `tests/unit/test_ams_backend_afc.cpp`

**UI Layout Fixes:**
- [x] Reduced "Currently Loaded" card padding for better button visibility
- [x] Reduced swatch size from 72px to 56px
- [x] All three buttons visible: Enable Bypass, Unload Filament, Reset

**Part F: Hardware Bypass Sensor Support**
- [x] Added `has_hardware_bypass_sensor` capability flag to `AmsSystemInfo`
- [x] `true` = hardware sensor auto-detects bypass (button disabled, shows state)
- [x] `false` = virtual bypass (button enabled, user toggles manually)
- [x] Mock backend: Added `set_has_hardware_bypass_sensor()` for testing
- [x] AFC backend: Defaults to `true` (BoxTurtle typically has hardware sensor)
- [x] Happy Hare backend: Defaults to `false` (uses selector movement)
- [x] UI: Button shows `LV_STATE_DISABLED` when hardware sensor controls bypass
- [x] UI: Label shows "Bypass Active/Inactive" for hardware, "Enable/Disable Bypass" for virtual
- [x] Toast feedback "Bypass controlled by sensor" if toggle attempted on hardware bypass

**Unit Tests for Hardware Sensor:**
- [x] Added 5 new test sections in `test_ams_backend_mock_bypass.cpp`:
  - Default is virtual bypass (no hardware sensor)
  - Can set hardware bypass sensor mode
  - Can toggle back to virtual bypass
  - Bypass operations work regardless of sensor setting
  - `supports_bypass` flag independent of sensor setting
- [x] All 43 assertions in 3 bypass test cases pass

**Verification:**
- [x] AFC backend reports `supports_bypass = true`
- [x] Path canvas shows bypass entry point with "Bypass" label
- [x] "Enable Bypass" button visible and toggles state
- [x] Mock backend can toggle bypass for testing
- [x] Hardware sensor mode disables button and shows state
- [ ] Live testing with real AFC printer (deferred)

---

### 🔄 Phase 5: Print Integration (PARTIALLY COMPLETE)

**Goal:** Color preview and in-print status

**Status:** G-code color extraction and print preview done. In-print overlay remaining.

#### ✅ Already Implemented

**G-Code Color Extraction** (`src/gcode_parser.cpp`):
- [x] `parse_extruder_color_metadata()` - Parses slicer color comments
- [x] Supports OrcaSlicer, PrusaSlicer, BambuStudio format: `"; extruder_colour = #ED1C24;#00C1AE"`
- [x] Semicolon-separated hex color parsing
- [x] Stores in `ParsedGCodeFile::tool_color_palette`
- [x] Tool change command parsing (T0, T1, T2)

**Print File Data Flow**:
- [x] `PrintFileData::filament_colors` vector passes colors through system
- [x] Moonraker metadata fetched includes slicer color info
- [x] Colors flow from file list → detail view

**Print File Detail UI** (`ui_xml/print_file_detail.xml` + `src/ui_print_select_detail_view.cpp`):
- [x] `color_requirements_card` - Hidden by default, shown for multi-color
- [x] `color_swatches_row` - Flex row for color chips
- [x] `update_color_swatches()` - Creates T0, T1, T2 labeled swatches (lines 279-347)
- [x] Contrast text calculation for readability on light/dark colors
- [x] Card auto-hides for single-color prints

**Verification (Print Preview):**
- [x] Print detail shows required colors with tool labels
- [x] Multi-color G-code files display color swatches
- [x] Single-color files hide the color card

#### ❌ Remaining (In-Print Features)

**AMS In-Print Status Overlay:**
- [ ] `ui_xml/ams_in_print_status.xml` - Compact overlay during printing
- [ ] Show current/next tool color during printing
- [ ] Integrate with `ui_panel_print_status.cpp`

**Real-Time Tool Change Detection:**
- [ ] Detect upcoming M600/tool changes during print
- [ ] Pre-select AMS slot before tool change
- [ ] Show "Tool change imminent" notification

**Color Mismatch Warning** (`src/ui_panel_print_select.cpp`):
- [x] `check_ams_color_match()` - Compares G-code tool colors to AMS slot colors (lines 1754-1812)
- [x] `show_color_mismatch_warning()` - Modal dialog listing missing tools/colors (lines 1814-1865)
- [x] Perceptual color distance matching with tolerance (40/255 threshold)
- [x] User can proceed anyway or cancel to load correct filaments

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
- [ ] Tool-to-slot mapping UI
- [ ] Endless spool group configuration
- [ ] Calibration shortcuts (slot calibration, encoder calibration)
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

enum class SlotStatus {
    UNKNOWN = 0,   // Not yet queried
    EMPTY = 1,     // No filament detected
    AVAILABLE = 2, // Filament present, ready to load
    LOADED = 3,    // Currently loaded in extruder
    FROM_BUFFER = 4, // Available from buffer (endless spool)
    BLOCKED = 5    // Sensor error or jam
};

enum class AmsAction {
    IDLE = 0, LOADING, UNLOADING, SELECTING, RESETTING,
    FORMING_TIP, CUTTING, PAUSED, ERROR
};

struct SlotInfo {
    int slot_index = -1;
    SlotStatus status = SlotStatus::UNKNOWN;
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
    int current_slot = -1;  // -2 = bypass mode
    int current_tool = -1;
    bool filament_loaded = false;
    int total_slots = 0;
    std::vector<AmsUnit> units;
    // Helper: get_slot_global(index) for flat access
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

## Test Hardware

| System | Type | Address | Notes |
|--------|------|---------|-------|
| Voron v2 | AFC Lite (BoxTurtle) | `192.168.1.112` / `voronv2.local` | Primary test target for AFC backend |
| Spoolman | Filament manager | `zeus.local:7912` | Spool/material database for Phase 3 |

---

## Testing Commands

```bash
# Build
cd /Users/pbrown/Code/Printing/helixscreen-ams-feature
make -j

# Run AMS panel directly with mock backend (recommended for UI testing)
./build/bin/helix-screen --test -p ams -s large -vv

# Run normal UI with mock AMS available
./build/bin/helix-screen --test -vv

# With screenshot on startup
HELIX_AUTO_SCREENSHOT=1 HELIX_AUTO_QUIT_MS=3000 ./build/bin/helix-screen --test -p ams -vv

# Connect to real Voron v2 with AFC BoxTurtle (when AFC backend is ready)
./build/bin/helix-screen -c voronv2.local -vv
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
