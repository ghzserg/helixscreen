# Architectural Debt Register

This document tracks known architectural issues identified during codebase audits.
These are not urgent but should be addressed when touching related code.

> **Last Updated:** 2026-01-12
> **Audit Method:** Multi-agent codebase analysis

---

## Priority Legend
- 🔴 **CRITICAL** - Significant maintainability/correctness risk
- 🟠 **HIGH** - Should address soon
- 🟡 **MEDIUM** - Address when touching related code
- 🟢 **LOW** - Nice to have

---

## 1. God Classes

### 1.1 PrinterState (✅ RESOLVED - 2026-01-12)

**File:** `src/printer/printer_state.cpp`
**Header:** `include/printer_state.h`

**Resolution:**
Decomposed into 13 focused, testable domain state classes with 300+ characterization tests:

```
PrinterState (facade - delegates to all components)
├── PrinterTemperatureState    - 4 subjects: extruder/bed temps and targets
├── PrinterMotionState         - 8 subjects: position, homed axes, speed/flow, Z-offset
├── PrinterLedState            - 6 subjects: LED state, RGBW, brightness
├── PrinterFanState            - 2+ dynamic: fan speeds, per-fan subjects
├── PrinterPrintState          - 17 subjects: progress, filename, layers, time, phases
├── PrinterCapabilitiesState   - 14 subjects: hardware capability flags
├── PrinterPluginStatusState   - 2 subjects: helix plugin, phase tracking
├── PrinterCalibrationState    - 7 subjects: retraction, manual probe, motors
├── PrinterHardwareValidationState - 11 subjects: issues, severity, status
├── PrinterCompositeVisibilityState - 5 subjects: derived can_show_* flags
├── PrinterNetworkState        - 5 subjects: connection, klippy, nav buttons
├── PrinterVersionsState       - 2 subjects: klipper/moonraker versions
└── PrinterExcludedObjectsState - 2 subjects: excluded objects version + set
```

**Pattern:** Each component follows SubjectManager RAII, init_subjects/reset_for_testing lifecycle, thread-safe via helix::async::invoke.

**Archived Handoff:** `docs/archive/PRINTERSTATE_DECOMPOSITION_HANDOFF.md`

---

### 1.2 PrintStatusPanel (✅ RESOLVED - 2026-01-12)

**File:** `src/ui/ui_panel_print_status.cpp` (reduced from 2983 to ~1700 lines)

**Resolution:**
Decomposed into focused, testable components with 83 characterization tests (304 assertions):

```
PrintStatusPanel (UI orchestration only)
├── PrintCancelModal              - Cancel print confirmation
├── SaveZOffsetModal              - Z-offset save warning
├── ExcludeObjectModal            - Object exclusion confirmation
├── RunoutGuidanceModal           - 6-action filament runout response
├── PrintTuneOverlay              - Speed/flow/Z-offset tuning (~280 lines)
├── PrintLightTimelapseControls   - LED/timelapse toggles (~90 lines)
├── PrintExcludeObjectManager     - Long-press → modal → undo → API (~200 lines)
└── FilamentRunoutHandler         - Runout detection → modal → macros (~160 lines)
```

**Commits:** 1070db09, 5175942d, 6b9466cb, 5266aed6

---

### 1.3 SettingsPanel (✅ RESOLVED - 2026-01-12)

**File:** `src/ui/ui_panel_settings.cpp` (reduced from 1976 to 935 lines - 53% reduction)

**Resolution:**
Decomposed into focused overlay components with 92 characterization tests (528 assertions):

```
SettingsPanel (UI orchestration only)
├── MachineLimitsOverlay            - Velocity/accel/jerk limits (~220 lines)
├── MacroButtonsOverlay             - Configurable macro buttons (~250 lines)
├── FilamentSensorSettingsOverlay   - Filament sensor list/toggle (~180 lines)
├── HardwareHealthOverlay           - Hardware issue display/actions (~360 lines)
└── DisplaySettingsOverlay          - Brightness, sleep, render modes (~240 lines)
```

**Pattern:** Each overlay follows singleton accessor with StaticPanelRegistry cleanup, lazy init, register_callbacks() for XML event bindings.

**Commits:** 3cea3e6b, b0b3c8c1, bec9de47, ff9235fe, 235d9ada

---

### 1.4 Application (🟠 HIGH)

**File:** `src/application/application.cpp` (1249 lines)

**Problem:**
- Orchestrates 12+ initialization phases
- Owns 6 manager instances
- Mixes startup orchestration with runtime operations
- Hardcoded overlay creation

**Suggested Improvement:**
- Extract `ApplicationBootstrapper` for initialization sequence
- Move runtime operations to `ApplicationRuntime`
- Use dependency injection instead of direct manager creation

---

## 2. Singleton Cascade Pattern

**Severity:** 🟡 MEDIUM

**Pattern Found:**
UI panels call 3-5 singletons per method:
```cpp
SettingsManager::instance().get_dark_mode();
Config::instance().get_display_timeout();
NavigationManager::instance().get_active();
PrinterState::instance().get_printer_connection_state();
```

**Files Affected:** All UI panels (34 calls in ui_panel_settings.cpp alone)

**Suggested Improvement:**
- Create `UIPanelContext` value object passed to all panels
- Use factory pattern: `PanelFactory::create(context)`
- Consider service locator for testing

---

## 3. Inappropriate Intimacy

### 3.1 NavigationManager & UI Panels (🟡 MEDIUM)

**File:** `src/ui/ui_nav_manager.cpp` (1072 lines)

