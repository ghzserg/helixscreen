# Theme System QA

## Status: GLOBAL THEME SYSTEM DONE - Theme Edit Panel & Individual QA Remain

## Session 2026-01-27 PM - Global Border/Radius via LVGL Theme

**Border Properties Now Global (DONE):**
- Added `border_opacity` parameter to `theme_core_init`, `theme_core_update_colors`, `theme_core_preview_colors`
- `button_style` and `input_bg_style` now set: `border_width`, `border_color`, `border_opa`, `radius`
- Affects ALL buttons, textareas, dropdowns, rollers, spinboxes globally via LVGL theme system
- No more hardcoding border properties in XML!

**ui_card Component Defaults (DONE):**
- Border: width, color, opacity from theme
- Padding: `space_md` from theme
- Pad gap: `space_md` from theme
- Radius: `card_radius` from theme (was incorrectly using `border_radius` token)
- Background: `card_bg` from theme

**ChatGPT Theme:**
- Set `border_opacity: 255` for visible borders (was 60, too faint)

**Theme Preview XML Cleanup (DONE):**
- Converted preview cards from `lv_obj` to `ui_card`
- Removed redundant `style_radius` from all buttons/dropdowns/textareas
- Removed redundant `style_border_width`, `style_border_color` from inputs
- Removed redundant `style_text_color="#text_muted"` from `text_small` (it's the default)
- Removed redundant `style_text_color="#text"` from `text_button` (uses auto-contrast)

**Commits:**
- `ae097e82` - feat(theme): apply border properties globally via LVGL theme system
- `f5415511` - refactor(theme-preview): remove redundant hardcoded defaults

## Next: Theme Edit Panel Issues

### Theme Editor Bugs (PRIORITY)
- User reported border color showing as "gold" in edit panel but actual value is purple-gray
- Need to investigate color display/picker issues
- Sliders for border_radius, border_width, border_opacity may not be updating preview correctly

### Shadow System (Not Started)
- `shadow_intensity` is dead code - stored in JSON, shown in editor, never applied
- "Raised" elements (modals, dialogs, dropdowns) should have configurable drop shadows

## Completed Items (Archived)

<details>
<summary>Session 2026-01-27 Morning - Knob/Icon/Dropdown Fixes</summary>

- Knob color: `more_saturated_color(primary, tertiary)` instead of `brighter_color()`
- Icon accent: `more_saturated_color(primary, secondary)`
- Dropdown selection highlight: all state combinations (CHECKED, PRESSED, etc.)
- DRY refactor: `theme_get_knob_color()`, `theme_get_accent_color()`, tree walker helpers
</details>

<details>
<summary>Session 2026-01-26 - Initial Theme Preview Work</summary>

- Kanagawa secondary/tertiary swap
- Checkbox theming
- Header bar dual button support
- Theme preview UI improvements
- `text_subtle` fixes across 12 themes
- Switch OFF knob contrast
- Disabled button text styling
</details>

## QA Progress

### Themes Verified
- [x] ayu
- [x] catppuccin (both modes)
- [x] chatgpt (knobs/dropdowns use saturation-based accent)

### Remaining Themes
- [ ] dracula (dark only) ← IN PROGRESS
- [ ] everforest
- [ ] gruvbox
- [ ] material-design
- [ ] nord
- [ ] onedark
- [ ] rose-pine
- [ ] solarized
- [ ] tokyonight
- [ ] yami (dark only)

## Test Command

```bash
HELIX_THEME=dracula ./build/bin/helix-screen --test -v -p theme
```
