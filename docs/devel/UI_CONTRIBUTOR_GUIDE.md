# UI Contributor Guide

A hands-on guide for contributing layout fixes and alternate screen layouts to HelixScreen. Whether you're fixing clipping at 480x320 or building a portrait layout from scratch, this document covers everything you need. XML changes don't require a rebuild -- just relaunch the app.

For C++ internals, threading, and observer patterns, see the [Deep Dive References](#10-deep-dive-references) at the bottom.

---

## 1. Quick Start

Build once, then iterate on XML without rebuilding:

```bash
# Build the binary
make -j

# Run at any screen size (mock printer, debug logging)
./build/bin/helix-screen --test -vv -s 800x480
```

The `-s WIDTHxHEIGHT` flag sets the window size. The `--test` flag runs against a mock printer so you don't need real hardware. `-vv` gives you debug-level logs (helpful when things don't look right).

### Common sizes to test

| Size | Category | Notes |
|------|----------|-------|
| `480x320` | Tiny | Smallest supported. Where most bugs live. |
| `800x480` | Standard / Medium | The "default" target. Most common screen. |
| `1024x600` | Large | Waveshare 7" and similar. |
| `1280x720` | XLarge | Larger desktop-class displays. |
| `1920x480` | Ultrawide | Bar-style displays. Very wide, very short. |
| `480x800` | Portrait | Rotated standard display. |

### Screenshots

Take a screenshot of a specific panel at a specific size:

```bash
# Saves to /tmp/ui-screenshot-<name>.png
./scripts/screenshot.sh helix-screen tiny-home home --test -s 480x320
```

This requires ImageMagick. You can also press **S** while the app is running for a quick screenshot.

### Force a layout

```bash
./build/bin/helix-screen --test -vv --layout ultrawide -s 1920x480
```

### Navigation panels

`home`, `print-select`, `controls`, `filament`, `settings`, `advanced`

### Key overlays

`motion`, `print-status`, `console`, `bed-mesh`, `input-shaper`, `macros`, `spoolman`, `ams`

### The golden rule

**XML changes don't need a rebuild.** Edit any `.xml` file in `ui_xml/`, relaunch the app, and see your changes immediately. This makes layout iteration very fast.

---

## 2. Screen Breakpoints

Breakpoints are based on screen **height**, because vertical space is always the constraint. Width varies wildly (480 to 1920+), but it's running out of vertical room that causes clipping, overlapping, and broken layouts.

### The 5-tier system

| Tier | Suffix | Height Range | Target Devices | Fallback |
|------|--------|-------------|----------------|----------|
| TINY | `_tiny` | <= 390px | 480x320 | Falls back to `_small` |
| SMALL | `_small` | 391 -- 460px | 480x400, 1920x440 | **Required** (core tier) |
| MEDIUM | `_medium` | 461 -- 550px | 800x480 | **Required** (core tier) |
| LARGE | `_large` | 551 -- 700px | 1024x600 | **Required** (core tier) |
| XLARGE | `_xlarge` | > 700px | 1280x720+ | Falls back to `_large` |

Every responsive value needs three core variants: `_small`, `_medium`, and `_large`. The `_tiny` and `_xlarge` tiers are optional -- define them only when values actually need to differ from `_small` or `_large` respectively.

### How it works

In `globals.xml`, you define the suffixed variants of each token:

```xml
<px name="space_lg_small" value="12"/>
<px name="space_lg_medium" value="16"/>
<px name="space_lg_large" value="20"/>
```

At startup, `theme_manager` detects the screen height, picks the matching suffix, and registers the **base name** (`space_lg`) pointing to the correct value. So when your XML says `style_pad_all="#space_lg"`, it resolves to 12, 16, or 20 depending on the screen.

### CRITICAL: Never define the base name in globals.xml

This is the most common mistake. Do NOT do this:

```xml
<!-- WRONG -- this will silently break responsive overrides -->
<px name="space_lg" value="16"/>
<px name="space_lg_small" value="12"/>
<px name="space_lg_medium" value="16"/>
<px name="space_lg_large" value="20"/>
```

