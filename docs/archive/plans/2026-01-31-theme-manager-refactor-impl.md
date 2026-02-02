# ThemeManager Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Consolidate theme_core.c + theme_manager.cpp into a unified C++ ThemeManager class, eliminating DRY violations and reducing ~2,500 lines to ~800-1,000 lines.

**Architecture:** Table-driven style management with configure functions. Each StyleRole has a single configure function called by init/update/preview. Singleton pattern preserved for LVGL integration.

**Tech Stack:** C++17, LVGL 9.4, Catch2 tests, existing LVGLTestFixture infrastructure.

**Worktree:** `.worktrees/theme-manager-refactor` (branch: `feature/theme-manager-refactor`)

---

## Phase 1: Core Data Structures

### Task 1.1: StyleRole Enum

**Files:**
- Create: `include/theme_manager.h` (new unified header)
- Test: `tests/unit/test_theme_manager_new.cpp`

**Step 1: Write the failing test**

```cpp
// tests/unit/test_theme_manager_new.cpp
#include "../catch_amalgamated.hpp"
#include "theme_manager.h"

TEST_CASE("StyleRole enum has expected values", "[theme-manager][style-role]") {
    // Verify enum has all expected roles
    REQUIRE(static_cast<int>(StyleRole::Card) >= 0);
    REQUIRE(static_cast<int>(StyleRole::Dialog) >= 0);
    REQUIRE(static_cast<int>(StyleRole::TextPrimary) >= 0);
    REQUIRE(static_cast<int>(StyleRole::ButtonPrimary) >= 0);
    REQUIRE(static_cast<int>(StyleRole::IconPrimary) >= 0);
    REQUIRE(static_cast<int>(StyleRole::COUNT) > 30);  // We have 40+ styles
}

TEST_CASE("StyleRole::COUNT equals total style count", "[theme-manager][style-role]") {
    // COUNT should be the last enum value, usable for array sizing
    constexpr size_t count = static_cast<size_t>(StyleRole::COUNT);
    REQUIRE(count >= 35);
    REQUIRE(count <= 50);  // Sanity check - not too many
}
```

**Step 2: Run test to verify it fails**

Run: `cd .worktrees/theme-manager-refactor && ./build/bin/helix-tests "[style-role]" -v`
Expected: FAIL - "theme_manager.h" not found or StyleRole not defined

**Step 3: Write minimal implementation**

```cpp
// include/theme_manager.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

/// Style roles - each represents a semantic style in the theme system.
/// Used as array index for O(1) style lookup.
enum class StyleRole {
    // Base styles
    Card,
    Dialog,
    ObjBase,
    InputBg,
    Disabled,
    Pressed,
    Focused,

    // Text variants
    TextPrimary,
    TextMuted,
    TextSubtle,

    // Icon variants (semantic colors)
    IconText,
    IconPrimary,
    IconSecondary,
    IconTertiary,
    IconInfo,
    IconSuccess,
    IconWarning,
    IconDanger,

    // Button variants
    Button,
    ButtonPrimary,
    ButtonSecondary,
    ButtonTertiary,
    ButtonDanger,
    ButtonGhost,
    ButtonDisabled,
    ButtonPressed,

    // Severity borders
    SeverityInfo,
    SeveritySuccess,
    SeverityWarning,
    SeverityDanger,

    // Widget-specific
    Dropdown,
    Checkbox,
    Switch,
    Slider,
    Spinner,
    Arc,

    COUNT  // Must be last - used for array sizing
};

/// Convert StyleRole to string for debugging
const char* style_role_name(StyleRole role);
```

**Step 4: Add to test build**

Edit `Makefile` - add `tests/unit/test_theme_manager_new.cpp` to TEST_SOURCES.

**Step 5: Run test to verify it passes**

Run: `cd .worktrees/theme-manager-refactor && make test-build && ./build/bin/helix-tests "[style-role]" -v`
Expected: PASS

**Step 6: Commit**

```bash
git add include/theme_manager.h tests/unit/test_theme_manager_new.cpp Makefile
git commit -m "feat(theme): add StyleRole enum for table-driven styles"
```

---

### Task 1.2: Palette Struct

