# Project Status - LVGL 9 UI Prototype

**Last Updated:** 2025-10-10 14:30

## Current State

✅ **Fully functional navigation system with home panel content (Bambu X1C-inspired design)**

### What Works

- **5 Panel Navigation** - Click icons to switch between Home, Controls, Filament, Settings, and Advanced panels
- **Reactive Icon Colors** - Active panel icon shows in red (#ff4444), inactive in white (#ffffff)
- **Home Panel Content** - Temperature display, network status (WiFi icon), light control button (Bambu X1C-inspired)
- **Hybrid Icon Support** - Mix FontAwesome fonts (fa_icons_64, fa_icons_48, fa_icons_32) and custom PNG images
- **Subject-Observer Pattern** - Automatic UI updates via LVGL 9's reactive data binding
- **XML-Based UI** - Complete layout defined declaratively in XML files
- **FontAwesome Icons** - Multiple sizes (64px, 48px, 32px) with auto-generated UTF-8 constants
- **Custom Icons** - SVG-to-PNG conversion with `style_img_recolor` support
- **Theme System** - Global color/size constants in `globals.xml`
- **Screenshot Utility** - Automated 2-second capture to `/tmp`
- **Testing Framework** - Catch2-based unit tests with `make test` integration
- **Clean Architecture** - Minimal C++ code, mostly initialization and reactive updates

### Active Development

**Current Focus:** Phase 3 - Building content for remaining panels

**Next Tasks:**
- **Home Panel:** Add reactivity for network/light state changes (C++ observers)
- Design and implement Controls panel (movement, extrusion, temperature)
- Design and implement Filament panel (load/unload, profiles)
- Design and implement Settings panel (network, display, printer config)
- Design and implement Advanced panel (bed mesh, console, file manager)

## Recent Achievements (2025-10-09)

### ✅ Home Panel XML Migration

**Implemented:**
- Migrated home panel from C++ mockup code to XML UI system
- Three display areas: temperature (top-left), network status (center), light control (right)
- Based on Bambu X1C home screen design pattern
- Temperature display with thermometer icon (fa_icons_32) + reactive text
- Network status with WiFi icon (fa_icons_48) + label
- Light control button with lightbulb icon (fa_icons_48) + click handler
- Event callback registration (`light_toggle_cb`) for button interactions

**Critical Discoveries:**
- **Subject registration conflict** - If `globals.xml` declares subjects, they're registered first with empty/default values, blocking C++ initialization
- **Solution:** Remove `<subjects>` section from `globals.xml`, let C++ handle all subject initialization
- **Icon constants work, subject bindings don't** - Using `text="#icon_wifi"` renders correctly, but `bind_text="network_icon"` fails to display FontAwesome UTF-8 glyphs on fa_icons_48
- **Icon generation required** - Must run `python3 scripts/generate-icon-consts.py` to populate UTF-8 bytes in `globals.xml` (invisible in editors but present)
- **Mixed positioning works** - Combining absolute (`x/y`), alignment (`align="center"`), and no flex on parent is valid when children don't conflict

**Reactivity Status:** ✅ **FULLY WORKING**
- Network icon/label dynamically update when calling `ui_panel_home_set_network()`
- Light icon color changes when calling `ui_panel_home_set_light()`
- Observer pattern successfully updates FontAwesome icon glyphs and colors
- Click handler on light button toggles state correctly

**Files Modified (2025-10-09):**
- `ui_xml/globals.xml` - Removed `<subjects>` section, kept icon constants
- `ui_xml/home_panel.xml` - Changed from subject bindings to constants (static icons)
- `src/ui_panel_home.cpp` - Already has reactive API functions ready
- `include/ui_panel_home.h` - Complete API with `network_type_t` enum

### ✅ Home Panel Reactivity Restored (2025-10-10)

**Implemented:**
- C++ observer callbacks for network and light state subjects
- Widget retrieval using `lv_obj_get_child()` (same approach as navigation panel)
- Observer functions update icon text, labels, and colors dynamically
- All three network states cycle correctly: WiFi → Ethernet → Disconnected
- Light state toggles correctly: OFF (gray) ↔ ON (gold)

**Technical Solution:**
- Added `ui_panel_home_setup_observers()` function called after XML panel creation
- Navigate widget hierarchy using child indices instead of deprecated ID system
- Observers watch subjects and update widgets via `lv_label_set_text()` and `lv_obj_set_style_text_color()`
- FontAwesome UTF-8 icon glyphs update correctly through subject changes

**Key Discovery:**
- LVGL 9 provides `lv_obj_find_by_name()` - The proper way to access XML widgets by name
- CRITICAL: Always use name-based lookup, never index-based `lv_obj_get_child(parent, index)`
- Enabled `LV_USE_OBJ_NAME` in `lv_conf.h` (required for name-based widget lookup)
- Disabled `LV_USE_OBJ_ID` (deprecated approach, using names instead)

**Files Modified:**
- `lv_conf.h` - Enabled `LV_USE_OBJ_ID` (line 384)
- `src/ui_panel_home.cpp` - Added observer setup and callbacks
- `include/ui_panel_home.h` - Added `ui_panel_home_setup_observers()` declaration
- `src/main.cpp` - Call observer setup after XML creation
- `ui_xml/home_panel.xml` - Added `name` attributes (though using child indices instead)

### ✅ Name-Based Widget Lookup Implementation (2025-10-10)

**Implemented:**
- Replaced all fragile index-based widget access with name-based lookup
- Updated `ui_panel_home.cpp` to use `lv_obj_find_by_name()` instead of child indices
- Updated `ui_nav.cpp` navigation system to use widget names from arrays
- Added semantic names to all navigation XML widgets (`nav_btn_home`, `nav_icon_home`, etc.)
- Enabled `LV_USE_OBJ_NAME=1` in `lv_conf.h` for name-based lookup functions
- Disabled `LV_USE_OBJ_ID=0` (deprecated approach, not needed with names)

**Technical Solution:**
```cpp
// Old approach (FRAGILE - breaks when layout changes):
lv_obj_t* widget = lv_obj_get_child(parent, 3);

// New approach (RESILIENT - survives layout changes):
lv_obj_t* widget = lv_obj_find_by_name(parent, "my_widget_name");
```

**Benefits:**
- Layout changes (reordering, adding/removing widgets) won't break widget access
- Self-documenting code - widget names show intent
- Consistent with XML-first architecture
- Eliminates brittle child index counting

**Files Modified:**
- `lv_conf.h` - Enabled `LV_USE_OBJ_NAME`, disabled `LV_USE_OBJ_ID`
- `src/ui_panel_home.cpp` - Replaced 6 index lookups with 3 name-based calls (line 80-82)
- `src/ui_nav.cpp` - Replaced loop index access with name arrays (line 121-122)
- `ui_xml/navigation_bar.xml` - Added names to all 10 navigation widgets
- `CLAUDE.md` - Documented name-based lookup as standard pattern
- `.claude/agents/widget-maker.md` - Updated widget-maker agent pattern

### ✅ Home Panel Polish & Consolidation (2025-10-10)

**Fixed:**
- **Removed border from light button** - Light control button had unwanted 2px border with opacity in `home_panel.xml`. Changed to `style_border_width="0"` and `style_pad_all="0"` for clean appearance
- **Consolidated file naming** - Moved XML-based home panel from `ui_panel_home_xml.*` to standard `ui_panel_home.*` files, removing the "_xml" suffix
- **Cleaned up implementation** - Removed obsolete `ui_panel_home_xml.{cpp,h}` files, updated `main.cpp` to use renamed functions

**Files Modified:**
- `ui_xml/home_panel.xml` - Removed border styling from light button (line 35)
- `src/ui_panel_home.cpp` - Now contains XML-based implementation (was pure C++ mockup)
- `include/ui_panel_home.h` - Added `ui_panel_home_init_subjects()` function
- `src/main.cpp` - Updated to call `ui_panel_home_init_subjects()` instead of `_xml` variant
- **Deleted:** `src/ui_panel_home_xml.cpp`, `include/ui_panel_home_xml.h`

**Result:**
- Light button appears as borderless clickable area with just the icon
- Cleaner file organization without redundant "_xml" suffixes
- Consistent naming convention across the project

### ✅ Testing Framework Setup (2025-10-10)

**Implemented:**
- Full testing infrastructure using Catch2 v2.x single-header framework
- Unit test suite for navigation system (5 test cases, 13 assertions)
- Integration test organization (moved `test-navigation.sh` to proper directory)
- Automated test builds via Makefile (`make test` target)
- Comprehensive testing documentation in `tests/README.md`

**Directory Structure:**
```
tests/
├── framework/
│   └── catch.hpp              # Catch2 v2.x (642KB, vendored)
├── unit/
│   ├── test_main.cpp          # Test runner entry point
│   └── test_navigation.cpp    # Navigation system tests
├── integration/
│   └── test-navigation.sh     # Manual UI testing script
└── README.md                  # Complete testing guide
```

**Technical Solution:**
- Vendored Catch2 v2.x single-header (standard practice, no submodule overhead)
- Headless LVGL testing with minimal display buffers (no SDL window needed)
- Test fixtures for proper LVGL initialization and cleanup
- Name-based widget lookup in tests matches production patterns

**Test Coverage:**
```cpp
✅ Default initialization (HOME panel active)
✅ Switching to all 5 panels individually
✅ Invalid panel ID rejection
✅ Repeated panel selection safety
✅ Full panel enumeration
```

**Build Integration:**
```makefile
test: $(TEST_BIN)              # Build and run all tests
$(TEST_BIN): $(TEST_OBJS) $(LVGL_OBJS) $(OBJ_DIR)/ui_nav.o
```

**Files Created:**
- `tests/framework/catch.hpp` - Catch2 v2.13.10 single-header
- `tests/unit/test_main.cpp` - Test runner with `CATCH_CONFIG_MAIN`
- `tests/unit/test_navigation.cpp` - Navigation system tests
- `tests/README.md` - 400+ line testing guide with examples
- `Makefile` - Added test targets and compilation rules

**Files Moved:**
- `test-navigation.sh` → `tests/integration/test-navigation.sh`

**Result:**
- All 13 assertions passing on first run
- Fast unit tests (no UI rendering overhead)
- Pattern established for testing LVGL components
- Auto-discovery of new test files in `tests/unit/*.cpp`
- Comprehensive documentation for writing new tests

## Previous Achievements (2025-10-08)

### ✅ Phase 2 Completed: Navigation & Blank Panels

**Implemented:**
- Clickable navigation with C++ event handlers (`ui_nav.cpp`)
- Active panel tracking via integer subject
- Panel show/hide management based on active state
- Reactive icon color updates (Subject-Observer pattern)
- All 5 panels registered (Home with content, others blank)

**Critical Discovery:**
- **Never call `SDL_PollEvent()` manually** - breaks click events and violates LVGL's display driver abstraction
- LVGL's internal timer (every 5ms) handles all SDL event polling
- Manual polling drains event queue before LVGL can process it
- This principle applies to ALL display drivers (SDL, framebuffer, DRM, etc.)

**Technical Implementation:**
```cpp
// Correct event loop pattern
while (lv_display_get_next(NULL)) {
    lv_timer_handler();  // Internally polls SDL events
    SDL_Delay(5);
}

// Required setup
lv_display_t* display = lv_sdl_window_create(1024, 800);
lv_indev_t* mouse = lv_sdl_mouse_create();  // ESSENTIAL for clicks
```

**Code Architecture:**
- `ui_nav.cpp` - Navigation system with Subject-Observer pattern
- `navigation_bar.xml` - 5 icon buttons with transparent styling
- `app_layout.xml` - Horizontal layout (navbar + content area)
- Panel XML files - 5 panels, content area switches visibility
- `main.cpp` - Initialization, subject registration, event loop

## Completed Phases

### ✅ Phase 1: Foundation
- LVGL 9.3 with XML support
- SDL2 display backend
- Reactive data binding
- Theme system
- FontAwesome integration
- Documentation

### ✅ Phase 2: Navigation & Blank Panels
- Clickable navigation
- Reactive icon highlighting
- Panel switching
- All 5 panel placeholders

## Upcoming Work

### Phase 3: Panel Content (IN PROGRESS)
Priority: High - Build out actual functionality for each panel

### Phase 4: Enhanced UI Components
- Integer subjects for progress bars/sliders
- Custom reusable widgets
- Modal dialogs
- Scrolling lists

### Phase 5: Transitions & Polish
- Panel fade/slide animations
- Button press feedback
- Visual polish

### Phase 6: Backend Integration
- Moonraker WebSocket client
- Real printer state updates
- File operations
- Live temperature/progress monitoring

## Build & Run

```bash
# Build
make

# Run
./build/bin/guppy-ui-proto

# Screenshot
./scripts/screenshot.sh
```

## Documentation

- **[README.md](README.md)** - Overview and quick start
- **[ROADMAP.md](docs/ROADMAP.md)** - Detailed development roadmap
- **[XML_UI_SYSTEM.md](docs/XML_UI_SYSTEM.md)** - Complete XML guide
- **[QUICK_REFERENCE.md](docs/QUICK_REFERENCE.md)** - Common patterns

## Key Metrics

- **Lines of XML:** ~550 (entire UI layout)
- **Lines of C++:** ~700 (initialization + reactive logic)
- **Lines of Test Code:** ~100 (unit tests)
- **Panels:** 5 (1 with content, 4 blank)
- **Subjects:** 10 (nav colors, active panel, home panel data)
- **FontAwesome Sizes:** 3 (64px nav, 48px status cards, 32px inline)
- **Test Coverage:** 5 test cases, 13 assertions (navigation system)
- **Build Time:** ~3 seconds (incremental), ~45 seconds (clean)
- **Test Time:** <1 second (unit tests)
- **Binary Size:** ~2.5MB (debug build)
- **Memory Usage:** ~15MB runtime (SDL backend)

## Architecture Highlights

**Separation of Concerns:**
- XML = UI structure, layout, styling
- C++ = Initialization, business logic, reactive updates
- Subjects = Data flow (one-way: C++ → XML)

**Zero Manual Widget Management:**
- No `lv_obj_t*` stored in application code
- No manual `lv_label_set_text()` calls
- Subject updates trigger all bound widgets automatically

**Display Driver Abstraction:**
- Never call display-specific APIs (SDL, DRM, etc.) directly
- LVGL handles all input/event processing internally
- Portable across all LVGL display backends

---

**Repository:** `prototype-ui9/` (LVGL 9 XML-based implementation)

**Status:** ✅ Navigation complete, home panel content reactive, testing framework operational, remaining panels pending
