# Dynamic Theming System Design

**Date:** 2026-01-21
**Status:** Approved

## Overview

Replace hardcoded Nord color constants with a dynamic theming system that loads color palettes from JSON files, supports both light/dark modes, and provides an interactive theme editor.

## Goals

1. Enable custom color themes via JSON files
2. Provide live theme preview in settings UI
3. Support both light and dark modes with a single 16-color palette
4. Allow users to create and save custom themes
5. Maintain backward compatibility with existing UI

## Theme File Format

Location: `$HELIXDIR/config/themes/*.json`

```json
{
  "name": "Nord",
  "colors": {
    "bg_darkest": "#2e3440",
    "bg_dark": "#3b4252",
    "bg_dark_highlight": "#434c5e",
    "border_muted": "#4c566a",
    "text_light": "#d8dee9",
    "bg_light": "#e5e9f0",
    "bg_lightest": "#eceff4",
    "accent_highlight": "#8fbcbb",
    "accent_primary": "#88c0d0",
    "accent_secondary": "#81a1c1",
    "accent_tertiary": "#5e81ac",
    "status_error": "#bf616a",
    "status_danger": "#d08770",
    "status_warning": "#ebcb8b",
    "status_success": "#a3be8c",
    "status_special": "#b48ead"
  },
  "border_radius": 12,
  "border_width": 1,
  "border_opacity": 40,
  "shadow_intensity": 0
}
```

### Palette Color Semantics

| Slot | Name | Purpose |
|------|------|---------|
| 0 | `bg_darkest` | Dark mode app background |
| 1 | `bg_dark` | Dark mode cards/surfaces |
| 2 | `bg_dark_highlight` | Selection highlight on dark |
| 3 | `border_muted` | Borders, muted text |
| 4 | `text_light` | Primary text on dark surfaces |
| 5 | `bg_light` | Light mode cards/surfaces |
| 6 | `bg_lightest` | Light mode app background |
| 7 | `accent_highlight` | Subtle highlights, hover states |
| 8 | `accent_primary` | Primary accent, links |
| 9 | `accent_secondary` | Secondary accent |
| 10 | `accent_tertiary` | Tertiary accent |
| 11 | `status_error` | Error, danger (red) |
| 12 | `status_danger` | Danger, attention (orange) |
| 13 | `status_warning` | Warning, caution (yellow) |
| 14 | `status_success` | Success, positive (green) |
| 15 | `status_special` | Special, unusual (purple) |

### Non-Color Properties

| Property | Type | Description |
|----------|------|-------------|
| `border_radius` | int | Corner roundness in pixels (0 = sharp, 12 = soft) |
| `border_width` | int | Default border width in pixels |
| `border_opacity` | int | Border opacity (0-255) |
| `shadow_intensity` | int | Shadow strength (0 = disabled, useful for embedded performance) |

## Semantic Color Mapping

Palette colors map to semantic colors used in XML/C++. Mapping is hardcoded in C++.

### Background Colors

| Semantic Color | Dark Mode | Light Mode |
|----------------|-----------|------------|
| `app_bg_color` | bg_darkest (0) | bg_lightest (6) |
| `card_bg` | bg_dark (1) | bg_light (5) |
| `selection_highlight` | bg_dark_highlight (2) | text_light (4) |

### Text Colors

| Semantic Color | Dark Mode | Light Mode |
|----------------|-----------|------------|
| `text_primary` | bg_lightest (6) | bg_darkest (0) |
| `text_secondary` | text_light (4) | border_muted (3) |
| `header_text` | bg_light (5) | bg_dark (1) |

### Border/Muted

| Semantic Color | Dark Mode | Light Mode |
|----------------|-----------|------------|
| `theme_grey` | border_muted (3) | text_light (4) |

### Accents (same in both modes)

| Semantic Color | Palette |
|----------------|---------|
| `primary_color` | accent_primary (8) |
| `secondary_color` | accent_secondary (9) |
| `tertiary_color` | accent_tertiary (10) |
| `highlight_color` | accent_highlight (7) |

### Status Colors (same in both modes)

