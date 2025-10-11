# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the **LVGL 9 UI Prototype** for GuppyScreen - a declarative XML-based touch UI system using LVGL 9.3 with reactive Subject-Observer data binding. The prototype runs on SDL2 for rapid development and will eventually target framebuffer displays on embedded hardware.

**Key Innovation:** Complete separation of UI layout (XML) from business logic (C++), similar to modern web frameworks. No manual widget management - all updates happen through reactive subjects.

## Build Commands

```bash
# Incremental build (automatic parallel compilation)
make

# Clean rebuild
make clean && make

# Run the simulator
./build/bin/guppy-ui-proto

# Build and capture screenshot (2 seconds after launch)
./scripts/screenshot.sh

# Generate FontAwesome icon constants for globals.xml
python3 scripts/generate-icon-consts.py
```

**Binary Location:** `build/bin/guppy-ui-proto`

## Architecture Overview

### XML-Based Reactive UI System

The entire UI is defined in XML components with reactive data binding:

```
┌──────────────────────────────────────────────┐
│            main.cpp                          │
│  1. Register fonts/images/components         │
│  2. Initialize subjects (ui_nav_init, etc)   │
│  3. Create UI: lv_xml_create("app_layout")   │
│  4. Event loop: lv_timer_handler()           │
└──────────────────────────────────────────────┘
         │
         ↓
┌──────────────────────────────────────────────┐
│       XML Components (ui_xml/*.xml)          │
│  • app_layout.xml (root: navbar + content)   │
│  • navigation_bar.xml (5 button nav)         │
│  • home_panel.xml (printer status)           │
│  • globals.xml (theme constants + icons)     │
└──────────────────────────────────────────────┘
         │ bind_text="subject_name"
         ↓
┌──────────────────────────────────────────────┐
│         Subjects (Reactive Data)             │
│  • String subjects (status_text, temp_text)  │
│  • Integer subjects (active_panel: 0-4)      │
│  • Color subjects (icon colors)              │
└──────────────────────────────────────────────┘
         │ lv_subject_copy_string()
         ↓
┌──────────────────────────────────────────────┐
│      C++ Panel Wrappers (src/*.cpp)          │
│  • ui_panel_home.cpp - Home panel logic      │
│  • ui_nav.cpp - Navigation system            │
│  • main.cpp - Initialization & event loop    │
└──────────────────────────────────────────────┘
```

### Component Hierarchy

```
app_layout.xml (horizontal: navbar + content area)
├── navigation_bar.xml (vertical: 5 button navigation)
│   ├── Home button (fa_icons_64 house icon)
│   ├── Controls button (fa_icons_64 sliders)
│   ├── Filament button (custom PNG image)
│   ├── Settings button (fa_icons_64 gear)
│   └── Advanced button (fa_icons_64 ellipsis)
└── content_area (5 panels, visibility toggled)
    ├── home_panel.xml (printer image + status cards)
    ├── controls_panel.xml (blank placeholder)
    ├── filament_panel.xml (blank placeholder)
    ├── settings_panel.xml (blank placeholder)
    └── advanced_panel.xml (blank placeholder)
```

All components reference `globals.xml` for shared constants (`#primary_color`, `#nav_width`, etc).

## Critical Implementation Patterns

### 1. Subject Initialization Order

**MUST initialize subjects BEFORE creating XML components:**

```cpp
// CORRECT ORDER:
lv_xml_component_register_from_file("A:/path/to/globals.xml");
lv_xml_component_register_from_file("A:/path/to/home_panel.xml");

ui_nav_init();              // Initialize navigation subjects
ui_panel_home_init_subjects();  // Initialize home panel subjects

lv_xml_create(screen, "app_layout", NULL);  // NOW create UI
```

If subjects are created in XML before C++ initialization, they'll have empty/default values and block proper initialization.

### 3. FontAwesome Icon Constants

Icons are auto-generated in `globals.xml` to avoid UTF-8 encoding issues:

```bash
# Regenerate after adding icons to ui_fonts.h
python3 scripts/generate-icon-consts.py
```

The script reads icon definitions from `include/ui_fonts.h` and writes UTF-8 byte sequences to `globals.xml`. These appear invisible in editors but render correctly.

**Usage in XML:**
```xml
<lv_label text="#icon_home" style_text_font="fa_icons_64"/>
```

