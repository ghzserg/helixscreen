# Layout System

> Alternative XML layouts for different screen aspect ratios and orientations.

## Overview

HelixScreen's UI is built from XML files that define the layout and structure of each panel.
The default XML files are designed for standard landscape displays (4:3 to 16:9, like 800x480
or 1024x600). But users with ultrawide screens (1920x480), portrait-mounted displays, or very
small screens need fundamentally different arrangements — not just scaling, but different
structures.

The **Layout System** lets you create alternative XML files for these different screen shapes.
You only need to override the files that need to change — everything else automatically falls
back to the standard version.

**Key distinction:** Themes control *colors*. Layouts control *structure*. They're independent —
any theme works with any layout.

## Current Status

> **This is early days.** The layout infrastructure is complete and working, but most layouts
> still need to be created. This is a great area for community contributions — you only need
> to know XML, not C++.

| Layout | Status | What Exists |
|--------|--------|-------------|
| `standard` | **Complete** | All panels — this is the default UI everyone uses today |
| `ultrawide` | **Started** | `home_panel.xml` only (initial draft, needs refinement) |
| `portrait` | Not started | Directory doesn't exist yet |
| `tiny` | Not started | Directory doesn't exist yet |
| `tiny-portrait` | Not started | Directory doesn't exist yet |

## How It Works

When HelixScreen starts up, it detects your screen's aspect ratio and picks a layout:

| Layout | When It's Chosen | Example Screens |
|--------|-----------------|-----------------|
| `standard` | Normal landscape (4:3 to 16:9) | 800x480, 1024x600, 1280x720 |
| `ultrawide` | Very wide (ratio > 2.5:1) | 1920x480, 1920x400 |
| `portrait` | Tall/narrow (ratio < 0.8:1) | 480x800, 600x1024 |
| `tiny` | Small landscape (max dimension ≤ 480) | 480x320, 320x240 |
| `tiny-portrait` | Small portrait (max dimension ≤ 480) | 320x480, 240x320 |

You can also force a layout manually:
- **CLI flag:** `--layout ultrawide`
- **Config file:** Set `display.layout` to `"ultrawide"` in `helixconfig.json`

When a layout is active, HelixScreen checks for an override file in the layout's subdirectory.
If it finds one, it uses it. If not, it falls back to the standard file. This means you can
override one panel at a time — you don't have to recreate everything from scratch.

## Directory Structure

```
ui_xml/
  globals.xml              ← Shared by ALL layouts (design tokens, never override this)
  app_layout.xml           ← Standard app chrome (navbar + content area)
  home_panel.xml           ← Standard home panel
  controls_panel.xml       ← Standard controls panel
  settings_panel.xml       ← ...and ~50 more XML files
  ...

  ultrawide/               ← Ultrawide overrides (only files that differ)
    home_panel.xml         ← Ultrawide home panel (exists, needs work)

  portrait/                ← Portrait overrides (doesn't exist yet — create it!)
  tiny/                    ← Tiny landscape overrides (doesn't exist yet)
  tiny-portrait/           ← Tiny portrait overrides (doesn't exist yet)
```

**The rule is simple:** to override `controls_panel.xml` for ultrawide screens, create
`ui_xml/ultrawide/controls_panel.xml`. That's it. HelixScreen will automatically pick it up.

---

## Contributing a Layout

This section is for anyone who wants to help create or improve layouts. You don't need to
know C++ — all layout work is pure XML.

### What You Need

1. A clone of the HelixScreen repo
2. A working build (see the project README)
3. Familiarity with the XML layout system (see `docs/LVGL9_XML_GUIDE.md`)

### Step-by-Step: Creating a New Layout Override

Let's say you want to create an ultrawide version of the controls panel.

**1. Find the standard file to use as a starting point**

```bash
# Look at the standard version
cat ui_xml/controls_panel.xml
```

**2. Create your override file**

```bash
# Copy the standard file into the layout directory
cp ui_xml/controls_panel.xml ui_xml/ultrawide/controls_panel.xml
```

**3. Edit the override to rearrange the layout**