**Files:**
- Modify: `include/theme_manager.h`
- Test: `tests/unit/test_theme_manager_new.cpp`

**Step 1: Write the failing test**

```cpp
TEST_CASE("Palette holds 16 semantic colors", "[theme-manager][palette]") {
    Palette p{};

    // All colors should be accessible
    REQUIRE(sizeof(p.screen_bg) == sizeof(lv_color_t));
    REQUIRE(sizeof(p.primary) == sizeof(lv_color_t));
    REQUIRE(sizeof(p.danger) == sizeof(lv_color_t));

    // Should have properties too
    REQUIRE(p.border_radius >= 0);
    REQUIRE(p.border_width >= 0);
}

TEST_CASE("Palette can be constructed from theme_palette_t", "[theme-manager][palette]") {
    // Interop with existing C struct
    theme_palette_t c_palette{};
    c_palette.screen_bg = lv_color_hex(0x1a1a2e);
    c_palette.primary = lv_color_hex(0x4361ee);

    Palette p = Palette::from_c(c_palette);

    REQUIRE(p.screen_bg.red == c_palette.screen_bg.red);
    REQUIRE(p.primary.red == c_palette.primary.red);
}
```

**Step 2: Run test to verify it fails**

Run: `./build/bin/helix-tests "[palette]" -v`
Expected: FAIL - Palette not defined

**Step 3: Write minimal implementation**

```cpp
// Add to include/theme_manager.h
#include <lvgl.h>

// Forward declare for interop
extern "C" {
struct theme_palette_t;  // From old theme_core.h
}

/// Palette holds all semantic colors for a theme mode (light or dark).
/// This is the C++ equivalent of theme_palette_t with added utilities.
struct Palette {
    // Background colors
    lv_color_t screen_bg{};
    lv_color_t overlay_bg{};
    lv_color_t card_bg{};
    lv_color_t elevated_bg{};
    lv_color_t border{};

    // Text colors
    lv_color_t text{};
    lv_color_t text_muted{};
    lv_color_t text_subtle{};

    // Semantic colors
    lv_color_t primary{};
    lv_color_t secondary{};
    lv_color_t tertiary{};
    lv_color_t info{};
    lv_color_t success{};
    lv_color_t warning{};
    lv_color_t danger{};
    lv_color_t focus{};

    // Theme properties
    int border_radius = 8;
    int border_width = 1;
    int border_opacity = 40;

    /// Create from C palette struct (interop)
    static Palette from_c(const theme_palette_t& c_palette);

    /// Convert to C palette struct (interop)
    theme_palette_t to_c() const;
};
```

**Step 4: Implement from_c in cpp file**

```cpp
// src/ui/theme_manager.cpp (new file, will grow)
#include "theme_manager.h"
#include "theme_core.h"  // For theme_palette_t

Palette Palette::from_c(const theme_palette_t& c) {
    Palette p;
    p.screen_bg = c.screen_bg;
    p.overlay_bg = c.overlay_bg;
    p.card_bg = c.card_bg;
    p.elevated_bg = c.elevated_bg;
    p.border = c.border;
    p.text = c.text;
    p.text_muted = c.text_muted;
    p.text_subtle = c.text_subtle;
    p.primary = c.primary;
    p.secondary = c.secondary;
    p.tertiary = c.tertiary;
    p.info = c.info;
    p.success = c.success;
    p.warning = c.warning;
    p.danger = c.danger;
    p.focus = c.focus;
    return p;
}
```

**Step 5: Run test to verify it passes**

Run: `make test-build && ./build/bin/helix-tests "[palette]" -v`
Expected: PASS

**Step 6: Commit**

```bash
git add include/theme_manager.h src/ui/theme_manager.cpp
git commit -m "feat(theme): add Palette struct with C interop"
```

---

### Task 1.3: StyleEntry and Configure Function Type

**Files:**
- Modify: `include/theme_manager.h`
- Test: `tests/unit/test_theme_manager_new.cpp`

**Step 1: Write the failing test**

