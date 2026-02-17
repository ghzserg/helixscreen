# Theme System

> **For contributors:** If you're doing layout or styling work, start with the **[UI Contributor Guide](UI_CONTRIBUTOR_GUIDE.md)** instead. This document covers the internal architecture of the theme system — style objects, theme_core C API, and extending the system with new themed widgets.

## Overview

The reactive theme system enables **live theme switching** (dark/light modes) without recreating widgets. When the user toggles dark mode or previews a theme, all UI elements update instantly.

### Key Principle

> **DATA in C++, APPEARANCE in XML, Shared Styles connect them.**

> **Note:** The theme system handles colors, spacing tokens, and typography. For structural layout changes across different screen shapes (ultrawide, portrait, tiny), see the [Layout System](LAYOUT_SYSTEM.md) — themes and layouts are independent and any combination works.

- **Data** (C++): Printer state, temperatures, positions
- **Appearance** (XML): Layout, colors via tokens, spacing via tokens
- **Shared Styles** (`theme_core.c`): LVGL `lv_style_t` objects that apply colors and update reactively

### What Problem It Solves

**Before:** Changing themes required restarting the app or manually updating hundreds of widgets.

**After:** Call `theme_core_update_colors()` → all 47+ shared styles update in-place → `lv_obj_report_style_change(NULL)` triggers LVGL's style cascade → every widget using those styles redraws with new colors.

---

## Architecture

### Two-Layer System

| Layer | File | Responsibility |
|-------|------|----------------|
| **theme_manager** (C++) | `src/ui/theme_manager.cpp` | Token lookup, XML parsing, responsive constants, theme loading |
| **theme_core** (C) | `src/theme_core.c` | LVGL style objects, color application, update propagation |

### When to Use Each

| Task | Use |
|------|-----|
| Get a color token in C++ | `theme_manager_get_color("card_bg")` |
| Get responsive spacing | `theme_manager_get_spacing("space_lg")` |
| Get responsive font | `theme_manager_get_font("font_body")` |
| Toggle dark/light mode | `theme_manager_toggle_dark_mode()` |
| Check current mode | `theme_manager_is_dark_mode()` |
| Apply style to custom widget | `theme_core_get_card_style()` etc. |

### Data Flow

```
Theme JSON (nord.json, etc.)
    ↓
theme_manager.cpp (loads palette, registers tokens)
    ↓
theme_core.c (applies colors to 47+ lv_style_t objects)
    ↓
Widgets (reference styles via XML bind_style or C++ add_style)
```

### How Changes Propagate

```
User toggles dark mode
    ↓
theme_manager_toggle_dark_mode()
    ↓
theme_core_update_colors(&palette)
    ↓
Updates all 47 style objects in-place
    ↓
lv_obj_report_style_change(NULL)  ← CRITICAL: triggers LVGL cascade
    ↓
All widgets using shared styles redraw
```

---

## Color System

### Token Naming Conventions

All colors are referenced as tokens with `#` prefix in XML:

| Token | Purpose |
|-------|---------|
| `#app_bg` | Main application background |
| `#overlay_bg` | Sidebar/panel backgrounds |
| `#card_bg` | Card surfaces |
| `#elevated_bg` | Elevated/control surfaces (dialogs, inputs) |
| `#border` | Borders and dividers |
| `#text` | Primary text |
| `#text_muted` | Secondary/dimmed text |
| `#text_subtle` | Hint/tertiary text |
| `#primary` | Primary accent color |
| `#secondary` | Secondary accent |
| `#tertiary` | Tertiary accent |
| `#info` | Info state (blue) |
| `#success` | Success state (green) |
| `#warning` | Warning state (amber) |
| `#danger` | Error/danger (red) |
| `#focus` | Focus ring outline |

### Light/Dark Variants

Theme-aware colors have `_light` and `_dark` suffixes defined in the theme JSON. The system automatically selects the right one:

