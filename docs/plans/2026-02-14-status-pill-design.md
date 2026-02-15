# Status Pill Component — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** A reusable `status_pill` XML component with variant-based colors, DRYed with `icon`'s variant system.

**Architecture:** Extract icon's variant enum + color lookup into a shared `ui_variant` module. Refactor `ui_icon.cpp` to consume it (zero behavior change). Build `status_pill` widget on top of the same shared module.

**Tech Stack:** LVGL 9.4 XML widget system, ThemeManager palette colors, C++ custom widget registration.

---

### Task 1: Create shared variant module

**Files:**
- Create: `include/ui_variant.h`
- Create: `src/ui/ui_variant.cpp`

**Step 1: Create `include/ui_variant.h`**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/// @file ui_variant.h
/// @brief Shared semantic color variants for icon, status_pill, and future components.
/// @pattern Variant enum + ThemeManager palette lookup. Used by ui_icon and ui_status_pill.
/// @threading Main thread only

#pragma once

#include "lvgl/lvgl.h"

namespace helix::ui {

/// Semantic color variants — shared across icon, status_pill, and future components.
enum class Variant {
    NONE,      // Primary text color (default)
    TEXT,      // Same as NONE
    MUTED,     // De-emphasized
    PRIMARY,   // Accent/brand
    SECONDARY, // Secondary accent
    TERTIARY,  // Tertiary accent
    DISABLED,  // Text @ 50% opacity
    SUCCESS,
    WARNING,
    DANGER,
    INFO
};

/// Parse variant string ("success", "danger", etc.) to enum. Returns NONE on unknown.
Variant parse_variant(const char* str);

/// Get the text/icon color for a variant from ThemeManager's current palette.
lv_color_t variant_color(Variant v);

/// Get the opacity for a variant (LV_OPA_COVER for all except DISABLED = LV_OPA_50).
lv_opa_t variant_opa(Variant v);

/// Apply variant as text color + opa to an lv_obj (convenience for icons/labels).
/// Removes previously applied variant style first, then adds the matching IconXxx style.
void apply_variant_text_style(lv_obj_t* obj, Variant v);

/// Remove all variant-related styles from obj (cleanup before re-applying).
void remove_variant_styles(lv_obj_t* obj);

} // namespace helix::ui
```

**Step 2: Create `src/ui/ui_variant.cpp`**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_variant.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>

namespace helix::ui {

Variant parse_variant(const char* str) {
    if (!str || str[0] == '\0') return Variant::NONE;

    // Ordered by expected frequency
    if (strcmp(str, "success") == 0)   return Variant::SUCCESS;
    if (strcmp(str, "muted") == 0)     return Variant::MUTED;
    if (strcmp(str, "danger") == 0)    return Variant::DANGER;
    if (strcmp(str, "warning") == 0)   return Variant::WARNING;
    if (strcmp(str, "info") == 0)      return Variant::INFO;
    if (strcmp(str, "primary") == 0)   return Variant::PRIMARY;
    if (strcmp(str, "secondary") == 0) return Variant::SECONDARY;
    if (strcmp(str, "tertiary") == 0)  return Variant::TERTIARY;
    if (strcmp(str, "disabled") == 0)  return Variant::DISABLED;
    if (strcmp(str, "text") == 0)      return Variant::TEXT;
    if (strcmp(str, "none") == 0)      return Variant::NONE;

    spdlog::warn("[Variant] Unknown variant '{}', using NONE", str);
    return Variant::NONE;
}

lv_color_t variant_color(Variant v) {
    auto& p = ThemeManager::instance().current_palette();
    switch (v) {
    case Variant::SUCCESS:   return p.success;
    case Variant::WARNING:   return p.warning;
    case Variant::DANGER:    return p.danger;
    case Variant::INFO:      return p.info;
    case Variant::PRIMARY:   return p.primary;
    case Variant::SECONDARY: return p.secondary;
    case Variant::TERTIARY:  return p.tertiary;
    case Variant::MUTED:     return p.text_muted;
    case Variant::DISABLED:  return p.text;
    case Variant::TEXT:
    case Variant::NONE:
    default:                 return p.text;
    }
}

lv_opa_t variant_opa(Variant v) {
    return (v == Variant::DISABLED) ? LV_OPA_50 : LV_OPA_COVER;
}

void remove_variant_styles(lv_obj_t* obj) {
    auto& tm = ThemeManager::instance();
    lv_style_t* styles[] = {
        tm.get_style(StyleRole::IconText),
        tm.get_style(StyleRole::TextMuted),
        tm.get_style(StyleRole::IconPrimary),
        tm.get_style(StyleRole::IconSecondary),
        tm.get_style(StyleRole::IconTertiary),
        tm.get_style(StyleRole::IconSuccess),
        tm.get_style(StyleRole::IconWarning),
        tm.get_style(StyleRole::IconDanger),
        tm.get_style(StyleRole::IconInfo),
    };
    for (auto* s : styles) {
        if (s) lv_obj_remove_style(obj, s, LV_PART_MAIN);
    }
}

void apply_variant_text_style(lv_obj_t* obj, Variant v) {
    remove_variant_styles(obj);

    auto& tm = ThemeManager::instance();
    lv_style_t* style = nullptr;
    lv_opa_t opa = LV_OPA_COVER;

    switch (v) {
    case Variant::TEXT:
    case Variant::NONE:     style = tm.get_style(StyleRole::IconText);      break;
    case Variant::MUTED:    style = tm.get_style(StyleRole::TextMuted);     break;
    case Variant::PRIMARY:  style = tm.get_style(StyleRole::IconPrimary);   break;
    case Variant::SECONDARY:style = tm.get_style(StyleRole::IconSecondary); break;
    case Variant::TERTIARY: style = tm.get_style(StyleRole::IconTertiary);  break;
    case Variant::DISABLED: style = tm.get_style(StyleRole::IconText); opa = LV_OPA_50; break;
    case Variant::SUCCESS:  style = tm.get_style(StyleRole::IconSuccess);   break;
    case Variant::WARNING:  style = tm.get_style(StyleRole::IconWarning);   break;
    case Variant::DANGER:   style = tm.get_style(StyleRole::IconDanger);    break;
    case Variant::INFO:     style = tm.get_style(StyleRole::IconInfo);      break;
    }

    if (style) {
        lv_obj_add_style(obj, style, LV_PART_MAIN);
    }
    if (opa != LV_OPA_COVER) {
        lv_obj_set_style_text_opa(obj, opa, LV_PART_MAIN);
    }
}

} // namespace helix::ui
```