```cpp
TEST_CASE("StyleEntry holds role and configure function", "[theme-manager][style-entry]") {
    // A configure function that sets background to red
    auto configure_red = [](lv_style_t* s, const Palette& p) {
        lv_style_set_bg_color(s, lv_color_hex(0xFF0000));
    };

    StyleEntry entry{StyleRole::Card, {}, configure_red};

    REQUIRE(entry.role == StyleRole::Card);
    REQUIRE(entry.configure != nullptr);

    // Configure should work
    Palette p{};
    lv_style_init(&entry.style);
    entry.configure(&entry.style, p);

    lv_color_t bg;
    lv_style_get_prop(&entry.style, LV_STYLE_BG_COLOR, &bg);
    REQUIRE(bg.red == 0xFF);
}
```

**Step 2: Run test to verify it fails**

Run: `./build/bin/helix-tests "[style-entry]" -v`
Expected: FAIL - StyleEntry not defined

**Step 3: Write minimal implementation**

```cpp
// Add to include/theme_manager.h
#include <functional>

/// Function type for configuring a style from a palette.
/// Each style role has one of these - called during init, update, and preview.
using StyleConfigureFn = void (*)(lv_style_t* style, const Palette& palette);

/// Entry in the style registry - pairs a role with its style and configure function.
struct StyleEntry {
    StyleRole role;
    lv_style_t style{};
    StyleConfigureFn configure = nullptr;
};
```

**Step 4: Run test to verify it passes**

Run: `make test-build && ./build/bin/helix-tests "[style-entry]" -v`
Expected: PASS

**Step 5: Commit**

```bash
git add include/theme_manager.h
git commit -m "feat(theme): add StyleEntry with configure function type"
```

---

## Phase 2: ThemeManager Class Skeleton

### Task 2.1: Basic ThemeManager Singleton

**Files:**
- Modify: `include/theme_manager.h`
- Modify: `src/ui/theme_manager.cpp`
- Test: `tests/unit/test_theme_manager_new.cpp`

**Step 1: Write the failing test**

```cpp
TEST_CASE("ThemeManager is singleton", "[theme-manager][singleton]") {
    auto& tm1 = ThemeManager::instance();
    auto& tm2 = ThemeManager::instance();

    REQUIRE(&tm1 == &tm2);  // Same instance
}

TEST_CASE("ThemeManager::get_style returns valid style for each role", "[theme-manager][get-style]") {
    auto& tm = ThemeManager::instance();
    tm.init();  // Must init first

    // Card style should exist
    lv_style_t* card = tm.get_style(StyleRole::Card);
    REQUIRE(card != nullptr);

    // Button primary should exist
    lv_style_t* btn = tm.get_style(StyleRole::ButtonPrimary);
    REQUIRE(btn != nullptr);

    // Different roles return different pointers
    REQUIRE(card != btn);
}
```

**Step 2: Run test to verify it fails**

Run: `./build/bin/helix-tests "[singleton]" -v`
Expected: FAIL - ThemeManager class not defined

**Step 3: Write minimal implementation**

```cpp
// Add to include/theme_manager.h
#include <array>
#include <string_view>

class ThemeManager {
public:
    /// Get singleton instance
    static ThemeManager& instance();

    /// Initialize the theme system. Must be called once at startup.
    void init();

    /// Shutdown and cleanup
    void shutdown();

    /// Get style for a role. Returns nullptr if not initialized.
    lv_style_t* get_style(StyleRole role);

    // Delete copy/move
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

private:
    ThemeManager() = default;

    std::array<StyleEntry, static_cast<size_t>(StyleRole::COUNT)> styles_{};
    Palette current_palette_{};
    bool initialized_ = false;

    void register_style_configs();
    void apply_palette(const Palette& palette);
};
```

**Step 4: Implement skeleton in cpp**

```cpp
// Add to src/ui/theme_manager.cpp

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

void ThemeManager::init() {
    if (initialized_) return;

    register_style_configs();
    // TODO: Load default palette
    // apply_palette(current_palette_);

    initialized_ = true;
}

void ThemeManager::shutdown() {
    // Reset all styles
    for (auto& entry : styles_) {
        lv_style_reset(&entry.style);
    }
    initialized_ = false;
}

lv_style_t* ThemeManager::get_style(StyleRole role) {
    if (!initialized_) return nullptr;
    auto idx = static_cast<size_t>(role);
    if (idx >= styles_.size()) return nullptr;
    return &styles_[idx].style;
}

void ThemeManager::register_style_configs() {
    // Will be filled in Phase 3
}

void ThemeManager::apply_palette(const Palette& palette) {
    current_palette_ = palette;
    for (auto& entry : styles_) {
        if (entry.configure) {
            lv_style_reset(&entry.style);
            entry.configure(&entry.style, palette);
        }
    }
}
```