```xml
<!-- In your XML - just use the base name -->
<lv_obj style_bg_color="#card_bg"/>

<!-- At runtime, resolves to card_bg_light or card_bg_dark based on mode -->
```

Internally:
```cpp
// theme_manager.cpp checks for both variants
const char* light_str = lv_xml_get_const_silent(nullptr, "card_bg_light");
const char* dark_str = lv_xml_get_const_silent(nullptr, "card_bg_dark");
// Returns appropriate value based on use_dark_mode flag
```

### Color Functions

| Function | Use Case |
|----------|----------|
| `theme_manager_get_color("card_bg")` | Get themed color token (handles _light/_dark) |
| `theme_manager_parse_hex_color("#FF0000")` | Parse hex string only (NOT for tokens) |

**Common mistake:** Using `theme_manager_parse_hex_color()` with tokens. It only parses hex strings.

```cpp
// WRONG - parse_hex_color doesn't handle tokens
lv_color_t bg = theme_manager_parse_hex_color("#card_bg");

// CORRECT - get_color resolves tokens
lv_color_t bg = theme_manager_get_color("card_bg");
```

---

## Responsive Sizing

### The Breakpoint System

Every responsive value requires three core variants: `_small`, `_medium`, `_large`. Optional `_tiny` and `_xlarge` variants can be added where values differ. The system automatically selects based on screen height.

**Breakpoints (5-tier, height-based):**
| Suffix | Height | Target Devices | Fallback |
|--------|--------|----------------|----------|
| `_tiny` | ≤390px | 480×320 | → `_small` |
| `_small` | 391-460px | 480×400, 1920×440 | required |
| `_medium` | 461-550px | 800×480 | required |
| `_large` | 551-700px | 1024×600 | required |
| `_xlarge` | >700px | 1280×720+ | → `_large` |

Height = vertical resolution (the real constraint for layout).

### Spacing Tokens

Defined in `ui_xml/globals.xml`:

| Token | Small | Medium | Large | Use Case |
|-------|-------|--------|-------|----------|
| `#space_xxs` | 2px | 3px | 4px | Keypad rows, compact icon gaps |
| `#space_xs` | 4px | 5px | 6px | Button icon+text gaps, dense info |
| `#space_sm` | 6px | 7px | 8px | Tight layouts, minor separations |
| `#space_md` | 8px | 10px | 12px | Standard flex gaps, compact padding |
| `#space_lg` | 12px | 16px | 20px | Container padding, major sections |
| `#space_xl` | 16px | 20px | 24px | Emphasis cards, major separations |
| `#space_2xl` | 24px | 32px | 40px | Toast/overlay offsets |

### How It Works

```xml
<!-- In globals.xml - define triplet variants -->
<px name="space_lg_small" value="12"/>
<px name="space_lg_medium" value="16"/>
<px name="space_lg_large" value="20"/>

<!-- At runtime, theme_manager registers base name -->
lv_xml_register_const(scope, "space_lg", "16");  // On 800x480 screen

<!-- In your XML - use base name -->
<lv_obj style_pad_all="#space_lg"/>
```

**Critical:** Do NOT define base constants (`space_lg`) in globals.xml. Only define the triplet variants. The theme_manager registers the base name at runtime. LVGL ignores duplicate registrations, so pre-defining base names breaks responsive overrides.

### Font Tokens

| Token | Small | Medium | Large |
|-------|-------|--------|-------|
| `#font_heading` | noto_sans_20 | noto_sans_26 | noto_sans_28 |
| `#font_body` | noto_sans_14 | noto_sans_18 | noto_sans_20 |
| `#font_small` | noto_sans_light_12 | noto_sans_light_16 | noto_sans_light_18 |
| `#font_xs` | noto_sans_light_10 | noto_sans_light_12 | noto_sans_light_14 |

---

## Pre-Themed Widgets

These semantic widgets apply shared theme styles automatically. Use them instead of raw LVGL widgets.

### Typography