LVGL ignores duplicate variable registrations. If the base name `space_lg` is already registered (from the first line), the responsive override from `theme_manager` is silently discarded. Only define the suffixed variants.

---

## 3. Design Tokens

Design tokens are the shared vocabulary for spacing, sizing, and typography. Use them everywhere instead of hardcoded values. They automatically adapt to the current breakpoint.

### Spacing Tokens

| Token | Small | Medium | Large | Use Case |
|-------|-------|--------|-------|----------|
| `#space_xxs` | 2px | 3px | 4px | Keypad rows, compact icon gaps |
| `#space_xs` | 4px | 5px | 6px | Button icon+text gaps, dense info |
| `#space_sm` | 6px | 7px | 8px | Tight layouts, minor separations |
| `#space_md` | 8px | 10px | 12px | Standard flex gaps, compact padding |
| `#space_lg` | 12px | 16px | 20px | Container padding, major sections |
| `#space_xl` | 16px | 20px | 24px | Emphasis cards, major separations |
| `#space_2xl` | 24px | 32px | 40px | Toast/overlay offsets |

In XML:
```xml
<lv_obj style_pad_all="#space_lg" style_pad_gap="#space_md"/>
```

In C++:
```cpp
int padding = theme_manager_get_spacing("space_lg");
```

### Font Tokens

| Token | Component | Small | Medium | Large |
|-------|-----------|-------|--------|-------|
| `#font_heading` | `<text_heading>` | noto_sans_20 | noto_sans_26 | noto_sans_28 |
| `#font_body` | `<text_body>` | noto_sans_14 | noto_sans_18 | noto_sans_20 |
| `#font_small` | `<text_small>` | noto_sans_light_12 | noto_sans_light_16 | noto_sans_light_18 |
| `#font_xs` | `<text_xs>` | noto_sans_light_10 | noto_sans_light_12 | noto_sans_light_14 |

