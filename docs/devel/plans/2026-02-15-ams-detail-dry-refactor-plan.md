# AMS Detail View DRY Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Eliminate ~200 lines of duplicated spool grid + tray rendering code between `AmsPanel` and `AmsOverviewPanel` by extracting shared XML components and C++ helper functions.

**Architecture:** Create an `ams_unit_detail` XML component for the slot grid/tray/labels structure, a `ams_loaded_card` XML component for the "Currently Loaded" card, and C++ free functions in `ui_ams_detail.h/cpp` for slot creation, tray sizing, label management, and path canvas setup. Both panels compose these shared pieces. Action buttons stay panel-specific (too many small behavioral differences).

**Tech Stack:** C++17, LVGL 9.4 XML components, Catch2 tests

**Design doc:** `docs/devel/plans/2026-02-15-ams-detail-dry-refactor-design.md`

**Worktree:** `.worktrees/multi-unit-ams` (branch `feature/multi-unit-ams`)

---

## Context for Implementers

### Key files to understand before starting

| File | Why |
|------|-----|
| `ui_xml/ams_panel.xml` | Current AmsPanel XML — tray/grid/labels structure to extract (lines 70-110) |
| `ui_xml/ams_overview_panel.xml` | Current overview XML — duplicated detail section (lines 55-85) |
| `ui_xml/ams_slot_view.xml` | Individual slot component — NOT being changed, for reference |
| `src/ui/ui_panel_ams.cpp` | `create_slots()` (686-799), `update_tray_size()` (801-832), `setup_path_canvas()` (854-886), `update_path_canvas_from_backend()` (888-940) |
| `src/ui/ui_panel_ams_overview.cpp` | `create_detail_slots()` (857-934), `setup_detail_path_canvas()` (944-1012) |
| `include/ui_ams_slot_layout.h` | Already-extracted `calculate_ams_slot_layout()` — extend this pattern |
| `src/ui/ui_ams_slot.cpp` | `ui_ams_slot_move_label_to_layer()` — already shared helper |

### Data flow

```
AmsState subjects → AmsPanel / AmsOverviewPanel (observers)
                         ↓ calls shared helpers
                    ams_detail_create_slots() → creates ams_slot widgets in grid
                    ams_detail_update_tray() → sizes tray to 1/3 grid height
                    ams_detail_update_labels() → moves labels to overlay layer
                    ams_detail_setup_path_canvas() → configures path from backend
```

Both panels find the shared child widgets via `lv_obj_find_by_name()` after embedding `<ams_unit_detail/>`.

---

## Task 1: Create `ams_unit_detail` XML component

**Files:**
- Create: `ui_xml/components/ams_unit_detail.xml`
- Modify: `src/ui/ui_panel_ams.cpp` (lines 85-133, `ensure_ams_widgets_registered()`)

**Step 1: Create the shared XML component**

Create `ui_xml/components/ams_unit_detail.xml`:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<component>
  <!-- Tray styling tokens - single source of truth for both AmsPanel and AmsOverviewPanel -->
  <consts>
    <color name="tray_bg" value="#505050"/>
    <color name="tray_border" value="#606060"/>
    <number name="tray_bg_opa" value="200"/>
    <number name="tray_border_opa" value="140"/>
    <px name="tray_default_height" value="30"/>
    <number name="tray_accent_border_opa" value="180"/>
  </consts>
  <!--
    AMS Unit Detail - Shared spool grid + tray + labels layer structure.

    Used by both AmsPanel (single-unit view) and AmsOverviewPanel (detail mode).
    C++ resolves child widgets via lv_obj_find_by_name():
    - "slot_grid": flex row container for ams_slot widgets (created dynamically)
    - "labels_layer": overlay for material labels when 5+ slots overlap
    - "slot_tray": visual "holder" that renders ON TOP of spool bottoms

    Z-order (front to back): labels_layer > slot_tray > slot_grid
    Badge z-order handled by lv_obj_move_to_index(-1) in ui_ams_slot.cpp.
  -->
  <view name="ams_unit_detail" extends="lv_obj"
        width="100%" height="content" style_pad_all="0" scrollable="false">
    <!-- Slot Grid (rendered first = behind everything) -->
    <lv_obj name="slot_grid"
            width="content" height="content" style_pad_all="0" style_pad_row="0"
            style_pad_column="0" flex_flow="row" style_flex_main_place="start"
            style_flex_cross_place="start" scrollable="false">
      <!-- Slots created dynamically by ams_detail_create_slots() -->
    </lv_obj>
    <!-- Labels Layer - renders on top of overlapping slots for 5+ gates -->
    <lv_obj name="labels_layer"
            width="100%" height="80" style_pad_all="0" scrollable="false"
            clickable="false"/>
    <!-- Tray - visual "holder" in front of spools, positioned at bottom.
         Height set dynamically by ams_detail_update_tray() to 1/3 of slot height. -->
    <lv_obj name="slot_tray"
            width="100%" height="#tray_default_height" align="bottom_mid"
            style_bg_color="#tray_bg" style_bg_opa="#tray_bg_opa"
            style_border_width="1" style_border_color="#tray_border"
            style_border_opa="#tray_border_opa"
            style_radius="#border_radius" style_pad_all="0"
            scrollable="false" clickable="false">
      <!-- Left accent line -->
      <lv_obj width="#space_xs" height="80%" align="left_mid"
              style_translate_x="#space_xs" style_border_width="#space_xxs"
              style_border_color="#text" style_border_opa="#tray_accent_border_opa"
              style_border_side="left" style_radius="#border_radius"
              style_pad_all="0" scrollable="false" clickable="false"/>
      <!-- Right accent line -->
      <lv_obj width="#space_xs" height="80%" align="right_mid"
              style_translate_x="-2" style_border_width="#space_xxs"
              style_border_color="#text" style_border_opa="#tray_accent_border_opa"
              style_border_side="right" style_radius="#border_radius"
              style_pad_all="0" scrollable="false" clickable="false"/>
    </lv_obj>
  </view>