| Widget | Font | Text Style | Use Case |
|--------|------|-----------|----------|
| `<text_heading>` | font_heading | Muted | Section titles |
| `<text_body>` | font_body | Primary | Body paragraphs |
| `<text_muted>` | font_body | Muted | Secondary metadata |
| `<text_small>` | font_small | Muted | Helper text |
| `<text_xs>` | font_xs | Muted | Compact info, badges |
| `<text_button>` | font_body | Primary | Button labels (centered) |

**Attributes:**
- `text` - Label text (supports `bind_text="subject_name"`)
- `stroke_width` - Text outline thickness
- `stroke_color` - Outline color (hex)

```xml
<text_heading text="Section Title"/>
<text_body text="Description paragraph"/>
<text_muted text="Last updated: 5m ago"/>
<text_small text="Helper text"/>
```

### Containers

#### ui_card
Standard card surface with themed background, border, and radius.

```xml
<ui_card width="200" height="150">
  <text_body text="Card content"/>
</ui_card>
```

Uses `theme_core_get_card_style()` → `card_bg` color, `border` color, theme radius.

#### ui_dialog
Modal/overlay container with elevated surface color.

```xml
<ui_dialog width="80%" height="60%">
  <text_heading text="Dialog Title"/>
  <!-- content -->
</ui_dialog>
```

Uses `theme_core_get_dialog_style()` → `elevated_bg` color.

### Buttons

`<ui_button>` supports multiple variants with auto-contrast text:

| Variant | Background | Use Case |
|---------|-----------|----------|
| `primary` | Primary accent | Main actions |
| `secondary` | Surface control | Secondary actions |
| `danger` | Danger red | Destructive actions |
| `success` | Success green | Confirmations |
| `warning` | Warning amber | Caution actions |
| `tertiary` | Tertiary accent | Tertiary actions |
| `ghost` | Transparent | Subtle actions |

**Attributes:**
- `variant` - Button style (default: "primary")
- `text` - Button label
- `icon` - MDI icon name
- `icon_position` - "left" (default) or "right"

```xml
<ui_button variant="primary" text="Save"/>
<ui_button variant="danger" text="Delete" icon="trash_can"/>
<ui_button variant="ghost" text="Cancel"/>
<ui_button icon="settings"/>  <!-- Icon only -->
```

**Auto-contrast:** Text color automatically adjusts based on background luminance. Dark backgrounds get light text, light backgrounds get dark text. This updates reactively when themes change.

### Icons

`<icon>` widget with color variants and sizes:

**Color Variants:**
| Variant | Color Source |
|---------|-------------|
| `text` | Primary text color |
| `muted` | Muted text color |
| `primary` | Primary accent |
| `secondary` | Secondary accent |
| `tertiary` | Tertiary accent |
| `success` | Success green |
| `warning` | Warning amber |
| `danger` | Danger red |
| `info` | Info blue |
| `disabled` | Text color at 50% opacity |

**Size Variants:**

Icon sizes map directly to fixed-size icon fonts (not responsive):

| Size | Font | Typical Use |
|------|------|-------------|
| `xs` | mdi_icons_16 | Inline with small text |
| `sm` | mdi_icons_24 | Buttons, list items |
| `md` | mdi_icons_32 | Card headers |
| `lg` | mdi_icons_48 | Status indicators |
| `xl` | mdi_icons_64 | Navigation, hero icons |

> **Note:** For responsive icon sizing, use the `icon_size` token (`size="#icon_size"`) which selects sm/md/lg/xl based on breakpoint (tiny/small/medium/large/xlarge).

**Attributes:**
- `src` - MDI icon name ("home", "settings", "wifi")
- `size` - "xs", "sm", "md", "lg", "xl"
- `variant` - Color variant
- `color` - Custom color override (hex)

```xml
<icon src="home" size="lg" variant="primary"/>
<icon src="warning" size="md" variant="warning"/>
<icon src="settings" color="#FF0000"/>  <!-- Custom color -->
```