**Step 3: Verify it compiles**

Run: `make -j` (in worktree)
Expected: Clean build, no errors.

**Step 4: Commit**

```bash
git add include/ui_variant.h src/ui/ui_variant.cpp
git commit -m "refactor(ui): extract shared variant module from icon widget"
```

---

### Task 2: Refactor ui_icon.cpp to use shared variant module

**Files:**
- Modify: `src/ui/ui_icon.cpp`

The goal is to remove the local `IconVariant` enum, `parse_variant()`, `remove_icon_styles()`, and `apply_variant()` functions. Replace them with calls to `helix::ui::parse_variant()`, `helix::ui::apply_variant_text_style()`, and `helix::ui::remove_variant_styles()`.

**Step 1: Add include and alias**

At top of `ui_icon.cpp`, add after existing includes:

```cpp
#include "ui_variant.h"
```

**Step 2: Remove local IconVariant enum (lines 40-52)**

Delete the entire `enum class IconVariant { ... };` block.

**Step 3: Remove local parse_variant function (lines 113-142)**

Delete the entire `static IconVariant parse_variant(...)` function and its comment block (lines 97-142).

**Step 4: Remove local remove_icon_styles function (lines 164-180)**

Delete the entire `static void remove_icon_styles(...)` function.

**Step 5: Remove local apply_variant function (lines 191-237)**

