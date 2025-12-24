# Architecture Guide

This document explains the HelixScreen prototype's system design, data flow patterns, and architectural decisions.

## Overview

HelixScreen uses a modern, declarative approach to embedded UI development that completely separates presentation from logic:

```
XML Layout Definitions (ui_xml/*.xml)
    ↓ bind_text / bind_value / bind_flag
Reactive Subject System (lv_subject_t)
    ↓ lv_subject_set_* / copy_*
C++ Application Logic (src/*.cpp)
```

**Key Innovation:** The entire UI is defined in XML files. C++ code only handles initialization and reactive data updates—zero layout or styling logic.

## Architectural Principles

### 1. Declarative UI Definition

All layout, styling, and component structure is defined in XML:

```xml
<!-- Complete panel definition in XML -->
<component>
  <view extends="lv_obj" style_bg_color="#bg_dark" style_pad_all="20">
    <lv_label text="Nozzle Temperature" style_text_color="#text_primary"/>
    <lv_label bind_text="temp_text" style_text_font="montserrat_28"/>
  </view>
</component>
```

**Benefits:**
- UI changes don't require recompilation
- Visual designers can modify layouts without C++ knowledge
- Complete separation of concerns between presentation and logic

### 2. Reactive Data Binding

LVGL 9's Subject-Observer pattern enables automatic UI updates:

```cpp
// C++ is pure logic - zero layout code
ui_panel_nozzle_init_subjects();
lv_xml_create(screen, "nozzle_panel", NULL);
ui_panel_nozzle_update(210);  // All bound widgets update automatically
```

**Benefits:**
- No manual widget searching or updating
- Type-safe data updates
- One update propagates to multiple UI elements
- Clean separation between data and presentation

### 3. Custom HelixScreen Theme

HelixScreen uses a custom LVGL theme that wraps the default theme for enhanced styling:

**Architecture:** XML → C++ → Custom Theme → LVGL Default Theme

```xml
<!-- ui_xml/globals.xml - Single source of truth for theme values -->
<consts>
  <color name="primary_color" value="..."/>
  <color name="secondary_color" value="..."/>

  <!-- Theme-specific color variants for light/dark mode -->
  <color name="app_bg_color_light" value="..."/>
  <color name="app_bg_color_dark" value="..."/>
  <color name="card_bg_light" value="..."/>
  <color name="card_bg_dark" value="..."/>
  <color name="text_primary_light" value="..."/>
  <color name="text_primary_dark" value="..."/>

  <str name="font_body" value="..."/>
  <str name="font_heading" value="..."/>
</consts>
```

```cpp
// src/helix_theme.c - Custom theme wrapper
static void helix_theme_apply_cb(lv_theme_t* theme, lv_obj_t* obj) {
    // Apply default theme first
    lv_theme_apply(helix_theme->default_theme, obj);

    // Override input widgets with computed background color
    if(lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &helix_theme->input_bg_style, 0);
    }
    // Similar for dropdown, roller, spinbox...
}
```

```cpp
// src/ui_theme.cpp - Initializes custom theme
void ui_theme_init(lv_display_t* display, bool dark_mode) {
    // Read colors from XML (NO hardcoded colors!)
    lv_color_t card_bg = parse_color(dark_mode ? card_bg_dark : card_bg_light);

    // Initialize custom HelixScreen theme (wraps default)
    lv_theme_t* theme = helix_theme_init(display, primary, secondary,
                                          dark_mode, font, screen_bg, card_bg, grey);
    lv_display_set_theme(display, theme);
}
```

**Key Features:**
- ✅ **No recompilation needed** - Edit `globals.xml` to change theme colors
- ✅ **Automatic styling** - Input widgets get computed backgrounds automatically
- ✅ **Computed colors** - Input backgrounds are lighter/darker than cards based on mode
- ✅ **Responsive** - Scales padding/sizing for different screen resolutions
- ✅ **Maintainable** - Uses LVGL public API, no fragile private structure patching
- ✅ **Dark/Light mode** - Runtime theme switching support
- ✅ **State-based styling** - Automatic pressed/disabled/checked states

**Theme Customization:**
- **Colors:** `primary_color`, `secondary_color`, `text_primary`, `text_secondary` defined in globals.xml
- **Fonts:** `font_heading`, `font_body`, `font_small` for manual widget styling when needed
- **Mode:** Dark/light mode controlled via config file or command-line flags

**Config Persistence:**
Theme preference saved to `helixconfig.json` and restored on next launch:
```json
{
  "dark_mode": true,
  ...
}
```

## ⚠️ CRITICAL: Reactive-First Principle - "The HelixScreen Way"

