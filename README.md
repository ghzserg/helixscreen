# GuppyScreen LVGL 9 UI Prototype

Modern, declarative XML-based UI for GuppyScreen using LVGL 9 with reactive data binding.

## Features

- **LVGL 9.3.0** - Latest version with XML support, flex layouts, and enhanced rendering
- **Declarative XML UI** - Complete UI defined in XML files, separate from application logic
- **Reactive Data Binding** - Subject-Observer pattern for automatic UI updates
- **Clickable Navigation** - Icon-based navigation with reactive color highlighting
- **Hybrid Icons** - Mix FontAwesome fonts and custom PNG images with unified reactive recoloring
- **SDL2 Display** - Cross-platform simulator for rapid development
- **FontAwesome 6** - Icon font integration with auto-generated constants
- **Custom SVG Icons** - Convert SVG to PNG with automatic reactive color support
- **Theme System** - Global constants in XML for easy theme customization
- **Platform-Independent Screenshots** - LVGL native snapshot API
- **Hot-Reload Friendly** - XML changes without recompilation

## Project Structure

```
prototype-ui9/
├── src/
│   ├── main.cpp                 # Application entry point with subject init
│   ├── ui_panel_home_xml.cpp    # Home panel C++ wrapper with reactive binding
│   └── ui_nav.cpp               # Legacy C++ navbar (for reference)
├── include/
│   ├── ui_panel_home_xml.h      # Home panel API with subjects
│   ├── ui_fonts.h               # FontAwesome icon declarations
│   └── ui_theme.h               # Theme color definitions
├── ui_xml/
│   ├── globals.xml              # Global constants (colors, sizes, auto-generated icons)
│   ├── app_layout.xml           # Root layout: navbar + content
│   ├── navigation_bar.xml       # Vertical navigation with proper flex layout
│   └── home_panel.xml           # Home screen with reactive data bindings
├── assets/
│   ├── fonts/                   # Custom fonts (FontAwesome 6)
│   └── images/                  # UI images
├── docs/
│   ├── XML_UI_SYSTEM.md         # Complete XML UI system guide
│   └── QUICK_REFERENCE.md       # Quick reference for common patterns
└── scripts/
    ├── generate-icon-consts.py  # Auto-generate FontAwesome icon constants
    └── screenshot.sh            # Build, run, and capture screenshots
```

## Building

```bash
# Build the prototype
make

# Clean rebuild
make clean && make

# Run
./build/bin/guppy-ui-proto

# Quick build + screenshot
./scripts/screenshot.sh [binary_name] [output_name]

# Controls
# Click navigation icons to switch panels
# Press S: Save screenshot
# Close window to exit
```

## XML UI System

The entire UI is defined declaratively in XML files, separate from application logic.

### Component Hierarchy

```
app_layout.xml          # Root: horizontal container with nav + content
├── navigation_bar.xml  # Left: vertical icon navigation (5 buttons)
└── home_panel.xml      # Right: printer status with image + info cards
```

All components reference `globals.xml` for shared theme constants.

### Global Constants

Define shared theme values in `ui_xml/globals.xml`:

```xml
<component>
    <consts>
        <!-- Colors -->
        <color name="bg_dark" value="0x1a1a1a"/>
        <color name="primary_color" value="0xff4444"/>

        <!-- Dimensions -->
        <px name="nav_width" value="102"/>
        <px name="card_radius" value="8"/>
    </consts>
    <view extends="lv_obj"/>
</component>
```

Reference constants with `#name` syntax: `style_bg_color="#bg_dark"`

**Key Benefit:** Change theme colors in one file, affects entire UI instantly.

### Component Registration & Usage

Components must be registered before use (order matters - globals first):

```cpp
// 1. Register fonts and images globally
lv_xml_register_font(NULL, "fa_icons_64", &fa_icons_64);
lv_xml_register_image(NULL, "printer_img", "A:/path/to/image.png");

// 2. Register XML components (globals first!)
lv_xml_component_register_from_file("A:/path/to/globals.xml");
lv_xml_component_register_from_file("A:/path/to/navigation_bar.xml");
lv_xml_component_register_from_file("A:/path/to/home_panel.xml");
lv_xml_component_register_from_file("A:/path/to/app_layout.xml");

// 3. Create entire UI (one line!)
lv_xml_create(screen, "app_layout", NULL);
```

The C++ code is now **pure initialization and reactive updates** - zero layout or styling logic!

## Reactive Data Binding

LVGL 9's Subject-Observer pattern enables automatic UI updates without manual widget references:

```cpp
// 1. Initialize subjects before XML creation
ui_panel_home_xml_init_subjects();

// 2. Create UI from XML (automatically binds to registered subjects)
lv_xml_create(screen, "app_layout", NULL);

// 3. Update from anywhere (all bound widgets update automatically)
ui_panel_home_xml_update("Printing...", 210);
```

XML bindings:
```xml
<!-- Labels automatically update when subjects change -->
<lv_label bind_text="status_text" style_text_color="#text_primary"/>
<lv_label bind_text="temp_text" style_text_font="montserrat_28"/>
```

**Key Benefits:**
- No manual widget searching/storing
- Type-safe updates through C++ API
- One update triggers all bound widgets
- Clean separation of UI and logic