</component>
```

**Step 2: Register the component**

In `src/ui/ui_panel_ams.cpp`, in `ensure_ams_widgets_registered()`, add before the `ams_panel.xml` registration (before line 122):

```cpp
    lv_xml_register_component_from_file("A:ui_xml/components/ams_unit_detail.xml");
```

**Step 3: Build to verify the XML parses**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-unit-ams && make -j
```
Expected: builds clean (component registered but not yet used)

**Step 4: Commit**

```bash
git add ui_xml/components/ams_unit_detail.xml src/ui/ui_panel_ams.cpp
git commit -m "feat(ams): add shared ams_unit_detail XML component for spool grid + tray"
```

---

## Task 2: Create C++ helper functions

**Files:**
- Create: `include/ui_ams_detail.h`
- Create: `src/ui/ui_ams_detail.cpp`

**Step 1: Write the header**

Create `include/ui_ams_detail.h`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_ams_slot_layout.h"

#include "lvgl/lvgl.h"

/**
 * @file ui_ams_detail.h
 * @brief Shared helper functions for AMS spool grid rendering
 *
 * Eliminates duplication between AmsPanel and AmsOverviewPanel.
 * Both panels embed an <ams_unit_detail/> XML component and call
 * these free functions to manage slot creation, tray sizing,
 * label management, and path canvas setup.
 */

/// Maximum slots supported in a single detail view
static constexpr int AMS_DETAIL_MAX_SLOTS = 16;

/// Widget pointers resolved from an ams_unit_detail component
struct AmsDetailWidgets {
    lv_obj_t* root = nullptr;         ///< The ams_unit_detail root object
    lv_obj_t* slot_grid = nullptr;    ///< Flex row container for ams_slot widgets
    lv_obj_t* slot_tray = nullptr;    ///< Visual "holder" in front of spool bottoms
    lv_obj_t* labels_layer = nullptr; ///< Overlay for material labels (5+ slots)
};

/**
 * @brief Resolve child widget pointers from an ams_unit_detail root
 * @param root The ams_unit_detail object (from lv_obj_find_by_name or lv_xml_create)
 * @return Populated AmsDetailWidgets (members may be nullptr if not found)
 */
AmsDetailWidgets ams_detail_find_widgets(lv_obj_t* root);

/**
 * @brief Create slot widgets in the grid from backend data
 *
 * Clears existing slots, creates new ams_slot widgets via XML, applies
 * layout sizing (width, overlap), and wires click handlers.
 *
 * @param w           Widget pointers from ams_detail_find_widgets()
 * @param slot_widgets Output array of created slot widget pointers
 * @param max_slots   Size of slot_widgets array (use AMS_DETAIL_MAX_SLOTS)
 * @param unit_index  Backend unit index (-1 = whole backend for single-unit panels)
 * @param click_cb    Per-slot click callback (panel-specific)
 * @param user_data   User data for click callback (typically panel pointer)
 * @return Number of slots created, and populated AmsSlotLayout via out param
 */
