# Architectural Debt Register

This document tracks known architectural issues identified during codebase audits.
These are not urgent but should be addressed when touching related code.

> **Last Updated:** 2026-01-01
> **Audit Method:** Multi-agent codebase analysis

---

## Priority Legend
- 🔴 **CRITICAL** - Significant maintainability/correctness risk
- 🟠 **HIGH** - Should address soon
- 🟡 **MEDIUM** - Address when touching related code
- 🟢 **LOW** - Nice to have

---

## 1. God Classes

### 1.1 PrinterState (🔴 CRITICAL)

**File:** `src/printer/printer_state.cpp` (1514 lines)
**Header:** `include/printer_state.h`

**Problem:**
- 194 public methods (getters/setters) for 80+ reactive subjects
- Single monolithic state container managing: temperatures, print progress, motion, fans, LED, hardware validation, manual probe, excluded objects, print start phases, versions, capabilities
- Referenced by 50+ files across UI, API, and application layers
- Mixes concerns: UI state binding, domain logic, data caching

**Suggested Decomposition:**
```
PrinterState (facade)
├── PrintJobManager        - print state, progress, layers, ETA
├── TemperatureController  - extruder/bed temps, targets, presets
├── MotionState           - position, homed axes, speed/flow factors
├── FanManager            - fan speeds, controllable fans
├── HardwareHealthMonitor - hardware issues, thermal runaway
└── CapabilityRegistry    - printer capabilities, feature flags
```

**Migration Strategy:**
1. Create domain objects with their own subjects
2. PrinterState delegates to domain objects
3. Gradually move callers to use domain objects directly
4. Eventually PrinterState becomes thin facade

---

### 1.2 PrintStatusPanel (🔴 CRITICAL)

**File:** `src/ui/ui_panel_print_status.cpp` (2983 lines - largest in codebase)

**Problem:**
- 200+ methods, 121+ private methods
- Mixes: UI rendering, print control logic, G-code handling, thermal monitoring
- Direct calls to 5+ external managers
- Embedded business logic for print phases, G-code modification

**Suggested Decomposition:**
```
PrintStatusPanel (UI only)
├── PrintJobController     - pause/resume/cancel/exclude logic
├── PrintDisplayModel      - computed display values (layers, ETA, progress)
├── GCodePrintHandler      - G-code processing for print
└── ThermalMonitor         - thermal runaway detection
```

---

### 1.3 Application (🟠 HIGH)

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

**Severity:** 🟡 MEDIUM

**File:** `src/api/moonraker_client.cpp` (1595 lines)

**Problem:**
- Mixes JSON-RPC transport with domain logic
- Contains: connection management, printer discovery, bed mesh parsing, object list parsing
- Discovery callbacks are domain events in transport layer

**Suggested Improvement:**
- Extract `MoonrakerDomainService` for discovery/parsing
- Keep `MoonrakerClient` as pure WebSocket JSON-RPC transport
- Use event bus for discovery events

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
- Concrete `PrinterCapabilities` struct instead of interface
- Concrete `MoonrakerAPI*` parameters instead of interface
- No `IPanel` or `IPanelFactory` interfaces

**Impact:** Hinders testing and extensibility

---

## Appendix: File Size Reference

| File | Lines | Notes |
|------|-------|-------|
| ui_panel_print_status.cpp | 2983 | Largest - needs decomposition |
| ui_panel_settings.cpp | 1654 | |
| ui_panel_controls.cpp | 1653 | |
| moonraker_client.cpp | 1595 | Mixed concerns |
| printer_state.cpp | 1514 | God class |
| ui_panel_home.cpp | 1344 | |
| application.cpp | 1249 | |
| ui_nav_manager.cpp | 1072 | |

---

## Change Log

| Date | Change |
|------|--------|
| 2026-01-01 | Initial audit - documented all findings from multi-agent analysis |