### Font and Image Registration

Fonts and images must be registered globally before loading XML:

```cpp
lv_xml_register_font(NULL, "fa_icons_64", &fa_icons_64);
lv_xml_register_font(NULL, "montserrat_16", &lv_font_montserrat_16);
lv_xml_register_image(NULL, "image_name", "A:/path/to/image.png");
```

## FontAwesome Icons

Icons are auto-generated in `globals.xml` to avoid UTF-8 editing issues:

```bash
# Regenerate icon constants from ui_fonts.h definitions
python3 scripts/generate-icon-consts.py
```

Icon definitions (`include/ui_fonts.h`):
- `ICON_HOME` (0xF015) - House
- `ICON_CONTROLS` (0xF1DE) - Sliders
- `ICON_FILAMENT` (0xF576) - Fill drip
- `ICON_SETTINGS` (0xF013) - Gear
- `ICON_ADVANCED` (0xF142) - Ellipsis vertical

Reference in XML:
```xml
<lv_label text="#icon_home" style_text_font="fa_icons_64"/>
```

## Custom PNG Icons with Reactive Recoloring

You can mix custom PNG icons with FontAwesome fonts in the navigation bar. The system automatically detects widget type and applies appropriate reactive styling.

**Convert SVG to PNG:**
```bash
magick -background none -channel RGB -negate source.svg -resize 70x70 output.png
```

**Register in main.cpp:**
```cpp
lv_xml_register_image(NULL, "filament_spool", "A:/path/to/filament_spool.png");
```

**Use in XML:**
```xml
<lv_button>
    <lv_image src="filament_spool" align="center" style_img_recolor_opa="255"/>
</lv_button>
```

**Key Requirements:**
- PNG must have **white pixels** on transparent background for recoloring to work
- Set `style_img_recolor_opa="255"` in XML to enable recoloring
- Navigation system uses `lv_obj_set_style_img_recolor()` for reactive color changes

## Screenshots

**Interactive Mode:**
- Press 'S' while running to save a screenshot with unique timestamp
- Files saved to `/tmp/ui-screenshot-{timestamp}.bmp`

**Automated Mode:**
```bash
./scripts/screenshot.sh                    # Default: guppy-ui-proto
./scripts/screenshot.sh guppy-ui-proto ui5 # Named: ui-screenshot-ui5.png
```

**Implementation:**
- Uses LVGL's native `lv_snapshot_take()` API (platform-independent)
- Works on any display backend (SDL, framebuffer, DRM, etc.)
- Custom BMP encoder (~40 lines) for simplicity

## Configuration

Key LVGL settings in `lv_conf.h`:
- `LV_USE_XML 1` - Enable XML UI support
- `LV_USE_SNAPSHOT 1` - Enable screenshot API
- `LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1` - Required by XML parser
- `LV_FONT_MONTSERRAT_16/20/28 1` - Enable text fonts

## Event Loop & SDL Integration

**CRITICAL**: LVGL's SDL driver handles all event polling internally. Do NOT call `SDL_PollEvent()` manually:

```cpp
// ✓ CORRECT - Let LVGL handle everything
while (lv_display_get_next(NULL)) {
    lv_timer_handler();  // Internally polls SDL events
    SDL_Delay(5);
}

// ✗ WRONG - Breaks click events and violates display driver abstraction
while (running) {
    SDL_Event event;
    SDL_PollEvent(&event);  // Drains event queue before LVGL sees it!
    lv_timer_handler();
}
```

**Why This Matters:**
- LVGL creates an internal timer (every 5ms) that calls `SDL_PollEvent()`
- Manual polling consumes events before LVGL's timer runs
- Result: Mouse clicks never reach LVGL's input system
- This applies to ALL display drivers (SDL, DRM, framebuffer, etc.)

**Required Setup:**
```cpp
lv_display_t* display = lv_sdl_window_create(1024, 800);
lv_indev_t* mouse = lv_sdl_mouse_create();  // REQUIRED for clicks!
```

The `lv_sdl_mouse_create()` call is essential - it registers the input device that converts SDL mouse events to LVGL input events.

## Documentation

- **[XML UI System Guide](docs/XML_UI_SYSTEM.md)** - Complete guide with examples
- **[Quick Reference](docs/QUICK_REFERENCE.md)** - Common patterns and gotchas

## Notes

- XML support is experimental in LVGL 9.3.0
- Uses POSIX filesystem with 'A:' drive letter for file paths
- All XML files must be UTF-8 encoded
- Flex layouts: use `style_flex_main_place`/`style_flex_cross_place` (not `flex_align`)
- Subject initialization must happen before XML creation
- **Never call `SDL_PollEvent()` manually** - violates display driver abstraction
- Navigation uses C++ event handlers (`ui_nav.cpp`) with reactive Subject-Observer pattern

## Next Steps

- [x] Implement clickable navigation between panels
- [ ] Add content to remaining panels (Controls, Filament, Settings, Advanced)
- [ ] Add animations and transitions
- [ ] Integrate with Klipper/Moonraker backend
- [ ] Theme variants (light mode)
- [ ] Integer subjects for numeric displays (progress bars, sliders)