**ALL UI control MUST be reactive via subjects. Direct widget manipulation is an anti-pattern.**

### ✅ The Correct Way: Reactive UI Control

Control UI elements by updating subjects in C++, binding to them in XML:

```cpp
// C++ - Pure data updates, zero widget manipulation
lv_subject_t connection_test_passed;
lv_subject_init_int(&connection_test_passed, 0);  // Button starts disabled

// Later: Update subject when connection succeeds
lv_subject_set_int(&connection_test_passed, 1);  // Button becomes enabled automatically
```

```xml
<!-- XML - Reactive bindings control UI state -->
<lv_button name="next_button">
  <!-- Button automatically updates when subject changes -->
  <lv_obj-bind_flag_if_eq subject="connection_test_passed" flag="clickable" ref_value="0" negate="true"/>
  <lv_obj-bind_flag_if_eq subject="connection_test_passed" flag="user_1" ref_value="0"/>
</lv_button>
<lv_style selector="LV_STATE_USER_1" style_opa="128"/>  <!-- Disabled style -->
```

**Benefits:**
- ✅ UI automatically stays in sync with application state
- ✅ Zero manual widget searching/updating
- ✅ Testable - can verify subject values without UI
- ✅ Reusable - multiple widgets can bind to same subject

###  ❌ ANTI-PATTERN: Direct Widget Manipulation

**DO NOT** search for widgets by name/ID and manipulate them from C++:

```cpp
// ❌ WRONG - Direct widget manipulation (ANTI-PATTERN)
lv_obj_t* button = lv_obj_find_by_name(screen, "next_button");
lv_obj_add_state(button, LV_STATE_DISABLED);      // Manual state management
lv_obj_set_style_opa(button, 128, 0);              // Manual styling

// ❌ WRONG - Searching for labels to update text
lv_obj_t* label = lv_obj_find_by_name(panel, "temp_display");
lv_label_set_text(label, "210°C");                 // Manual text update
```

**Why this is wrong:**
- ❌ Couples C++ code to specific widget names/structure
- ❌ Breaks if XML layout changes
- ❌ Difficult to test (requires UI to exist)
- ❌ Fragile - easy to forget updates, cause inconsistent state
- ❌ Violates separation of concerns

### Reactive Patterns for Common UI Tasks

| UI Task | ❌ Anti-Pattern | ✅ Reactive Way |
|---------|----------------|----------------|
| Update text | `lv_label_set_text(label, "...")` | `bind_text` in XML, `lv_subject_set_string()` in C++ |
| Enable/disable | `lv_obj_add_state(obj, DISABLED)` | `bind_flag_if_eq` in XML, update subject in C++ |
| Show/hide | `lv_obj_add_flag(obj, HIDDEN)` | `bind_flag_if_eq` for `hidden` flag |
| Update value | `lv_slider_set_value(slider, val)` | `bind_value` in XML, `lv_subject_set_int()` in C++ |
| Visual feedback | Manual style changes | `bind_flag_if_eq` + conditional styles |

### When Direct Access IS Acceptable

The ONLY acceptable use of `lv_obj_find_by_name()` is during **initialization** for special cases:

```cpp
// ✅ OK - One-time initialization during panel setup
void ui_panel_init() {
    lv_obj_t* dropdown = lv_obj_find_by_name(panel, "hardware_dropdown");
    ui_dropdown_populate(dropdown, get_available_hardware());  // One-time setup
}
```

**After initialization, ALL updates must be reactive.**

## Component Hierarchy

```
app_layout.xml
├── navigation_bar.xml      # 5-button vertical navigation
└── content_area
    ├── home_panel.xml       # Print status overview
    ├── controls_panel.xml   # Motion/temperature/extrusion launcher
    │   ├── motion_panel.xml
    │   ├── nozzle_temp_panel.xml
    │   ├── bed_temp_panel.xml
    │   └── extrusion_panel.xml
    ├── print_select_panel.xml
    ├── filament_panel.xml
    ├── settings_panel.xml
    └── advanced_panel.xml
```

**Design Patterns:**
- **App Layout** - Root container with navigation + content area
- **Panel Components** - Self-contained UI screens with reactive data
- **Sub-Panel Overlays** - Motion/temp controls that slide over main content
- **Global Navigation** - Persistent 5-button navigation bar

## ⚠️ PREFERRED: Class-Based Architecture

**All new code should use class-based patterns.** This applies to:
- **UI panels** (PanelBase)
- **Modals** (ModalBase)
- **Backend managers** (WiFiManager, EthernetManager, MoonrakerClient)
- **Services and utilities**

### Why Class-Based?

