# Session Handoff Document

**Last Updated:** 2025-10-15
**Current Focus:** Interactive button wiring and Moonraker integration prep

---

## Current State

**Completed Phases:**
- ✅ Phase 5.5: Extrusion Panel with safety checks
- ✅ Phase 5.4: Temperature Sub-Screens (Nozzle + Bed)
- ✅ Phase 5.3: Motion Panel with 8-direction jog pad
- ✅ Phase 5.2: Numeric Keypad modal component
- ✅ Phase 5.1: Controls Panel launcher
- ✅ Phase 4: Print Select Panel (dual views, sorting, responsive cards)

**What Works:**
- Navigation between all 6 main panels
- Home panel with temperature/network/light displays
- Controls launcher → sub-screens (motion, temps, extrusion)
- Print Select panel with card/list views, sorting, file operations
- Responsive design across tiny/small/medium/large screens
- Material Design icon system with dynamic recoloring
- Reactive Subject-Observer data binding

**What Needs Wiring:**
- Temperature panel preset buttons → update target temps
- Custom temp button → open numeric keypad
- Confirm/back buttons on all sub-screens
- Extrusion/retract buttons
- Motion jog buttons → position updates
- All interactive flows need end-to-end testing

See **STATUS.md** for complete chronological development history.

---

## Critical Architecture Patterns

### 1. Name-Based Widget Lookup (CRITICAL)

**Always use names, never indices:**

```cpp
// ✓ CORRECT - Resilient to layout changes
lv_obj_t* widget = lv_obj_find_by_name(parent, "widget_name");

// ✗ WRONG - Breaks when XML changes
lv_obj_t* widget = lv_obj_get_child(parent, 3);
```

### 2. Component Instantiation Names (CRITICAL)

**Always add explicit `name` attributes when instantiating components:**

```xml
<!-- app_layout.xml -->
<lv_obj name="content_area">
  <controls_panel name="controls_panel"/>  <!-- Explicit name required -->
  <home_panel name="home_panel"/>
</lv_obj>
```

**Why:** Component names in `<view name="...">` definitions do NOT propagate to `<component_tag/>` instantiations. Without explicit names, `lv_obj_find_by_name()` returns NULL.

### 3. Subject Initialization Order (CRITICAL)

**Subjects MUST be initialized BEFORE XML creation:**

```cpp
// 1. Register XML components
lv_xml_component_register_from_file("A:/ui_xml/globals.xml");
lv_xml_component_register_from_file("A:/ui_xml/home_panel.xml");

// 2. Initialize subjects FIRST
ui_nav_init();
ui_panel_home_init_subjects();

// 3. NOW create UI
lv_obj_t* screen = lv_xml_create(NULL, "app_layout", NULL);
```

If subjects are created in XML before C++ initialization, they'll have empty/default values.

### 4. LVGL XML Event Callbacks (CRITICAL)

**Use `<lv_event-call_function>`, NOT `<event_cb>`:**

```xml
<!-- CORRECT (matches LVGL source code) -->
<lv_button name="my_button">
    <lv_event-call_function trigger="clicked" callback="my_handler"/>
</lv_button>

<!-- WRONG (online docs show this, but it doesn't exist) -->
<lv_button name="my_button">
    <event_cb trigger="clicked" callback="my_handler"/>
</lv_button>
```

**Registration in C++ (BEFORE XML loads):**
```cpp
lv_xml_register_event_cb(NULL, "my_handler", my_handler_function);
```

**Source:** `lvgl/src/others/xml/lv_xml.c:113` - LVGL registers `"lv_event-call_function"`, not `"event_cb"`. Online docs are incorrect.

### 5. LVGL 9 XML Attribute Names (CRITICAL)

**Two systematic bugs to avoid:**

1. **No abbreviations - Use full words:**
   ```xml
   <!-- ✗ WRONG - Parser silently ignores -->
   <lv_image style_img_recolor="#primary_color" style_img_recolor_opa="255"/>

   <!-- ✓ CORRECT - Full word "image" -->
   <lv_image style_image_recolor="#primary_color" style_image_recolor_opa="255"/>
   ```

2. **No `zoom` attribute - Use `scale_x`/`scale_y`:**
   ```xml
   <!-- ✗ WRONG - zoom doesn't exist in LVGL 9 -->
   <lv_image src="mat_clock" zoom="180"/>

   <!-- ✓ CORRECT - scale where 256 = 100% -->
   <lv_image src="mat_clock" scale_x="72" scale_y="72"/>
   ```

**When attributes don't work:** Check the parser source code in `/lvgl/src/others/xml/parsers/`, not just online docs.

### 6. Margin vs Padding for Dividers

**Critical distinction for thin elements:**

