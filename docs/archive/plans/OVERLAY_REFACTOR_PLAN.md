# Overlay Refactor: Convert Panel-as-Overlays to OverlayBase

**Status:** ✅ COMPLETE
**Started:** 2024-12-30
**Last Updated:** 2025-12-30

---

## Getting Started (New Session)

**If resuming this work in a fresh session:**

1. Read this document to understand current status
2. Check the Quick Status table below for what's complete/in-progress
3. Look at the phase marked 🔄 IN PROGRESS for current work
4. Use the agent workflow: Investigate → Implement → Review → Commit

**Key files to understand:**
- `include/overlay_base.h` - Target base class (has `create()`, lifecycle hooks)
- `include/ui_panel_base.h` - Current base class (has `setup()`, lifecycle hooks)
- `include/ui_nav_manager.h` - `NavigationManager` with `overlay_instances_` map
- `src/ui/network_settings_overlay.cpp` - Gold standard OverlayBase example

**The pattern:** Panels using `PanelBase::setup(panel, parent)` need to become overlays using `OverlayBase::create(parent)` that return their root widget and register with NavigationManager.

---

## Quick Status

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: IPanelLifecycle Interface | ✅ COMPLETE | Interface created, both bases inherit |
| Phase 2a: MotionOverlay | ✅ COMPLETE | Converted to OverlayBase pattern |
| Phase 2b: FanOverlay | ✅ COMPLETE | Converted to OverlayBase pattern |
| Phase 2c: MacrosOverlay | ✅ COMPLETE | Converted to OverlayBase pattern |
| Phase 2d: SpoolmanOverlay | ✅ COMPLETE | Converted to OverlayBase pattern |
| Phase 2e: ConsoleOverlay | ✅ COMPLETE | Has WebSocket lifecycle hooks |
| Phase 2f: HistoryListOverlay | ✅ COMPLETE | Converted to OverlayBase pattern |
| Phase 2g: BedMeshOverlay | ✅ COMPLETE | Most complex panel successfully converted |
| Phase 3: Final Review & Cleanup | ✅ COMPLETE | All overlays audited, dead code identified |

**Legend:** ⬜ Not Started | 🔄 In Progress | ✅ Complete | ❌ Blocked

---

## Problem Statement

When opening Motion panel from Controls, warning appears:
```
[NavigationManager] Overlay 0x... pushed without lifecycle registration
```

**Root Cause:** 7 panels inherit from `PanelBase` but are pushed as overlays. They should inherit from `OverlayBase` for proper lifecycle management.

---

## Solution Overview

1. Create `IPanelLifecycle` interface for shared lifecycle methods
2. Convert all 7 panels from `PanelBase` to `OverlayBase`
3. Rename appropriately (`MotionPanel` → `MotionOverlay`)

---

## Agent Workflow (MANDATORY)

**Main session is for ORCHESTRATION ONLY. All implementation work MUST be delegated to agents.**

### Why This Matters
- Main session context is LIMITED - every file read/edit consumes tokens
- Agents get fresh context and return only what matters
- This preserves main session for thinking, evaluating, and coordinating

### Workflow Per Phase

1. **Investigation Agent** (`Explore`)
   - Explore current state, identify specifics
   - Returns: Summary of findings, files to modify, concerns

2. **Implementation Agent** (`general-purpose`)
   - Make the actual code changes
   - Must receive: Clear task description, files to modify, acceptance criteria
   - Returns: Summary of changes made, any issues encountered

3. **Review Agent** (`general-purpose`)
   - Code review the implementation
   - Check for: Correct patterns, memory safety, observer cleanup, no regressions
   - Returns: Approval or issues to fix

4. **Documentation Agent** (`general-purpose`)
   - Update this document with status, notes, checkboxes
   - Returns: Confirmation of updates

5. **Commit** (main session)
   - Only after review passes
   - Stage specific files per [S002]
   - Conventional commit format per [S001]

### Stop and Discuss Protocol

**CRITICAL:** If during investigation, implementation, or review you notice:
- Something significantly different from the documented plan
- Unexpected complexity or dependencies not previously identified
- Patterns that contradict project conventions
- Potential regressions or breaking changes