struct AmsDetailSlotResult {
    int slot_count = 0;
    AmsSlotLayout layout;
};

AmsDetailSlotResult ams_detail_create_slots(
    AmsDetailWidgets& w,
    lv_obj_t* slot_widgets[],
    int max_slots,
    int unit_index,
    lv_event_cb_t click_cb,
    void* user_data);

/**
 * @brief Destroy all slot widgets in the grid
 * @param w           Widget pointers
 * @param slot_widgets Array of slot widget pointers to clear
 * @param slot_count  Number of slots to destroy (reset to 0)
 */
void ams_detail_destroy_slots(
    AmsDetailWidgets& w,
    lv_obj_t* slot_widgets[],
    int& slot_count);

/**
 * @brief Size tray to 1/3 of slot grid height (minimum 20px)
 * @param w Widget pointers — uses slot_grid height and positions slot_tray
 */
void ams_detail_update_tray(AmsDetailWidgets& w);

/**
 * @brief Move material labels to overlay layer for 5+ overlapping slots
 * @param w           Widget pointers
 * @param slot_widgets Array of slot widget pointers
 * @param slot_count  Number of active slots
 * @param layout      Slot layout (width + overlap) from create_slots result
 */
void ams_detail_update_labels(
    AmsDetailWidgets& w,
    lv_obj_t* slot_widgets[],
    int slot_count,
    const AmsSlotLayout& layout);

/**
 * @brief Configure a path canvas from backend state
 *
 * Sets slot count, topology, active slot, filament segments, colors,
 * slot sizing, and Voron toolhead mode.
 *
 * @param canvas     The filament_path_canvas widget
 * @param slot_grid  The slot grid (for sizing sync) — may be nullptr
 * @param unit_index Backend unit index (-1 = whole backend)
 * @param hub_only   If true, only draw slots → hub (skip downstream)
 */
void ams_detail_setup_path_canvas(
    lv_obj_t* canvas,
    lv_obj_t* slot_grid,
    int unit_index,
    bool hub_only);
```

**Step 2: Write the implementation**

Create `src/ui/ui_ams_detail.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_detail.h"

#include "ams_state.h"
#include "printer_discovery.h"
#include "ui_ams_slot.h"
#include "ui_filament_path_canvas.h"
#include "ui_xml_utils.h"

#include "spdlog/spdlog.h"

AmsDetailWidgets ams_detail_find_widgets(lv_obj_t* root) {
    AmsDetailWidgets w;
    if (!root) return w;

    w.root = root;
    w.slot_grid = lv_obj_find_by_name(root, "slot_grid");
    w.slot_tray = lv_obj_find_by_name(root, "slot_tray");
    w.labels_layer = lv_obj_find_by_name(root, "labels_layer");

    if (!w.slot_grid) {
        spdlog::warn("[AmsDetail] slot_grid not found in ams_unit_detail");
    }

    return w;
}

AmsDetailSlotResult ams_detail_create_slots(
    AmsDetailWidgets& w,
    lv_obj_t* slot_widgets[],
    int max_slots,
    int unit_index,
    lv_event_cb_t click_cb,
    void* user_data) {

    AmsDetailSlotResult result;

    if (!w.slot_grid) return result;

    // Determine slot count and offset from backend
    int count = 0;
    int slot_offset = 0;

    auto* backend = AmsState::instance().get_backend();
    if (backend) {
        AmsSystemInfo info = backend->get_system_info();
        if (unit_index >= 0 && unit_index < static_cast<int>(info.units.size())) {
            count = info.units[unit_index].slot_count;
            slot_offset = info.units[unit_index].first_slot_global_index;
        } else {
            count = info.total_slots;
        }
    }

    if (count <= 0) return result;
    if (count > max_slots) {
        spdlog::warn("[AmsDetail] Clamping slot_count {} to max {}", count, max_slots);
        count = max_slots;
    }

    // Create slot widgets via XML system
    for (int i = 0; i < count; ++i) {
        lv_obj_t* slot =
            static_cast<lv_obj_t*>(lv_xml_create(w.slot_grid, "ams_slot", nullptr));
        if (!slot) {
            spdlog::error("[AmsDetail] Failed to create ams_slot for index {}", i);
            continue;
        }

        int global_index = i + slot_offset;
        ui_ams_slot_set_index(slot, global_index);
        ui_ams_slot_set_layout_info(slot, i, count);

        slot_widgets[i] = slot;
        lv_obj_set_user_data(slot,
                             reinterpret_cast<void*>(static_cast<intptr_t>(global_index)));
        lv_obj_add_event_cb(slot, click_cb, LV_EVENT_CLICKED, user_data);
    }

    result.slot_count = count;

    // Calculate and apply slot sizing
    lv_obj_t* slot_area = lv_obj_get_parent(w.slot_grid);
    lv_obj_update_layout(slot_area);
    int32_t available_width = lv_obj_get_content_width(slot_area);
    result.layout = calculate_ams_slot_layout(available_width, count);

    lv_obj_set_style_pad_column(
        w.slot_grid, result.layout.overlap > 0 ? -result.layout.overlap : 0, LV_PART_MAIN);

    for (int i = 0; i < count; ++i) {
        if (slot_widgets[i]) {
            lv_obj_set_width(slot_widgets[i], result.layout.slot_width);
        }
    }

    spdlog::debug("[AmsDetail] Created {} slots (offset={}, width={}, overlap={})",
                  count, slot_offset, result.layout.slot_width, result.layout.overlap);

    return result;
}

