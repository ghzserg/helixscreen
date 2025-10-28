# Session Handoff Document

**Last Updated:** 2025-10-27
**Current Focus:** UI testing expansion (blocked by fixture issues)

---

## 🎯 Active Work & Next Priorities

### ✅ Recently Completed

**WiFi Wizard UI Test Expansion:**
- Added 10 comprehensive UI test cases (396 lines, only 1 passing)
- Password modal validation (title, SSID, input fields, buttons)
- Network list state verification tests
- Connection flow integration tests
- Test limitations documented (see Known Issues)

**Previous Work:**
- WiFi wizard step 0 with password modal (commit 8cab855)
- UI testing infrastructure with virtual input device (commit ded96ef)

### Next Priorities

1. **Fix wizard fixture cleanup** - Resolve segfaults when creating multiple wizard instances
2. **Debug virtual input events** - ui_switch not responding to UITest::click()
3. **Hardware detection screens** - Wizard steps 4-7 (bed, hotend, fan, LED selection)
4. **Alternative test approach** - Consider single-fixture tests or direct API calls

---

## 📋 Critical Architecture Patterns (Essential How-To Reference)

### Pattern #0: Flex Layout Height Requirements 🚨 CRITICAL

**When using `flex_grow` on children, parent MUST have explicit height:**

```xml
<!-- BROKEN: Parent has no height -->
<lv_obj flex_flow="row">
    <lv_obj flex_grow="3">Left (30%)</lv_obj>
    <lv_obj flex_grow="7">Right (70%)</lv_obj>
</lv_obj>
<!-- Result: Columns collapse to 0 height -->

<!-- CORRECT: Two-column pattern (30/70 split) -->
<view height="100%" flex_flow="column">
    <lv_obj width="100%" flex_grow="1" flex_flow="column">
        <lv_obj width="100%" flex_grow="1" flex_flow="row">
            <!-- BOTH columns MUST have height="100%" -->
            <lv_obj flex_grow="3" height="100%"
                    flex_flow="column" scrollable="true" scroll_dir="VER">
                <lv_obj height="100">Card 1</lv_obj>
                <lv_obj height="100">Card 2</lv_obj>
            </lv_obj>
            <lv_obj flex_grow="7" height="100%"
                    scrollable="true" scroll_dir="VER">
                <!-- Content -->
            </lv_obj>
        </lv_obj>
    </lv_obj>
</view>
```

**Critical Checks:**
1. Parent has explicit height (`height="300"`, `height="100%"`, or `flex_grow="1"`)
2. ALL columns have `height="100%"` (row height = tallest child)
3. Every level has sizing (wrapper → row → columns)
4. Cards use fixed heights (`height="100"`), NOT `LV_SIZE_CONTENT` in nested flex

**Diagnostic:** Add `style_bg_color="#ff0000"` to visualize bounds

**Reference:** `docs/LVGL9_XML_GUIDE.md:634-716`, `.claude/agents/widget-maker.md:107-149`, `.claude/agents/ui-reviewer.md:101-152`

### Pattern #1: Custom Switch Widget

**Available:** `<ui_switch>` registered for XML use

```xml
<ui_switch name="my_toggle" checked="true"/>
<ui_switch orientation="horizontal"/>  <!-- auto|horizontal|vertical -->
```

**Supports:** All standard `lv_obj` properties (width, height, style_*)

**Files:** `include/ui_switch.h`, `src/ui_switch.cpp`, `src/main.cpp:643`

### Pattern #2: Navigation History Stack

**When to use:** Overlay panels (motion, temps, extrusion, keypad)

```cpp
ui_nav_push_overlay(motion_panel);  // Shows overlay, saves history
if (!ui_nav_go_back()) { /* fallback */ }
```

**Files:** `ui_nav.h:54-62`, `ui_nav.cpp:250-327`

### Pattern #3: Global Keyboard for Textareas

```cpp
// One-time init in main.cpp (already done)
ui_keyboard_init(lv_screen_active());

// For each textarea
ui_keyboard_register_textarea(my_textarea);  // Auto show/hide on focus
```

**Files:** `include/ui_keyboard.h`, `src/ui_keyboard.cpp`

### Pattern #4: Subject Initialization Order

**MUST initialize subjects BEFORE creating XML:**

```cpp
lv_xml_component_register_from_file("A:/ui_xml/my_panel.xml");
ui_my_panel_init_subjects();  // FIRST
lv_xml_create(screen, "my_panel", NULL);  // AFTER
```

### Pattern #5: Component Instantiation Names

**Always add explicit `name` attributes:**

```xml
<!-- WRONG --><my_panel/>
<!-- CORRECT --><my_panel name="my_panel"/>
```

**Why:** Component `<view name="...">` doesn't propagate to instantiation

### Pattern #6: Image Scaling in Flex Layouts

```cpp
lv_obj_update_layout(container);  // Force layout calculation FIRST
ui_image_scale_to_cover(img, container);
```