**Available icon fonts:** `fa_icons_64`, `fa_icons_48`, `fa_icons_32`

### 4. Reactive Navigation System

Navigation uses Subject-Observer pattern for automatic icon color updates:

```cpp
// C++ side (ui_nav.cpp)
void ui_nav_set_active(ui_panel_id_t panel_id) {
    lv_subject_set_int(&active_panel_subject, panel_id);
    // All 5 icon color subjects update automatically via observer callback
}

// XML side (navigation_bar.xml)
<lv_label text="#icon_home" bind_style_text_color="nav_icon_0_color"/>
```

When active panel changes, observer callback updates all 5 icon colors (active=red, inactive=white) and shows/hides panels.

### 5. Custom PNG Icons with Reactive Recoloring

Mix FontAwesome fonts with custom PNG icons in navigation:

**Requirements:**
- PNG must have **white pixels** on transparent background
- Set `style_img_recolor_opa="255"` in XML
- Register image: `lv_xml_register_image(NULL, "name", "A:/path.png")`

**Navigation system detects widget type** (label vs image) and applies appropriate reactive styling.

### 6. Widget Lookup by Name (ALWAYS USE THIS APPROACH)

**CRITICAL:** Always use LVGL 9's name-based widget lookup instead of index-based access.

**✓ CORRECT - Name-based lookup (resilient to layout changes):**
```cpp
// Add name attribute to XML widgets
<lv_label name="my_widget" text="Hello"/>

// Find widget by name in C++
lv_obj_t* widget = lv_obj_find_by_name(parent, "my_widget");
```

**✗ WRONG - Index-based lookup (fragile, breaks when layout changes):**
```cpp
lv_obj_t* widget = lv_obj_get_child(parent, 3);  // DON'T DO THIS
```

**Two functions available:**
- `lv_obj_find_by_name(parent, "name")` - Recursively searches entire tree (deep search)
- `lv_obj_get_child_by_name(parent, "path/to/widget")` - Uses path like "container/button/label"

**Benefits:**
- Layout changes (reordering, adding/removing widgets) won't break code
- Self-documenting - widget name shows intent
- Works seamlessly with XML `name` attributes

**Pattern:**
```cpp
// In XML: Add name attributes to widgets you need to access
<lv_obj>
    <lv_label name="temperature_display" bind_text="temp_text"/>
    <lv_label name="status_message" bind_text="status_text"/>
</lv_obj>

// In C++: Find by name instead of index
lv_obj_t* temp_label = lv_obj_find_by_name(panel, "temperature_display");
lv_obj_t* status_label = lv_obj_find_by_name(panel, "status_message");
```

## File Organization

```
prototype-ui9/
├── src/
│   ├── main.cpp                # Entry point, initialization, event loop
│   ├── ui_nav.cpp             # Navigation system (5 panel switching)
│   └── ui_panel_home.cpp      # Home panel logic (XML-based)
├── include/
│   ├── ui_nav.h               # Navigation API
│   ├── ui_panel_home.h        # Home panel API
│   ├── ui_fonts.h             # FontAwesome icon constants
│   └── ui_theme.h             # Theme color macros
├── ui_xml/
│   ├── globals.xml            # Theme constants + auto-generated icons
│   ├── app_layout.xml         # Root: navbar + content area
│   ├── navigation_bar.xml     # 5-button vertical navigation
│   ├── home_panel.xml         # Printer status with cards
│   └── {controls,filament,settings,advanced}_panel.xml  # Placeholders
├── assets/
│   ├── fonts/                 # Custom fonts (FontAwesome 6)
│   │   ├── fa_icons_64.c     # Generated by lv_font_conv
│   │   ├── fa_icons_48.c
│   │   └── fa_icons_32.c
│   └── images/                # UI images
│       ├── printer_400.png
│       └── filament_spool.png
├── scripts/
│   ├── generate-icon-consts.py  # Auto-generate icon constants
│   └── screenshot.sh            # Build + run + screenshot automation
├── docs/
│   ├── XML_UI_SYSTEM.md         # Complete XML system guide
│   ├── QUICK_REFERENCE.md       # Common patterns
│   └── ROADMAP.md               # Development plan
├── Makefile                     # Build system (auto-parallel)
├── lv_conf.h                    # LVGL configuration
└── STATUS.md                    # Development journal
```