void ams_detail_destroy_slots(
    AmsDetailWidgets& w,
    lv_obj_t* slot_widgets[],
    int& slot_count) {

    for (int i = 0; i < slot_count; ++i) {
        lv_obj_safe_delete(slot_widgets[i]);
        slot_widgets[i] = nullptr;
    }
    slot_count = 0;
}

void ams_detail_update_tray(AmsDetailWidgets& w) {
    if (!w.slot_tray || !w.slot_grid) return;

    lv_obj_update_layout(w.slot_grid);
    int32_t grid_height = lv_obj_get_height(w.slot_grid);
    if (grid_height <= 0) return;

    int32_t tray_height = grid_height / 3;
    if (tray_height < 20) tray_height = 20;

    lv_obj_set_height(w.slot_tray, tray_height);
    lv_obj_align(w.slot_tray, LV_ALIGN_BOTTOM_MID, 0, 0);

    spdlog::debug("[AmsDetail] Tray sized to {}px (1/3 of {}px grid)", tray_height, grid_height);
}

void ams_detail_update_labels(
    AmsDetailWidgets& w,
    lv_obj_t* slot_widgets[],
    int slot_count,
    const AmsSlotLayout& layout) {

    if (!w.labels_layer || slot_count <= 4) return;

    lv_obj_clean(w.labels_layer);

    int32_t slot_spacing = layout.slot_width - layout.overlap;

    for (int i = 0; i < slot_count; ++i) {
        if (slot_widgets[i]) {
            int32_t slot_center_x = layout.slot_width / 2 + i * slot_spacing;
            ui_ams_slot_move_label_to_layer(slot_widgets[i], w.labels_layer, slot_center_x);
        }
    }

    spdlog::debug("[AmsDetail] Moved {} labels to overlay layer", slot_count);
}