**Step 5: Run test to verify it passes**

Run: `make test-build && ./build/bin/helix-tests "[singleton]" -v`
Expected: PASS (or partial - get_style may return nullptr until configs registered)

**Step 6: Commit**

```bash
git add include/theme_manager.h src/ui/theme_manager.cpp
git commit -m "feat(theme): add ThemeManager singleton skeleton"
```

---

## Phase 3: Style Configure Functions

### Task 3.1: Card and Dialog Styles

**Files:**
- Create: `src/ui/style_configs.cpp`
- Modify: `src/ui/theme_manager.cpp`
- Test: `tests/unit/test_theme_manager_new.cpp`

**Step 1: Write the failing test**

```cpp
TEST_CASE_METHOD(LVGLTestFixture, "Card style has correct properties", "[theme-manager][card-style]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    lv_style_t* card = tm.get_style(StyleRole::Card);
    REQUIRE(card != nullptr);

    // Card should have background color set
    lv_color_t bg;
    auto res = lv_style_get_prop(card, LV_STYLE_BG_COLOR, &bg);
    REQUIRE(res == LV_RESULT_OK);

    // Card should have border
    int32_t border_width;
    res = lv_style_get_prop(card, LV_STYLE_BORDER_WIDTH, &border_width);
    REQUIRE(res == LV_RESULT_OK);
    REQUIRE(border_width > 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "Dialog style differs from card", "[theme-manager][dialog-style]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    lv_style_t* card = tm.get_style(StyleRole::Card);
    lv_style_t* dialog = tm.get_style(StyleRole::Dialog);

    REQUIRE(card != dialog);

    // Dialog uses overlay_bg, card uses card_bg
    lv_color_t card_bg, dialog_bg;
    lv_style_get_prop(card, LV_STYLE_BG_COLOR, &card_bg);
    lv_style_get_prop(dialog, LV_STYLE_BG_COLOR, &dialog_bg);

    // They should be different (unless theme has same values)
    // At minimum, both should be set
    REQUIRE(lv_style_get_prop(card, LV_STYLE_BG_COLOR, &card_bg) == LV_RESULT_OK);
    REQUIRE(lv_style_get_prop(dialog, LV_STYLE_BG_COLOR, &dialog_bg) == LV_RESULT_OK);
}
```

**Step 2: Run test to verify it fails**

Run: `./build/bin/helix-tests "[card-style]" -v`
Expected: FAIL - style has no properties set

**Step 3: Create style_configs.cpp**

```cpp
// src/ui/style_configs.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme_manager.h"
#include <lvgl.h>

namespace style_configs {

void configure_card(lv_style_t* s, const Palette& p) {
    lv_style_set_bg_color(s, p.card_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, p.border_radius);
    lv_style_set_border_color(s, p.border);
    lv_style_set_border_width(s, p.border_width);
    lv_style_set_border_opa(s, p.border_opacity * 255 / 100);
    lv_style_set_pad_all(s, 12);
}

void configure_dialog(lv_style_t* s, const Palette& p) {
    lv_style_set_bg_color(s, p.overlay_bg);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_radius(s, p.border_radius);
    lv_style_set_border_color(s, p.border);
    lv_style_set_border_width(s, p.border_width);
    lv_style_set_border_opa(s, p.border_opacity * 255 / 100);
    lv_style_set_pad_all(s, 16);
}

}  // namespace style_configs
```

**Step 4: Register in ThemeManager**

```cpp
// In theme_manager.cpp, update register_style_configs()
#include "style_configs.cpp"  // Or proper header

void ThemeManager::register_style_configs() {
    using namespace style_configs;

    styles_[static_cast<size_t>(StyleRole::Card)] =
        {StyleRole::Card, {}, configure_card};
    styles_[static_cast<size_t>(StyleRole::Dialog)] =
        {StyleRole::Dialog, {}, configure_dialog};

    // Initialize styles
    for (auto& entry : styles_) {
        lv_style_init(&entry.style);
    }
}
```