**Why:** LVGL uses deferred layout - containers report 0x0 until forced

**Files:** `ui_utils.cpp:213-276`, `ui_panel_print_status.cpp:249-314`

### Pattern #7: Logging Policy

**ALWAYS use spdlog, NEVER printf/cout/LV_LOG:**

```cpp
#include <spdlog/spdlog.h>
spdlog::info("Operation complete: {}", value);  // fmt-style formatting
spdlog::error("Failed: {}", (int)enum_val);     // Cast enums
```

**Reference:** `CLAUDE.md:77-134`

### Pattern #8: Copyright Headers

**ALL new files MUST include GPL v3 header**

**Reference:** `docs/COPYRIGHT_HEADERS.md`

### Pattern #9: UI Testing Infrastructure

**Headless LVGL testing with virtual input device:**

```cpp
#include "../ui_test_utils.h"

class MyUIFixture {
    WizardWiFiUIFixture() {
        static bool lvgl_initialized = false;
        if (!lvgl_initialized) {
            lv_init();
            lvgl_initialized = true;
        }

        display = lv_display_create(800, 480);
        lv_display_set_buffers(display, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, [](lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
            lv_display_flush_ready(disp);
        });

        screen = lv_obj_create(lv_screen_active());
        UITest::init(screen);
    }
};

TEST_CASE_METHOD(MyUIFixture, "My UI test", "[tag]") {
    lv_obj_t* btn = UITest::find_by_name(screen, "my_button");
    UITest::click(btn);
    UITest::wait_ms(100);
    REQUIRE(some_state_changed);
}
```

**Test Utilities:**
- `UITest::click(widget)` - Simulate touch at widget center
- `UITest::type_text(textarea, "text")` - Direct text input
- `UITest::wait_until(condition, timeout)` - Async condition wait
- `UITest::is_visible(widget)` - State verification
- `UITest::find_by_name(parent, "name")` - Widget lookup

**Files:** `tests/ui_test_utils.h/cpp`, `tests/unit/test_wizard_wifi_ui.cpp`

**Run tests:** `./build/bin/run_tests "[tag]"`

---

## 🔧 Known Issues & Gotchas

### UI Test Fixture Segfaults 🚨 CRITICAL

**Problem:** Creating multiple wizard instances causes segmentation faults after first test

**Impact:** Only 1/10 WiFi UI tests can run (9 disabled with `[.disabled]` tag)

**Location:** `tests/unit/test_wizard_wifi_ui.cpp` (all tests using `WizardWiFiUIFixture`)

**Root Cause:** Wizard destructor doesn't properly clean up LVGL object hierarchy
- First test runs successfully (9 assertions pass)
- Second test segfaults during fixture construction
- LVGL subjects or widget tree not being reset between tests

**Workaround:** Run tests individually with `[.disabled]` filter exclusion

**Fix Required:**
1. Investigate wizard cleanup in `~WizardWiFiUIFixture()` destructor
2. Ensure all LVGL objects deleted before display deletion
3. Verify subject cleanup in `ui_wizard_init_subjects()`

### UI Test Virtual Input Not Triggering Events 🐛

**Problem:** `UITest::click()` doesn't trigger `ui_switch` VALUE_CHANGED events

**Impact:** WiFi toggle tests cannot verify state changes via UI interaction

**Location:** `tests/unit/test_wizard_wifi_ui.cpp:169-190` (disabled)

**Observation:** Virtual input device sends events, but ui_switch doesn't respond

**Possible Solutions:**
1. Call C++ APIs directly instead of simulating UI clicks
2. Investigate why ui_switch doesn't process indev events in test environment
3. Use subjects directly to test state without UI interaction

### LVGL 9 XML Roller Options ⚠️ WORKAROUND

**Problem:** LVGL 9 XML roller parser fails with `options="'item1\nitem2' normal"` syntax

**Workaround:** Set roller options programmatically in C++:
```cpp
lv_roller_set_options(roller, "Item 1\nItem 2\nItem 3", LV_ROLLER_MODE_NORMAL);
```

**Status:** Applied to wizard step 3 printer selection (32 printer types)

**Files:** `src/ui_wizard.cpp:352-387`

### LVGL 9 XML Flag Syntax ✅ FIXED

**NEVER use `flag_` prefix:**
- ❌ `flag_hidden="true"` → ✅ `hidden="true"`
- ❌ `flag_clickable="true"` → ✅ `clickable="true"`

**Status:** All XML files fixed (2025-10-24)

### LV_SIZE_CONTENT in Nested Flex

**Problem:** Evaluates to 0 before `lv_obj_update_layout()` is called

**Solutions:**
1. Call `lv_obj_update_layout()` after creation (timing sensitive)
2. Use explicit pixel dimensions (recommended)
3. Use `style_min_height`/`style_min_width` for cards

**Reference:** `docs/LVGL9_XML_GUIDE.md:705-708`

---

**Rule:** When work is complete, REMOVE it from HANDOFF immediately. Keep this document lean and current.