void ams_detail_setup_path_canvas(
    lv_obj_t* canvas,
    lv_obj_t* slot_grid,
    int unit_index,
    bool hub_only) {

    if (!canvas) return;

    auto* backend = AmsState::instance().get_backend();
    if (!backend) return;

    AmsSystemInfo info = backend->get_system_info();

    // Hub-only mode: only draw slots → hub, skip downstream
    ui_filament_path_canvas_set_hub_only(canvas, hub_only);

    // Determine slot count and offset for this unit
    int slot_count = info.total_slots;
    int slot_offset = 0;
    if (unit_index >= 0 && unit_index < static_cast<int>(info.units.size())) {
        slot_count = info.units[unit_index].slot_count;
        slot_offset = info.units[unit_index].first_slot_global_index;
    }

    ui_filament_path_canvas_set_slot_count(canvas, slot_count);
    ui_filament_path_canvas_set_topology(canvas,
                                         static_cast<int>(backend->get_topology()));

    // Sync slot sizing with grid layout
    if (slot_grid) {
        lv_obj_t* slot_area = lv_obj_get_parent(slot_grid);
        lv_obj_update_layout(slot_area);
        int32_t available_width = lv_obj_get_content_width(slot_area);
        auto layout = calculate_ams_slot_layout(available_width, slot_count);

        ui_filament_path_canvas_set_slot_width(canvas, layout.slot_width);
        ui_filament_path_canvas_set_slot_overlap(canvas, layout.overlap);
    }

    // Map active slot to local index for unit-scoped views
    int active_slot = info.current_slot;
    if (unit_index >= 0) {
        int local_active = info.current_slot - slot_offset;
        active_slot = (local_active >= 0 && local_active < slot_count) ? local_active : -1;
    }
    ui_filament_path_canvas_set_active_slot(canvas, active_slot);

    // Set filament color from active slot
    int global_active = (unit_index >= 0) ? active_slot + slot_offset : active_slot;
    if (global_active >= 0) {
        SlotInfo slot_info = backend->get_slot_info(global_active);
        ui_filament_path_canvas_set_filament_color(canvas, slot_info.color_rgb);
    }

    // Set filament and error segments
    PathSegment segment = backend->get_filament_segment();
    ui_filament_path_canvas_set_filament_segment(canvas, static_cast<int>(segment));

    PathSegment error_seg = backend->infer_error_segment();
    ui_filament_path_canvas_set_error_segment(canvas, static_cast<int>(error_seg));

    // Use Stealthburner toolhead for Voron printers
    if (PrinterDetector::is_voron_printer()) {
        ui_filament_path_canvas_set_faceted_toolhead(canvas, true);
    }

    // Set per-slot filament states (using local indices for unit-scoped views)
    ui_filament_path_canvas_clear_slot_filaments(canvas);
    for (int i = 0; i < slot_count; ++i) {
        int global_idx = i + slot_offset;
        PathSegment slot_seg = backend->get_slot_filament_segment(global_idx);
        if (slot_seg != PathSegment::NONE) {
            SlotInfo si = backend->get_slot_info(global_idx);
            ui_filament_path_canvas_set_slot_filament(
                canvas, i, static_cast<int>(slot_seg), si.color_rgb);
        }
    }

    ui_filament_path_canvas_refresh(canvas);

    spdlog::debug("[AmsDetail] Path canvas configured: slots={}, unit={}, hub_only={}",
                  slot_count, unit_index, hub_only);
}
```

**Step 3: Build to verify it compiles**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-unit-ams && make -j
```
Expected: builds clean (helpers compiled but not yet called)

**Step 4: Commit**

```bash
git add include/ui_ams_detail.h src/ui/ui_ams_detail.cpp
git commit -m "feat(ams): add shared helper functions for spool grid rendering"
```

---

## Task 3: Create `ams_loaded_card` XML component

**Files:**
- Create: `ui_xml/components/ams_loaded_card.xml`
- Modify: `src/ui/ui_panel_ams.cpp` (lines 85-133, add registration)

**Step 1: Create the shared component**

Create `ui_xml/components/ams_loaded_card.xml`:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<component>
  <consts>
    <px name="swatch_size" value="40"/>
  </consts>
  <!--
    AMS Currently Loaded Card - Shows current filament info.

    Binds to AmsState subjects:
    - ams_current_material_text: Material name (e.g., "Red PLA")
    - ams_current_slot_text: Slot location (e.g., "Box Turtle 1 · Slot 1")
    - ams_current_weight_text: Remaining weight (e.g., "1000g")
    - ams_current_has_weight: Show/hide weight line (0 = hidden)

    Color swatch updated by C++ via lv_obj_find_by_name(card, "loaded_swatch").
  -->
  <view name="ams_loaded_card" extends="ui_card"
        width="100%" height="content" style_radius="#border_radius"
        style_pad_all="#space_sm" flex_flow="row"
        style_flex_main_place="start" style_flex_cross_place="center"
        style_pad_gap="#space_sm">
    <!-- Color swatch (left side) -->
    <lv_obj name="loaded_swatch"
            width="#swatch_size" height="#swatch_size" style_radius="#border_radius"
            style_bg_color="#text_muted" style_bg_opa="255" style_border_width="#space_xxs"
            style_border_color="#text_muted" style_border_opa="80"
            scrollable="false" clickable="false"/>
    <!-- Details column (right side) -->
    <lv_obj height="content" style_pad_all="0" flex_flow="column"
            style_flex_main_place="center" style_flex_cross_place="start"
            style_pad_gap="0" scrollable="false" flex_grow="1">
      <!-- Material name (wraps for long names) -->
      <text_body name="loaded_material" width="100%"
                 bind_text="ams_current_material_text" long_mode="wrap"/>
      <!-- Slot location -->
      <text_small name="loaded_slot" bind_text="ams_current_slot_text"/>
      <!-- Weight (hidden when not available) -->
      <text_small name="loaded_weight" bind_text="ams_current_weight_text">
        <bind_flag_if_eq subject="ams_current_has_weight" flag="hidden" ref_value="0"/>
      </text_small>
    </lv_obj>
  </view>