Delete the entire `static void apply_variant(...)` function and its comment block (lines 182-237).

**Step 6: Update ui_icon_xml_create — default variant**

Change line ~290 from:
```cpp
apply_variant(obj, IconVariant::NONE);
```
to:
```cpp
helix::ui::apply_variant_text_style(obj, helix::ui::Variant::NONE);
```

**Step 7: Update ui_icon_xml_apply — variant parsing and application**

In `ui_icon_xml_apply()`:
- Change the local variable type from `IconVariant variant = IconVariant::NONE;` to `helix::ui::Variant variant = helix::ui::Variant::NONE;`
- Change the parse call from `variant = parse_variant(value);` to `variant = helix::ui::parse_variant(value);`
- Change the apply call from `apply_variant(obj, variant);` to `helix::ui::apply_variant_text_style(obj, variant);`

**Step 8: Update ui_icon_set_variant public API**

In `ui_icon_set_variant()` (~line 388):
Change from:
```cpp
IconVariant variant = parse_variant(variant_str);
apply_variant(icon, variant);
```
to:
```cpp
auto variant = helix::ui::parse_variant(variant_str);
helix::ui::apply_variant_text_style(icon, variant);
```

**Step 9: Verify build + run**

Run: `make -j`
Expected: Clean build, zero behavior change.

Run: `./build/bin/helix-screen --test -vv`
Expected: Icons render identically. Spot-check settings panel icons, filament panel, etc.

**Step 10: Commit**

```bash
git add src/ui/ui_icon.cpp
git commit -m "refactor(ui): use shared variant module in icon widget"
```

---

### Task 3: Create status_pill XML component + C++ widget

**Files:**
- Create: `ui_xml/status_pill.xml`
- Create: `include/ui_status_pill.h`
- Create: `src/ui/ui_status_pill.cpp`
- Modify: `src/application/application.cpp` (add registration call)

**Step 1: Create `ui_xml/status_pill.xml`**

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Semantic status pill/badge with variant-based coloring. -->
<!-- Usage: <status_pill text="Active" variant="success"/> -->
<!-- Variants: success, warning, danger, info, muted, primary, secondary, tertiary, disabled -->
<component>
  <api>
    <prop name="text" type="string" default=""/>
    <prop name="variant" type="string" default="muted"/>
  </api>
  <view extends="lv_obj"/>
</component>
```

**Step 2: Create `include/ui_status_pill.h`**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/// @file ui_status_pill.h
/// @brief Status pill/badge widget with semantic color variants.
/// @pattern Custom XML widget using shared ui_variant module.
/// @threading Main thread only
/// @gotchas Must call ui_status_pill_register_widget() BEFORE loading status_pill.xml

#pragma once

#include "lvgl/lvgl.h"

/// Register the status_pill widget with LVGL's XML system.
/// Call once at startup, before registering XML components.
void ui_status_pill_register_widget();

/// Change pill text at runtime.
void ui_status_pill_set_text(lv_obj_t* pill, const char* text);

/// Change pill variant at runtime ("success", "danger", "muted", etc.).
void ui_status_pill_set_variant(lv_obj_t* pill, const char* variant_str);
```

**Step 3: Create `src/ui/ui_status_pill.cpp`**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_status_pill.h"
#include "ui_variant.h"
#include "theme_manager.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_widget.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"

#include <spdlog/spdlog.h>

#include <cstring>

// ----- Internal helpers -----

/// The pill stores current variant in user_data as an int cast.
static helix::ui::Variant get_stored_variant(lv_obj_t* pill) {
    auto raw = reinterpret_cast<intptr_t>(lv_obj_get_user_data(pill));
    return static_cast<helix::ui::Variant>(raw);
}

