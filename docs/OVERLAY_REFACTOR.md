# Overlay Refactor: Convert Panel-as-Overlays to OverlayBase

**Status:** 🟡 IN PROGRESS
**Started:** 2024-12-30
**Last Updated:** 2024-12-30

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
| Phase 2b: FanOverlay | ⬜ NOT STARTED | Simple |
| Phase 2c: MacrosOverlay | ⬜ NOT STARTED | Simple |
| Phase 2d: SpoolmanOverlay | ⬜ NOT STARTED | Medium |
| Phase 2e: ConsoleOverlay | ⬜ NOT STARTED | Medium, has lifecycle |
| Phase 2f: HistoryListOverlay | ⬜ NOT STARTED | Has lifecycle |
| Phase 2g: BedMeshOverlay | ⬜ NOT STARTED | Complex |
| Phase 3: Final Review & Cleanup | ⬜ NOT STARTED | |

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

### Status: ⬜ NOT STARTED

### Files
- [ ] Rename `include/ui_panel_fan.h` → `include/fan_overlay.h`
- [ ] Rename `src/ui/ui_panel_fan.cpp` → `src/ui/fan_overlay.cpp`
- [ ] Update caller in `ui_panel_controls.cpp`

### Current State
- Constructor: `FanPanel(PrinterState&, MoonrakerAPI*)`
- Observers: 1x ObserverGuard (fan_speed)
- Lifecycle hooks: None
- Singleton: `get_global_fan_panel()`

### Acceptance Criteria
- [ ] Opens from Controls without warning
- [ ] Fan slider works
- [ ] Preset buttons work

### Review Notes
<!-- Code review agent findings go here -->

---

## Phase 2c: MacrosOverlay

### Status: ⬜ NOT STARTED

### Files
- [ ] Rename `include/ui_panel_macros.h` → `include/macros_overlay.h`
- [ ] Rename `src/ui/ui_panel_macros.cpp` → `src/ui/macros_overlay.cpp`
- [ ] Update caller in `ui_panel_advanced.cpp`

### Current State
- Constructor: `MacrosPanel(PrinterState&, MoonrakerAPI*)`
- Observers: None
- Lifecycle hooks: None
- Singleton: `get_global_macros_panel()`

### Acceptance Criteria
- [ ] Opens from Advanced panel without warning
- [ ] Macro list populates
- [ ] Macro execution works

### Review Notes
<!-- Code review agent findings go here -->

---

## Phase 2d: SpoolmanOverlay

### Status: ⬜ NOT STARTED

### Files
- [ ] Rename `include/ui_panel_spoolman.h` → `include/spoolman_overlay.h`
- [ ] Rename `src/ui/ui_panel_spoolman.cpp` → `src/ui/spoolman_overlay.cpp`
- [ ] Update caller in `ui_panel_advanced.cpp`

### Current State
- Constructor: `SpoolmanPanel(PrinterState&, MoonrakerAPI*)`
- Observers: None
- Lifecycle hooks: None
- Singleton: `init_global_spoolman_panel()` (early init)
- Has panel state subject

### Acceptance Criteria
- [ ] Opens from Advanced panel without warning
- [ ] Spool list loads
- [ ] Spool selection works

### Review Notes
<!-- Code review agent findings go here -->

---

## Phase 2e: ConsoleOverlay

### Status: ⬜ NOT STARTED

### Files
- [ ] Rename `include/ui_panel_console.h` → `include/console_overlay.h`
- [ ] Rename `src/ui/ui_panel_console.cpp` → `src/ui/console_overlay.cpp`
- [ ] Update caller

### Current State
- Constructor: `ConsolePanel(PrinterState&, MoonrakerAPI*)`
- Observers: None (uses WebSocket subscription)
- Lifecycle hooks: **Already has on_activate/on_deactivate** for WebSocket
- Singleton: `init_global_console_panel()` (early init)

### Acceptance Criteria
- [ ] Opens without warning
- [ ] Real-time gcode responses appear
- [ ] Command input works
- [ ] WebSocket subscription managed correctly

### Review Notes
<!-- Code review agent findings go here -->

---

## Phase 2f: HistoryListOverlay

### Status: ⬜ NOT STARTED

### Files
- [ ] Rename `include/ui_panel_history_list.h` → `include/history_list_overlay.h`
- [ ] Rename `src/ui/ui_panel_history_list.cpp` → `src/ui/history_list_overlay.cpp`
- [ ] Update caller in `ui_panel_history_dashboard.cpp`

### Current State
- Constructor: `HistoryListPanel(PrinterState&, MoonrakerAPI*, PrintHistoryManager*)`
- Observers: 1x ObserverGuard + manager callback
- Lifecycle hooks: **Already has on_activate/on_deactivate**
- Singleton: `init_global_history_list_panel()` (early init)
- Extra dependency: PrintHistoryManager

### Acceptance Criteria
- [ ] Opens from History Dashboard without warning
- [ ] Job list loads
- [ ] Infinite scroll works
- [ ] Detail overlay opens correctly

### Review Notes
<!-- Code review agent findings go here -->

---

## Phase 2g: BedMeshOverlay

### Status: ⬜ NOT STARTED

### Files
- [ ] Rename `include/ui_panel_bed_mesh.h` → `include/bed_mesh_overlay.h`
- [ ] Rename `src/ui/ui_panel_bed_mesh.cpp` → `src/ui/bed_mesh_overlay.cpp`
- [ ] Update caller in `ui_panel_settings.cpp`

### Current State
- Constructor: `BedMeshPanel(PrinterState&, MoonrakerAPI*)`
- Observers: Mixed (some raw, async safety flag)
- Lifecycle hooks: None
- Singleton: `get_global_bed_mesh_panel()`
- **Complex:** Multiple modal dialogs, async callback safety

### Special Considerations
- Modal dialogs: calibrate, rename, delete, save_config
- Async safety: `std::shared_ptr<std::atomic<bool>> alive_`
- Profile management with dropdown

### Acceptance Criteria
- [ ] Opens from Settings without warning
- [ ] Mesh visualization works
- [ ] Profile switching works
- [ ] All modals work (calibrate, rename, delete, save)
- [ ] No async callback crashes

### Review Notes
<!-- Code review agent findings go here -->

---

## Phase 3: Final Review & Cleanup

### Status: ⬜ NOT STARTED

### Tasks
- [ ] Remove any dead code from old pattern
- [ ] Update any remaining callers
- [ ] Run full test suite
- [ ] Manual testing of all converted overlays
- [ ] Update this document with lessons learned

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

<!-- Populate after completion -->

---

## References

- `include/overlay_base.h` - Target base class
- `include/ui_panel_base.h` - Current base class for these panels
- `include/ui_nav_manager.h` - Navigation manager with lifecycle dispatch
- `src/ui/network_settings_overlay.cpp` - Gold standard OverlayBase implementation