**C++ API:**
```cpp
ui_icon_set_source(icon, "check");
ui_icon_set_size(icon, "md");
ui_icon_set_variant(icon, "success");
```

### Status Indicators

#### ui_spinner
Indeterminate loading spinner with themed arc color.

Spinner sizes are **responsive** - the pixel values vary by screen height breakpoint:

| Size | Small (391-460) | Medium (461-550) | Large (551-700) | XLarge (>700) |
|------|-----------------|-------------------|-----------------|---------------|
| `xs` | 12px / 2px arc | 14px / 2px arc | 16px / 2px arc | 16px / 2px arc |
| `sm` | 16px / 2px arc | 18px / 2px arc | 20px / 2px arc | 20px / 2px arc |
| `md` | 24px / 2px arc | 28px / 3px arc | 32px / 3px arc | 32px / 3px arc |
| `lg` | 48px / 3px arc | 56px / 4px arc | 64px / 4px arc | 64px / 4px arc |

> Note: _xlarge falls back to _large unless explicitly defined. _tiny falls back to _small.

```xml
<spinner size="lg"/>
<spinner size="md" align="center"/>
```

Uses `theme_core_get_spinner_style()` → primary accent color.

#### ui_severity_card
Status card with severity-colored border:

| Severity | Border Color |
|----------|-------------|
| `info` | Info blue |
| `success` | Success green |
| `warning` | Warning amber |
| `error` | Danger red |

```xml
<ui_severity_card severity="warning">
  <text_body text="Nozzle temperature is high"/>
</ui_severity_card>
```

Uses `theme_core_get_severity_*_style()` functions.

### Layout

#### Dividers

```xml
<divider_horizontal/>  <!-- Full-width horizontal line -->
<divider_vertical/>    <!-- Full-height vertical line -->
```

Use `border` color from theme.

---

## XML Usage Examples

### Using Color Tokens

```xml
<!-- In attributes -->
<lv_obj style_bg_color="#card_bg" style_border_color="#border"/>

<!-- With bind_style for reactive updates -->
<lv_obj bind_style="card_bg_style"/>
```

### Using Spacing Tokens

```xml
<lv_obj style_pad_all="#space_lg" style_pad_gap="#space_md"/>

<!-- Flex container with responsive gap -->
<lv_obj style_flex_flow="row" style_pad_gap="#space_sm">
  <ui_button text="A"/>
  <ui_button text="B"/>
</lv_obj>
```

### Using Pre-Themed Widgets

```xml
<ui_card width="100%" height="content">
  <text_heading text="Temperature"/>
  <text_body bind_text="nozzle_temp_subject"/>
  <ui_button variant="primary" text="Heat" icon="fire"/>
</ui_card>
```

### Semantic Widget Composition

```xml
<ui_dialog>
  <text_heading text="Confirm Action"/>
  <divider_horizontal/>
  <text_body text="Are you sure you want to proceed?"/>
  <lv_obj style_flex_flow="row" style_pad_gap="#space_md">
    <ui_button variant="ghost" text="Cancel"/>
    <ui_button variant="danger" text="Delete"/>
  </lv_obj>
</ui_dialog>
```

---

## C++ Integration

### When to Use theme_core Styles Directly

Use `theme_core_get_*_style()` when creating **custom widgets** that need reactive theming:

```cpp
// In your custom widget's create handler
lv_style_t* card_style = theme_core_get_card_style();
if (card_style) {
    lv_obj_remove_style(obj, nullptr, LV_PART_MAIN);  // Remove LVGL defaults
    lv_obj_add_style(obj, card_style, LV_PART_MAIN);  // Apply shared style
}
```

### Available Style Getters

**Surfaces:**
- `theme_core_get_card_style()` - Card background
- `theme_core_get_dialog_style()` - Dialog/modal background

**Text:**
- `theme_core_get_text_style()` - Primary text
- `theme_core_get_text_muted_style()` - Secondary text
- `theme_core_get_text_subtle_style()` - Tertiary text

