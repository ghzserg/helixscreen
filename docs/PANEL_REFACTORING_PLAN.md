# Panel Refactoring Plan: C++ Class-Based Architecture

## Executive Summary

Refactor all 15 function-based panels, 4 wizard steps, and 7 non-panel modules to follow the `TempControlPanel` class-based pattern with RAII resource management, dependency injection, and proper C++ encapsulation.

**Scope:**
- 15 panels (~6,316 lines) from function-based to class-based (including test/debug panels)
- 4 wizard step modules (~800 lines) to class-based
- 7 non-panel candidates (~2,500 lines) to proper singletons/classes
- Test updates and new test coverage
- ~10-12 weeks estimated effort

**Key Decisions:**
- ✅ Create `PanelBase` abstract class for shared functionality
- ✅ Include ALL panels (including test/debug panels)
- ✅ Include wizard steps in scope
- ✅ Keep deprecated wrappers during transition with clear clean-break documentation

---

## Reference Architecture: TempControlPanel Pattern

The refactored `TempControlPanel` (`include/ui_temp_control_panel.h`, `src/ui_temp_control_panel.cpp`) establishes the target pattern:

| Pattern | Implementation |
|---------|---------------|
| **Constructor DI** | `TempControlPanel(PrinterState& printer_state, MoonrakerAPI* api)` |
| **RAII Observers** | Observer handles stored as members, cleaned up in destructor |
| **Static Trampolines** | `static void xxx_cb()` casts user_data to `this`, calls instance method |
| **Two-Phase Init** | `init_subjects()` → XML creation → `setup_*_panel()` |
| **Move Semantics** | Non-copyable, movable for `std::unique_ptr` ownership |
| **Encapsulated State** | All state as private members, no file-scope statics |

---

## Phase 1: Infrastructure (Week 1) - ✅ COMPLETE

### 1.1 Create PanelBase Abstract Class - ✅ COMPLETE

**Completed 2025-11-25:**
- `include/ui_panel_base.h` - Full Doxygen documentation
- `src/ui_panel_base.cpp` - RAII observer cleanup, move semantics
- `docs/PANEL_MIGRATION.md` - Tracking document created

**File:** `include/ui_panel_base.h`

```cpp
class PanelBase {
public:
    PanelBase(PrinterState& printer_state, MoonrakerAPI* api);
    virtual ~PanelBase();

    // Non-copyable
    PanelBase(const PanelBase&) = delete;
    PanelBase& operator=(const PanelBase&) = delete;

    // Movable
    PanelBase(PanelBase&&) noexcept;
    PanelBase& operator=(PanelBase&&) noexcept;

    // Core lifecycle (must implement)
    virtual void init_subjects() = 0;
    virtual void setup(lv_obj_t* panel, lv_obj_t* parent_screen) = 0;
    virtual const char* get_name() const = 0;
    virtual const char* get_xml_component_name() const = 0;

    // Optional lifecycle hooks
    virtual void on_activate() {}
    virtual void on_deactivate() {}

    // Common API
    void set_api(MoonrakerAPI* api) { api_ = api; }
    lv_obj_t* get_panel() const { return panel_; }

protected:
    PrinterState& printer_state_;
    MoonrakerAPI* api_;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    bool subjects_initialized_ = false;

    // RAII observer management
    void register_observer(lv_observer_t* observer);
    void cleanup_observers();  // Called in destructor

private:
    std::vector<lv_observer_t*> observers_;
};
```

All panels will inherit from `PanelBase` for consistent observer cleanup and API.

### 1.2 Refactor Non-Panel Singletons

| Module | Current File | New Class | Priority |
|--------|--------------|-----------|----------|
| Navigation | `ui_nav.cpp` | `NavigationManager` | HIGH |
| Status Bar | `ui_status_bar.cpp` | `StatusBarManager` | HIGH |
| Theme | `ui_theme.cpp` | `ThemeManager` | MEDIUM |
| Keyboard | `ui_keyboard.cpp` | `KeyboardManager` | MEDIUM |

---

## Phase 2: Simple Panels (Week 2) - 🔄 IN PROGRESS

Start with simplest panels to validate the pattern:

| Order | Panel | Lines | Complexity | Status |
|-------|-------|-------|------------|--------|
| 1 | `ui_panel_test` | 102 | Minimal | ✅ COMPLETE (2025-11-25) |
| 2 | `ui_panel_glyphs` | 194 | Display only | 📋 Next |
| 3 | `ui_panel_step_test` | 162 | Minimal | 📋 Pending |

