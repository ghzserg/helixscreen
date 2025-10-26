# Session Handoff Document

**Last Updated:** 2025-10-26 Late Night
**Current Focus:** Temperature panel refactoring complete and committed

---

## 🎯 Active Work & Next Priorities

**Status:** Temperature panel consolidation complete - LVGL 9 API migration finished, Y-axis labels added, code duplication eliminated

**Completed This Session:**
- ✅ Refactored temperature graph to use LVGL 9 public API (minimized private header usage)
- ✅ Added Y-axis temperature labels to both nozzle and bed panels
- ✅ Consolidated duplicate code using heater config structs and common helpers
- ✅ Fixed visual issues: 2px line thickness, hidden point circles, removed rounded corners
- ✅ Enabled montserrat_10 font for axis labels
- ✅ Commit: `204cf2f` - refactor(temp): consolidate panels and refactor graph to use LVGL 9 public API

**Next Steps (when ready):**
1. X-axis time labels for temperature graphs (optional enhancement)
2. Gradient fill implementation using LVGL 9 draw task system (future work - requires deep dive into new API)
3. Interactive keyboard testing with wizard screens
4. Wizard hardware detection/mapping screens implementation
5. Moonraker WebSocket integration for live printer communication

---

## 📋 Critical Architecture Patterns (Essential How-To Reference)

### Pattern #0: Navigation History Stack

**When to use:** Overlay panels (motion, temps, extrusion, keypad)

```cpp
// When showing overlay
ui_nav_push_overlay(motion_panel);  // Pushes current to history

// In back button callback
if (!ui_nav_go_back()) {
    // Fallback if history empty
}
```

**Files:** `ui_nav.h:54-62`, `ui_nav.cpp:250-327`

### Pattern #1: Global Keyboard for Textareas

**When to use:** Any textarea that needs text input

```cpp
// One-time init in main.cpp (already done)
ui_keyboard_init(lv_screen_active());

// For each textarea
ui_keyboard_register_textarea(my_textarea);
// That's it! Auto show/hide on focus
```

**Files:** `include/ui_keyboard.h`, `src/ui_keyboard.cpp`, `src/main.cpp:514`

### Pattern #2: Subject Initialization Order

**MUST initialize subjects BEFORE creating XML:**

```cpp
// CORRECT ORDER:
lv_xml_component_register_from_file("A:/ui_xml/globals.xml");
lv_xml_component_register_from_file("A:/ui_xml/my_panel.xml");

ui_my_panel_init_subjects();  // Initialize FIRST

lv_xml_create(screen, "my_panel", NULL);  // Create AFTER
```

### Pattern #3: Component Instantiation Names

**Always add explicit `name` attributes:**

```xml
<!-- WRONG: No name on instantiation -->
<app_layout>
  <my_panel/>
</app_layout>

<!-- CORRECT: Explicit name -->
<app_layout>
  <my_panel name="my_panel"/>
</app_layout>
```

**Why:** Component `<view name="...">` doesn't propagate to instantiation tags

### Pattern #4: Image Scaling in Flex Layouts

**When scaling images after layout changes, call `lv_obj_update_layout()` first:**

```cpp
lv_obj_update_layout(container);  // Force layout calculation
ui_image_scale_to_cover(img, container);  // Now works correctly
```

**Why:** LVGL uses deferred layout - containers report 0x0 until forced

**Files:** `ui_utils.cpp:213-276`, `ui_panel_print_status.cpp:249-314`

### Pattern #5: Copyright Headers

**ALL new files MUST include GPL v3 header**

**Reference:** `docs/COPYRIGHT_HEADERS.md` for C/C++ and XML templates

### Pattern #6: Logging Policy

**ALWAYS use spdlog, NEVER printf/cout/LV_LOG:**

```cpp
#include <spdlog/spdlog.h>

spdlog::debug("[Component] Debug info: {}", value);
spdlog::info("Operation complete");
spdlog::warn("Validation failed, using default");
spdlog::error("Critical failure: {}", error);
```

**Formatting:**
- Use fmt-style: `spdlog::info("Val: {}", x)` NOT printf-style
- Cast enums: `(int)panel_id`
- Cast pointers: `(void*)widget`

**Reference:** `CLAUDE.md` lines 77-134

### Pattern #7: LV_SIZE_CONTENT Pitfalls

**Problem:** Evaluates to 0 due to deferred layout calculation

**Solutions:**
1. Call `lv_obj_update_layout()` after creation (forces layout)
2. Use explicit pixel dimensions in XML (recommended)
3. Use `style_min_height`/`style_min_width` for flex containers

**Reference:** `docs/LVGL9_XML_GUIDE.md:1241-1329` (comprehensive guide)

---

## 🔧 Known Issues & Gotchas

### LVGL 9 XML Flag Syntax ✅ FIXED

**NEVER use `flag_` prefix in XML attributes:**
- ❌ `flag_hidden="true"`, `flag_clickable="true"`
- ✅ `hidden="true"`, `clickable="true"`

**Status:** All 12 XML files fixed (2025-10-24)

### Icon Constants Rendering

Icon values appear empty in grep/terminal (FontAwesome uses Unicode Private Use Area U+F000-U+F8FF) but contain UTF-8 bytes.

**Fix:** `python3 scripts/generate-icon-consts.py` regenerates constants

**Verify:** `python3 -c "import re; f=open('ui_xml/globals.xml'); print([match.group(1).encode('utf-8').hex() for line in f for match in [re.search(r'icon_backspace.*value=\"([^\"]*)\"', line)] if match])"`

---

**Rule:** When work is complete, REMOVE it from HANDOFF and document in STATUS.md. Keep this document lean and current.