## Key Configuration (lv_conf.h)

```c
#define LV_USE_XML 1                         // Enable XML UI system
#define LV_USE_SNAPSHOT 1                    // Enable screenshots
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1   // Required by XML parser
#define LV_USE_OBSERVER 1                    // Enable Subject-Observer
#define LV_FONT_MONTSERRAT_16/20/28 1        // Text fonts
#define LV_USE_SDL 1                         // SDL2 display driver
#define LV_COLOR_DEPTH 16                    // RGB565
#define LV_MEM_SIZE (2 * 1024 * 1024U)       // 2MB for images
```

## Panel Development Pattern

When adding a new panel:

1. **Create XML layout** in `ui_xml/panel_name.xml`
2. **Define C++ wrapper** in `include/ui_panel_name.h` and `src/ui_panel_name.cpp`
3. **Initialize subjects** before creating XML:
   ```cpp
   void ui_panel_name_init_subjects() {
       lv_subject_init_string(&subject, buffer, NULL, size, "default");
       lv_xml_register_subject(NULL, "subject_name", &subject);
   }
   ```
4. **Register component** in `main.cpp` (after globals.xml)
5. **Add to navigation** in `ui_nav.cpp` (update UI_PANEL_COUNT enum)

## Screenshot Handling

**Best Practice:** Use the screenshot.sh utility for automated capturing:

```bash
# Build, run, wait 2 seconds, and capture screenshot automatically
./scripts/screenshot.sh

# With custom binary and output name
./scripts/screenshot.sh guppy-ui-proto my-screenshot
```

**Manual Screenshots:** Press 'S' while the UI is running to save a screenshot with timestamp to `/tmp/ui-screenshot-{timestamp}.bmp`.

**IMPORTANT for File Reading:** BMP screenshots are ~3MB and exceed file read limits. Always convert before reading:

```bash
# Find latest screenshot
LATEST_BMP=$(ls -t /tmp/ui-screenshot-*.bmp | head -1)

# Convert to PNG
magick "$LATEST_BMP" "${LATEST_BMP%.bmp}.png"

# Now safe to read
```

**Never try to Read a .bmp file directly!**

## Development Workflow

```bash
# 1. Make XML changes (no recompilation needed for minor tweaks)
vim ui_xml/home_panel.xml

# 2. Rebuild C++ if logic changed
make

# 3. Run and test
./build/bin/guppy-ui-proto

# 4. Take screenshot (Press 'S' in UI or wait 2 seconds after launch)
# Files appear in /tmp/ui-screenshot-{timestamp}.bmp

# 5. Convert for viewing
magick /tmp/ui-screenshot-*.bmp /tmp/screenshot.png
```

## Common Gotchas

1. **Subject registration conflict** - If `globals.xml` declares subjects, they're registered with empty values before C++ initialization. Solution: Remove `<subjects>` from globals.xml, let C++ handle all subject registration.

2. **Icon constants not rendering** - Run `python3 scripts/generate-icon-consts.py` to regenerate UTF-8 byte sequences in globals.xml.

## Testing & Debugging

```cpp
// Enable verbose LVGL logging (in main.cpp)
lv_log_register_print_cb([](lv_log_level_t level, const char * buf) {
    printf("[LVGL %d] %s\n", level, buf);
});

// Subject state inspection
printf("Active panel: %d\n", lv_subject_get_int(&active_panel_subject));
```

## Related Documentation

- **[STATUS.md](STATUS.md)** - Development journal with daily updates
- **[README.md](README.md)** - Project overview and quick start
- **[XML_UI_SYSTEM.md](docs/XML_UI_SYSTEM.md)** - Complete XML guide with examples
- **[QUICK_REFERENCE.md](docs/QUICK_REFERENCE.md)** - Common patterns and solutions
- **[ROADMAP.md](docs/ROADMAP.md)** - Planned features and milestones

## Future Integration

This prototype will eventually integrate with the main GuppyScreen project:
- Replace SDL2 with framebuffer driver for embedded targets
- Connect to Moonraker WebSocket API for real printer data
- Add remaining panel content (Controls, Filament, Settings, Advanced)
- Implement animations and transitions
- Add multi-language support