static void store_variant(lv_obj_t* pill, helix::ui::Variant v) {
    lv_obj_set_user_data(pill, reinterpret_cast<void*>(static_cast<intptr_t>(v)));
}

/// Find the child label (first child).
static lv_obj_t* get_label(lv_obj_t* pill) {
    return lv_obj_get_child(pill, 0);
}

/// Apply variant colors to pill: bg @ 40% opa, text @ full opa.
static void apply_pill_variant(lv_obj_t* pill, helix::ui::Variant v) {
    store_variant(pill, v);
    lv_color_t color = helix::ui::variant_color(v);
    lv_opa_t text_opa = helix::ui::variant_opa(v);

    // Background: variant color at 40% opacity
    lv_obj_set_style_bg_color(pill, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, 40, LV_PART_MAIN);

    // Text: variant color at full (or reduced for disabled) opacity
    lv_obj_t* label = get_label(pill);
    if (label) {
        lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, text_opa, LV_PART_MAIN);
    }
}

// ----- XML widget callbacks -----

static void* ui_status_pill_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    (void)attrs;
    lv_obj_t* parent = (lv_obj_t*)lv_xml_state_get_parent(state);

    // Container: content-sized pill with rounded corners
    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_scrollbar_mode(pill, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(pill, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);

    // Padding: space_xs horizontal, 2px vertical
    // Use hardcoded 6px for horizontal (space_xs varies 4-6, 6 is safe for pill)
    lv_obj_set_style_pad_left(pill, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(pill, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(pill, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(pill, 2, LV_PART_MAIN);

    // Child label for text
    lv_obj_t* label = lv_label_create(pill);
    lv_label_set_text(label, "");

    // Use small font — get from theme
    const lv_font_t* font = theme_manager_get_font("font_small");
    if (font) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    }

    // Default: muted variant
    apply_pill_variant(pill, helix::ui::Variant::MUTED);

    return pill;
}

static void ui_status_pill_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* pill = (lv_obj_t*)lv_xml_state_get_item(state);

    // Apply common lv_obj properties first (width, height, align, etc.)
    lv_xml_obj_apply(state, attrs);

    // Process status_pill-specific properties
    const char* text = nullptr;
    const char* variant_str = nullptr;

    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];

        if (strcmp(name, "text") == 0) {
            text = value;
        } else if (strcmp(name, "variant") == 0) {
            variant_str = value;
        }
    }

    if (text) {
        lv_obj_t* label = get_label(pill);
        if (label) lv_label_set_text(label, text);
    }

    if (variant_str) {
        auto v = helix::ui::parse_variant(variant_str);
        apply_pill_variant(pill, v);
    }
}

// ----- Public API -----

void ui_status_pill_register_widget() {
    lv_xml_register_widget("status_pill", ui_status_pill_xml_create, ui_status_pill_xml_apply);
    spdlog::trace("[StatusPill] Widget registered with XML system");
}

void ui_status_pill_set_text(lv_obj_t* pill, const char* text) {
    if (!pill || !text) return;
    lv_obj_t* label = get_label(pill);
    if (label) lv_label_set_text(label, text);
}

void ui_status_pill_set_variant(lv_obj_t* pill, const char* variant_str) {
    if (!pill || !variant_str) return;
    auto v = helix::ui::parse_variant(variant_str);
    apply_pill_variant(pill, v);
}
```

**Step 4: Register in `application.cpp`**

In `Application::register_widgets()` (line ~723), add after `ui_icon_register_widget();`:

```cpp
#include "ui_status_pill.h"  // add to includes at top