</component>
```

**Step 2: Register the component**

In `src/ui/ui_panel_ams.cpp`, in `ensure_ams_widgets_registered()`, add before `ams_panel.xml` registration:

```cpp
    lv_xml_register_component_from_file("A:ui_xml/components/ams_loaded_card.xml");
```

**Step 3: Build to verify**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-unit-ams && make -j
```

**Step 4: Commit**

```bash
git add ui_xml/components/ams_loaded_card.xml src/ui/ui_panel_ams.cpp
git commit -m "feat(ams): add shared ams_loaded_card XML component"
```

---

## Task 4: Wire AmsPanel to use shared components

**Files:**
- Modify: `ui_xml/ams_panel.xml` (replace inline slot area + loaded card with shared components)
- Modify: `src/ui/ui_panel_ams.cpp` (replace `create_slots()`, `update_tray_size()`, path canvas setup)
- Modify: `include/ui_panel_ams.h` (update member types)

This is the core refactor task. Replace AmsPanel's duplicated code with calls to shared helpers.

**Step 1: Update `ams_panel.xml` — replace slots_wrapper content**

In `ui_xml/ams_panel.xml`, replace the `slots_wrapper` contents (lines 70-110) with the shared component.

The `slots_wrapper` remains (it also holds the `endless_spool_arrows`), but its children change:

Replace lines 72-109 (everything inside `slots_wrapper`) with:

```xml
              <!-- Shared spool grid + tray + labels component -->
              <ams_unit_detail name="unit_detail"/>
              <!-- Endless Spool Arrows - overlapping labels area
                   Uses absolute positioning relative to slots_wrapper -->
              <endless_spool_arrows name="endless_arrows"
                                    width="100%" height="50" align="top_mid"
                                    clickable="false" scrollable="false"
                                    style_bg_opa="0"/>
```

Remove the tray consts from lines 12-22 (now owned by the shared component).

**Step 2: Update `ams_panel.xml` — replace loaded card**

Replace lines 141-174 (the "Currently Loaded" section header + card) with:

```xml
        <!-- Currently Loaded Section Header -->
        <text_small width="100%" text="Currently Loaded" translation_tag="Currently Loaded"/>
        <!-- Shared loaded card component -->
        <ams_loaded_card name="current_loaded_card"/>
```

**Step 3: Update `src/ui/ui_panel_ams.cpp` — use shared helpers**

Add include:
```cpp
#include "ui_ams_detail.h"
```

Replace `setup_slots()` (lines 665-684) with:

```cpp
void AmsPanel::setup_slots() {
    lv_obj_t* unit_detail = lv_obj_find_by_name(panel_, "unit_detail");
    if (!unit_detail) {
        spdlog::warn("[{}] unit_detail not found in XML", get_name());
        return;
    }

    detail_widgets_ = ams_detail_find_widgets(unit_detail);
    slot_grid_ = detail_widgets_.slot_grid; // Keep for path canvas sync

    spdlog::debug("[{}] setup_slots: widgets resolved, slot creation deferred to on_activate()",
                  get_name());
}
```

Replace `create_slots()` body (lines 686-799) — keep the method signature but delegate to helpers:

```cpp
void AmsPanel::create_slots(int count) {
    // Destroy existing
    ams_detail_destroy_slots(detail_widgets_, slot_widgets_, current_slot_count_);

    // Determine unit index for scoped views
    int unit_index = scoped_unit_index_;

    // Create new slots
    auto result = ams_detail_create_slots(
        detail_widgets_, slot_widgets_, MAX_VISIBLE_SLOTS,
        unit_index, on_slot_clicked, this);

    current_slot_count_ = result.slot_count;

    // Labels overlay for 5+ slots
    ams_detail_update_labels(detail_widgets_, slot_widgets_,
                             result.slot_count, result.layout);

    // Update path canvas sizing
    if (path_canvas_) {
        ui_filament_path_canvas_set_slot_overlap(path_canvas_, result.layout.overlap);
        ui_filament_path_canvas_set_slot_width(path_canvas_, result.layout.slot_width);
    }

    spdlog::info("[{}] Created {} slot widgets via shared helpers", get_name(),
                 result.slot_count);

    // Update tray
    ams_detail_update_tray(detail_widgets_);
}
```

Delete `update_tray_size()` (lines 801-832) — replaced by `ams_detail_update_tray()`.

Replace `setup_path_canvas()` body (lines 854-886):