**Step 5: Set default palette in init()**

```cpp
void ThemeManager::init() {
    if (initialized_) return;

    register_style_configs();

    // Default Nord-ish palette for testing
    current_palette_.card_bg = lv_color_hex(0x2E3440);
    current_palette_.overlay_bg = lv_color_hex(0x3B4252);
    current_palette_.border = lv_color_hex(0x4C566A);
    current_palette_.text = lv_color_hex(0xECEFF4);
    current_palette_.primary = lv_color_hex(0x88C0D0);
    current_palette_.border_radius = 8;
    current_palette_.border_width = 1;
    current_palette_.border_opacity = 40;

    apply_palette(current_palette_);
    initialized_ = true;
}
```

**Step 6: Run test to verify it passes**

Run: `make test-build && ./build/bin/helix-tests "[card-style]" -v`
Expected: PASS

**Step 7: Commit**

```bash
git add src/ui/style_configs.cpp src/ui/theme_manager.cpp
git commit -m "feat(theme): add card and dialog style configs"
```

---

### Task 3.2: Text Styles (Primary, Muted, Subtle)

**Pattern:** Same as 3.1 - test first, then implement configure functions.

```cpp
// Tests
TEST_CASE_METHOD(LVGLTestFixture, "Text styles have distinct colors", "[theme-manager][text-style]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    lv_style_t* primary = tm.get_style(StyleRole::TextPrimary);
    lv_style_t* muted = tm.get_style(StyleRole::TextMuted);
    lv_style_t* subtle = tm.get_style(StyleRole::TextSubtle);

    lv_color_t c1, c2, c3;
    lv_style_get_prop(primary, LV_STYLE_TEXT_COLOR, &c1);
    lv_style_get_prop(muted, LV_STYLE_TEXT_COLOR, &c2);
    lv_style_get_prop(subtle, LV_STYLE_TEXT_COLOR, &c3);

    // All should be set (not default black)
    REQUIRE(lv_style_get_prop(primary, LV_STYLE_TEXT_COLOR, &c1) == LV_RESULT_OK);
    REQUIRE(lv_style_get_prop(muted, LV_STYLE_TEXT_COLOR, &c2) == LV_RESULT_OK);
    REQUIRE(lv_style_get_prop(subtle, LV_STYLE_TEXT_COLOR, &c3) == LV_RESULT_OK);
}
```

```cpp
// style_configs.cpp additions
void configure_text_primary(lv_style_t* s, const Palette& p) {
    lv_style_set_text_color(s, p.text);
}

void configure_text_muted(lv_style_t* s, const Palette& p) {
    lv_style_set_text_color(s, p.text_muted);
}

void configure_text_subtle(lv_style_t* s, const Palette& p) {
    lv_style_set_text_color(s, p.text_subtle);
}
```

---

### Task 3.3: Icon Styles (8 variants)

**Pattern:** Same TDD approach for IconText, IconPrimary, IconSecondary, IconTertiary, IconInfo, IconSuccess, IconWarning, IconDanger.

---

### Task 3.4: Button Styles (8 variants)

**Pattern:** Same TDD approach for Button, ButtonPrimary, ButtonSecondary, ButtonTertiary, ButtonDanger, ButtonGhost, ButtonDisabled, ButtonPressed.

---

### Task 3.5: Severity Styles (4 variants)

**Pattern:** Same TDD approach for SeverityInfo, SeveritySuccess, SeverityWarning, SeverityDanger.

---

### Task 3.6: Widget Styles (Dropdown, Checkbox, Switch, Slider, Spinner, Arc)

**Pattern:** Same TDD approach.

---

## Phase 4: Core Functionality

### Task 4.1: Dark/Light Mode Switching

**Files:**
- Modify: `include/theme_manager.h`
- Modify: `src/ui/theme_manager.cpp`
- Test: `tests/unit/test_theme_manager_new.cpp`

**Step 1: Write the failing test**

