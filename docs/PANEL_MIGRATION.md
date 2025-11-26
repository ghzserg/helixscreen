# Panel Migration Status

This file tracks the migration of function-based panels to the class-based `PanelBase` architecture.

## Migration Complete ✅

All 16 panels and 7 wizard steps have been migrated to class-based architecture. The **Clean Break Phase** has been completed - all deprecated legacy API wrappers have been removed.

## Reference Implementations

| Type | File | Key Pattern |
|------|------|-------------|
| Panel | `ui_panel_home.h/cpp` | 6 subjects, timer lifecycle, observers |
| Panel | `ui_panel_print_select.h/cpp` | Complex file browser, async loading |
| Wizard | `ui_wizard_wifi.h/cpp` | WiFiManager integration, password modal |
| Wizard | `ui_wizard_connection.h/cpp` | Async WebSocket callbacks |

## Global Instance Pattern

Each panel/wizard step uses a singleton pattern with a global accessor function:

```cpp
// In .cpp file:
static std::unique_ptr<HomePanel> g_home_panel;

HomePanel& get_global_home_panel() {
    if (!g_home_panel) {
        g_home_panel = std::make_unique<HomePanel>(get_printer_state(), nullptr);
    }
    return *g_home_panel;
}

// Usage in main.cpp or other files:
get_global_home_panel().init_subjects();
get_global_home_panel().setup(panel_obj, screen);
```

### Wizard Steps (pointer pattern):
```cpp
WizardWifiStep* get_wizard_wifi_step() {
    if (!g_wizard_wifi_step) {
        g_wizard_wifi_step = std::make_unique<WizardWifiStep>();
    }
    return g_wizard_wifi_step.get();
}

void destroy_wizard_wifi_step() {
    g_wizard_wifi_step.reset();
}
```

## Cross-Panel Calls

When one panel needs to call another panel's methods, use forward declarations:

```cpp
// In ui_panel_controls.cpp:
class MotionPanel;
MotionPanel& get_global_motion_panel();

void ControlsPanel::handle_motion_clicked() {
    motion_panel_ = static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "motion_panel", nullptr));
    get_global_motion_panel().setup(motion_panel_, parent_screen_);
}
```

## Migrated Panels (16 total)

| Panel | Class | Subjects | Key Pattern |
|-------|-------|----------|-------------|
| `ui_panel_home` | `HomePanel` | 6 | Timer, tip rotation, network observers |
| `ui_panel_controls` | `ControlsPanel` | 0 | Launcher, lazy child panel creation |
| `ui_panel_motion` | `MotionPanel` | 3 | Jog pad widget, position display |
| `ui_panel_settings` | `SettingsPanel` | 0 | Launcher, bed mesh overlay |
| `ui_panel_filament` | `FilamentPanel` | 6 | Hybrid reactive/imperative |
| `ui_panel_print_select` | `PrintSelectPanel` | 5 | File browser, async thumbnails |
| `ui_panel_print_status` | `PrintStatusPanel` | 10 | Mock simulation, resize callback |
| `ui_panel_controls_extrusion` | `ExtrusionPanel` | 4 | Cross-panel observer |
| `ui_panel_controls_temp` | `TempControlPanel` | 6 | Dual nozzle/bed modes |
| `ui_panel_bed_mesh` | `BedMeshPanel` | 2 | TinyGL 3D renderer |
| `ui_panel_notification_history` | `NotificationHistoryPanel` | 0 | Service injection |
| `ui_panel_gcode_test` | `GcodeTestPanel` | 0 | File picker, async loading |
| `ui_panel_glyphs` | `GlyphsPanel` | 0 | Display only |
| `ui_panel_step_test` | `StepTestPanel` | 0 | Basic callbacks |
| `ui_panel_test` | `TestPanel` | 0 | Minimal test panel |

## Migrated Wizard Steps (7 total)

| Module | Class | Subjects | Key Pattern |
|--------|-------|----------|-------------|
| `ui_wizard_wifi` | `WizardWifiStep` | 7 | WiFiManager, password modal |
| `ui_wizard_connection` | `WizardConnectionStep` | 5 | Async WebSocket |
| `ui_wizard_printer_identify` | `WizardPrinterIdentifyStep` | 3 | Printer auto-detection |
| `ui_wizard_heater_select` | `WizardHeaterSelectStep` | 2 | Hardware dropdown |
| `ui_wizard_fan_select` | `WizardFanSelectStep` | 2 | Fan filtering |
| `ui_wizard_led_select` | `WizardLedSelectStep` | 1 | LED dropdown |
| `ui_wizard_summary` | `WizardSummaryStep` | 12 | Config summary display |

## Architecture Patterns

### 1. Static Destruction Order Bug (CRITICAL)

```cpp
// WRONG - crashes during exit() when LVGL is already destroyed
~HomePanel() {
    lv_obj_del(panel_);  // CRASH!
    spdlog::info("Cleaning up");  // CRASH!
}

// CORRECT - no LVGL/spdlog calls in destructors of global objects
~HomePanel() {
    panel_ = nullptr;  // Just null pointers
    // NOTE: Do NOT log here - spdlog may be destroyed first
}
```

### 2. Static Trampolines with user_data

```cpp
// Registration - pass 'this' as user_data
lv_obj_add_event_cb(btn, on_click_static, LV_EVENT_CLICKED, this);

// Static trampoline
static void on_click_static(lv_event_t* e) {
    auto* self = static_cast<HomePanel*>(lv_event_get_user_data(e));
    if (self) self->handle_click();
}
```

### 3. Two-Phase Initialization

```cpp
// Phase 1: Before XML creation
panel.init_subjects();  // Register subjects with LVGL

// XML creation (subjects bound automatically)
lv_obj_t* panel_obj = lv_xml_create(parent, "home_panel", nullptr);

// Phase 2: After XML creation
panel.setup(panel_obj, screen);  // Wire events, find widgets, setup observers
```

### 4. RAII Observer Management

```cpp
void HomePanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);

    // register_observer() stores handle for automatic cleanup in ~PanelBase()
    register_observer(lv_subject_add_observer(&printer_state_.network_subject,
                                               network_observer_cb, this));
}
```

### 5. const_cast for LVGL API

```cpp
// LVGL's lv_subject_get_string() requires non-const pointer
bool get_url(char* buffer, size_t size) const {
    const char* ip = lv_subject_get_string(const_cast<lv_subject_t*>(&connection_ip_));
    // ...
}
```

## Files Modified in Clean Break

### Entry Points Updated:
- `src/main.cpp` - All `ui_panel_*` calls replaced with class API
- `src/ui_wizard.cpp` - All `ui_wizard_*` calls replaced with class API
- `src/ui_status_bar.cpp` - Notification history call updated

### Deprecated Wrappers Removed From:
- 14 panel header files (`include/ui_panel_*.h`)
- 14 panel implementation files (`src/ui_panel_*.cpp`)
- 7 wizard header files (`include/ui_wizard_*.h`)
- 7 wizard implementation files (`src/ui_wizard_*.cpp`)

## Future Work

### WizardStepBase (Optional Enhancement)

Consider creating a `WizardStepBase` class similar to `PanelBase`:

```cpp
class WizardStepBase {
public:
    virtual void init_subjects() = 0;
    virtual void register_callbacks() = 0;
    virtual lv_obj_t* create(lv_obj_t* parent) = 0;
    virtual void cleanup() = 0;
    virtual bool is_validated() const { return true; }
    virtual const char* get_name() const = 0;
};
```

This would further standardize wizard step implementations.