**Icons (9 variants):**
- `theme_core_get_icon_text_style()`
- `theme_core_get_icon_muted_style()`
- `theme_core_get_icon_primary_style()`
- `theme_core_get_icon_secondary_style()`
- `theme_core_get_icon_tertiary_style()`
- `theme_core_get_icon_success_style()`
- `theme_core_get_icon_warning_style()`
- `theme_core_get_icon_danger_style()`
- `theme_core_get_icon_info_style()`

**Buttons (7 variants):**
- `theme_core_get_button_primary_style()`
- `theme_core_get_button_secondary_style()`
- `theme_core_get_button_danger_style()`
- `theme_core_get_button_success_style()`
- `theme_core_get_button_warning_style()`
- `theme_core_get_button_tertiary_style()`
- `theme_core_get_button_ghost_style()`

**Status:**
- `theme_core_get_spinner_style()`
- `theme_core_get_severity_info_style()`
- `theme_core_get_severity_success_style()`
- `theme_core_get_severity_warning_style()`
- `theme_core_get_severity_danger_style()`

**Contrast helpers:**
- `theme_core_get_text_for_dark_bg()` - Light text color
- `theme_core_get_text_for_light_bg()` - Dark text color

### When to Use theme_manager Tokens

Use `theme_manager_get_*()` when you need **values** for dynamic styling:

```cpp
// Get color for custom drawing
lv_color_t primary = theme_manager_get_color("primary");

// Get spacing for custom layout
int32_t padding = theme_manager_get_spacing("space_lg");

// Get font for custom text
const lv_font_t* font = theme_manager_get_font("font_body");
```

### Listening for Theme Changes

For widgets that need custom update logic:

```cpp
// In widget create
lv_obj_add_event_cb(obj, style_changed_cb, LV_EVENT_STYLE_CHANGED, user_data);

// Callback
static void style_changed_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    // Re-apply custom styling based on new theme colors
    update_my_custom_colors(obj);
}
```

---

## Quick Reference

### Adding a New Themed Widget

1. **Create shared style** in `theme_core.c`:
   - Add `lv_style_t my_style_` to `helix_theme_t`
   - Initialize in `theme_core_init()`
   - Update in `theme_core_update_colors()` and `theme_core_preview_colors()`
   - Add getter `theme_core_get_my_style()`

2. **Apply style** in widget create handler:
   ```cpp
   lv_obj_remove_style(obj, nullptr, LV_PART_MAIN);
   lv_obj_add_style(obj, theme_core_get_my_style(), LV_PART_MAIN);
   ```

3. **Register widget** with XML parser:
   ```cpp
   lv_xml_register_widget("my_widget", my_widget_create, my_widget_apply);
   ```

### Adding a New Color Token

1. Add to theme JSON files (`themes/*.json`):
   ```json
   "dark": { "my_color": "#hexvalue" },
   "light": { "my_color": "#hexvalue" }
   ```

2. If semantic (part of 16-color palette), add to `ModePalette` in `theme_loader.h`

3. Reference in XML: `style_bg_color="#my_color"`

### Adding Responsive Spacing

1. Add triplet to `ui_xml/globals.xml`:
   ```xml
   <px name="my_space_small" value="8"/>
   <px name="my_space_medium" value="12"/>
   <px name="my_space_large" value="16"/>
   ```

2. Use in XML: `style_pad_all="#my_space"`

3. Use in C++: `theme_manager_get_spacing("my_space")`

---

## File Reference

| File | Purpose |
|------|---------|
| `src/theme_core.c` | Shared lv_style_t objects, init/update/preview |
| `include/theme_core.h` | Style getter function declarations |
| `src/ui/theme_manager.cpp` | Token system, responsive constants, theme loading |
| `include/theme_manager.h` | Token lookup API |
| `ui_xml/globals.xml` | Spacing, font, and icon token definitions |
| `themes/*.json` | Theme color definitions |