```xml
<!-- ✗ WRONG - Padding adds to height, causes overflow -->
<lv_obj width="1" height="100%" style_pad_ver="12"/>  <!-- 100% + 24px = overflow -->

<!-- ✓ CORRECT - Margin pushes inward, no overflow -->
<lv_obj width="1" height="100%" style_margin_ver="12"/>  <!-- Creates inset -->
```

**Pattern:** Use **margin for insets**, **padding for internal spacing**.

### 7. Responsive Design Philosophy

**Use responsive percentages for layout, fixed pixels for interactions:**

```xml
<!-- ✓ Responsive layouts -->
<percent name="overlay_panel_width" value="68%"/>
<percent name="file_card_width" value="22%"/>

<!-- ✓ Fixed interactive elements -->
<px name="button_height_normal" value="56"/>  <!-- Minimum touch target -->
<px name="padding_normal" value="20"/>        <!-- Visual consistency -->
```

**Benefits:** Single codebase supports tiny/small/medium/large screens without duplication.

---

## Material Design Icon System

**Icon Format:** RGB565A8 (16-bit color + 8-bit alpha) LVGL 9 C arrays

**Conversion Workflow:**
```bash
# Automated: SVG → PNG (Inkscape) → LVGL 9 C array
./scripts/convert-material-icons-lvgl9.sh
```

**CRITICAL:** Use **Inkscape** for SVG→PNG conversion. ImageMagick loses alpha channel, causing icons to render as solid squares.

**Dynamic Recoloring in XML:**
```xml
<lv_image src="mat_heater"
          style_image_recolor="#primary_color"
          style_image_recolor_opa="255"/>
```

**Dynamic Recoloring in C++:**
```cpp
lv_obj_set_style_img_recolor(icon, UI_COLOR_PRIMARY, LV_PART_MAIN);
lv_obj_set_style_img_recolor_opa(icon, 255, LV_PART_MAIN);
```

**Scaling:**
- Material icons are 64×64px at scale 256 (100%)
- For 14px icons: `scale_x="56" scale_y="56"` (14/64 × 256 = 56)
- For 32px icons: `scale_x="128" scale_y="128"`

---

## Component Registration & Initialization

**Order matters in main.cpp:**

```cpp
int main() {
    // 1. Register custom widgets FIRST
    material_icons_register();
    ui_icon_register_widget();

    // 2. Register XML components
    lv_xml_component_register_from_file("A:/ui_xml/globals.xml");
    lv_xml_component_register_from_file("A:/ui_xml/navigation_bar.xml");
    lv_xml_component_register_from_file("A:/ui_xml/home_panel.xml");
    // ... all other panels

    // 3. Initialize subjects BEFORE creating UI
    ui_nav_init();
    ui_panel_home_init_subjects();
    ui_panel_controls_temp_init_subjects();
    // ... all panel subjects

    // 4. Create UI
    lv_obj_t* screen = lv_xml_create(NULL, "app_layout", NULL);

    // 5. Setup observers and wire events
    ui_panel_home_setup_observers(panels[UI_PANEL_HOME]);
    ui_panel_controls_wire_events(panels[UI_PANEL_CONTROLS]);

    // 6. Start event loop
    while (lv_display_get_next(NULL)) {
        lv_timer_handler();
        SDL_Delay(5);
    }
}
```

**Component Naming:** LVGL uses **filename** as component name:
- File: `nozzle_temp_panel.xml` → Component: `nozzle_temp_panel`
- File: `controls_panel.xml` → Component: `controls_panel`

---

## Testing Commands

```bash
# Build
make                          # Incremental build (auto-parallel)
make clean && make            # Clean rebuild

# Run with different configs
./build/bin/guppy-ui-proto                    # Default (medium screen, home panel)
./build/bin/guppy-ui-proto -s tiny            # 480x320 screen
./build/bin/guppy-ui-proto -s large           # 1280x720 screen
./build/bin/guppy-ui-proto -p controls        # Start at Controls panel
./build/bin/guppy-ui-proto -s small -p print-select  # Combined options

# Screenshot
./scripts/screenshot.sh guppy-ui-proto output-name [panel-name]

# Examples:
./scripts/screenshot.sh guppy-ui-proto home-test home
./scripts/screenshot.sh guppy-ui-proto controls-launcher controls
./scripts/screenshot.sh guppy-ui-proto motion-panel motion

# Unit tests
make test                     # Run all tests
```

**Screen Size Mappings:**
- `tiny` = 480x320
- `small` = 800x480
- `medium` = 1024x600 (default)
- `large` = 1280x720

---

## Next Priorities

### Priority 1: Interactive Button Wiring