```cpp
void AmsPanel::setup_path_canvas() {
    path_canvas_ = lv_obj_find_by_name(panel_, "path_canvas");
    if (!path_canvas_) {
        spdlog::warn("[{}] path_canvas not found in XML", get_name());
        return;
    }

    // Set slot click callback (panel-specific)
    ui_filament_path_canvas_set_slot_callback(path_canvas_, on_path_slot_clicked, this);

    // Configure from backend using shared helper
    ams_detail_setup_path_canvas(path_canvas_, slot_grid_, scoped_unit_index_, false);

    spdlog::debug("[{}] Path canvas setup complete", get_name());
}
```

Replace `update_path_canvas_from_backend()` body (lines 888-940):

```cpp
void AmsPanel::update_path_canvas_from_backend() {
    ams_detail_setup_path_canvas(path_canvas_, slot_grid_, scoped_unit_index_, false);
}
```

**Step 4: Update `include/ui_panel_ams.h`**

Add include and member:

```cpp
#include "ui_ams_detail.h"

// In private section, add:
AmsDetailWidgets detail_widgets_;

// Remove: update_tray_size() declaration (replaced by shared helper)
```

**Step 5: Verify `lv_obj_find_by_name` still finds swatch**

The loaded card swatch is now `loaded_swatch` (was `current_swatch`). Search for `current_swatch` in `ui_panel_ams.cpp` and update to `loaded_swatch`. Similarly update any `current_material`, `current_slot_label`, `current_weight_label` references if the C++ code looks them up by name (versus relying on subject bindings which work automatically).

Check: `grep -n "current_swatch\|current_material\|current_slot_label\|current_weight" src/ui/ui_panel_ams.cpp`

Update any `lv_obj_find_by_name` calls that reference old widget names.

**Step 6: Build and run**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-unit-ams && make -j
./build/bin/helix-screen --test -vv
```

Verify: AMS panel opens, spools display correctly, tray renders on top of spools, badges visible, currently loaded card shows data.

**Step 7: Run tests**

```bash
make test-run
```

Expected: all existing tests pass (no behavioral changes)

**Step 8: Commit**

```bash
git add ui_xml/ams_panel.xml include/ui_panel_ams.h src/ui/ui_panel_ams.cpp
git commit -m "refactor(ams): wire AmsPanel to shared ams_unit_detail and ams_loaded_card"
```

---

## Task 5: Wire AmsOverviewPanel to use shared components

**Files:**
- Modify: `ui_xml/ams_overview_panel.xml` (replace detail section + loaded card)
- Modify: `src/ui/ui_panel_ams_overview.cpp` (replace `create_detail_slots()`, path canvas)
- Modify: `include/ui_panel_ams_overview.h` (update members)

**Step 1: Update `ams_overview_panel.xml` — replace detail_slot_area**

Replace lines 55-80 (the `detail_slot_area` with inline tray/grid/labels) with:

```xml
            <lv_obj name="detail_slot_area"
                    width="100%" height="content" style_pad_all="0" scrollable="false">
              <!-- Shared spool grid + tray + labels component -->
              <ams_unit_detail name="detail_unit_detail"/>
            </lv_obj>
```

**Step 2: Update `ams_overview_panel.xml` — replace loaded card**

Replace lines 94-118 (the "Currently Loaded" header + card) with:

```xml
        <!-- Currently Loaded Section Header -->
        <text_small width="100%" text="Currently Loaded" translation_tag="Currently Loaded"/>
        <!-- Shared loaded card component -->
        <ams_loaded_card name="current_loaded_card"/>
```

**Step 3: Update `src/ui/ui_panel_ams_overview.cpp` — use shared helpers**

Add include:
```cpp
#include "ui_ams_detail.h"
```

Replace widget pointer resolution (around line 225) — change:
```cpp
    detail_slot_grid_ = lv_obj_find_by_name(panel_, "detail_slot_grid");
    detail_labels_layer_ = lv_obj_find_by_name(panel_, "detail_labels_layer");
    detail_slot_tray_ = lv_obj_find_by_name(panel_, "detail_slot_tray");
```
to:
```cpp
    lv_obj_t* detail_unit = lv_obj_find_by_name(panel_, "detail_unit_detail");
    detail_widgets_ = ams_detail_find_widgets(detail_unit);