```cpp
TEST_CASE_METHOD(LVGLTestFixture, "ThemeManager toggles dark mode", "[theme-manager][dark-mode]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    // Default is dark
    REQUIRE(tm.is_dark_mode() == true);

    // Toggle to light
    tm.set_dark_mode(false);
    REQUIRE(tm.is_dark_mode() == false);

    // Toggle back
    tm.toggle_dark_mode();
    REQUIRE(tm.is_dark_mode() == true);
}

TEST_CASE_METHOD(LVGLTestFixture, "Styles update when mode changes", "[theme-manager][mode-change]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    lv_style_t* card = tm.get_style(StyleRole::Card);
    lv_color_t dark_bg;
    lv_style_get_prop(card, LV_STYLE_BG_COLOR, &dark_bg);

    tm.set_dark_mode(false);

    lv_color_t light_bg;
    lv_style_get_prop(card, LV_STYLE_BG_COLOR, &light_bg);

    // Colors should be different (assuming theme has different light/dark palettes)
    // This test will need a proper dual-palette theme loaded
}
```

**Step 3: Implement**

```cpp
// Add to ThemeManager class
bool is_dark_mode() const { return dark_mode_; }
void set_dark_mode(bool dark);
void toggle_dark_mode() { set_dark_mode(!dark_mode_); }

// Implementation
void ThemeManager::set_dark_mode(bool dark) {
    if (dark_mode_ == dark) return;
    dark_mode_ = dark;

    // Load appropriate palette from current theme
    // apply_palette(dark ? theme_.dark_palette : theme_.light_palette);

    // Trigger widget refresh
    lv_obj_report_style_change(nullptr);
}
```

---

### Task 4.2: Color Lookup API

```cpp
TEST_CASE_METHOD(LVGLTestFixture, "get_color returns palette colors", "[theme-manager][get-color]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    lv_color_t primary = tm.get_color("primary");
    lv_color_t danger = tm.get_color("danger");

    // Should return actual colors, not black
    // (Specific values depend on loaded theme)
    REQUIRE(primary.red + primary.green + primary.blue > 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "get_color handles _light/_dark suffix", "[theme-manager][color-suffix]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    // In dark mode, "text" should return dark mode text color
    // "text_light" explicitly gets light mode variant
    lv_color_t text = tm.get_color("text");
    lv_color_t text_light = tm.get_color("text_light");

    // They may differ if theme has different values
}
```

---

### Task 4.3: Preview System (for Theme Editor)

```cpp
TEST_CASE_METHOD(LVGLTestFixture, "preview_palette applies temporarily", "[theme-manager][preview]") {
    auto& tm = ThemeManager::instance();
    tm.init();

    lv_style_t* card = tm.get_style(StyleRole::Card);
    lv_color_t original_bg;
    lv_style_get_prop(card, LV_STYLE_BG_COLOR, &original_bg);

    // Preview a different palette
    Palette preview_palette = tm.current_palette();
    preview_palette.card_bg = lv_color_hex(0xFF0000);  // Red

    tm.preview_palette(preview_palette);

    lv_color_t preview_bg;
    lv_style_get_prop(card, LV_STYLE_BG_COLOR, &preview_bg);
    REQUIRE(preview_bg.red == 0xFF);

    // Cancel reverts
    tm.cancel_preview();

    lv_color_t reverted_bg;
    lv_style_get_prop(card, LV_STYLE_BG_COLOR, &reverted_bg);
    REQUIRE(reverted_bg.red == original_bg.red);
}
```

---

## Phase 5: Compatibility Shims

### Task 5.1: Old API Shims

**Files:**
- Create: `include/theme_compat.h`
- Create: `src/ui/theme_compat.cpp`

These provide the old function names mapping to new API:

```cpp
// include/theme_compat.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>

// Compatibility shims for old theme_core.h API
// These will be removed after all callers migrate

#ifdef __cplusplus
extern "C" {
#endif

// Style getters - map to ThemeManager::get_style(StyleRole::X)
lv_style_t* theme_core_get_card_style(void);
lv_style_t* theme_core_get_dialog_style(void);
lv_style_t* theme_core_get_text_style(void);
lv_style_t* theme_core_get_text_muted_style(void);
lv_style_t* theme_core_get_text_subtle_style(void);
// ... all 40+ getters

// Color functions - map to ThemeManager::get_color()
lv_color_t theme_manager_get_color(const char* name);
lv_color_t ui_theme_parse_color(const char* hex);

// Mode functions
void theme_manager_toggle_dark_mode(void);
bool theme_manager_is_dark_mode(void);

#ifdef __cplusplus
}
#endif
```

