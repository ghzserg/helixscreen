# Session Handoff Document

**Last Updated:** 2025-10-27
**Current Focus:** Wizard screens implementation

---

## 🎯 Active Work & Next Priorities

### Wizard Step 0 (WiFi Setup) - READY TO COMMIT ✅

**Status:** Fully implemented and tested - awaiting commit

**Major Implementation Complete:**
- ✅ WiFi Manager platform abstraction (`wifi_manager.h/cpp`, 298 lines)
  - macOS mock mode with 10 test networks
  - Linux hardware detection (WiFi via `/sys/class/net/*/wireless`, Ethernet via interface names)
  - Periodic scanning with 7-second refresh timer
  - Async connection flow with callbacks
- ✅ Custom `<ui_switch>` widget (fixed LVGL 9 XML registration)
- ✅ Network list item component (`network_list_item.xml`)
- ✅ Two-column flex layout working (WiFi/Ethernet cards 30%, network list 70%)
- ✅ WiFi toggle event handler with 3-second scan delay
- ✅ Dynamic network list population from scan results
- ✅ Network list dimming when WiFi disabled (UI_DISABLED_OPA constant)
- ✅ Wizard integration as Step 0 with auto-skip logic
- ✅ CLI support: `--wizard-step wifi`

**Key Technical Decisions:**
- Reactive data flow: Toggle → 3s delay → scan → populate list (all async)
- Critical bug fix: `clear_network_list()` iterates backwards when deleting children
- One-shot timer: `lv_timer_create()` + `lv_timer_set_repeat_count(1)` for delay
- UI_DISABLED_OPA constant (50%) in both `ui_theme.h` and `globals.xml`

**Remaining WiFi Work:**
- Password entry modal component (for encrypted networks)
- Connection flow with success/error feedback
- Unit tests for wifi_manager
- Integration tests for wizard WiFi UI

### Next Priorities

1. **COMMIT WiFi wizard implementation** (all functionality working)
2. Fix wizard Step 3 (Printer Identify) - roller widget collapsed/not visible
3. Implement WiFi password modal and connection flow
4. Hardware detection/mapping screens (Steps 4-7)
5. Integration tests for complete wizard flow

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

---

## 🔧 Known Issues & Gotchas

### Wizard Step 3: Printer Type Roller Collapsed 🔴 OPEN

**Problem:** Printer type roller widget appears collapsed - only shows label, not dropdown options

**Symptoms:**
- Gray box where roller should be
- Options (Voron, Creality, Prusa, etc.) not visible
- Cannot select printer type

**Screenshot:** `/tmp/ui-screenshot-wizard-step3-current.png` (2025-10-27)

**Possible causes:**
1. Height too small for visible rows (currently 160px)
2. Missing `visible_row_count` attribute
3. Flex layout calculation issue

**Files:** `ui_xml/wizard_printer_identify.xml`, `src/ui_wizard.cpp`

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