```

Replace `create_detail_slots()` (lines 857-934):

```cpp
void AmsOverviewPanel::create_detail_slots(const AmsUnit& unit) {
    ams_detail_destroy_slots(detail_widgets_, detail_slot_widgets_, detail_slot_count_);

    // Find unit index from backend
    int unit_index = find_unit_index(unit);

    auto result = ams_detail_create_slots(
        detail_widgets_, detail_slot_widgets_, MAX_DETAIL_SLOTS,
        unit_index, on_detail_slot_clicked, this);

    detail_slot_count_ = result.slot_count;

    ams_detail_update_labels(detail_widgets_, detail_slot_widgets_,
                             result.slot_count, result.layout);
    ams_detail_update_tray(detail_widgets_);

    spdlog::debug("[{}] Created {} detail slots via shared helpers", get_name(),
                  result.slot_count);
}
```

Replace `setup_detail_path_canvas()` (lines 944-1012):

```cpp
void AmsOverviewPanel::setup_detail_path_canvas(const AmsUnit& unit,
                                                 const AmsSystemInfo& info) {
    if (!detail_path_canvas_) return;

    int unit_index = find_unit_index(unit);
    ams_detail_setup_path_canvas(detail_path_canvas_, detail_widgets_.slot_grid,
                                 unit_index, true /* hub_only */);
}
```

**Step 4: Update `include/ui_panel_ams_overview.h`**

Add include and member:
```cpp
#include "ui_ams_detail.h"

// Replace detail_slot_grid_, detail_labels_layer_, detail_slot_tray_ with:
AmsDetailWidgets detail_widgets_;
```

Remove old individual widget pointers that are now in `detail_widgets_`.

**Step 5: Update swatch name references**

Search for `overview_swatch` in the C++ and update to `loaded_swatch` (or whatever the C++ uses to set the swatch color).

**Step 6: Build and run both views**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-unit-ams && make -j
HELIX_MOCK_AMS_TYPE=toolchanger ./build/bin/helix-screen --test -vv
```

Verify toolchanger view. Then:

```bash
./build/bin/helix-screen --test -vv
```

Verify AFC/default AMS view — open Multi-Filament, click into a unit detail.

Both should look identical: same tray color, same accent lines, same badge rendering.

**Step 7: Run tests**

```bash
make test-run
```

**Step 8: Commit**

```bash
git add ui_xml/ams_overview_panel.xml include/ui_panel_ams_overview.h src/ui/ui_panel_ams_overview.cpp
git commit -m "refactor(ams): wire AmsOverviewPanel to shared ams_unit_detail and ams_loaded_card"
```

---

## Task 6: Clean up dead code and verify

**Files:**
- Modify: `src/ui/ui_panel_ams.cpp` — remove dead `update_tray_size()` if not already removed
- Modify: `src/ui/ui_panel_ams_overview.cpp` — remove dead `destroy_detail_slots()` if replaced
- Verify: no remaining references to old widget names

**Step 1: Search for dead code**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-unit-ams
grep -rn "detail_slot_grid_\|detail_labels_layer_\|detail_slot_tray_\|update_tray_size\|current_swatch\|overview_swatch" src/ include/
```

Remove any remaining references to the old individual widget pointers or methods.

**Step 2: Verify both mock modes visually**

```bash
HELIX_MOCK_AMS_TYPE=toolchanger ./build/bin/helix-screen --test -vv
# Check: tray on top of spools, badges visible, loaded card works

./build/bin/helix-screen --test -vv
# Check: AFC view, drill into unit detail, same visual as toolchanger
```

**Step 3: Run full test suite**

```bash
make test-run
```

**Step 4: Final commit**

```bash
git add -p  # Stage only cleanup changes
git commit -m "refactor(ams): remove dead code from DRY refactor"
```

---

## Summary

| Task | What | Key Files | Commit |
|------|------|-----------|--------|
| 1 | Shared `ams_unit_detail` XML component | `ui_xml/components/ams_unit_detail.xml` | `feat(ams): add shared ams_unit_detail` |
| 2 | C++ helper functions | `include/ui_ams_detail.h`, `src/ui/ui_ams_detail.cpp` | `feat(ams): add shared helper functions` |
| 3 | Shared `ams_loaded_card` XML component | `ui_xml/components/ams_loaded_card.xml` | `feat(ams): add shared ams_loaded_card` |
| 4 | Wire AmsPanel to shared code | `ams_panel.xml`, `ui_panel_ams.cpp/h` | `refactor(ams): wire AmsPanel to shared` |
| 5 | Wire AmsOverviewPanel to shared code | `ams_overview_panel.xml`, `ui_panel_ams_overview.cpp/h` | `refactor(ams): wire AmsOverviewPanel to shared` |
| 6 | Clean up dead code + verify | Various | `refactor(ams): remove dead code` |