**→ STOP and discuss with the user before proceeding.**

Don't attempt to "fix it and move on" - bring it up for discussion first.

### DO NOT in Main Session
- ❌ Read files directly (use Explore agent)
- ❌ Edit code directly (use general-purpose agent)
- ❌ Update docs directly (use general-purpose agent)

### DO in Main Session
- ✅ Orchestrate agent workflow
- ✅ Evaluate agent outputs
- ✅ Make decisions based on findings
- ✅ Commit after review passes

---

## Phase 1: IPanelLifecycle Interface

### Status: ✅ COMPLETE

### Files to Create/Modify
- [x] `include/panel_lifecycle.h` - **CREATED** interface with on_activate/on_deactivate/get_name
- [x] `include/overlay_base.h` - Added `IPanelLifecycle` inheritance + `override` specifiers
- [x] `include/ui_panel_base.h` - Added `IPanelLifecycle` inheritance + `override` specifiers
- [x] `include/ui_nav_manager.h` - No changes needed (already uses OverlayBase*)
- [x] `src/ui/ui_nav_manager.cpp` - No changes needed (Phase 2 will register panels as overlays)

### Interface Definition
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class IPanelLifecycle {
  public:
    virtual ~IPanelLifecycle() = default;
    virtual void on_activate() = 0;
    virtual void on_deactivate() = 0;
    virtual const char* get_name() const = 0;
};
```

### Acceptance Criteria
- [x] Build succeeds with no warnings (only pre-existing unused field warnings)
- [x] Existing overlays still work (backward compatible)
- [x] Tests pass (1,635,982 assertions in 43 test cases)

### Notes
**Investigation Complete (2024-12-30):**
- OverlayBase already has: `on_activate()`, `on_deactivate()`, `get_name()` with default implementations
- PanelBase already has: same methods with empty defaults for activate/deactivate
- Warning generated at `ui_nav_manager.cpp:810-811` when widget not in `overlay_instances_` map
- NetworkSettingsOverlay shows gold pattern: `create()` returns widget → register with NavigationManager
- MotionPanel currently pushed as overlay but never registered (root cause of warning)
- Solution: Make both inherit from IPanelLifecycle, NavigationManager uses interface for dispatch

---

## Phase 2a: MotionOverlay

### Status: ✅ COMPLETE

### Files
- [x] Modified `include/ui_panel_motion.h` - converted to OverlayBase
- [x] Modified `src/ui/ui_panel_motion.cpp` - new pattern
- [x] Updated `src/ui/ui_panel_controls.cpp` - caller uses new pattern

### Current State (from investigation)
- Constructor: `MotionPanel(PrinterState&, MoonrakerAPI*)`
- Observers: 4x ObserverGuard (position_x/y/z, bed_moves)
- Lifecycle hooks: None currently
- Singleton: `get_global_motion_panel()`

### Conversion Pattern
```cpp
// BEFORE:
class MotionPanel : public PanelBase {
    void setup(lv_obj_t* panel, lv_obj_t* parent) override;
};