**TestPanel Migration Notes (2025-11-25):**
- Template validated successfully
- Deprecated wrapper pattern works - warnings guide migration
- Required `app_globals.h` for `get_printer_state()` singleton access
- Simple panels with no subjects just set `subjects_initialized_ = true`

**Milestone:** Validated template, documented gotchas.

---

## Phase 3: Launcher & Subject Patterns (Week 3-4)

| Order | Panel | Lines | Key Pattern |
|-------|-------|-------|-------------|
| 4 | `ui_panel_notification_history` | 202 | DI with NotificationHistory |
| 5 | `ui_panel_settings` | 145 | Launcher, creates sub-panels |
| 6 | `ui_panel_controls` | 246 | References TempControlPanel |
| 7 | `ui_panel_motion` | 368 | 3 subjects, custom jog_pad widget |

**Milestone:** Launcher pattern proven, subject migration established.

---

## Phase 4: Observer & Complex State (Week 5-6)

| Order | Panel | Lines | Key Pattern |
|-------|-------|-------|-------------|
| 8 | `ui_panel_controls_extrusion` | 316 | Temperature subjects, API calls |
| 9 | `ui_panel_bed_mesh` | 356 | 3D renderer lifecycle |
| 10 | `ui_panel_filament` | 455 | 6 subjects, safety state machine |
| 11 | `ui_panel_home` | 477 | 4 observers, timer |

**Milestone:** Observer RAII validated, complex state proven.

---

## Phase 5: High Complexity (Week 7-8)

| Order | Panel | Lines | Key Pattern |
|-------|-------|-------|-------------|
| 12 | `ui_panel_print_status` | 468 | Heavy PrinterState integration |
| 13 | `ui_panel_gcode_test` | 533 | API testing interface |
| 14 | `ui_panel_print_select` | 1167 | File browser, thumbnails, pagination |

**Milestone:** Panel migration complete.

---

## Phase 6: Wizard Steps (Week 9-10)

| Order | Module | Lines | Key Pattern |
|-------|--------|-------|-------------|
| 15 | `ui_wizard_summary` | ~300 | 12 subjects, config population |
| 16 | `ui_wizard_wifi` | ~250 | Network scanning, selection |
| 17 | `ui_wizard_printer` | ~200 | Printer database, selection |
| 18 | `ui_wizard_moonraker` | ~200 | Connection testing |

Create `WizardStepBase` class inheriting from `PanelBase` with wizard-specific methods:

```cpp
class WizardStepBase : public PanelBase {
public:
    using PanelBase::PanelBase;

    virtual void populate_from_config(Config* config) = 0;
    virtual bool validate() { return true; }
    virtual void on_next() {}
    virtual void on_back() {}
};
```

**Milestone:** Full migration complete.

---

## Migration Checklist Template

### Per-Panel Checklist

#### Pre-Migration
- [ ] Read entire current implementation
- [ ] Document all `static` variables
- [ ] Document all subjects and registration names
- [ ] Document all observer subscriptions
- [ ] Identify dependencies (PrinterState, MoonrakerAPI, singletons)
- [ ] Check for timers or async operations

#### Implementation
- [ ] Create header with class declaration
- [ ] Move statics to private members
- [ ] Implement constructor with DI
- [ ] Implement destructor with observer cleanup
- [ ] Implement move semantics (if observers)
- [ ] Migrate `init_subjects()`
- [ ] Migrate `setup()` with static trampolines
- [ ] Update `main.cpp` integration

#### Post-Migration
- [ ] No file-scope statics (except LVGL callback data)
- [ ] All subjects are class members
- [ ] All observers cleaned up in destructor
- [ ] Compiles without warnings
- [ ] Panel creates/destroys cleanly
- [ ] AddressSanitizer clean

---

## File Naming Convention

```
include/
├── ui_panel_motion.h           # MotionPanel class
├── ui_panel_home.h             # HomePanel class
├── navigation_manager.h        # NavigationManager singleton
├── status_bar_manager.h        # StatusBarManager singleton
└── ui_panel_base.h             # Optional base class

src/
├── ui_panel_motion.cpp         # MotionPanel implementation
├── navigation_manager.cpp      # NavigationManager implementation
└── ui_panel_base.cpp           # Base class implementation
```

---

## Testing Strategy

### Test Updates Required

| Test File | Action |
|-----------|--------|
| `tests/unit/test_ui_panel_print_select.cpp` | Refactor to use class |
| `tests/unit/test_temp_graph.cpp` | Likely stable |
| `tests/unit/test_printer_state.cpp` | Reference for observer tests |

### New Tests to Add

