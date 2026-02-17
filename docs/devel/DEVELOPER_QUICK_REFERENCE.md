# Developer Quick Reference

Quick patterns and cheat sheets for developers working on the HelixScreen codebase. For system design and architectural rationale, see [ARCHITECTURE.md](ARCHITECTURE.md). For comprehensive XML syntax, see [LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md).

---

## Class Patterns

HelixScreen uses class-based patterns for all new code. For architectural rationale, see [ARCHITECTURE.md](ARCHITECTURE.md#preferred-class-based-architecture).

### Panel Pattern

**Canonical example:** `include/ui_panel_motion.h` + `src/ui_panel_motion.cpp`

```cpp
class ExamplePanel : public PanelBase {  // Use SubjectManager subjects_; member for auto cleanup
public:
    explicit ExamplePanel(lv_obj_t* parent);
    ~ExamplePanel() override;

    void show() override;
    void hide() override;
    lv_obj_t* get_root() const override { return root_; }

private:
    void init_subjects();  // Call BEFORE lv_xml_create()
    void setup_observers();  // Wire reactive bindings

    lv_obj_t* root_ = nullptr;
    lv_subject_t my_subject_{};
    char buf_[128]{};  // Static storage for string subjects
};
```

**Note:** Use `PanelBase` with a `SubjectManager subjects_;` member for automatic observer cleanup on destruction.

### Manager Pattern (Backend)

**Canonical example:** `include/wifi_manager.h` + `src/wifi_manager.cpp`

```cpp
class WiFiManager {
public:
    static WiFiManager& instance();  // Singleton access

    bool start();   // Initialize and begin operation
    void stop();    // Graceful shutdown

    // Async operations with callbacks
    void scan(ScanCallback on_complete);
    void connect(const std::string& ssid, ConnectCallback on_result);

private:
    WiFiManager();  // Private constructor for singleton
    ~WiFiManager();
    std::unique_ptr<WifiBackend> backend_;  // Pluggable implementation
};
```

### Modal Pattern

**Canonical example:** `src/ui_wizard_*.cpp`

```cpp
class ConfirmDialog : public Modal {
public:
    void show(const std::string& title, ConfirmCallback on_confirm);
    void dismiss() override;

private:
    lv_obj_t* backdrop_ = nullptr;
    lv_obj_t* dialog_ = nullptr;
};
```

### Domain State Pattern

**Canonical example:** `include/printer_temperature_state.h` + `src/printer/printer_temperature_state.cpp`

PrinterState is composed of 13 focused domain classes. Each owns its LVGL subjects:

```cpp
class PrinterTemperatureState {
public:
    void init_subjects();       // Initialize all subjects
    void deinit_subjects();     // Shutdown cleanup
    void reset_for_testing();   // Test reset

    lv_subject_t* nozzle_temp_subject();   // Accessor for binding
    void set_nozzle_temp(int temp);        // Update via ui_async_call

private:
    lv_subject_t nozzle_temp_{};
    bool initialized_ = false;
};
```

**Domain classes:** `printer_*_state.h` — Temperature, Motion, Fan, Print, Calibration, Capabilities, ExcludedObjects, Network, Versions, Led, HardwareValidation, PluginStatus, CompositeVisibility

**For architectural rationale, see [ARCHITECTURE.md § Domain Decomposition](ARCHITECTURE.md#domain-decomposition-printerstate).**

---

### Singleton Subject Cleanup Pattern

**Canonical example:** `src/printer/ams_state.cpp`, `src/system/settings_manager.cpp`

Singletons with LVGL subjects must register cleanup to avoid Static Destruction Order Fiasco:

```cpp
// In init_subjects():
void MySingleton::init_subjects() {
    if (initialized_) return;
    lv_subject_init_int(&my_subject_, 0);
    initialized_ = true;
}

// deinit_subjects() - required for LVGL subject cleanup
void MySingleton::deinit_subjects() {
    if (!initialized_) return;
    spdlog::debug("[MySingleton] Deinitializing subjects");
    lv_subject_deinit(&my_subject_);  // Disconnects all observers
    initialized_ = false;
}

// Registration (in SubjectInitializer):
StaticSubjectRegistry::instance().register_deinit(
    "MySingleton", []() { MySingleton::instance().deinit_subjects(); });
```

**See [ARCHITECTURE.md § Shutdown Order](ARCHITECTURE.md#shutdown-order-staticpanelregistry--staticsubjectregistry) for full pattern.**

---

## Observer Factory (CRITICAL)

Use `observer_factory.h` for type-safe, auto-cleaned observers. **Never use raw `lv_subject_add_observer_*` in panels.**

```cpp
#include "observer_factory.h"

// In setup_observers():
// Integer observer with async UI update
add_observer(observe_int_async<MyPanel>(
    &PrinterState::instance().temp_nozzle_subject(),
    this,
    [](MyPanel* self, int32_t temp) {
        self->update_temp_display(temp);
    }
));

// String observer
add_observer(observe_string<MyPanel>(
    &PrinterState::instance().filename_subject(),
    this,
    [](MyPanel* self, const char* name) {
        lv_label_set_text(self->filename_label_, name);
    }
));

// Connection state observer (special case)
add_observer(observe_connection_state<MyPanel>(
    this,
    [](MyPanel* self, bool connected) {
        self->set_controls_enabled(connected);
    }
));
```

**Key patterns:**
- `observe_int_sync<Panel>()` - Direct callback (same thread)
- `observe_int_async<Panel>()` - Queued to LVGL thread (safe from WebSocket)
- `observe_string<Panel>()` - String subjects
- `observe_connection_state<Panel>()` - Connection status

---

## RAII Guards

### ObserverGuard

Auto-removes LVGL observer on destruction. Safe during shutdown (checks `lv_is_initialized()`).

```cpp
class MyPanel {
    std::vector<ObserverGuard> observers_;

    void add_observer(lv_observer_t* obs) {
        observers_.emplace_back(obs);
    }
    // Observers auto-cleaned when panel destroyed
};
```

### SubscriptionGuard

Auto-unsubscribes from MoonrakerClient notifications:

```cpp
SubscriptionGuard sub_;

void setup() {
    sub_.reset(MoonrakerClient::instance().subscribe_notify(
        "notify_gcode_response",
        [this](const json& data) { handle_response(data); }
    ));
}
// Auto-unsubscribes on destruction
```

### LvglTimerGuard

Auto-deletes LVGL timers:

```cpp
helix::ui::LvglTimerGuard timer_;

void start_polling() {
    timer_.reset(lv_timer_create(poll_cb, 1000, this));
}
// Timer auto-deleted on destruction
```

---

## Threading Model

**WebSocket callbacks run on background thread.** Never call `lv_subject_set_*()` directly!

```cpp
// ❌ WRONG - called from WebSocket thread
void on_ws_message(const json& data) {
    lv_subject_set_int(&temp_subject_, data["temp"]);  // CRASH!
}

// ✅ CORRECT - queue to LVGL thread
void on_ws_message(const json& data) {
    int temp = data["temp"];
    ui_async_call([this, temp]() {
        lv_subject_set_int(&temp_subject_, temp);
    });
}

// ✅ BETTER - use ui_queue_update for batching
void on_ws_message(const json& data) {
    ui_queue_update([this, data]() {
        lv_subject_set_int(&temp_subject_, data["temp"]);
        lv_subject_set_int(&bed_subject_, data["bed"]);
    });
}
```

**Pattern:** See `printer_state.cpp` `set_*_internal()` methods.

---

## Component Names (CRITICAL)

**Always add explicit `name` on component tags** - internal view names don't propagate:

```xml
<controls_panel name="controls_panel"/>  <!-- ✅ Findable -->
<controls_panel/>                        <!-- ❌ lv_obj_find_by_name returns NULL -->
```

---

## Icon Component

Font-based MDI icons (~50KB total vs ~4.6MB for images).

```xml
<icon src="home" size="lg" variant="accent"/>
<icon src="wifi" size="sm" color="#warning_color"/>
```

| Property | Values |
|----------|--------|
| `src` | Icon name (see `include/ui_icon_codepoints.h`) |
| `size` | `xs`=16px, `sm`=24px, `md`=32px, `lg`=48px, `xl`=64px (default) |
| `variant` | `primary`, `secondary`, `accent`, `disabled`, `warning` |
| `color` | Hex override (e.g., `"#FF0000"`) - overrides variant |

**C++ API:**
```cpp
ui_icon_set_source(icon, "wifi_strength_4");
ui_icon_set_size(icon, "lg");
ui_icon_set_variant(icon, "accent");
```

**Adding icons:** Find at [Pictogrammers MDI](https://pictogrammers.com/library/mdi/) → add to `ui_icon_codepoints.h` (sorted!) → add codepoint to `scripts/regen_mdi_fonts.sh` → run `./scripts/regen_mdi_fonts.sh`

---

## ui_switch Component

Screen-responsive switch with semantic sizes:

```xml
<ui_switch size="medium" checked="true"/>
```

| Size | SMALL screen | MEDIUM screen | LARGE screen |
|------|--------------|---------------|--------------|
| `tiny` | 32×16px | 48×24px | 64×32px |
| `small` | 40×20px | 64×32px | 88×44px |
| `medium` | 48×24px | 80×40px | 112×56px |
| `large` | 56×28px | 88×44px | 128×64px |

---

## Step Progress Widget

```cpp
#include "ui_step_progress.h"

ui_step_t steps[] = {
    {"Step 1", UI_STEP_STATE_COMPLETED},
    {"Step 2", UI_STEP_STATE_ACTIVE},
    {"Step 3", UI_STEP_STATE_PENDING}
};
lv_obj_t* progress = ui_step_progress_create(parent, steps, 3, false);  // false=vertical
ui_step_progress_set_current(progress, 2);  // Advance to step 3
```

---

## Sensor Framework

Extensible sensor system via `ISensorManager` interface. See `include/sensors/`.

**Available managers:**
- `AccelSensorManager` - ADXL345, LIS2DW, LIS3DH, MPU9250, ICM20948
- `FilamentSensorManager` - Runout detection
- `ProbePositionZOffsetSensor` - Z-probe tracking
- `ColorSensorManager` - Filament color
- `WidthSensorManager` - Filament diameter
- `HumiditySensorManager` - Chamber humidity

**Registration:**
```cpp
auto& registry = SensorRegistry::instance();
registry.register_manager("accel", std::make_unique<AccelSensorManager>());
```

**Accessing state:** Sensors expose LVGL subjects for reactive binding.

---

## Responsive Design Tokens

See **[UI Contributor Guide](UI_CONTRIBUTOR_GUIDE.md)** for the complete responsive token reference (breakpoints, spacing, fonts, component tokens, colors, and how to add new tokens).

**C++ access patterns:**
```cpp
// Spacing
int padding = theme_manager_get_spacing("space_lg");

// Colors — token lookup (handles light/dark)
lv_color_t bg = theme_manager_get_color("card_bg");

// Colors — hex parsing only (NOT for tokens)
lv_color_t c = theme_manager_parse_hex_color("#FF0000");

// Fonts
const lv_font_t* font = theme_manager_get_font("font_body");
```

---

## Subjects & Bindings

### Initialization (BEFORE lv_xml_create!)

```cpp
// String
static char buf[128];
lv_subject_init_string(&subj, buf, NULL, sizeof(buf), "init");
lv_xml_register_subject(nullptr, "my_text", &subj);

// Integer
lv_subject_init_int(&subj, 0);

// Color
lv_subject_init_color(&subj, lv_color_hex(0xFF0000));
```

### XML Bindings

| Widget | Binding | Subject Type |
|--------|---------|--------------|
| `lv_label` | `bind_text="name"` | String |
| `lv_slider` | `bind_value="name"` | Integer |
| `lv_arc` | `bind_value="name"` | Integer |

---

## Registration Order (CRITICAL)

Subjects must be initialized BEFORE creating XML to ensure bindings find initialized values. For detailed rationale, see [ARCHITECTURE.md](ARCHITECTURE.md#subject-initialization-pattern).

```cpp
lv_xml_register_font(...);                    // 1. Fonts
lv_xml_register_image(...);                   // 2. Images
lv_xml_component_register_from_file(...);     // 3. Components (globals first!)
lv_subject_init_*(...);                       // 4. Init subjects
lv_xml_register_subject(...);                 // 5. Register subjects
lv_xml_create(...);                           // 6. Create UI
```

---

## Style Properties

⚠️ In `<styles>`: bare names (`bg_color`). On widgets: `style_` prefix (`style_bg_color`).

```xml
<styles>
    <style name="my_style" bg_color="#ff0000"/>  <!-- NO prefix -->
</styles>
<lv_obj style_bg_color="#card_bg"/>              <!-- WITH prefix -->

<!-- Part selectors -->
<lv_spinner style_arc_width:indicator="3"/>
<lv_slider style_bg_color:knob="#ffffff"/>
```

**Parts:** `main`, `indicator`, `knob`, `items`, `selected`, `cursor`, `scrollbar`

**Common properties:**
```xml
width="100" height="200" align="center"
flex_flow="row|column" flex_grow="1"
style_bg_color="#hex" style_bg_opa="50%"
style_pad_all="#space_md" style_radius="8"
style_flex_main_place="space_evenly"
style_flex_cross_place="center"
```

---

## Common Gotchas

| ❌ Wrong | ✅ Correct | See Also |
|----------|-----------|----------|
| `char buf[128];` (stack) | `static char buf[128];` (static/heap) | [ARCHITECTURE.md - Subject Lifecycle](ARCHITECTURE.md#subject-lifecycle) |
| `flex_align="..."` | `style_flex_main_place` + `style_flex_cross_place` | [LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md) |
| Register subjects after `lv_xml_create` | Register subjects BEFORE | [ARCHITECTURE.md - Subject Initialization](ARCHITECTURE.md#subject-initialization-pattern) |
| `style_img_recolor` | `style_image_recolor` (full word) | |
| `style_pad_row` + `style_flex_track_place="space_evenly"` | Use one or the other (track_place overrides pad_row) | |
| `<lv_label><lv_label-bind_text subject="x"/></lv_label>` | `<lv_label bind_text="x"/>` (attribute, not child) | |
| `lv_obj_add_event_cb()` in C++ | XML `<event_cb trigger="clicked" callback="name"/>` | [ARCHITECTURE.md - Reactive-First](ARCHITECTURE.md#critical-reactive-first-principle---the-helixscreen-way) |
| `lv_label_set_text()` for reactive data | `bind_text` subject binding | [ARCHITECTURE.md - Reactive Patterns](ARCHITECTURE.md#reactive-patterns-for-common-ui-tasks) |
| Hardcoded colors in C++ | `ui_theme_get_color("card_bg")` | [Responsive Design Tokens](#responsive-design-tokens) |
| `lv_subject_set_*()` from WebSocket | `ui_async_call()` or `ui_queue_update()` | [Threading Model](#threading-model) |
| Raw `lv_subject_add_observer_*()` | `observe_int_async<Panel>()` from factory | [Observer Factory](#observer-factory-critical) |

---

## modal_button_row Component

Standard Ok/Cancel button row for modals. Used at the bottom of modal XML layouts.

```xml
<modal_button_row
    primary_text="Save"
    secondary_text="Cancel"
    primary_callback="on_save_clicked"
    secondary_callback="on_cancel_clicked"
    show_secondary="true"/>
```

| Prop | Type | Default | Description |
|------|------|---------|-------------|
| `primary_text` | string | `"OK"` | Right button label |
| `secondary_text` | string | `"Cancel"` | Left button label |
| `primary_callback` | string | — | XML event callback name for primary |
| `secondary_callback` | string | — | XML event callback name for secondary |
| `primary_bg_color` | string | `""` | Optional color override for primary button |
| `show_secondary` | string | `"true"` | Set `"false"` to hide cancel button |

**Note:** `primary_bg_color` and `show_secondary` are declared but not yet wired in the component template. They are currently no-ops.

Renders a horizontal divider + two equal-width buttons. See any `*_modal.xml` for usage examples.

---

## ui_markdown Widget

Theme-aware markdown viewer registered as an XML custom widget. Wraps `lv_markdown`.

```xml
<!-- Bind to a subject (reactive updates) -->
<ui_markdown bind_text="release_notes_subject" width="100%"/>

<!-- Static text -->
<ui_markdown text="# Title\nSome **bold** text" width="100%"/>
```

- Uses `LV_SIZE_CONTENT` for height (grows to fit). Wrap in a scrollable container for long content.
- Theme-aware: colors pulled from design tokens automatically.
- Initialize with `ui_markdown_init()` after `lv_xml_init()` and theme init.

**Header:** `include/ui_markdown.h` | **Source:** `src/ui/ui_markdown.cpp`

---

## Shaper CSV Parser

Parses Klipper input shaper calibration CSV files for frequency response charting.

```cpp
#include "shaper_csv_parser.h"

auto data = helix::calibration::parse_shaper_csv("/tmp/calibration_data_x.csv", 'X');

// data.frequencies    - frequency bins (Hz)
// data.raw_psd        - raw PSD for requested axis
// data.shaper_curves  - per-shaper filtered responses (name, freq, values)
```

**Header:** `include/shaper_csv_parser.h` | **Tests:** `tests/unit/test_shaper_csv_parser.cpp`

---

## Layout System / LayoutManager

Auto-detects screen aspect ratio and loads alternative XML layouts. Themes control colors; layouts control structure.

```cpp
auto& lm = helix::LayoutManager::instance();
lm.init(display_width, display_height);      // Auto-detect
lm.set_override("ultrawide");                // Force layout

std::string path = lm.resolve_xml_path("home_panel.xml");
// Returns "ui_xml/ultrawide/home_panel.xml" if override exists,
// otherwise "ui_xml/home_panel.xml"
```

| Layout | Detection | Example Screens |
|--------|-----------|-----------------|
| `standard` | 4:3 to ~16:9 | 800x480, 1024x600 |
| `ultrawide` | ratio > 2.5:1 | 1920x480 |
| `portrait` | ratio < 0.8:1 | 480x800 |
| `tiny` | max dim <= 480, landscape | 480x320 |
| `tiny_portrait` | max dim <= 480, portrait | 320x480 |

CLI: `--layout ultrawide` | Config: `display.layout` in helixconfig.json

**Full docs:** [LAYOUT_SYSTEM.md](LAYOUT_SYSTEM.md) | **Header:** `include/layout_manager.h`

---

## Config Migration Pattern

Versioned migration system for upgrading existing user configs on update.

**Adding a migration:**
1. Bump `CURRENT_CONFIG_VERSION` in `include/config.h`
2. Write `migrate_vN_to_vM()` in `src/system/config.cpp` (anonymous namespace)
3. Add `if (version < M) migrate_vN_to_vM(config);` in `run_versioned_migrations()`
4. Update `get_default_config()` if the new key needs a default for fresh installs
5. Write tests in `tests/unit/test_config.cpp` with tags `[config][migration][versioning]`

**Rules:** Migrations are append-only, idempotent, never overwrite user data, and log what they do.

**Full docs:** [CONFIG_MIGRATION.md](CONFIG_MIGRATION.md) | **Key files:** `include/config.h`, `src/system/config.cpp`

---

## Modal System

Unified modal system with RAII lifecycle, backdrop, stacking, and animations.

```cpp
// Simple modal (no subclass):
lv_obj_t* dialog = Modal::show("print_cancel_confirm_modal");
Modal::hide(dialog);

// Confirmation dialog helper:
ui_modal_show_confirmation("Delete?", "Cannot undo.",
    ModalSeverity::Warning, "Delete",
    on_confirm_cb, on_cancel_cb, this);

// Alert (single OK button):
ui_modal_show_alert("Done", "Operation complete.");

// Subclassed modal:
class MyModal : public Modal {
    const char* get_name() const override { return "My Modal"; }
    const char* component_name() const override { return "my_modal"; }
    void on_ok() override { save(); Modal::on_ok(); }
};
```

**Header:** `include/ui_modal.h` | **Source:** `src/ui/ui_modal.cpp`

---

## Multi-Extruder & Multi-Tool Access

Quick patterns for working with the multi-extruder and tool abstraction systems.

### Accessing Multi-Extruder Temperatures

```cpp
#include "printer_state.h"

auto& ps = helix::PrinterState::instance();
auto& pts = ps.temperature();

// Legacy: first extruder (backward compatible)
lv_subject_t* temp = pts.get_extruder_temp_subject();     // centidegrees
lv_subject_t* target = pts.get_extruder_target_subject();

// Per-extruder by Klipper name
lv_subject_t* t1_temp = pts.get_extruder_temp_subject("extruder1");
if (t1_temp) {
    int centi = lv_subject_get_int(t1_temp);   // 2053 = 205.3C
    float degrees = centi / 10.0f;
}

// Enumerate all extruders
for (const auto& [name, info] : pts.extruders()) {
    spdlog::info("{}: {:.1f}/{:.1f}C",
        info.display_name, info.temperature, info.target);
}

// React to extruder list changes
add_observer(observe_int_async<MyPanel>(
    pts.get_extruder_version_subject(), this,
    [](MyPanel* self, int32_t) { self->rebuild_temp_display(); }
));
```

### Getting Active Tool Info

```cpp
#include "tool_state.h"

auto& ts = helix::ToolState::instance();
const auto* tool = ts.active_tool();
if (tool) {
    spdlog::info("Tool: {} extruder: {} fan: {}",
        tool->name,
        tool->extruder_name.value_or("none"),
        tool->fan_name.value_or("none"));

    // Get temperature for this tool's extruder
    if (tool->extruder_name) {
        auto* temp = ps.temperature().get_extruder_temp_subject(*tool->extruder_name);
    }
}
```

### Enumerating Tools

```cpp
auto& ts = helix::ToolState::instance();

for (const auto& tool : ts.tools()) {
    spdlog::info("T{}: mounted={} active={} detect={}",
        tool.index, tool.mounted, tool.active,
        static_cast<int>(tool.detect_state));
}

// Observe tool changes
add_observer(observe_int_async<MyPanel>(
    ts.get_tools_version_subject(), this,
    [](MyPanel* self, int32_t) { self->rebuild_tool_list(); }
));

// Hide UI element on single-tool printers (XML)
// <bind_flag_if_eq subject="tool_count" flag="hidden" ref_value="1"/>
```

### Multi-Backend AMS Access

```cpp
#include "ams_state.h"

auto& ams = AmsState::instance();

// Number of filament system backends
int count = ams.backend_count();

// Access specific backend
AmsBackend* primary = ams.get_backend(0);
AmsBackend* secondary = ams.get_backend(1);

// Per-backend slot subjects (for UI binding)
lv_subject_t* color = ams.get_slot_color_subject(/*backend=*/1, /*slot=*/0);
lv_subject_t* status = ams.get_slot_status_subject(/*backend=*/1, /*slot=*/0);

// Switch active backend (updates all system-level subjects)
ams.set_active_backend(1);

// Observe backend count changes
// <bind_flag_if_gt subject="backend_count" flag="hidden" ref_value="1"/>
```

**Full docs:** [TOOL_ABSTRACTION.md](TOOL_ABSTRACTION.md), [MULTI_EXTRUDER_TEMPERATURE.md](MULTI_EXTRUDER_TEMPERATURE.md), [FILAMENT_MANAGEMENT.md](FILAMENT_MANAGEMENT.md)

---

## See Also

- **[ARCHITECTURE.md](ARCHITECTURE.md)** - System design, patterns, architectural decisions
- **[LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md)** - Complete XML syntax reference
- **[DEVELOPMENT.md](DEVELOPMENT.md)** - Build system and daily workflow
- **[MOONRAKER_ARCHITECTURE.md](MOONRAKER_ARCHITECTURE.md)** - WebSocket API integration
- **[TESTING.md](TESTING.md)** - Test infrastructure and Catch2 usage
- **[TRANSLATION_SYSTEM.md](TRANSLATION_SYSTEM.md)** - i18n system, YAML workflow, adding translations
- **[plans/](plans/)** - Active implementation plans and technical debt tracker