| Benefit | Description |
|---------|-------------|
| **RAII** | Resources acquired in constructor, released in destructor - no leaks |
| **Encapsulation** | State and behavior together, clear ownership |
| **Testability** | Mock via interface inheritance, isolate dependencies |
| **Lifecycle** | Explicit init/start/stop/destroy - no hidden state |

**For implementation examples, see [QUICK_REFERENCE.md](QUICK_REFERENCE.md#class-patterns).**

### ❌ AVOID: Function-Based Patterns

```cpp
// ❌ OLD PATTERN - Do not use for new code
void ui_panel_motion_init(lv_obj_t* parent);
void ui_panel_motion_show();
void ui_panel_motion_hide();

// ✅ NEW PATTERN - Use classes
auto motion = std::make_unique<MotionPanel>(parent);
motion->show();
```

## Data Flow Architecture

### Subject Initialization Pattern

**Critical:** Subjects must be initialized BEFORE creating XML:

```cpp
// 1. Register XML components
lv_xml_register_component_from_file("A:/ui_xml/globals.xml");
lv_xml_register_component_from_file("A:/ui_xml/home_panel.xml");

// 2. Initialize subjects (BEFORE XML creation)
ui_nav_init();
ui_panel_home_init_subjects();

// 3. NOW create UI - bindings will find initialized subjects
lv_xml_create(screen, "app_layout", NULL);
```

**Why this order matters:**
- XML bindings look up subjects by name during creation
- If subjects don't exist, bindings fail silently with empty values
- C++ initialization creates subjects with proper default values

### Reactive Update Flow

```cpp
// Business logic updates subject
lv_subject_set_string(temp_text_subject, "210°C");

// LVGL automatically:
// 1. Notifies all observers (bound widgets)
// 2. Updates widget properties (text, values, flags)
// 3. Triggers redraws as needed

// Zero manual widget management required
```

### Event Handling Pattern

XML defines event bindings, C++ implements handlers:

```xml
<!-- XML: Declarative event binding -->
<lv_button>
    <event_cb trigger="clicked" callback="on_temp_increase"/>
</lv_button>
```

```cpp
// C++: Pure business logic
void on_temp_increase(lv_event_t* e) {
    int current = get_target_temp();
    set_target_temp(current + 5);

    // UI updates automatically via subject binding
    lv_subject_set_int(temp_target_subject, current + 5);
}
```

## Memory Management

### Subject Lifecycle

- **Creation:** During `ui_*_init_subjects()` functions
- **Lifetime:** Persistent throughout application runtime
- **Updates:** Via `lv_subject_set_*()` functions from any thread
- **Cleanup:** Automatic when application exits

### Widget Management

- **Creation:** Automatic during `lv_xml_create()`
- **Lifetime:** Managed by LVGL parent-child hierarchy
- **Updates:** Automatic via subject-observer bindings
- **Cleanup:** Automatic when parent objects are deleted

### LVGL Memory Patterns

LVGL uses automatic memory management:
- Widget memory allocated during creation
- Parent widgets automatically free child widgets
- No manual `free()` calls needed for UI elements
- Use LVGL's built-in reference counting for shared resources

### ⚠️ **REQUIRED:** RAII for Custom Widget Memory

**MANDATORY PATTERN for all custom widgets that allocate memory:**

Custom widgets must use RAII (Resource Acquisition Is Initialization) for exception-safe memory management. Manual `lv_malloc/lv_free` is **forbidden** due to leak risks from exceptions or early returns.

**Required header:** `#include "ui_widget_memory.h"`

**Pattern 1: Widget user_data (most common):**

```cpp
// Widget state structure
struct MyWidgetState {
    int value;
    lv_obj_t* button;
};

// ✅ REQUIRED: Delete callback uses RAII wrapper
static void my_widget_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    // Transfer ownership to RAII wrapper - automatic cleanup
    lvgl_unique_ptr<MyWidgetState> state(
        (MyWidgetState*)lv_obj_get_user_data(obj)
    );
    lv_obj_set_user_data(obj, nullptr);

    // Even if cleanup code throws exceptions, state is freed
    cleanup_resources();
}

// ✅ REQUIRED: Widget creation uses RAII helper
lv_obj_t* my_widget_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    if (!obj) return nullptr;

    // Allocate using RAII helper
    auto state_ptr = lvgl_make_unique<MyWidgetState>();
    if (!state_ptr) {
        lv_obj_delete(obj);
        return nullptr;
    }

    // Get raw pointer for initialization
    MyWidgetState* state = state_ptr.get();
    state->value = 0;
    state->button = nullptr;

    // Transfer ownership to LVGL widget
    lv_obj_set_user_data(obj, state_ptr.release());

    // Register cleanup
    lv_obj_add_event_cb(obj, my_widget_delete_cb, LV_EVENT_DELETE, nullptr);

    return obj;
}
```

**Pattern 2: Standalone widget structures (e.g., ui_temp_graph):**

```cpp
// ✅ REQUIRED: Creation returns unique_ptr ownership
ui_temp_graph_t* ui_temp_graph_create(lv_obj_t* parent) {
    auto graph_ptr = std::make_unique<ui_temp_graph_t>();
    if (!graph_ptr) return nullptr;

    // Initialize...
    graph_ptr->chart = lv_chart_create(parent);
    if (!graph_ptr->chart) {
        return nullptr; // graph_ptr auto-freed
    }

    // Transfer ownership to caller
    return graph_ptr.release();
}

// ✅ REQUIRED: Destruction uses RAII wrapper
void ui_temp_graph_destroy(ui_temp_graph_t* graph) {
    if (!graph) return;

    // Transfer ownership to RAII wrapper
    std::unique_ptr<ui_temp_graph_t> graph_ptr(graph);

    // Cleanup LVGL widgets
    if (graph_ptr->chart) {
        lv_obj_del(graph_ptr->chart);
    }

    // graph_ptr automatically freed via ~unique_ptr()
}
```

**Pattern 3: Nested allocations (e.g., ui_step_progress):**

```cpp
struct WidgetData {
    char** label_buffers;  // Array of strings
    int count;
};

// Allocate nested arrays using RAII
auto data_ptr = lvgl_make_unique<WidgetData>();
auto label_buffers_ptr = lvgl_make_unique_array<char*>(count);

// Initialize...
data_ptr->label_buffers = label_buffers_ptr.get();
data_ptr->count = count;

for (int i = 0; i < count; i++) {
    auto label = lvgl_make_unique_array<char>(128);
    data_ptr->label_buffers[i] = label.release();
}

// Release ownership
label_buffers_ptr.release();
lv_obj_set_user_data(obj, data_ptr.release());
```

**Why RAII is mandatory:**
1. **Exception Safety:** If code between malloc and free throws, memory leaks
2. **Early Returns:** Manual free is skipped if function returns early
3. **Maintenance:** RAII is self-documenting and enforces cleanup
4. **Future-Proof:** Adding exception-throwing code won't introduce leaks

**❌ FORBIDDEN ANTI-PATTERN:**

```cpp
// ❌ WRONG: Manual malloc/free is NOT ALLOWED
lv_obj_t* my_widget_create(lv_obj_t* parent) {
    MyWidgetState* state = (MyWidgetState*)lv_malloc(sizeof(MyWidgetState));
    if (!state) return nullptr;

    // If this throws exception, state leaks!
    do_something_that_might_throw();

    lv_obj_set_user_data(obj, state);
    return obj;
}

static void my_widget_delete_cb(lv_event_t* e) {
    MyWidgetState* state = ...;

    // If this throws exception, state leaks!
    cleanup_resources();

    lv_free(state);  // ❌ Never reached if exception thrown
}
```

**See also:** `include/ui_widget_memory.h` for full API documentation

**Examples:**
- `src/ui_jog_pad.cpp` - Simple widget user_data
- `src/ui_step_progress.cpp` - Complex nested allocations
- `src/ui_temp_graph.cpp` - Standalone structure with custom destroy

### Static Object Destructors and Logging

**Problem:** Static/global objects are destroyed during `exit()` in undefined order across translation units (static destruction order fiasco). If your destructor tries to use spdlog, it may crash because spdlog's global logger might already be destroyed.

**Solution:** Use `fprintf(stderr, ...)` instead of spdlog in destructors of static/global objects:

```cpp
MyManager::~MyManager() {
    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[MyManager] Shutting down\n");
    cleanup_resources();
}
```

**When this applies:**
- Destructors of objects stored in static/global variables (e.g., `static std::unique_ptr<WiFiManager>`)
- Any destructor that might run during `exit()` or program termination

**Reference implementations:**
- `src/wifi_manager.cpp:71-72`
- `src/ethernet_manager.cpp:38-41`
- `src/ethernet_backend_*.cpp` (all backend destructors)

**Note:** This is separate from the weak_ptr pattern used for async callback safety - that protects against managers being explicitly destroyed via `.reset()` while async operations are queued.

### ⚠️ Timer Lifecycle Management

**LVGL timers are NOT automatically cleaned up.** Timers created with `lv_timer_create()` continue running until explicitly deleted with `lv_timer_delete()`. If the object passed as `user_data` is destroyed without deleting the timer, the timer will fire with a dangling pointer causing use-after-free crashes.

**Recommended: Use LvglTimerGuard RAII wrapper**

```cpp
#include "ui_timer_guard.h"

class MyPanel {
    LvglTimerGuard update_timer_;

    void start_updates() {
        // Timer automatically deleted when MyPanel is destroyed
        update_timer_.reset(lv_timer_create(update_cb, 1000, this));
    }

    void stop_updates() {
        update_timer_.reset();  // Explicitly stop timer
    }
};
```

**Alternative: Manual cleanup with lv_is_initialized() guard**

For panels/classes that manage timers manually:

```cpp
MyPanel::~MyPanel() {
    // Check LVGL is still running (avoids crash during static destruction)
    if (lv_is_initialized()) {
        if (my_timer_) {
            lv_timer_delete(my_timer_);
            my_timer_ = nullptr;
        }
    }
}
```

**Timer patterns:**

| Pattern | Safe? | Notes |
|---------|-------|-------|
| One-shot with `lv_timer_delete(t)` in callback | YES | Timer self-destructs |
| One-shot with `lv_timer_set_repeat_count(t, 1)` | YES | LVGL auto-deletes |
| LvglTimerGuard member | YES | RAII cleanup |
| Manual delete in destructor with `lv_is_initialized()` check | YES | Explicit cleanup |
| Timer stored in member, no cleanup | **NO** | Use-after-free risk |

**See also:** `include/ui_timer_guard.h` for full API documentation

## Thread Safety

### ⚠️ CRITICAL: LVGL Main Thread Requirement

**LVGL is NOT thread-safe.** All widget creation and modification MUST happen on the main thread.

### ⚠️ Subject Updates Are NOT Thread-Safe

**CRITICAL MISCONCEPTION:** You might assume `lv_subject_set_*()` is thread-safe because it's just updating a value. **THIS IS WRONG.**

```cpp
// ❌ DANGEROUS - looks safe but isn't!
void update_from_websocket_thread(int temp) {
    lv_subject_set_int(temp_subject, temp);  // Triggers observers!
    // If LVGL is rendering → assertion failure → infinite loop
}
```

**Why this fails:** Subject updates trigger bound observers. Those observers often:
1. Call `lv_label_set_text()` → triggers `lv_obj_invalidate()`
2. Call `lv_obj_add_flag()` → triggers `lv_obj_invalidate()`
3. Any widget modification during `lv_timer_handler()` rendering → **ASSERTION FAILURE**

The LVGL assertion `!disp->rendering_in_progress` will fire, and on embedded targets this causes an infinite `while(1)` loop.

**Real-world example:** WebSocket callback calls `FilamentSensorManager::discover_sensors()` which calls `lv_subject_set_int()`. If LVGL happens to be rendering, the app hangs and stops responding to pings.

### Safe Pattern: Defer to Main Thread

**Always use `ui_async_call()` for subject updates from background threads:**

```cpp
#include "ui_update_queue.h"

// ✅ CORRECT - defers to main thread via ui_async_call()
struct TempUpdateContext {
    PrinterState* state;
    int temperature;
};

void async_temp_callback(void* user_data) {
    auto* ctx = static_cast<TempUpdateContext*>(user_data);
    lv_subject_set_int(ctx->state->temp_subject(), ctx->temperature);  // Now safe!
    delete ctx;
}

void update_from_websocket_thread(int temp) {
    auto* ctx = new TempUpdateContext{&get_printer_state(), temp};
    ui_async_call(async_temp_callback, ctx);  // Queued for main thread, runs BEFORE render
}
```

**Why `ui_async_call()` not `lv_async_call()`?** LVGL's native `lv_async_call()` can fire *during* the render phase, causing assertion failures. Our `ui_async_call()` uses `LV_EVENT_REFR_START` to guarantee execution *before* rendering begins.

**Reference Implementation:** See `printer_state.cpp` for the `set_*_internal()` pattern used by:
- `set_printer_capabilities()` → `set_printer_capabilities_internal()`
- `set_klipper_version()` → `set_klipper_version_internal()`
- `set_klippy_state()` → `set_klippy_state_internal()`

### Unsafe Operations (Main Thread Only)

**Widget manipulation requires main thread:**
```cpp
// Main thread only
void handle_ui_event(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKED);  // NOT safe from background threads
}
```

### Backend Integration Pattern: ui_async_call()

**Problem:** Backend threads (networking, file I/O, WiFi scanning) need to update UI but cannot call LVGL APIs directly.

**Solution:** Use `ui_async_call()` to marshal widget updates to the main thread:

```cpp
#include "ui_update_queue.h"

// Backend callback running in std::thread
void WiFiManager::handle_scan_complete(const std::string& data) {
    // Parse results (safe - no LVGL calls)
    auto networks = parse_networks(data);

    // Create data for dispatch
    struct CallbackData {
        std::vector<WiFiNetwork> networks;
        std::function<void(const std::vector<WiFiNetwork>&)> callback;
    };
    auto* cb_data = new CallbackData{networks, scan_callback_};

    // Dispatch to LVGL main thread (safe - runs before render)
    ui_async_call([](void* user_data) {
        auto* data = static_cast<CallbackData*>(user_data);

        // NOW safe to create/modify widgets
        data->callback(data->networks);  // Calls populate_network_list()

        delete data;  // Clean up
    }, cb_data);
}
```

**Key Points:**
1. **Backend thread:** Parse data, prepare callback data structure
2. **ui_async_call():** Queues callback to execute on main thread BEFORE rendering
3. **Main thread callback:** Creates/modifies widgets safely
4. **Memory management:** Heap-allocate data, delete in callback

**Without this pattern:** Race conditions, segfaults, undefined behavior when backend thread creates widgets while LVGL is rendering.

**Reference Implementation:** `src/wifi_manager.cpp` (all event handlers use this pattern)

### When to Use ui_async_call()

✅ **ALWAYS use when on a background thread and:**
- Need to create/modify widgets (`lv_obj_*()` functions)
- Need to update subjects (`lv_subject_set_*()`) - **subjects trigger observers!**
- WebSocket callbacks (libhv event loop thread)
- Network/file I/O completion handlers
- Timer callbacks from non-LVGL timers

❌ **Don't need when:**
- Already on main thread (LVGL event handlers, `lv_timer_create()` callbacks)
- Pure computation with no LVGL calls at all
- Just logging or updating non-LVGL state

**Key insight:** If you're in a callback from libhv, std::thread, or any networking library, assume you're on a background thread and use `ui_async_call()`.

## LVGL Configuration

### Required Features

Key settings in `lv_conf.h` for XML support:

```c
#define LV_USE_XML 1                           // Enable XML UI support
#define LV_USE_SNAPSHOT 1                      // Enable screenshot API
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1     // Required by XML parser
#define LV_FONT_MONTSERRAT_16 1                // Text fonts
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
```

### Display Driver Integration

**SDL2 Simulator:**
- Uses `lv_sdl_window_create()` for desktop development
- Automatic event handling via SDL2 backend
- Multi-display positioning support via environment variables

**Future Embedded Targets:**
- Framebuffer driver for direct hardware rendering
- Touch input via evdev integration
- Same XML/Subject code runs unchanged

## Design Decisions & Trade-offs

### Why XML Instead of Code?

**Advantages:**
- ✅ Rapid iteration without recompilation
- ✅ Designer-friendly declarative syntax
- ✅ Complete separation of presentation and logic
- ✅ Global theming capabilities
- ✅ Reduced C++ complexity

**Trade-offs:**
- ❌ XML support is experimental in LVGL 9
- ❌ Additional layer of abstraction
- ❌ Limited debugging tools for XML issues
- ❌ Requires UTF-8 encoding for all files

**Verdict:** The benefits outweigh the trade-offs for a touch UI where visual design changes frequently.

### Why Subject-Observer Instead of Direct Widget Updates?

**Advantages:**
- ✅ One data change updates multiple UI elements
- ✅ Type-safe data binding
- ✅ Automatic UI consistency
- ✅ Easier to test business logic separately

**Trade-offs:**
- ❌ Additional conceptual complexity
- ❌ Indirect relationship between data and UI
- ❌ Subject name string matching (not compile-time checked)

**Verdict:** The reactive pattern scales better as UI complexity grows and provides cleaner separation of concerns.

### Why LVGL 9 Instead of Native Platform UI?

**Advantages:**
- ✅ Single codebase for all platforms
- ✅ Embedded-optimized (low memory, no GPU required)
- ✅ Touch-first design patterns
- ✅ Extensive widget library
- ✅ Active development and community

**Trade-offs:**
- ❌ Custom look-and-feel (not native platform appearance)
- ❌ Learning curve for LVGL-specific patterns
- ❌ Limited platform integration (no native menus, etc.)

**Verdict:** Perfect for embedded touch interfaces where native platform UI isn't available or suitable.

## Test Mode Architecture

### Overview

The test mode system provides a comprehensive mocking infrastructure for development without hardware dependencies while ensuring production builds never accidentally use mock implementations.

### RuntimeConfig Structure

```cpp
// runtime_config.h
struct RuntimeConfig {
    bool test_mode = false;           // Master flag (--test)
    bool use_real_wifi = false;       // Override flag (--real-wifi)
    bool use_real_ethernet = false;   // Override flag (--real-ethernet)
    bool use_real_moonraker = false;  // Override flag (--real-moonraker)
    bool use_real_files = false;      // Override flag (--real-files)

    // Helper methods for clean code
    bool should_mock_wifi() const {
        return test_mode && !use_real_wifi;
    }
    // ... similar for other components
};
```

### Factory Pattern with Test Mode

Backend factories respect test configuration to control mock vs real selection:

```cpp
std::unique_ptr<WifiBackend> WifiBackend::create() {
    const auto& config = get_runtime_config();

    // Test mode check FIRST
    if (config.should_mock_wifi()) {
        spdlog::info("[TEST MODE] Using MOCK WiFi backend");
        return std::make_unique<WifiBackendMock>();
    }

    // Production path - try real, fail if unavailable
    auto backend = std::make_unique<WifiBackendMacOS>();
    if (!backend->start()) {
        spdlog::error("WiFi hardware unavailable");
        return nullptr;  // NEVER fall back to mock
    }

    return backend;
}
```

### Production Safety Rules

1. **No Automatic Fallbacks:** Production mode NEVER falls back to mocks
2. **Explicit Test Mode:** Mocks require `--test` flag
3. **Clear Error Messages:** Hardware failures show user-friendly errors
4. **Visual Indicators:** Test mode displays banner with mock/real status

### Mock Implementation Features

**WiFi Mock Backend:**
- 10 realistic networks with varied security (WPA2, WEP, Open)
- Signal strength variation (±5% per scan)
- Simulated scan delay (2 seconds)
- 5% authentication failure rate for realism
- Random IP address generation

**Ethernet Mock Backend:**
- Always reports interface available
- Static IP: 192.168.1.150
- Instant connection (no delays)

**File List Mock:**
- 8 test G-code files with varied metadata
- Realistic file sizes (2MB-50MB)
- Print times from 5 minutes to 8 hours
- Mix of directories and files

### Command-Line Interface

```bash
# Production mode (default)
./helix-screen                    # No mocks, requires hardware

# Test mode variations
./helix-screen --test              # All mocks
./helix-screen --test --real-moonraker  # Real printer only
./helix-screen --test --real-wifi --real-ethernet  # Real network only
```

### Implementation Files

- **Core:** `runtime_config.h`, `main.cpp:319-439`
- **WiFi:** `wifi_backend.cpp`, `wifi_backend_mock.cpp`
- **Ethernet:** `ethernet_backend.cpp`, `ethernet_backend_mock.cpp`
- **Tests:** `tests/unit/test_test_config.cpp`

## Critical Implementation Patterns

### Component Instantiation Names

**Always add explicit `name` attributes** to component instantiations:

```xml
<!-- app_layout.xml -->
<lv_obj name="content_area">
  <controls_panel name="controls_panel"/>  <!-- Explicit name required -->
  <home_panel name="home_panel"/>
</lv_obj>
```

**Why:** Component names in `<view name="...">` definitions do NOT propagate to `<component_tag/>` instantiations. Without explicit names, `lv_obj_find_by_name()` returns NULL.

**See [QUICK_REFERENCE.md](QUICK_REFERENCE.md#component-names-critical) for quick syntax reference.**

### Widget Lookup by Name

Use `lv_obj_find_by_name()` instead of index-based child access:

```cpp
// In XML: <lv_label name="temp_display" bind_text="temp_text"/>
// In C++:
lv_obj_t* label = lv_obj_find_by_name(panel, "temp_display");
if (label != NULL) {
    // Safe to use widget
}
```

**Benefits:**
- Robust against XML layout changes
- Self-documenting code
- Explicit error handling when widgets don't exist

### Image Scaling in Flex Layouts

**When scaling images immediately after layout changes**, call `lv_obj_update_layout()` first:

```cpp
// WRONG: Container reports 0x0 size
lv_coord_t w = lv_obj_get_width(container);  // Returns 0
ui_image_scale_to_cover(img, container);     // Fails

// CORRECT: Force layout calculation first
lv_obj_update_layout(container);
lv_coord_t w = lv_obj_get_width(container);  // Returns actual size
ui_image_scale_to_cover(img, container);     // Works correctly
```

**Why:** LVGL uses deferred layout calculation for performance. Immediate size queries after layout changes return stale values.

### Navigation History Stack

Use the navigation system for consistent overlay management:

```cpp
// When showing overlay
ui_nav_push_overlay(motion_panel);  // Pushes current to history, shows overlay

// In back button callback
if (!ui_nav_go_back()) {
    // Fallback: manual navigation if history is empty
    ui_nav_show_panel(home_panel);
}
```

**Benefits:**
- Automatic history management
- Consistent back button behavior
- State preservation when navigating back

**For common implementation patterns and code snippets, see [QUICK_REFERENCE.md](QUICK_REFERENCE.md).**

## Performance Characteristics

### XML Parsing Performance

- **One-time cost** during application startup
- Component registration is fast (simple file parsing)
- Widget creation is standard LVGL performance
- **No runtime XML parsing** after initialization

### Subject Update Performance

- **O(n) complexity** where n = number of bound widgets
- Optimized for small numbers of observers per subject
- **Batched updates** - multiple subject changes before next redraw
- **Efficient for typical UI** with 10-50 bound elements per panel

### Memory Footprint

- **Minimal XML overhead** - parsed structure discarded after creation
- **Subject storage** - ~100 bytes per subject (reasonable for 50-100 subjects)
- **Widget memory** - standard LVGL allocation patterns
- **Total overhead** - estimated <10KB for XML/Subject systems

## Future Architecture Considerations

### Moonraker Integration

The architecture is designed to accommodate real-time printer data:

```cpp
// WebSocket data updates subjects
void on_printer_temp_update(float nozzle, float bed) {
    lv_subject_set_string(nozzle_temp_subject, format_temp(nozzle));
    lv_subject_set_string(bed_temp_subject, format_temp(bed));
    // UI updates automatically
}
```

### State Persistence

Subject values can be saved/restored for session persistence:

```cpp
void save_ui_state() {
    config["target_temp"] = lv_subject_get_int(temp_target_subject);
    config["panel_mode"] = lv_subject_get_string(nav_current_subject);
}

void restore_ui_state() {
    lv_subject_set_int(temp_target_subject, config["target_temp"]);
    lv_subject_set_string(nav_current_subject, config["panel_mode"]);
}
```

### Platform Adaptation

The XML/Subject architecture adapts easily to different display sizes:

- **Small displays** - Use `style_flex_flow="column"` for vertical layouts
- **Large displays** - Use `style_flex_flow="row"` for horizontal layouts
- **Theme scaling** - Adjust font sizes and padding in `globals.xml`
- **Content adaptation** - Show/hide elements with conditional flag bindings

## Legitimate Exceptions to UI Patterns

During code audits, the following patterns may appear to violate declarative UI guidelines but are **acceptable exceptions**. Future audits should not flag these:

### 1. DELETE Event Handlers
`lv_obj_add_event_cb(obj, cb, LV_EVENT_DELETE, ...)` is required for RAII cleanup of widget user_data. Cannot be done in XML.

### 2. Canvas/Drawing Code
Files like `nozzle_renderer_*.cpp`, `ui_filament_path_canvas.cpp`, `ui_bed_mesh.cpp` use hardcoded colors for physical/material rendering (brass nozzle, charcoal frame). These are not theme colors.

### 3. Dynamic Widget Creation
Widgets created at runtime (step progress indicators, AMS mini status, keyboard overlays) cannot use XML bindings since they're generated programmatically.

### 4. Gesture State Machines
Keyboard long-press detection, jog pad touch handling require imperative state management for gesture recognition.

### 5. Bootstrap Components
Fatal error screen (`ui_fatal_error.cpp`) runs before theme is loaded - must use hardcoded colors.

### 6. Fallback Color Pattern
```cpp
const char* str = lv_xml_get_const("color_token");
lv_color_t c = str ? ui_theme_parse_hex_color(str) : lv_color_hex(0xFALLBACK);
```
This is correct - `str` contains an actual hex value from XML, not a token name.

### 7. fprintf in Destructors
spdlog may be destroyed during static destruction. Use `fprintf(stderr, ...)` in destructors of static/global objects.

### 8. Async Context new/delete
`ui_async_call()` requires heap-allocated context structs for thread marshaling. The async callback must `delete` the context.

### 9. CLI printf
Code in `cli_args.cpp` runs before logging infrastructure is initialized - printf is acceptable.

### 10. Experimental Test Code
Test scaffolding in `experimental/src/*.cpp` uses printf for test output - not production code.

### 11. Modal Dialog Buttons
Dynamically created modals (confirmation dialogs, warnings) wire buttons imperatively since they're created at runtime.

### 12. snprintf with sizeof()
```cpp
char buf[256];
snprintf(buf, sizeof(buf), "format...", args);
```
This is safe - truncates rather than overflows. Not a security issue.

---

## Related Documentation

This document focuses on system design, patterns, and architectural decisions ("why"). For implementation details:

- **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** - Code snippets, common patterns, quick lookups ("how")
- **[LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md)** - Complete XML syntax reference
- **[DEVELOPMENT.md](DEVELOPMENT.md)** - Build system and daily workflow
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Code standards and git workflow
- **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)** - Build configuration and patches