1. **Observer lifecycle tests** - Verify RAII cleanup
2. **Subject binding tests** - Verify XML binding works
3. **Callback tests** - Direct method calls for testability

### Mock Requirements

- `MockMoonrakerAPI` - For testing API calls
- `MockNavigation` - For testing navigation callbacks

---

## Critical Files to Read Before Implementation

1. **`include/ui_temp_control_panel.h`** - Header structure, member organization
2. **`src/ui_temp_control_panel.cpp`** - Full implementation reference
3. **`include/ui_subject_registry.h`** - Subject registration macros
4. **`include/printer_state.h`** - Dependency injection pattern
5. **`src/main.cpp`** - Integration point, initialization order

---

## Risk Mitigation

### Keep Codebase Working
- Parallel implementation: Create class alongside existing functions
- Temporary wrappers: Call class from old API during transition
- Test thoroughly before removing old functions

### Observer Lifecycle Safety
- Always null out handles after removal
- Move constructor nulls source observers
- Test with AddressSanitizer

### LVGL Callback Data
- Use file-scope static arrays (TempControlPanel pattern)
- Document that these statics are intentional
- Never store stack-allocated data

### Subject Registration Order
- `init_subjects()` MUST be called before `lv_xml_create()`
- Add warning in `setup()` if `!subjects_initialized_`

---

## Estimated Effort

| Phase | Duration | Panels/Modules |
|-------|----------|----------------|
| Infrastructure | 1 week | PanelBase + 4 singletons |
| Simple Panels | 1 week | 3 panels (test, glyphs, step_test) |
| Launcher/Subject | 2 weeks | 4 panels |
| Observer/Complex | 2 weeks | 4 panels |
| High Complexity | 2 weeks | 3 panels |
| Wizard Steps | 2 weeks | 4 wizard modules + WizardStepBase |
| **Total** | **~10 weeks** | **14 panels + 4 wizards + 4 singletons** |

**Velocity Notes:**
- Simple panels: ~0.5-1 day each
- Medium panels: ~1-2 days each
- Complex panels: ~2-3 days each
- `print_select` (1,167 lines): ~3-5 days alone

---

## Deprecation Strategy & Clean Break Guide

### During Migration: Deprecated Wrappers

Each refactored panel keeps old API as deprecated wrappers:

```cpp
// ui_panel_motion.cpp - Legacy API (deprecated)

#include "ui_panel_motion.h"

static std::unique_ptr<MotionPanel> g_motion_panel;

[[deprecated("Use MotionPanel class directly - see PANEL_MIGRATION.md")]]
void ui_panel_motion_init_subjects() {
    if (!g_motion_panel) {
        g_motion_panel = std::make_unique<MotionPanel>(get_printer_state(), nullptr);
    }
    g_motion_panel->init_subjects();
}

[[deprecated("Use MotionPanel class directly - see PANEL_MIGRATION.md")]]
void ui_panel_motion_setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    if (!g_motion_panel) {
        g_motion_panel = std::make_unique<MotionPanel>(get_printer_state(), nullptr);
    }
    g_motion_panel->setup(panel, parent_screen);
}

// ... etc for all public functions
```

### Future Session: Clean Break Procedure

**When ready to remove deprecated wrappers (future session):**

1. **Search for deprecated function calls:**
   ```bash
   grep -r "ui_panel_motion_init_subjects\|ui_panel_motion_setup" src/
   ```

2. **Update main.cpp integration:**
   ```cpp
   // OLD (deprecated):
   ui_panel_motion_init_subjects();
   // ... later ...
   ui_panel_motion_setup(panel, screen);

   // NEW (class-based):
   auto motion_panel = std::make_unique<MotionPanel>(get_printer_state(), nullptr);
   motion_panel->init_subjects();
   // ... after XML creation ...
   motion_panel->setup(panel, screen);
   ```

3. **Remove wrapper functions** from `.cpp` files

4. **Remove old function declarations** from `.h` files

5. **Update any tests** using old API

6. **Verify build** with `-Werror` to catch any remaining uses

### Tracking File: docs/PANEL_MIGRATION.md

Create this file to track migration status:

```markdown
# Panel Migration Status

## Completed (Class-Based)
- [x] TempControlPanel (reference implementation)
- [ ] TestPanel
- [ ] GlyphsPanel
...

## Deprecated Wrappers (Ready for Clean Break)
- [ ] MotionPanel - wrappers in ui_panel_motion.cpp
...

## Clean Break Checklist
When removing deprecated wrappers:
1. grep for old function names
2. Update main.cpp to use class directly
3. Remove wrapper functions
4. Remove old .h declarations
5. Rebuild with -Werror
```