**Problem:**
- Holds array of panel pointers, directly manipulates visibility
- Controls animation state through direct method calls
- Bidirectional coupling with panels

**Suggested Improvement:**
- Create `INavigable` interface with minimal contract
- Use event system instead of direct calls
- Extract `PanelAnimator` for animation logic

### 3.2 AmsBackend & AmsState (🟢 LOW-MEDIUM)

**Problem:**
- AmsState directly accesses backend internals via `get_backend()`
- Event callbacks create implicit contracts
- Stringly-typed events ("SLOT_CHANGED")

**Suggested Improvement:**
- Use structured event objects instead of strings
- Create `IAmsBackendObserver` interface

---

## 4. Feature Envy / Scattered Business Logic

**Severity:** 🟡 MEDIUM

**Problem:**
Business logic scattered across UI panels:
- Temperature formatting (divide by 10) in multiple places
- Print time estimation in PrintStatusPanel
- Z-offset conversion (microns ↔ mm) in multiple panels
- Fan speed computation in multiple controllers

**Suggested Improvement:**
Create `PrinterDisplay` model with pre-computed display values:
```cpp
struct PrinterDisplay {
    float extruder_temp_c;    // Already formatted
    int print_eta_seconds;    // Already computed
    float z_offset_mm;        // Already converted
    std::string layer_text;   // "Layer 42 / 100"
};
```

---

## 5. Mixed Concerns in MoonrakerClient

**Severity:** ~~🟡 MEDIUM~~ 🟢 PARTIALLY RESOLVED (2026-01-11)

**File:** `src/api/moonraker_client.cpp`

**Problem (PARTIALLY RESOLVED):**
- ~~Mixes JSON-RPC transport with domain logic~~ - **RESOLVED:** Hardware discovery data moved to `PrinterDiscovery` in MoonrakerAPI
- ~~Contains: connection management, printer discovery, bed mesh parsing, object list parsing~~ - **RESOLVED:** MoonrakerClient now dispatches via callbacks, MoonrakerAPI owns data
- ~~Discovery callbacks are domain events in transport layer~~ - **RESOLVED:** Clean callback-based architecture

**Completed Improvements (2026-01-11 Hardware Discovery Refactor):**
- MoonrakerClient is now pure transport layer
- Hardware data (heaters, fans, sensors, LEDs, macros, hostname) flows via callbacks to MoonrakerAPI
- Bed mesh data moved from MoonrakerClient to MoonrakerAPI
- `PrinterDiscovery` is the single source of truth for hardware capabilities

**Remaining:** File size still ~1500 lines, could benefit from further decomposition

---

## 6. Code Duplication Patterns

### 6.1 Subject Deinit Boilerplate (135-216 lines across 27 panels)

**Current:**
```cpp
void Panel::deinit_subjects() {
    if (!subjects_initialized_) return;
    lv_subject_deinit(&subject1_);
    lv_subject_deinit(&subject2_);
    // ... 5-15 more calls
    subjects_initialized_ = false;
}
```

**Suggested:** SubjectManagedPanel base class (being implemented)

### 6.2 Panel Singleton Getters (162-216 lines across 27 panels)

**Current:**
```cpp
static std::unique_ptr<MotionPanel> g_motion_panel;
MotionPanel& get_global_motion_panel() {
    if (!g_motion_panel) {
        g_motion_panel = std::make_unique<MotionPanel>();
        StaticPanelRegistry::instance().register_destroy(...);
    }
    return *g_motion_panel;
}
```

**Suggested:** DEFINE_GLOBAL_PANEL macro (being implemented)

### 6.3 Event Callback Registration (200-350 lines across 10 panels)

**Pattern:** Repeated `lv_xml_register_event_cb` calls with similar structure

**Suggested:** Batch registration helper or declarative callback table

### 6.4 Static Callback Trampolines (180-720 lines across 18 panels)

**Pattern:** Every panel declares identical static callback signatures

**Suggested:** Preprocessor macro to generate trampolines

---

## 7. Missing Abstractions

**Severity:** 🟢 LOW

**Patterns:**
- ~~Concrete `PrinterCapabilities` struct instead of interface~~ - **RESOLVED:** PrinterCapabilities deleted, replaced by `PrinterDiscovery` (2026-01-11)
- Concrete `MoonrakerAPI*` parameters instead of interface
- No `IPanel` or `IPanelFactory` interfaces

**Impact:** Hinders testing and extensibility

---

## Appendix: File Size Reference

| File | Lines | Notes |
|------|-------|-------|
| ui_panel_print_status.cpp | ~1700 | ✅ Decomposed (was 2983) |
| ui_panel_settings.cpp | 935 | ✅ Decomposed (was 1976) |
| ui_panel_controls.cpp | 1653 | |
| moonraker_client.cpp | 1595 | Mixed concerns |
| printer_state.cpp | ~627 | ✅ Decomposed (was 1514) - now a facade |
| ui_panel_home.cpp | 1344 | |
| application.cpp | 1249 | |
| ui_nav_manager.cpp | 1072 | |

---

## Change Log

| Date | Change |
|------|--------|
| 2026-01-01 | Initial audit - documented all findings from multi-agent analysis |
| 2026-01-11 | Updated for Hardware Discovery Refactor: PrinterCapabilities deleted, MoonrakerClient mixed concerns partially resolved |
| 2026-01-12 | **PrinterState god class RESOLVED** - All 13 domains extracted (~90+ subjects), PrinterState now a thin facade |
| 2026-01-12 | **SettingsPanel god class RESOLVED** - 5 overlay components extracted (1976→935 lines, 53% reduction), 92 tests with 528 assertions |