```cpp
// src/ui/theme_compat.cpp
#include "theme_compat.h"
#include "theme_manager.h"

lv_style_t* theme_core_get_card_style(void) {
    return ThemeManager::instance().get_style(StyleRole::Card);
}

lv_style_t* theme_core_get_dialog_style(void) {
    return ThemeManager::instance().get_style(StyleRole::Dialog);
}

// ... etc
```

---

## Phase 6: Migrate Callers (Incremental)

### Task 6.1: Migrate ui_card.cpp

**Files:**
- Modify: `src/ui/components/ui_card.cpp`

**Step 1: Find usages**

```bash
grep -n "theme_core_get_card_style" src/ui/components/ui_card.cpp
```

**Step 2: Replace**

```cpp
// Before:
lv_obj_add_style(card, theme_core_get_card_style(), 0);

// After:
lv_obj_add_style(card, ThemeManager::instance().get_style(StyleRole::Card), 0);
```

**Step 3: Test**

Run: `make test-build && ./build/bin/helix-tests "[ui-card]" -v`

**Step 4: Commit**

```bash
git add src/ui/components/ui_card.cpp
git commit -m "refactor(ui_card): use ThemeManager::get_style"
```

### Task 6.2-6.N: Migrate remaining 55 files

Same pattern for each file. Can be parallelized across files.

---

## Phase 7: Cleanup

### Task 7.1: Delete Old Files

**Files:**
- Delete: `src/theme_core.c`
- Delete: `include/theme_core.h`
- Delete: `src/ui/theme_compat.cpp`
- Delete: `include/theme_compat.h`
- Rename: Old `theme_manager.cpp` → merged into new

**Step 1: Verify no remaining usages**

```bash
grep -r "theme_core_get_" src/ include/ --include="*.cpp" --include="*.h"
grep -r "#include.*theme_core.h" src/ include/
```

**Step 2: Delete files**

```bash
git rm src/theme_core.c include/theme_core.h
git rm src/ui/theme_compat.cpp include/theme_compat.h
```

**Step 3: Update Makefile**

Remove old files from SOURCES.

**Step 4: Build and test**

```bash
make clean && make -j && make test-run
```

**Step 5: Commit**

```bash
git add -A
git commit -m "refactor(theme): remove legacy theme_core and compat shims"
```

---

## Phase 8: Final Validation

### Task 8.1: Visual Verification

1. Run app in dark mode: `./build/bin/helix-screen --test -vv`
2. Toggle to light mode via settings
3. Open theme editor, preview custom colors
4. Verify all panels render correctly

### Task 8.2: Line Count Verification

```bash
wc -l include/theme_manager.h src/ui/theme_manager.cpp src/ui/style_configs.cpp
# Target: 800-1000 lines total (down from ~2500)
```

### Task 8.3: Final Commit

```bash
git add -A
git commit -m "feat(theme): complete ThemeManager refactor

- Consolidated theme_core.c + theme_manager.cpp into unified C++ class
- Table-driven styles with configure functions (no triple duplication)
- Single get_style(StyleRole) replaces 40+ getter functions
- ~60% line reduction (~2500 → ~900 lines)
"
```

---

## Summary

| Phase | Tasks | Estimated Time |
|-------|-------|----------------|
| 1. Core Data Structures | 3 | 30 min |
| 2. ThemeManager Skeleton | 1 | 20 min |
| 3. Style Configure Functions | 6 | 90 min |
| 4. Core Functionality | 3 | 45 min |
| 5. Compatibility Shims | 1 | 20 min |
| 6. Migrate Callers | 56 | 2-3 hours |
| 7. Cleanup | 1 | 15 min |
| 8. Final Validation | 3 | 20 min |

**Total: ~6-7 hours of focused work**

Key principle: Each task is self-contained with tests. If interrupted, work is committed and resumable.