// AFTER:
class MotionOverlay : public OverlayBase {
    lv_obj_t* create(lv_obj_t* parent) override;
};
```

### Acceptance Criteria
- [x] Opens from Controls panel without warning
- [x] Jog controls work
- [x] Back button returns to Controls
- [x] No memory leaks

### Review Notes
**Completed 2024-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance
- Uses global accessors (get_moonraker_api, get_printer_state) instead of member references
- NavigationManager registration added before push (eliminates warning)
- Lifecycle hooks (on_activate, on_deactivate) implemented for resource management
- ObserverGuards provide RAII cleanup for subject observers
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 2b: FanOverlay

### Status: ✅ COMPLETE

### Files
- [x] Modified `include/ui_panel_fan.h` - converted to OverlayBase
- [x] Modified `src/ui/ui_panel_fan.cpp` - new pattern
- [x] Updated caller in `ui_panel_controls.cpp`

### Current State
- Constructor: `FanPanel(PrinterState&, MoonrakerAPI*)`
- Observers: 1x ObserverGuard (fan_speed)
- Lifecycle hooks: None
- Singleton: `get_global_fan_panel()`

### Acceptance Criteria
- [x] Opens from Controls without warning
- [x] Fan slider works
- [x] Preset buttons work

### Review Notes
**Completed 2024-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance
- Uses global accessors (get_moonraker_api, get_printer_state) instead of member references
- NavigationManager registration added before push (eliminates warning)
- Simpler than MotionPanel (1 observer, no jog pad complexity)
- ObserverGuards provide RAII cleanup for subject observers
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 2c: MacrosOverlay

### Status: ✅ COMPLETE

### Files
- [x] Modified `include/ui_panel_macros.h` - converted to OverlayBase
- [x] Modified `src/ui/ui_panel_macros.cpp` - new pattern
- [x] Updated caller in `ui_panel_advanced.cpp`

### Current State
- Constructor: `MacrosPanel(PrinterState&, MoonrakerAPI*)`
- Observers: None
- Lifecycle hooks: None
- Singleton: `get_global_macros_panel()`

### Acceptance Criteria
- [x] Opens from Advanced panel without warning
- [x] Macro list populates
- [x] Macro execution works

### Review Notes
**Completed 2024-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance
- Simplest panel so far: 1 subject (macro_list_changed), no observer cleanup complexity
- Uses global accessors (get_moonraker_api, get_printer_state) instead of member references
- NavigationManager registration added before push (eliminates warning)
- on_activate() refreshes macro list from MoonrakerAPI
- No manual observer management needed (subject-only approach)
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 2d: SpoolmanOverlay

### Status: ✅ COMPLETE

### Files
- [x] Modified `include/ui_panel_spoolman.h` - converted to OverlayBase
- [x] Modified `src/ui/ui_panel_spoolman.cpp` - new pattern
- [x] Updated caller in `ui_panel_advanced.cpp`

### Current State
- Constructor: `SpoolmanPanel(PrinterState&, MoonrakerAPI*)`
- Observers: 2x (panel_state, spool_count)
- Lifecycle hooks: on_activate() for data refresh
- Singleton: `get_global_spoolman_overlay()` (lazy-loaded)
- Has panel state subject

### Acceptance Criteria
- [x] Opens from Advanced panel without warning
- [x] Spool list loads
- [x] Spool selection works

### Review Notes
**Completed 2024-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance
- Removed early initialization (was `init_global_spoolman_panel()`, now lazy-loaded with `get_global_spoolman_overlay()`)
- Data refresh moved to on_activate() for proper timing when overlay opens
- Uses global accessors (get_moonraker_api, get_printer_state) instead of member references
- NavigationManager registration added before push (eliminates warning)
- ObserverGuards manage 2 subjects: panel_state and spool_count
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 2e: ConsoleOverlay

### Status: ✅ COMPLETE

### Files
- [x] Modified `include/ui_panel_console.h` - converted to OverlayBase
- [x] Modified `src/ui/ui_panel_console.cpp` - new pattern
- [x] Updated caller in `ui_panel_advanced.cpp`

### Current State
- Constructor: `ConsoleOverlay(PrinterState&, MoonrakerAPI*)`
- Observers: None (uses WebSocket subscription)
- Lifecycle hooks: **Already has on_activate/on_deactivate** for WebSocket
- Singleton: `get_global_console_overlay()` (lazy-loaded)

### Acceptance Criteria
- [x] Opens without warning
- [x] Real-time gcode responses appear
- [x] Command input works
- [x] WebSocket subscription managed correctly

### Review Notes
**Completed 2024-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance
- Preserved existing WebSocket lifecycle hooks from original implementation
- on_activate: calls base first, then subscribes to WebSocket responses
- on_deactivate: unsubscribes from WebSocket first, then calls base
- Removed early initialization (was `init_global_console_panel()`, now lazy-loaded with `get_global_console_overlay()`)
- Uses global accessors (get_moonraker_api, get_printer_state) instead of member references
- NavigationManager registration added before push (eliminates warning)
- Already using ui_async_call for thread safety (WebSocket callbacks run on background thread)
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 2f: HistoryListOverlay

### Status: ✅ COMPLETE

### Files
- [x] Modified `include/ui_panel_history_list.h` - converted to OverlayBase
- [x] Modified `src/ui/ui_panel_history_list.cpp` - new pattern
- [x] Updated caller in `ui_panel_history_dashboard.cpp`

### Current State
- Constructor: `HistoryListOverlay(PrinterState&, MoonrakerAPI*, PrintHistoryManager*)`
- Observers: 1x ObserverGuard (connection_status) + manager callback
- Lifecycle hooks: **Already has on_activate/on_deactivate**
- Singleton: `get_global_history_list_overlay()` (lazy-loaded)
- Extra dependency: PrintHistoryManager via global accessor

### Acceptance Criteria
- [x] Opens from History Dashboard without warning
- [x] Job list loads
- [x] Infinite scroll works
- [x] Detail overlay opens correctly

### Review Notes
**Completed 2024-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance
- Converted from PanelBase to OverlayBase pattern with `create()` return signature
- PrintHistoryManager obtained via global accessor (get_global_print_history_manager) instead of constructor parameter
- Preserved lifecycle hooks (on_activate, on_deactivate) with base class calls
- ObserverGuard retained for connection_status observer cleanup
- Has nested detail overlay (HistoryDetailOverlay) - valid pattern
- 13+ subjects drive detail view binding
- Removed early initialization (was `init_global_history_list_panel()`, now lazy-loaded with `get_global_history_list_overlay()`)
- Uses global accessors (get_moonraker_api, get_printer_state, get_global_print_history_manager) instead of member references
- NavigationManager registration added before push (eliminates warning)
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 2g: BedMeshOverlay

### Status: ✅ COMPLETE

### Files
- [x] Renamed `include/ui_panel_bed_mesh.h` → `include/bed_mesh_overlay.h`
- [x] Renamed `src/ui/ui_panel_bed_mesh.cpp` → `src/ui/bed_mesh_overlay.cpp`
- [x] Updated caller in `ui_panel_settings.cpp`

### Current State
- Constructor: `BedMeshOverlay(PrinterState&, MoonrakerAPI*)`
- Observers: 25 subjects for mesh stats and profile list
- Lifecycle hooks: on_activate/on_deactivate for resource management
- Singleton: `get_global_bed_mesh_overlay()`
- **Most complex panel:** 4 modal dialogs, async callback safety preserved

### Special Considerations
- Modal dialogs: calibrate, rename, delete, save_config
- Async safety: `std::shared_ptr<std::atomic<bool>> alive_` pattern preserved
- Profile management with dropdown
- Gold standard declarative UI patterns maintained

### Acceptance Criteria
- [x] Opens from Settings without warning
- [x] Mesh visualization works
- [x] Profile switching works
- [x] All modals work (calibrate, rename, delete, save)
- [x] No async callback crashes

### Review Notes
**Completed 2025-12-30:**
- Successfully converted from PanelBase to OverlayBase inheritance - most complex panel in entire refactor
- Preserved critical `alive_` async safety pattern: `std::shared_ptr<std::atomic<bool>>` for callback safety
- 4 modals fully functional: mesh calibrate, profile rename, profile delete, save config
- 25 subjects drive mesh visualization, profile list, and statistics display
- Uses global accessors pattern (get_moonraker_api, get_printer_state) instead of member references
- NavigationManager registration added before push (eliminates warning)
- Lifecycle hooks properly manage overlay state during activate/deactivate transitions
- Gold standard declarative UI patterns maintained (XML bindings, subject-driven updates)
- Build passes with no errors
- Review approved - ready for commit

---

## Phase 3: Final Review & Cleanup

### Status: ✅ COMPLETE

### Tasks
- [x] Audit all remaining panels and overlays for conversion candidates
- [x] Fix ControlsPanel calibration callers (BedMesh, ZOffset, ScrewsTilt)
- [x] Convert ExtrusionPanel from PanelBase to OverlayBase
- [x] Convert HistoryDashboardPanel from PanelBase to OverlayBase
- [x] Comprehensive audit identified all overlay panels in codebase
- [x] Identify and document dead code (PowerPanel)
- [x] Verify XML-only overlays are acceptable (nozzle_temp, bed_temp, etc.)

### Changes Made

**ControlsPanel Calibration Callers:**
- Fixed `bed_mesh_overlay_show()` caller to pass parent correctly
- Fixed `z_offset_overlay_show()` caller to pass parent correctly
- Fixed `screws_tilt_overlay_show()` caller to pass parent correctly

**ExtrusionPanel Conversion:**
- Converted from PanelBase to OverlayBase
- Changed `setup()` to `create()` returning lv_obj_t*
- Uses global accessors for dependencies
- Registered with NavigationManager before overlay push

**HistoryDashboardPanel Conversion:**
- Converted from PanelBase to OverlayBase
- Changed `setup()` to `create()` returning lv_obj_t*
- Uses global accessors for dependencies
- Registered with NavigationManager before overlay push

**Comprehensive Audit Results:**
- All panel-as-overlay patterns identified and converted
- PowerPanel found as unused/dead code (not converted as it's never instantiated)
- XML-only overlays confirmed acceptable for simple cases:
  - `nozzle_temp.xml` - Simple temperature display, no lifecycle needed
  - `bed_temp.xml` - Simple temperature display, no lifecycle needed
  - `plugin_install_modal.xml` - Modal dialog, no lifecycle needed

**Final Verification:**
- Build succeeds with no warnings
- All tests pass
- No remaining "overlay pushed without lifecycle registration" warnings
- Declarative UI patterns maintained throughout

---

## Decisions Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2024-12-30 | Named interface `IPanelLifecycle` not `IOverlayLifecycle` | "Panel" is the broader concept; overlays are a type of panel |
| 2024-12-30 | Convert all 7 panels at once | User preference for complete refactor |
| 2024-12-30 | Separate commits per panel | Easier review/revert, atomic changes |
| 2024-12-30 | Keep filenames during conversion | Rename in separate commit for cleaner git history |

---

## Lessons Learned

### 1. Multiple Callers Must Be Audited, Not Just the Obvious One

**Pattern:** When converting a panel that's pushed as an overlay, check ALL places where it's created, not just the most obvious caller.

**Example:** ControlsPanel had three calibration overlay callers (`bed_mesh_overlay_show()`, `z_offset_overlay_show()`, `screws_tilt_overlay_show()`) that all needed fixing. The per-panel conversion approach would have missed the callers beyond the first one.

**Lesson:** Use a comprehensive audit phase to find all creation sites before implementation.

### 2. XML-Only Overlays Are Acceptable for Simple Cases

**Pattern:** Not every overlay needs a C++ class with OverlayBase inheritance.

**Acceptable Cases:**
- Simple read-only displays (nozzle_temp, bed_temp)
- Modal dialogs with no lifecycle management (plugin_install_modal)

**When C++ Class Is Required:**
- Lifecycle hooks needed (on_activate/on_deactivate)
- Event handling with complex logic
- Dynamic content that changes based on application state
- Resource management (WebSocket subscriptions, observer cleanup)

**Lesson:** Declarative XML-only overlays reduce code complexity when they meet the criteria.

### 3. The Comprehensive Audit Found Issues the Per-Panel Conversion Missed

**Context:** Converting panels one-by-one led to subtle bugs (e.g., ControlsPanel callers, dead code like PowerPanel).

**Solution:** After completing per-panel conversions, run a comprehensive audit to:
- Find all remaining panel-as-overlay instances
- Identify dead code (classes created but never instantiated)
- Verify XML-only overlays meet the "acceptable cases" criteria
- Check all callers, not just primary ones

**Lesson:** Multi-phase refactors benefit from a final audit phase to catch edge cases the incremental work missed.

---

## References

- `include/overlay_base.h` - Target base class
- `include/ui_panel_base.h` - Current base class for these panels
- `include/ui_nav_manager.h` - Navigation manager with lifecycle dispatch
- `src/ui/network_settings_overlay.cpp` - Gold standard OverlayBase implementation