You almost never need to reference font tokens directly. Use the semantic `<text_*>` components instead (see [Pre-Themed Widgets](#5-pre-themed-widgets)).

### Component Tokens

| Token | Small | Medium | Large | Purpose |
|-------|-------|--------|-------|---------|
| `#border_radius` | 4px | 9px | 12px | Corner radius for cards, buttons |
| `#button_height` | 48px | 52px | 72px | Standard button height |
| `#button_height_sm` | 36px | 40px | 48px | Small buttons (back, icon-only) |
| `#button_height_lg` | 64px | 70px | 96px | Large buttons |
| `#header_height` | 48px | 56px | 60px | Panel header height |
| `#temp_card_height` | 64px | 72px | 80px | Temperature card in print status |
| `#icon_size` | md | lg | xl | Responsive icon size string |
| `#spinner_lg` | 48px | 56px | 64px | Large spinner |
| `#spinner_md` | 24px | 28px | 32px | Standard spinner |
| `#spinner_sm` | 16px | 18px | 20px | Small spinner |
| `#spinner_xs` | 12px | 14px | 16px | Compact spinner |

### Adding New Tokens

Follow the triplet pattern in `globals.xml`:

```xml
<!-- In globals.xml — define suffixed variants only -->
<px name="my_widget_height_small" value="48"/>
<px name="my_widget_height_medium" value="56"/>
<px name="my_widget_height_large" value="72"/>
```

Then use the base name in your layout:

```xml
<!-- In your panel XML -->
<lv_obj height="#my_widget_height"/>
```

From C++:
```cpp
int h = theme_manager_get_spacing("my_widget_height");
```

---

## 4. Color System

HelixScreen uses 16 semantic color tokens. These work across light and dark modes automatically -- you just reference the token name and the system resolves the right value.

### Color Tokens

| Token | Purpose |
|-------|---------|
| `#screen_bg` | Main application background |
| `#overlay_bg` | Sidebar/panel backgrounds |
| `#card_bg` | Card surfaces |
| `#elevated_bg` | Elevated surfaces (dialogs, inputs) |
| `#border` | Borders and dividers |
| `#text` | Primary text |
| `#text_muted` | Secondary/dimmed text |
| `#text_subtle` | Hint/tertiary text |
| `#primary` | Primary accent color |
| `#secondary` | Secondary accent |
| `#tertiary` | Tertiary accent |
| `#info` | Info state (purple in Nord) |
| `#success` | Success state (green) |
| `#warning` | Warning state (amber) |
| `#danger` | Error/danger (red) |
| `#focus` | Focus ring outline |

### Using colors in XML

Just reference the token. The system handles light/dark mode for you:

```xml
<lv_obj style_bg_color="#card_bg"/>
<text_body style_text_color="#warning" text="High temperature"/>
```

### Theme JSON structure

Themes are defined in `config/themes/`. Here's a snippet from `nord.json`:

```json
{
  "name": "Nord",
  "dark": {
    "screen_bg": "#2e3440",
    "card_bg": "#434c5e",
    "text": "#eceff4",
    "text_muted": "#d8dee9",
    "primary": "#88c0d0",
    "danger": "#bf616a",
    "success": "#a3be8c",
    "warning": "#ebcb8b"
  },
  "light": {
    "screen_bg": "#eceff4",
    "card_bg": "#ffffff",
    "text": "#2e3440",
    "text_muted": "#3b4252",
    "primary": "#5e81ac",
    "danger": "#b23a48",
    "success": "#3fa47d",
    "warning": "#b08900"
  },
  "border_radius": 12,
  "border_width": 1,
  "border_opacity": 40
}
```

### C++ color access

```cpp
// Token lookup -- correct
lv_color_t bg = theme_manager_get_color("card_bg");

// Hex string parsing -- correct
lv_color_t red = theme_manager_parse_hex_color("#FF0000");

// WRONG -- parse_hex_color does NOT look up tokens
// lv_color_t bad = theme_manager_parse_hex_color("#card_bg");
```

### Adding custom colors in globals.xml

Define `_light` and `_dark` variants. The system auto-discovers them by suffix:

```xml
<color name="my_custom_light" value="#E0E0E0"/>
<color name="my_custom_dark" value="#3B4252"/>

<!-- Usage in any XML layout -->
<lv_obj style_bg_color="#my_custom"/>
```

---

## 5. Pre-Themed Widgets

HelixScreen provides semantic widgets that already have the right colors, fonts, spacing, and responsive behavior baked in. **Use these instead of raw LVGL widgets** whenever possible -- it saves you from manually specifying styles and keeps things consistent.

### Typography

| Widget | Font Token | Text Style | Use Case |
|--------|-----------|------------|----------|
| `<text_heading>` | font_heading | Muted | Section titles |
| `<text_body>` | font_body | Primary | Body paragraphs |
| `<text_muted>` | font_body | Muted | Secondary metadata |
| `<text_small>` | font_small | Muted | Helper text |
| `<text_xs>` | font_xs | Muted | Compact info, badges |
| `<text_button>` | font_body | Primary | Button labels (centered) |

All of these support `bind_text="subject_name"` for dynamic content and `text="static text"` for fixed content. You can override the color with `style_text_color="#token"`.

```xml
<text_heading text="Temperature"/>
<text_body bind_text="nozzle_temp_display"/>
<text_muted text="Last updated 5 min ago"/>
<text_small text="Firmware v1.2.3"/>
```

### ui_card

A standard card surface with `card_bg` background, themed border, and `border_radius` already applied.

```xml
<ui_card width="100%" height="content">
  <text_heading text="Temperature"/>
  <text_body bind_text="nozzle_temp"/>
</ui_card>
```

Don't re-specify `style_radius`, `style_bg_color`, or `style_border_*` -- they're already themed.

### ui_button

Themed button with automatic contrast text color and responsive height.

Variants: `primary`, `secondary`, `danger`, `success`, `warning`, `tertiary`, `ghost`

```xml
<ui_button variant="primary" text="Save"/>
<ui_button variant="danger" text="Delete" icon="trash_can"/>
<ui_button variant="ghost" text="Cancel"/>
<ui_button icon="settings"/>  <!-- Icon only, no text -->
```

### icon

Material Design Icons with size and color variants.

Size variants: `xs` (16px), `sm` (24px), `md` (32px), `lg` (48px), `xl` (64px).

Color variants: `text`, `muted`, `primary`, `secondary`, `tertiary`, `success`, `warning`, `danger`, `info`, `disabled`.

```xml
<icon src="home" size="lg" variant="primary"/>
<icon src="settings" size="#icon_size" variant="muted"/>  <!-- Responsive sizing -->
```

### spinner

Responsive loading spinner. Sizes adapt per breakpoint.

```xml
<spinner size="lg"/>  <!-- 48/56/64px depending on breakpoint -->
<spinner size="md"/>  <!-- 24/28/32px -->
<spinner size="sm"/>  <!-- 16/18/20px -->
```

### dividers

Simple horizontal and vertical dividers with themed colors.

```xml
<divider_horizontal/>
<divider_vertical/>
```

### ui_severity_card

A card with a severity-colored left border. Great for status messages.

Severities: `info`, `success`, `warning`, `error`

```xml
<ui_severity_card severity="warning">
  <text_body text="Nozzle temperature is high"/>
</ui_severity_card>
```

### ui_switch

Responsive toggle switch. Sizes scale with the current breakpoint.

```xml
<ui_switch size="medium" checked="true"/>
```

Sizes: `tiny`, `small`, `medium`, `large`

### ui_markdown

Theme-aware markdown viewer for rich text content.

```xml
<ui_markdown bind_text="release_notes" width="100%"/>
```

### Widget Defaults -- DON'T re-specify

These widgets come pre-themed. Adding redundant style attributes clutters the XML and can conflict with theming:

| Widget | Already Themed (skip these) |
|--------|---------------------------|
| `ui_card` | `style_radius`, `style_bg_color`, `style_border_*` |
| `ui_button` | `style_radius`, `style_bg_color`, height, text color |
| `text_*` | `style_text_font`, `style_text_color` |
| `icon` | Font selection |
| `divider_*` | `style_bg_color`, width/height |
| `ui_markdown` | All styling |

---

## 6. XML Layout Essentials

This section covers the patterns you'll use most in layout work. For a complete reference, see [LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md) and [LVGL9_XML_ATTRIBUTES_REFERENCE.md](LVGL9_XML_ATTRIBUTES_REFERENCE.md).

### Flex Layout

Almost everything uses flexbox. The three flows you'll see:

```xml
<lv_obj flex_flow="row"/>        <!-- Horizontal: children side by side -->
<lv_obj flex_flow="column"/>     <!-- Vertical: children stacked -->
<lv_obj flex_flow="row_wrap"/>   <!-- Horizontal, wraps to new rows when full -->
```

### flex_grow

Children with `flex_grow` expand to fill remaining space in their parent. The parent **must** have an explicit size (not `content`).

```xml
<lv_obj flex_flow="row" width="100%" height="100%">
  <lv_obj flex_grow="3" height="100%"><!-- Left column, 30% --></lv_obj>
  <lv_obj flex_grow="7" height="100%"><!-- Right column, 70% --></lv_obj>
</lv_obj>
```

### Centering (THE GOTCHA)

Unlike CSS flexbox, LVGL needs **three** properties to fully center items -- not two. This trips up almost everyone:

```xml
<!-- Fully centered column -->
<lv_obj flex_flow="column"
        style_flex_main_place="center"
        style_flex_cross_place="center"
        style_flex_track_place="center">
  <text_body text="I am actually centered"/>
</lv_obj>
```

Without `style_flex_track_place`, children with explicit widths stay left-aligned even though the other two properties suggest centering. If something isn't centering the way you expect, add `style_flex_track_place="center"` first.

### Gaps and Padding

```xml
<!-- Gap between children -->
<lv_obj flex_flow="row" style_pad_gap="#space_md">
  <ui_button text="A"/>
  <ui_button text="B"/>
</lv_obj>

<!-- Internal padding around all edges -->
<lv_obj style_pad_all="#space_lg">
  <text_body text="Content with breathing room"/>
</lv_obj>
```

### Conditional Visibility

Show or hide elements based on subject values:

```xml
<!-- Hide this widget when status == 0 -->
<lv_obj>
  <bind_flag_if_eq subject="status" flag="hidden" ref_value="0"/>
</lv_obj>

<!-- Show only when connected (hide when not equal to 1) -->
<lv_obj>
  <bind_flag_if_not_eq subject="connected" flag="hidden" ref_value="1"/>
</lv_obj>
```

Available operators: `bind_flag_if_eq`, `bind_flag_if_not_eq`, `bind_flag_if_gt`, `bind_flag_if_ge`, `bind_flag_if_lt`, `bind_flag_if_le`.

### Event Callbacks

```xml
<lv_button name="save_btn">
  <event_cb trigger="clicked" callback="on_save_clicked"/>
  <text_body text="Save"/>
</lv_button>
```

Callbacks are registered in C++. For layout-only work, just keep existing `<event_cb>` elements in place -- don't remove them or rename them.

### Visual Debugging

When you can't tell why something is overflowing or misaligned, add a temporary background color to see the actual widget bounds:

```xml
<lv_obj style_bg_color="#ff0000" style_bg_opa="100%">
  <!-- Now you can see exactly where this container starts and ends -->
</lv_obj>
```

Remove the debug styles before submitting your PR.

### lv_obj Defaults in HelixScreen

Our theme makes `lv_obj` a pure layout container by default: transparent background, no border, no padding, sized to content. You don't need to clear any of these -- just use `lv_obj` as a flexbox wrapper and it stays invisible.

### Common Gotchas

| Wrong | Right | Why |
|-------|-------|-----|
| `width="LV_SIZE_CONTENT"` | `width="content"` | XML uses string names, not C constants |
| `flex_align="center center"` | `style_flex_main_place="center"` | `flex_align` is silently ignored |
| `style_img_recolor` | `style_image_recolor` | Full words, not abbreviations |
| `<lv_dropdown options="A\nB\nC"/>` | `options="A&#10;B&#10;C"` | Use XML entity for newlines |
| Hardcoded `style_pad_all="12"` | `style_pad_all="#space_lg"` | Always use design tokens |
| Hardcoded `style_text_font="..."` | `<text_body>` | Use semantic typography components |
| `style_bg_color="#2e3440"` | `style_bg_color="#screen_bg"` | Use color tokens, not hex values |

---

## 7. Layout Overrides

HelixScreen supports layout-specific XML overrides so you can rearrange panels for different screen shapes without touching the standard layouts.

### Layout types

| Layout | Detection | Example Screens |
|--------|-----------|-----------------|
| `standard` | Normal landscape (4:3 to 16:9) | 800x480, 1024x600, 1280x720 |
| `ultrawide` | Aspect ratio > 2.5:1 | 1920x480, 1920x400 |
| `portrait` | Aspect ratio < 0.8:1 | 480x800, 600x1024 |
| `tiny` | Max dimension <= 480, landscape | 480x320, 320x240 |
| `tiny-portrait` | Max dimension <= 480, portrait | 320x480, 240x320 |

Force a layout with `--layout ultrawide` on the command line, or set `display.layout` in `helixconfig.json`.

### Directory structure

```
ui_xml/
  globals.xml              <-- Shared by ALL layouts (never override this)
  home_panel.xml           <-- Standard home panel
  controls_panel.xml       <-- Standard controls panel
  ...                      <-- ~169 XML files total
  ultrawide/               <-- Ultrawide overrides
    home_panel.xml         <-- The only override that exists so far
  portrait/                <-- Doesn't exist yet
  tiny/                    <-- Doesn't exist yet
```

### How overrides work

If `ui_xml/<layout>/<panel>.xml` exists, it's used instead of `ui_xml/<panel>.xml`. Otherwise the standard version is loaded. You only need to override the panels that actually need different layouts -- everything else falls through automatically.

### Creating an override

1. Copy the standard layout as a starting point:
   ```bash
   cp ui_xml/controls_panel.xml ui_xml/ultrawide/controls_panel.xml
   ```

2. Edit the copy for the target screen shape.

3. Test it:
   ```bash
   ./build/bin/helix-screen --test -vv --layout ultrawide -s 1920x480
   ```

4. No rebuild needed. XML loads at runtime.

### The rules you must follow

When creating a layout override, you're rearranging the same content for a different screen shape. The C++ code still expects certain widgets, bindings, and callbacks to exist.

1. **Keep all named widgets** that C++ looks up via `lv_obj_find_by_name()`. Search the panel's `.cpp` file to find required names:
   ```bash
   grep lv_obj_find_by_name src/ui/panels/controls_panel.cpp
   ```

2. **Keep all subject bindings** (`bind_text`, `bind_value`, `bind_flag_if_*`, etc.). These connect the UI to live data.

3. **Keep all event callbacks** (`<event_cb>` elements). These wire up button presses and interactions.

4. **Use design tokens** for all colors, spacing, and fonts. No hardcoded values.

5. **Don't modify globals.xml.** It's shared across all layouts.

You're free to rearrange the visual hierarchy, change flex directions, adjust sizes, hide optional decorative elements, or add new layout containers. Just preserve the functional widgets.

### Design guidelines by layout type

**Ultrawide (1920x480):** Tons of horizontal space, very little vertical. Favor `flex_flow="row"` to spread content across columns. Aim for everything visible at once with no scrolling. Think "dashboard with columns" -- put related info side by side instead of stacking it.

**Portrait (480x800):** Lots of vertical space, narrow width. Content stacks naturally with `flex_flow="column"`. The navbar probably needs to move to the bottom of the screen. Consider overriding `navigation_bar.xml` and `app_layout.xml` to change the overall chrome.

**Tiny (480x320):** Very limited in both directions. Reduce information density, use bigger touch targets (48px minimum), show fewer labels. Hide optional elements with conditional visibility or just remove decorative content.

### Priority panels to override

Start with the panels that matter most:

| Priority | Panel | Why |
|----------|-------|-----|
| High | `home_panel.xml` | First thing users see |
| High | `app_layout.xml` | Overall chrome (navbar + content area) |
| High | `navigation_bar.xml` | Nav position/orientation differs per layout |
| Medium | `controls_panel.xml` | Multiple cards that benefit from rearranging |
| Medium | `print_status_panel.xml` | Important during active prints |
| Medium | `settings_panel.xml` | Could use multi-column on ultrawide |
| Low | Overlays | Usually modal dialogs that adapt reasonably well |

---

## 8. What Needs Work

This is where to start if you want to contribute. Issues are organized by severity and area. All observations below are primarily at 480x320 unless noted otherwise.

### Global Issues (affect multiple panels)

These cut across many screens and are high-value fixes:

- **Numeric keypad overlay** doesn't fit vertically at 480x320 -- bottom rows are cut off. This affects every panel that uses the keypad for numeric input.
- **Many modals** don't respect viewport height -- content clips at top and bottom on small screens.
- **Navbar icons** are clipped at 480x320, outlines overlap, and click targets may overlap each other.
- **Temperature labels** collide with values on the controls and filament panels.
- **Settings dropdown menus** are hardcoded too wide for small screens.

### Broken / Needs Major Rework (at 480x320)

These are the most impactful fixes:

- **Print Select list view** -- The most broken screen. Padding is wrong, row sizing is off, horizontal overflow everywhere.
- **Print Status overlay** -- Action buttons, temperature cards, and metadata are all fighting for space. Nothing fits.
- **Filament panel** -- Multi-filament card is invisible, material buttons crush the operations section.

### Needs Moderate Fixes (at 480x320)

Usable but clearly broken in places:

- **Controls panel** -- Position card labels overlap the header, Z-offset value wraps, cooling section overflows, quick actions are clipped.
- **Print Select card view** -- Metadata area is too tall, squeezing the file thumbnails.
- **PID Calibration** -- Chips are clipped, text doesn't wrap, slider padding is off, values wrap awkwardly.

### Minor / Cosmetic

Things that work but could look better:

- **Home panel** -- Tip text is borderline too large, status icon temperature padding is slightly off.
- **Print File Detail** -- Pre-print options are cramped when toggles are present.
- **Z-Offset Calibration** -- Slightly too tall for the viewport, could use scroll.
- **Spoolman** -- Too much padding, wasted space.
- **Print History list** -- Filter fields are too wide.

### Looking Good Already (at 480x320)

These panels work well and can serve as reference for how to do things right:

- Motion overlay
- Advanced settings
- Settings panel (except dropdown widths)
- Theme view and edit
- Print History dashboard

### Ultrawide Status

- Only `home_panel.xml` has an override (and it needs refinement).
- Every other panel uses the standard layout and would benefit from ultrawide-specific arrangements.

### Portrait / Tiny-Portrait Status

- Not started at all. The directories don't even exist yet.
- Wide open for contributions. If you have a portrait display, this is a great place to make a big impact.

---

## 9. Testing Your Changes

### Screenshot commands for each breakpoint

```bash
# Tiny (480x320)
./scripts/screenshot.sh helix-screen tiny-home home --test -s 480x320

# Standard/Medium (800x480)
./scripts/screenshot.sh helix-screen medium-home home --test -s 800x480

# Large (1024x600)
./scripts/screenshot.sh helix-screen large-home home --test -s 1024x600

# XLarge (1280x720)
./scripts/screenshot.sh helix-screen xlarge-home home --test -s 1280x720

# Ultrawide (1920x480)
./scripts/screenshot.sh helix-screen ultrawide-home home --test --layout ultrawide -s 1920x480

# Portrait (480x800)
./scripts/screenshot.sh helix-screen portrait-home home --test --layout portrait -s 480x800
```

Screenshots save to `/tmp/ui-screenshot-<name>.png`.

Set `HELIX_SCREENSHOT_DISPLAY=0` to prevent the app from opening a visible display window (useful for CI or batch screenshots).

Replace `home` with any panel or overlay name: `controls`, `filament`, `settings`, `print-status`, `motion`, etc.

### Widget destruction safety

**Never call `lv_obj_clean()` or `lv_obj_del()` on a container from inside an event callback on a child of that container.** This destroys the widget whose callback is still on the call stack, causing a use-after-free crash (see [issue #80](https://github.com/356C-LLC/helixscreen/issues/80)).

If your event callback needs to rebuild or destroy its own parent container:
- For `lv_obj_clean()`: wrap the rebuild in `ui_queue_update()` to defer it to the next tick
- For `lv_obj_del()`: use `lv_obj_del_async()` instead

```cpp
// BAD: swatch click handler destroys its own parent
void handle_color_selected(...) {
    lv_obj_clean(container);  // container is parent of the clicked swatch!
    rebuild(container);
}

// GOOD: defer the destruction
void handle_color_selected(...) {
    ui_queue_update([this]() {
        lv_obj_clean(container);
        rebuild(container);
    });
}
```

### Verification checklist

Before submitting, verify these for every change:

- [ ] No text clipping or overflow at the target size
- [ ] Touch targets are large enough (48px minimum recommended)
- [ ] Design tokens used throughout (no hardcoded pixel values, colors, or fonts)
- [ ] All subject bindings preserved from the standard layout
- [ ] All event callbacks preserved
- [ ] All named widgets still present (check with `grep lv_obj_find_by_name` in the C++ source)
- [ ] Standard layout still works at 800x480 (no regression)

### Submitting your work

PRs are welcome. To make review smooth:

- Include **before/after screenshots** at the target resolution.
- Test at the target size **and** at standard (800x480) to verify no regression.
- If you created a layout override, mention which named widgets you found in the C++ source so reviewers can verify coverage.
- Keep changes focused. One panel per PR is easier to review than five.

---

## 10. Deep Dive References

For the full details on any of these topics, see the dedicated docs:

| Document | What It Covers |
|----------|---------------|
| [LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md) | Full XML system guide -- subjects, events, component creation, implementation patterns |
| [LVGL9_XML_ATTRIBUTES_REFERENCE.md](LVGL9_XML_ATTRIBUTES_REFERENCE.md) | Quick-lookup cheatsheet for every XML attribute |
| [DEVELOPER_QUICK_REFERENCE.md](DEVELOPER_QUICK_REFERENCE.md) | C++ patterns -- observer factory, threading, class structures |
| [ARCHITECTURE.md](ARCHITECTURE.md) | System architecture and high-level design decisions |

For most layout and styling work, you shouldn't need these. But if you're adding new components, wiring up new subjects, or debugging why a binding isn't working, that's where the answers live.