Open `ui_xml/ultrawide/controls_panel.xml` and restructure it for the target screen shape.
The key rules are listed below in [Layout Override Rules](#layout-override-rules).

**4. Test it**

```bash
# Build (only needed if you changed C++ — XML changes don't need a rebuild!)
make -j

# Run with ultrawide layout on a 1920x480 window
./build/bin/helix-screen --test -vv --layout ultrawide -s 1920x480

# Take a screenshot for comparison
HELIX_SCREENSHOT_DISPLAY=0 ./scripts/screenshot.sh helix-screen ultrawide-controls controls --test --layout ultrawide -s 1920x480
```

**5. Compare with standard**

```bash
# Screenshot the standard layout too
HELIX_SCREENSHOT_DISPLAY=0 ./scripts/screenshot.sh helix-screen standard-controls controls --test
```

Screenshots are saved to `/tmp/ui-screenshot-<name>.png`.

### Step-by-Step: Starting a Brand New Layout Family

To start a layout that doesn't exist yet (e.g., `portrait`):

```bash
# Create the directory
mkdir -p ui_xml/portrait

# Start with the most impactful panel — usually home_panel
cp ui_xml/home_panel.xml ui_xml/portrait/home_panel.xml

# Edit it for portrait orientation
# Then test:
./build/bin/helix-screen --test -vv --layout portrait -s 480x800
```

You can override as many or as few files as you want. Any panel you *don't* override will
use the standard version automatically.

### Layout Override Rules

These rules **must** be followed for a layout override to work correctly. The C++ code is
shared between all layouts — only the XML structure changes.

**1. Keep all named widgets that C++ looks up**

The C++ code for each panel uses `lv_obj_find_by_name()` to find specific widgets. If your
layout is missing a named widget, the panel will break.

For `home_panel.xml`, these widget names are required:

| Widget Name | What It Does |
|-------------|-------------|
| `printer_image` | Displays the printer photo (set dynamically by C++) |
| `status_text_label` | Tip text (C++ animates fade transitions on this) |
| `print_card_thumb` | Benchy thumbnail in idle state |
| `print_card_active_thumb` | Print thumbnail during active print |
| `print_card_label` | "Print Files" text / progress text |
| `temp_icon` | Heater icon (C++ controls color animation while heating) |
| `light_icon` | Light bulb icon (C++ sets color/brightness dynamically) |
| `home_widget_area` | Plugin injection point (plugins add widgets here) |

**How to find required names for other panels:** search the corresponding `.cpp` file for
`lv_obj_find_by_name`. Any name referenced there must exist in your XML.

**2. Keep all subject bindings**

Subject bindings (`bind_text`, `bind_value`, `bind_flag_if_eq`, `bind_style`, etc.) connect
the XML to live data from the C++ code. Your layout must bind to the same subjects as the
standard layout. You can rearrange *where* the bound widgets appear, but the bindings
themselves must stay.

For example, the temperature display must still bind to `extruder_temp` and `extruder_target`:
```xml
<temp_display bind_current="extruder_temp" bind_target="extruder_target" .../>
```

**3. Keep all event callbacks**

Event callbacks (`<event_cb trigger="clicked" callback="..."/>`) connect buttons to C++ code.
Every callback in the standard file must appear in your override, attached to the appropriate
widget.

**4. Don't modify `globals.xml`**

Design tokens (colors, spacing, typography) are defined in `globals.xml` and shared across
all layouts. Your layout XML should use these tokens — not hardcoded values:

```xml
<!-- Good: uses design tokens -->
<lv_obj style_pad_all="#space_md" style_pad_gap="#space_sm">
<text_body style_text_color="#text"/>

<!-- Bad: hardcoded values -->
<lv_obj style_pad_all="16" style_pad_gap="8">
<lv_label style_text_color="0xE0E0E0"/>
```

**5. Use typography components, not raw labels**

```xml
<!-- Good -->
<text_heading text="Status"/>
<text_body bind_text="temperature"/>
<text_small text="Details"/>

<!-- Bad -->
<lv_label style_text_font="..." text="Status"/>
```

### Design Guidelines by Layout Type

**Ultrawide (1920x480):**
- You have tons of horizontal space but very little vertical space (480px minus the navbar)
- Prefer horizontal `flex_flow="row"` layouts — avoid vertical scrolling
- Aim for all content visible at once, no scrolling needed
- Think "dashboard" — information spread across columns

**Portrait (480x800, 600x1024):**
- Lots of vertical space, narrow width
- Navigation bar probably needs to move to the bottom (override `navigation_bar.xml`)
- Content stacks vertically naturally
- Consider which elements can be stacked vs. side-by-side

**Tiny (480x320, 320x240):**
- Very limited space in both directions
- Reduce information density — show less, make touch targets bigger
- Consider hiding optional elements (sensor indicators, etc.)
- Larger icons, fewer text labels

### Panels Worth Overriding

Not every panel needs a layout-specific version. Start with the ones that matter most:

| Priority | Panel | Why |
|----------|-------|-----|
| High | `home_panel.xml` | First thing users see, lots of information to arrange |
| High | `app_layout.xml` | The overall chrome (navbar position, content area) |
| High | `navigation_bar.xml` | Nav position/orientation differs per layout |
| Medium | `controls_panel.xml` | Common panel with multiple cards to rearrange |
| Medium | `print_status_panel.xml` | Important during prints |
| Medium | `settings_panel.xml` | Long list that could use multi-column on ultrawide |
| Low | Overlays (`*_overlay.xml`) | Usually modal dialogs that work OK at any size |
| Low | Simple panels | Panels with minimal content adapt naturally |

### XML Tips for Layout Work

- **No rebuild needed for XML changes.** Just relaunch the app.
- **`flex_flow="row"`** = horizontal layout, **`flex_flow="column"`** = vertical layout.
- **`flex_grow="1"`** makes an element stretch to fill available space.
- **`width="50%"` / `height="100%"`** for fixed proportions.
- **`scrollable="false"`** prevents unintended scroll behavior on containers.
- **`hidden="true"`** + `bind_flag_if_*` = conditional visibility (driven by data).
- See `docs/LVGL9_XML_GUIDE.md` for the full XML reference.

### Testing Your Layout

```bash
# Standard (800x480) — should be unchanged
./build/bin/helix-screen --test -vv

# Ultrawide
./build/bin/helix-screen --test -vv --layout ultrawide -s 1920x480

# Portrait
./build/bin/helix-screen --test -vv --layout portrait -s 480x800

# Tiny
./build/bin/helix-screen --test -vv --layout tiny -s 480x320

# Force any layout on any resolution (for testing)
./build/bin/helix-screen --test -vv --layout ultrawide -s 800x480
```

Use `-vv` for debug logging — it shows which layout was detected and which XML paths are
being resolved.

---

## Technical Reference

This section is for developers working on the layout infrastructure itself (C++ code).

### LayoutManager API

```cpp
class LayoutManager {
public:
    static LayoutManager& instance();
    void init(int display_width, int display_height);
    void set_override(const std::string& layout_name);

    LayoutType type() const;             // Enum value
    const std::string& name() const;     // "standard", "ultrawide", etc.
    bool is_standard() const;
    bool has_override(const std::string& filename) const;

    // Returns "ui_xml/<layout>/filename.xml" if override exists,
    // otherwise "ui_xml/filename.xml"
    std::string resolve_xml_path(const std::string& filename) const;
};
```

### How XML Registration Works

In `xml_registration.cpp`, a helper function resolves paths through the LayoutManager:

```cpp
static void register_xml(const char* filename) {
    auto& lm = helix::LayoutManager::instance();
    std::string path = "A:" + lm.resolve_xml_path(filename);
    lv_xml_register_component_from_file(path.c_str());
}

// Usage — automatically resolves layout overrides:
register_xml("home_panel.xml");
```

### Config

```json
// helixconfig.json
{
  "display": {
    "layout": "auto"
  }
}
```

Valid values: `"auto"` (default), `"standard"`, `"ultrawide"`, `"portrait"`, `"tiny"`,
`"tiny_portrait"`.

CLI flag `--layout <name>` overrides the config file.

### Auto-Detection Logic

```
ratio = width / height

if (max dimension ≤ 480)
    → tiny (landscape) or tiny-portrait (portrait)
else if (ratio > 2.5)
    → ultrawide
else if (ratio < 0.8)
    → portrait
else
    → standard
```

### Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Naming | "Layouts" | Distinct from "themes" (colors) and "profiles" (print start) |
| Detection | Auto with override | Users shouldn't need to configure, but can |
| Fallback | Per-file to standard | New layouts start empty, override incrementally |
| globals.xml | Never overridden | Design tokens are universal across all layouts |
| Runtime switching | Not supported | Would require full widget tree rebuild; startup-only is fine |
