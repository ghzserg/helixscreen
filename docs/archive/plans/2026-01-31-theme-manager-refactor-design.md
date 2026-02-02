# ThemeManager Refactor Design

## Status: APPROVED

**Created:** 2026-01-31
**Goal:** Consolidate theme_core.c + theme_manager.cpp into unified C++ ThemeManager class, eliminating DRY violations and technical debt.

---

## Problem Statement

Current theme system has ~5,138 lines across 4 files with significant duplication:

| Issue | Impact |
|-------|--------|
| Triple-duplicated style setup (init/update/preview) | ~720 lines → ~210 |
| 40+ copy-paste getter functions | Maintenance burden |
| C/C++ split with no benefit | Awkward data sharing |
| Scattered constant registration (3 similar functions) | Hard to extend |
| Duplicated color math in C and C++ | Bug risk |

**Target:** ~2,500 lines → ~800-1,000 lines

---

## Design

### Core Data Model

```cpp
enum class StyleRole {
    Card, Dialog, ObjBase, InputBg, Disabled, Pressed, Focused,
    TextPrimary, TextMuted, TextSubtle,
    IconText, IconPrimary, IconSecondary, IconTertiary,
    IconInfo, IconSuccess, IconWarning, IconDanger,
    Button, ButtonPrimary, ButtonSecondary, ButtonTertiary,
    ButtonDanger, ButtonGhost, ButtonDisabled, ButtonPressed,
    SeverityInfo, SeveritySuccess, SeverityWarning, SeverityDanger,
    Dropdown, Checkbox, Switch, Slider, Spinner, Arc,
    COUNT
};

struct StyleEntry {
    StyleRole role;
    lv_style_t style;
    void (*configure)(lv_style_t* style, const Palette& palette);
};
```

### Class API

```cpp
class ThemeManager {
public:
    static ThemeManager& instance();

    // Lifecycle
    void init();
    void shutdown();

    // Theme switching
    void set_dark_mode(bool dark);
    bool is_dark_mode() const;
    void toggle_dark_mode();

    // Theme files
    void load_theme(const std::string& filename);
    std::vector<ThemeInfo> list_available_themes();
    const ThemeData& current_theme() const;

    // Color lookup
    lv_color_t get_color(std::string_view name);
    lv_color_t parse_color(std::string_view hex);

    // Style access
    lv_style_t* get_style(StyleRole role);

    // Constants
    int get_spacing(std::string_view name);
    const lv_font_t* get_font(std::string_view name);
    void register_constants_from_xml(const char* xml_content);

    // Preview (theme editor)
    void preview_palette(const Palette& palette, lv_obj_t* subtree);
    void cancel_preview();

private:
    std::array<StyleEntry, static_cast<size_t>(StyleRole::COUNT)> styles_;
    Palette current_palette_;
    ThemeData current_theme_;
    bool dark_mode_ = true;
    lv_theme_t lvgl_theme_;
    std::unordered_map<std::string, Constant> constants_;

    void apply_palette(const Palette& palette);
    void refresh_widgets();
};
```

### DRY Elimination: Configure Functions

Each style defines its configuration once:

```cpp
namespace style_configs {
    void configure_card(lv_style_t* s, const Palette& p) {
        lv_style_set_bg_color(s, p.card_bg);
        lv_style_set_bg_opa(s, LV_OPA_COVER);
        lv_style_set_radius(s, p.border_radius);
        lv_style_set_border_color(s, p.border);
        lv_style_set_border_width(s, p.border_width);
    }
    // ... one per StyleRole
}

void ThemeManager::apply_palette(const Palette& palette) {
    for (auto& entry : styles_) {
        lv_style_reset(&entry.style);
        entry.configure(&entry.style, palette);
    }
}
```

Init, update, and preview all call `apply_palette()` - no duplication.

### Unified Constants System

```cpp
struct Constant {
    enum class Type { Color, ColorPair, Spacing, Font };
    Type type;
    std::string name;
    lv_color_t color;
    lv_color_t color_light, color_dark;
    std::array<int, 3> spacing;  // small, medium, large
    std::array<const lv_font_t*, 3> fonts;
};

void register_constant(std::string_view name, std::string_view value) {
    // Detect suffix pattern, register appropriate type
}
```

---

## Migration Strategy

### Phase 1: New Implementation
- Create `theme_manager.h` / `theme_manager.cpp` with new class
- Implement core functionality with TDD
- No breaking changes yet

### Phase 2: Compatibility Layer
- Add shims mapping old API → new API
- `theme_core_get_card_style()` → `ThemeManager::instance().get_style(StyleRole::Card)`
- All existing code continues to work

### Phase 3: Migrate Callers
- Update ~56 files to use new API
- Incremental, tests pass at each step
- Can parallelize across files

### Phase 4: Cleanup
- Delete `theme_core.c`, `theme_core.h`
- Remove compatibility shims
- Final cleanup pass

---

## File Changes

| Action | Files |
|--------|-------|
| Delete | `src/theme_core.c`, `include/theme_core.h` |
| Replace | `src/ui/theme_manager.cpp`, `include/theme_manager.h` |
| Keep | `src/ui/theme_loader.cpp`, `include/theme_loader.h` |
| Update | ~56 caller files |

---

## Testing Strategy

- TDD: Write tests before implementation
- Existing tests: `test_theme_core_styles.cpp`, `test_ui_theme.cpp` must pass
- New tests for StyleRole enum, configure functions, unified constants
- Visual verification in light/dark modes

---

## Success Criteria

- [ ] All existing theme tests pass
- [ ] New tests for refactored components
- [ ] ~60% line count reduction
- [ ] No duplicate style setup code
- [ ] Single `get_style(role)` replaces 40+ getters
- [ ] Visual verification: UI looks identical before/after
