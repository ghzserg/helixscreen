# Plan: App-Wide Reactive Theme System

## Problem

Widgets set inline styles at creation time. When theme/mode changes, these values are frozen:

```cpp
// ui_card.cpp - styles baked in at create time, never updated
lv_obj_set_style_bg_color(obj, theme_manager_get_color("card_bg"), LV_PART_MAIN);
```

Result: Theme preview is brittle (350 lines of `lv_obj_find_by_name()`), dark/light toggle doesn't work properly.

## Solution: Shared Styles in Widget Implementations

Replace inline style calls with **shared `lv_style_t` references**. When theme changes, update the shared styles -> `lv_obj_report_style_change()` -> LVGL auto-refreshes.

```cpp
// BEFORE: inline style (frozen at create time)
lv_obj_set_style_bg_color(obj, theme_manager_get_color("card_bg"), LV_PART_MAIN);

// AFTER: shared style (reactive to theme changes)
lv_obj_add_style(obj, theme_get_card_style(), LV_PART_MAIN);
```

**Key insight:** XML files barely change. The fix is in C++ widget implementations.

## Architecture

**Extend existing `helix_theme_t` in `theme_core.c`** - no new files needed.

## Semantic Styles

### Surface Styles
| Style | Properties | Used By |
|-------|------------|---------|
| `card_style_` | bg_color, border_color/width/opa, radius | `ui_card` |
| `dialog_style_` | bg_color, border_color/width/opa, radius, shadow | `ui_dialog`, modals |
| `app_bg_style_` | bg_color | overlay backgrounds, panels |
| `elevated_bg_style_` | bg_color (for inputs) | input backgrounds |

### Text Styles
| Style | Properties | Used By |
|-------|------------|---------|
| `text_primary_style_` | text_color (text) | `text_body` |
| `text_muted_style_` | text_color (text_muted) | `text_heading`, `text_small`, labels |

### Status Styles
| Style | Properties | Used By |
|-------|------------|---------|
| `success_style_` | text_color OR bg_color | status icons, badges |
| `warning_style_` | text_color OR bg_color | status icons, badges |
| `danger_style_` | text_color OR bg_color | status icons, badges |
| `info_style_` | text_color OR bg_color | status icons, badges |

### Button Styles (extend existing)
| Style | Properties | Used By |
|-------|------------|---------|
| `btn_primary_style_` | bg_color (primary) | buttons with variant="primary" |
| `btn_secondary_style_` | bg_color (secondary) | buttons with variant="secondary" |
| `btn_danger_style_` | bg_color (danger) | buttons with variant="danger" |

## Phases

### Phase 1: Extend theme_core + core widgets
**Files to modify:**
- `src/theme_core.c` - Add `card_style_`, `dialog_style_`, `text_primary_style_`, `text_muted_style_` to `helix_theme_t`; update them in `theme_core_update_colors()`
- `include/theme_core.h` - Add getter functions
- `src/ui/ui_card.cpp` - Replace inline styles with shared styles
- `src/ui/ui_dialog.cpp` - Replace inline styles with shared styles
- `src/ui/ui_text.cpp` - Add text styles to create handlers

### Phase 2: Icons + status colors + ui_button
**Files to modify:**
- `src/theme_core.c` - Add icon styles, severity styles, button variant styles, spinner style
- `include/theme_core.h` - Add getter functions for new styles
- `src/ui/ui_icon.cpp` - Refactor variant naming + use shared styles
- `src/ui/ui_severity_card.cpp` - Use severity styles
- `src/ui/ui_spinner.cpp` - Use spinner style

**Files to create:**
- `src/ui/ui_button.cpp` + `include/ui_button.h` - NEW semantic button widget

### Phase 3: Remove brittle preview code
**Files to modify:**
- `src/ui/theme_manager.cpp` - delete `theme_manager_refresh_preview_elements()` (~350 lines)
- `src/ui/ui_settings_display.cpp` - simplify preview handlers

## Icon Variant Refactor

| Variant | Color Token | Notes |
|---------|-------------|-------|
| `text` | `text` | Primary text (rename from `primary`) |
| `muted` | `text_muted` | De-emphasized (rename from `secondary`) |
| `primary` | `primary` | Primary accent (rename from `accent`) |
| `secondary` | `secondary` | Secondary accent (NEW) |
| `tertiary` | `tertiary` | Tertiary accent (NEW) |
| `success` | `success` | Green |
| `warning` | `warning` | Orange |
| `danger` | `danger` | Red (rename from `error`) |
| `info` | `info` | Blue |
| `contrast` | *computed* | Auto-contrast from parent bg (NEW) |
| `disabled` | `text` @ 50% | Opacity modifier |

## Expected Outcomes

1. **Runtime dark/light toggle works everywhere** - cards, dialogs, text, icons all update
2. **Theme preview is trivial** - just call `theme_styles_update(preview_palette)`
3. **~350 lines deleted** - no more widget lookups
4. **XML unchanged or cleaner** - remove redundant inline styles
5. **Adding themed widgets is simple** - just add shared style in create handler