**Temperature Panels:**
- Wire preset buttons (PLA/PETG/ABS/Off) → update target temperature subjects
- Wire Custom button → open numeric keypad with callback
- Wire Confirm button → apply temperature and close panel
- Wire back button → hide panel, show Controls launcher

**Motion Panel:**
- Wire jog pad buttons → update position subjects
- Wire distance selector → change jog increment
- Wire home buttons → mock homing operations
- Wire back button

**Extrusion Panel:**
- Wire amount selector → set extrusion distance
- Wire extrude/retract buttons → check temp, simulate extrusion
- Wire back button

**Test End-to-End Flows:**
1. Controls → Nozzle Temp → PLA preset → Custom (keypad) → Confirm → Back
2. Controls → Motion → jog XY → change distance → jog Z → Home All → Back
3. Controls → Extrusion → select 25mm → verify temp check → Extrude → Back

### Priority 2: Moonraker Integration Prep

**Dynamic Temperature Limits:**
APIs already implemented, ready to wire to Moonraker config:

```cpp
// Query printer heater config on startup:
// GET /printer/objects/query?heater_bed&extruder

// Then configure UI with actual printer limits:
ui_panel_controls_temp_set_nozzle_limits(extruder.min_temp, extruder.max_temp);
ui_panel_controls_temp_set_bed_limits(heater_bed.min_temp, heater_bed.max_temp);
ui_panel_controls_extrusion_set_limits(extruder.min_temp, extruder.max_temp);
```

**Default Safe Limits:**
- Nozzle: 0-500°C (covers all hotends)
- Bed: 0-150°C (covers all heatbeds)
- Extrusion min: 170°C (safety threshold)

### Priority 3: Future Enhancements

- Temperature graph visualization (replace static fire icon)
- Print status panel real-time updates
- File browser pagination/search
- Multi-language support
- Animations and transitions

---

## Known Gotchas

### LV_SIZE_CONTENT Width Bug

**Problem:** Labels inside XML components with property substitution render with zero width despite having content.

**Solution:** Use explicit pixel dimensions instead of `width="LV_SIZE_CONTENT"`:

```xml
<!-- ✗ WRONG - Renders with zero width -->
<lv_label width="LV_SIZE_CONTENT" bind_text="time_text"/>

<!-- ✓ CORRECT - Use explicit dimensions -->
<lv_label width="65" bind_text="time_text"/>  <!-- Fits "2h30m" -->
```

See `docs/LVGL9_XML_GUIDE.md` for complete troubleshooting.

### Subject Type Must Match API

**Image recoloring requires color subjects:**
```cpp
// ✓ CORRECT - Color subject + image recolor API
lv_subject_init_color(&subject, lv_color_hex(0xFFD700));
lv_obj_set_style_img_recolor(widget, color, LV_PART_MAIN);
lv_obj_set_style_img_recolor_opa(widget, 255, LV_PART_MAIN);

// ✗ WRONG - String subject + text color (doesn't work on images)
lv_subject_init_string(&subject, buffer, NULL, size, "0xFFD700");
lv_obj_set_style_text_color(widget, color, 0);
```

### Component Instantiation Requires Explicit Names

Always add `name` attributes to component tags in XML or `lv_obj_find_by_name()` will return NULL.

### LVGL 9 XML Silent Failures

Parser ignores unknown attributes without warnings. When attributes don't work, verify against source code in `/lvgl/src/others/xml/parsers/`.

---

## Documentation

- **[STATUS.md](STATUS.md)** - Complete chronological development journal
- **[ROADMAP.md](docs/ROADMAP.md)** - Planned features and milestones
- **[LVGL9_XML_GUIDE.md](docs/LVGL9_XML_GUIDE.md)** - Complete LVGL 9 XML reference
- **[QUICK_REFERENCE.md](docs/QUICK_REFERENCE.md)** - Common patterns quick lookup
- **[COPYRIGHT_HEADERS.md](docs/COPYRIGHT_HEADERS.md)** - Required GPL v3 headers for new files

---

## File Organization

```
prototype-ui9/
├── src/              # C++ business logic
├── include/          # Headers
├── ui_xml/           # XML component definitions
├── assets/           # Fonts, images, Material Design icons
├── scripts/          # Build/screenshot/icon conversion automation
├── docs/             # Documentation
└── Makefile          # Build system
```

**Key Files:**
- `src/main.cpp` - Entry point, initialization order, CLI args
- `ui_xml/globals.xml` - Theme constants, color definitions, icon variants
- `ui_xml/app_layout.xml` - Root layout (navbar + content area)
- `lv_conf.h` - LVGL configuration (has `LV_USE_OBJ_NAME=1`)

---

**End of Handoff Document**

For complete development history, see **STATUS.md**.
For implementation patterns and troubleshooting, see **docs/LVGL9_XML_GUIDE.md**.