// In register_widgets():
ui_status_pill_register_widget();
```

**Step 5: Verify build**

Run: `make -j`
Expected: Clean build.

**Step 6: Quick visual test**

Temporarily add a status_pill to an existing XML file to verify rendering:
```xml
<status_pill text="Active" variant="success"/>
```
Run: `./build/bin/helix-screen --test -vv`
Expected: Green pill with "Active" text renders correctly.
Remove temporary test XML after verifying.

**Step 7: Commit**

```bash
git add ui_xml/status_pill.xml include/ui_status_pill.h src/ui/ui_status_pill.cpp src/application/application.cpp
git commit -m "feat(ui): add status_pill component with variant-based coloring"
```

---

### Task 4: Tests for variant module and status_pill

**Files:**
- Create: `tests/unit/test_ui_variant.cpp`

**Step 1: Write variant parsing tests**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_all.hpp>
#include "ui_variant.h"

using helix::ui::Variant;
using helix::ui::parse_variant;
using helix::ui::variant_opa;

TEST_CASE("Variant parsing", "[ui][variant]") {
    SECTION("known variants parse correctly") {
        CHECK(parse_variant("success") == Variant::SUCCESS);
        CHECK(parse_variant("warning") == Variant::WARNING);
        CHECK(parse_variant("danger") == Variant::DANGER);
        CHECK(parse_variant("info") == Variant::INFO);
        CHECK(parse_variant("muted") == Variant::MUTED);
        CHECK(parse_variant("primary") == Variant::PRIMARY);
        CHECK(parse_variant("secondary") == Variant::SECONDARY);
        CHECK(parse_variant("tertiary") == Variant::TERTIARY);
        CHECK(parse_variant("disabled") == Variant::DISABLED);
        CHECK(parse_variant("text") == Variant::TEXT);
        CHECK(parse_variant("none") == Variant::NONE);
    }

    SECTION("null and empty return NONE") {
        CHECK(parse_variant(nullptr) == Variant::NONE);
        CHECK(parse_variant("") == Variant::NONE);
    }

    SECTION("unknown string returns NONE") {
        CHECK(parse_variant("bogus") == Variant::NONE);
        CHECK(parse_variant("SUCCESS") == Variant::NONE); // case-sensitive
    }
}

TEST_CASE("Variant opacity", "[ui][variant]") {
    CHECK(variant_opa(Variant::SUCCESS) == LV_OPA_COVER);
    CHECK(variant_opa(Variant::DANGER) == LV_OPA_COVER);
    CHECK(variant_opa(Variant::DISABLED) == LV_OPA_50);
    CHECK(variant_opa(Variant::NONE) == LV_OPA_COVER);
}
```

**Step 2: Run tests**

Run: `make test-run`
Expected: All variant tests pass alongside existing tests.

**Step 3: Commit**

```bash
git add tests/unit/test_ui_variant.cpp
git commit -m "test(ui): add variant parsing and opacity tests"
```

---

### Task 5: Register status_pill.xml component and handle padding with theme tokens

**Files:**
- Modify: `src/application/application.cpp` (register component file)

**Step 1: Add component registration**

Check existing pattern for registering XML component files. In `application.cpp`, find where `lv_xml_register_component_from_file()` calls are made. Add:

```cpp
lv_xml_register_component_from_file("A:ui_xml/status_pill.xml");
```

This must come AFTER `ui_status_pill_register_widget()` (Task 3) and AFTER other component registrations that status_pill might depend on (like `text_small`).

**Step 2: Check padding uses theme tokens**

In `ui_status_pill.cpp`, the padding uses hardcoded `6` and `2`. Check if `lv_xml_style.h` provides `lv_xml_to_size()` or similar for resolving `#space_xs`. If not, hardcoded is acceptable (matches beta_badge pattern which also uses hardcoded `2`). Document with a comment.

**Step 3: Verify build + visual test**

Run: `make -j && ./build/bin/helix-screen --test -vv`
Expected: status_pill available as XML component, renders correctly when used.

**Step 4: Commit if changes were needed**

```bash
git add src/application/application.cpp
git commit -m "chore(ui): register status_pill XML component"
```