| Semantic Color | Palette |
|----------------|---------|
| `error_color` | status_error (11) |
| `danger_color` | status_danger (12) |
| `attention_color` | status_warning (13) |
| `success_color` | status_success (14) |
| `special_color` | status_special (15) |
| `info_color` | accent_primary (8) |

## Architecture

### Theme Loading (Startup)

1. Read `/display/theme` from `config.json` (default: `"nord"`)
2. Load `$HELIXDIR/config/themes/{name}.json`
3. If missing, write default `nord.json` and use it
4. Parse JSON: 16 colors + 4 properties
5. Register palette colors via `lv_xml_register_const()`
6. Apply hardcoded semantic mapping (with `_light`/`_dark` variants)
7. Pass non-color properties to theme init

### Live Preview

Uses extended `helix_theme_update_colors()` to update styles in-place:

1. User adjusts color/property in editor
2. Call `helix_theme_update_colors()` with preview values
3. Call `lv_obj_report_style_change(NULL)` to trigger refresh
4. UI updates behind overlay

**Note:** Previous attempts at live dark mode toggle had issues. May need debugging. Fallback: preview overlay with sample widgets.

### File Operations

**Theme discovery:**
- Scan `$HELIXDIR/config/themes/*.json`
- Parse each file's `"name"` field for dropdown display
- Return list of `{filename, display_name}` pairs

**Theme saving:**
1. Collect editor state (colors + properties)
2. If "Save as New": prompt for theme name, sanitize to filename
3. Handle filename collision by appending number
4. Write JSON to themes directory
5. Update `/display/theme` in config.json
6. Set `restart_pending = true`

**Default theme creation:**
- On first run or if `nord.json` missing, write embedded Nord defaults

## UI Components

### Theme Selector

- Dropdown in display settings
- Lists all discovered themes by display name
- Selection updates `/display/theme` config
- Shows "Requires restart" note

### Theme Editor

Extends existing `theme_settings_overlay.xml`:

**Existing (keep):**
- Color swatch grid showing all 16 palette colors

**Add:**
- Make swatches clickable → open color picker overlay
- Property sliders: border_radius, border_width, border_opacity, shadow_intensity
- Action buttons: Save, Save as New, Revert
- Dirty state tracking with visual indicator

### Dirty State Handling

```
User edits colors/properties → mark theme "dirty"

User selects different theme from dropdown while dirty:
  Show confirmation: "Discard unsaved changes?"
  [Discard] → load new theme, clear dirty
  [Cancel] → revert dropdown selection

User clicks "Revert":
  Reload theme from disk, clear dirty

User clicks "Save as New":
  Show dialog: "Theme Name: [________]"
  Save to new file, clear dirty

User exits overlay while dirty:
  Same confirmation pattern
```

## File Changes

### New Files

| File | Purpose |
|------|---------|
| `src/ui/theme_loader.cpp` | JSON parsing, file discovery, load/save |
| `include/theme_loader.h` | Theme struct, public API |
| `$HELIXDIR/config/themes/nord.json` | Default theme (created on first run) |

### Modified Files

| File | Changes |
|------|---------|
| `globals.xml` | Remove `nord0`-`nord15` definitions |
| `ui_theme.cpp` | Call theme_loader at init, apply semantic mapping |
| `helix_theme.c` | Extend `helix_theme_update_colors()` for full palette + properties |
| `settings_manager.cpp` | Add theme name get/set, remove hardcoded preset arrays |
| `ui_settings_display.cpp` | Wire up theme selector dropdown |
| `theme_settings_overlay.xml` | Add property sliders, action buttons |
| `ThemeSettingsOverlay.cpp` | Color picker integration, save/revert, live preview |

## Risks & Considerations

1. **Live preview reliability:** Previous issues with `ui_theme_toggle_dark_mode()`. May need debugging or fallback approach.

2. **LVGL constant limitation:** Cannot update `lv_xml_register_const()` at runtime. Live preview must bypass constant system and update styles directly.

3. **Shadow performance:** Shadow rendering may be expensive on embedded hardware. Default to 0, document that users can increase on capable hardware.

4. **Theme validation:** Need to validate JSON has all required fields. Missing colors should fall back to Nord defaults rather than crash.

## Future Considerations

- Theme import/export (share themes between devices)
- Theme marketplace / community themes
- Per-panel theme overrides
- Animated theme transitions
