# Unified Slot Editor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Merge the "Edit" and "Assign Spool" flows into a single unified "Edit" modal that adapts based on Spoolman connection state and slot data.

**Architecture:** The edit modal gains a view-switching mechanism (form view ↔ picker view). A new `SpoolmanSlotSaver` helper encapsulates the smart save logic: detecting filament-level vs spool-level changes and orchestrating the find-or-create Spoolman API flow. The standalone `AmsSpoolmanPicker` is absorbed into the edit modal and removed.

**Tech Stack:** C++17, LVGL 9.4 XML, Catch2 tests, MoonrakerAPI mock, nlohmann/json

**Design doc:** `docs/plans/2026-02-17-unified-slot-editor-design.md`

---

## Phase 1: Smart Save Logic (Pure C++, No UI)

### Task 1: Enhance MoonrakerAPIMock for Filament Persistence

The mock's `create_spoolman_filament()` doesn't persist created filaments, and `update_spoolman_spool()` doesn't support `filament_id` patching. Fix both.

**Files:**
- Modify: `include/moonraker_api_mock.h`
- Modify: `src/api/moonraker_api_mock.cpp`
- Test: `tests/unit/test_spoolman.cpp`

**Step 1: Write failing tests**

In `tests/unit/test_spoolman.cpp`, add:

```cpp
TEST_CASE("Mock persists created filaments", "[spoolman][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    // Create a filament
    nlohmann::json filament_data;
    filament_data["material"] = "PETG";
    filament_data["name"] = "Blue";
    filament_data["color_hex"] = "#0000FF";
    filament_data["vendor_id"] = 1;

    FilamentInfo created;
    api.create_spoolman_filament(filament_data,
        [&](const FilamentInfo& f) { created = f; }, nullptr);
    REQUIRE(created.id > 0);

    // Verify it appears in subsequent filament list
    std::vector<FilamentInfo> filaments;
    api.get_spoolman_filaments(
        [&](const std::vector<FilamentInfo>& list) { filaments = list; }, nullptr);

    bool found = false;
    for (const auto& f : filaments) {
        if (f.id == created.id) {
            found = true;
            REQUIRE(f.material == "PETG");
            REQUIRE(f.color_name == "Blue");
        }
    }
    REQUIRE(found);
}

TEST_CASE("Mock update_spoolman_spool supports filament_id patch", "[spoolman][mock]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    auto& spools = api.get_mock_spools();
    int spool_id = spools[0].id;
    int original_filament_id = spools[0].filament_id;

    nlohmann::json patch;
    patch["filament_id"] = 999;

    bool success = false;
    api.update_spoolman_spool(spool_id, patch,
        [&]() { success = true; }, nullptr);

    REQUIRE(success);
    REQUIRE(spools[0].filament_id == 999);
    REQUIRE(spools[0].filament_id != original_filament_id);
}
```

**Step 2: Run tests to verify they fail**

```bash
make test && ./build/bin/helix-tests "[spoolman][mock]" -v
```

Expected: FAIL — `create_spoolman_filament` doesn't persist, `update_spoolman_spool` ignores `filament_id`.

**Step 3: Implement mock enhancements**

In `moonraker_api_mock.h`, add:
```cpp
std::vector<FilamentInfo> mock_filaments_;  // persistent filament storage
int next_filament_id_ = 300;
```

In `moonraker_api_mock.cpp`:
- `create_spoolman_filament()`: push to `mock_filaments_`, assign `next_filament_id_++`
- `get_spoolman_filaments()`: return `mock_filaments_` merged with synthesized-from-spools list
- `update_spoolman_spool()`: add `filament_id` to the patch fields it applies

**Step 4: Run tests to verify they pass**

```bash
make test && ./build/bin/helix-tests "[spoolman][mock]" -v
```

**Step 5: Commit**

```bash
git add include/moonraker_api_mock.h src/api/moonraker_api_mock.cpp tests/unit/test_spoolman.cpp
git commit -m "test(spoolman): enhance mock with filament persistence and filament_id patching"
```

---

### Task 2: SpoolmanSlotSaver — Detect Filament-Level Changes

A helper that compares old vs new SlotInfo and determines what changed. Pure logic, no async.

**Files:**
- Create: `include/spoolman_slot_saver.h`
- Create: `src/spoolman/spoolman_slot_saver.cpp`
- Create: `tests/unit/test_spoolman_slot_saver.cpp`

**Step 1: Write failing tests**

New file `tests/unit/test_spoolman_slot_saver.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "spoolman_slot_saver.h"

using namespace helix;

TEST_CASE("Detect no changes", "[spoolman][slot_saver]") {
    SlotInfo original;
    original.brand = "eSUN";
    original.material = "PLA";
    original.color_rgb = 0xFF0000;
    original.remaining_weight_g = 800;

    SlotInfo edited = original;

    auto changes = SpoolmanSlotSaver::detect_changes(original, edited);
    REQUIRE_FALSE(changes.filament_level);
    REQUIRE_FALSE(changes.spool_level);
    REQUIRE_FALSE(changes.any());
}

TEST_CASE("Detect filament-level changes", "[spoolman][slot_saver]") {
    SlotInfo original;
    original.brand = "eSUN";
    original.material = "PLA";
    original.color_rgb = 0xFF0000;

    SECTION("vendor changed") {
        SlotInfo edited = original;
        edited.brand = "Polymaker";
        auto changes = SpoolmanSlotSaver::detect_changes(original, edited);
        REQUIRE(changes.filament_level);
    }
    SECTION("material changed") {
        SlotInfo edited = original;
        edited.material = "PETG";
        auto changes = SpoolmanSlotSaver::detect_changes(original, edited);
        REQUIRE(changes.filament_level);
    }
    SECTION("color changed") {
        SlotInfo edited = original;
        edited.color_rgb = 0x00FF00;
        auto changes = SpoolmanSlotSaver::detect_changes(original, edited);
        REQUIRE(changes.filament_level);
    }
}

TEST_CASE("Detect spool-level changes", "[spoolman][slot_saver]") {
    SlotInfo original;
    original.remaining_weight_g = 800;
    original.total_weight_g = 1000;

    SlotInfo edited = original;
    edited.remaining_weight_g = 600;

    auto changes = SpoolmanSlotSaver::detect_changes(original, edited);
    REQUIRE(changes.spool_level);
    REQUIRE_FALSE(changes.filament_level);
}

TEST_CASE("Detect both filament and spool changes", "[spoolman][slot_saver]") {
    SlotInfo original;
    original.brand = "eSUN";
    original.material = "PLA";
    original.remaining_weight_g = 800;

    SlotInfo edited = original;
    edited.brand = "Polymaker";
    edited.remaining_weight_g = 600;

    auto changes = SpoolmanSlotSaver::detect_changes(original, edited);
    REQUIRE(changes.filament_level);
    REQUIRE(changes.spool_level);
    REQUIRE(changes.any());
}
```

**Step 2: Run tests to verify they fail**

```bash
make test && ./build/bin/helix-tests "[spoolman][slot_saver]" -v
```

Expected: FAIL — files don't exist.

**Step 3: Implement SpoolmanSlotSaver::detect_changes**

`include/spoolman_slot_saver.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ams_types.h"
#include "moonraker_api.h"
#include "spoolman_types.h"
#include <functional>

namespace helix {

class SpoolmanSlotSaver {
  public:
    struct ChangeSet {
        bool filament_level = false;  // vendor, material, or color changed
        bool spool_level = false;     // remaining weight changed
        [[nodiscard]] bool any() const { return filament_level || spool_level; }
    };

    /// Compare original and edited SlotInfo to determine what changed
    static ChangeSet detect_changes(const SlotInfo& original, const SlotInfo& edited);
};

}  // namespace helix
```

`src/spoolman/spoolman_slot_saver.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "spoolman_slot_saver.h"
#include <cmath>

namespace helix {

SpoolmanSlotSaver::ChangeSet SpoolmanSlotSaver::detect_changes(
    const SlotInfo& original, const SlotInfo& edited) {
    ChangeSet changes;
    changes.filament_level = (original.brand != edited.brand ||
                              original.material != edited.material ||
                              original.color_rgb != edited.color_rgb);
    changes.spool_level =
        std::abs(original.remaining_weight_g - edited.remaining_weight_g) > 0.1f;
    return changes;
}

}  // namespace helix
```

**Step 4: Run tests**

```bash
make test && ./build/bin/helix-tests "[spoolman][slot_saver]" -v
```

**Step 5: Commit**

```bash
git add include/spoolman_slot_saver.h src/spoolman/spoolman_slot_saver.cpp tests/unit/test_spoolman_slot_saver.cpp
git commit -m "feat(spoolman): add SpoolmanSlotSaver change detection"
```

---

### Task 3: SpoolmanSlotSaver — Find-or-Create Filament Flow

Add the async orchestration: search existing filaments, create if no match, re-link spool.

**Files:**
- Modify: `include/spoolman_slot_saver.h`
- Modify: `src/spoolman/spoolman_slot_saver.cpp`
- Modify: `tests/unit/test_spoolman_slot_saver.cpp`

**Step 1: Write failing tests**

```cpp
TEST_CASE("Save re-links spool to existing filament when vendor changes",
          "[spoolman][slot_saver]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    // Pre-create a filament that matches what we'll change to
    nlohmann::json fil_data;
    fil_data["material"] = "PLA";
    fil_data["name"] = "Red";
    fil_data["color_hex"] = "#FF0000";
    fil_data["vendor_id"] = 2;
    fil_data["vendor_name"] = "Polymaker";

    FilamentInfo target_filament;
    api.create_spoolman_filament(fil_data,
        [&](const FilamentInfo& f) { target_filament = f; }, nullptr);

    // Set up a spool linked to a different filament
    auto& spools = api.get_mock_spools();
    int spool_id = spools[0].id;
    int original_filament_id = spools[0].filament_id;

    SlotInfo original;
    original.spoolman_id = spool_id;
    original.brand = "eSUN";
    original.material = "PLA";
    original.color_rgb = 0xFF0000;
    original.color_name = "Red";

    SlotInfo edited = original;
    edited.brand = "Polymaker";  // changed vendor

    bool completed = false;
    SpoolmanSlotSaver saver(&api);
    saver.save(original, edited,
        [&](bool success) {
            completed = true;
            REQUIRE(success);
        });

    REQUIRE(completed);
    // Spool should now be linked to the target filament
    REQUIRE(spools[0].filament_id == target_filament.id);
}

TEST_CASE("Save creates new filament when no match exists",
          "[spoolman][slot_saver]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    auto& spools = api.get_mock_spools();
    int spool_id = spools[0].id;

    SlotInfo original;
    original.spoolman_id = spool_id;
    original.brand = "eSUN";
    original.material = "PLA";
    original.color_rgb = 0xFF0000;

    SlotInfo edited = original;
    edited.brand = "BrandNewVendor";
    edited.material = "Nylon";
    edited.color_rgb = 0x00FF00;
    edited.color_name = "Green";

    bool completed = false;
    SpoolmanSlotSaver saver(&api);
    saver.save(original, edited,
        [&](bool success) {
            completed = true;
            REQUIRE(success);
        });

    REQUIRE(completed);
    // Spool should be linked to a newly created filament
    REQUIRE(spools[0].filament_id != 0);
}

TEST_CASE("Save only updates weight when no filament-level changes",
          "[spoolman][slot_saver]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    auto& spools = api.get_mock_spools();
    int spool_id = spools[0].id;
    int original_filament_id = spools[0].filament_id;

    SlotInfo original;
    original.spoolman_id = spool_id;
    original.brand = "eSUN";
    original.material = "PLA";
    original.remaining_weight_g = 800;

    SlotInfo edited = original;
    edited.remaining_weight_g = 500;

    bool completed = false;
    SpoolmanSlotSaver saver(&api);
    saver.save(original, edited,
        [&](bool success) {
            completed = true;
            REQUIRE(success);
        });

    REQUIRE(completed);
    // Filament should NOT change
    REQUIRE(spools[0].filament_id == original_filament_id);
    // Weight should update
    REQUIRE(spools[0].remaining_weight_g == Catch::Approx(500.0));
}

TEST_CASE("Save does nothing for non-spoolman slots", "[spoolman][slot_saver]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    SlotInfo original;
    original.spoolman_id = 0;  // not linked
    original.brand = "eSUN";

    SlotInfo edited = original;
    edited.brand = "Polymaker";

    bool completed = false;
    SpoolmanSlotSaver saver(&api);
    saver.save(original, edited,
        [&](bool success) {
            completed = true;
            REQUIRE(success);  // succeeds (no-op for non-spoolman)
        });

    REQUIRE(completed);
}
```

**Step 2: Run tests to verify failure**

```bash
make test && ./build/bin/helix-tests "[spoolman][slot_saver]" -v
```

**Step 3: Implement save()**

Add to `spoolman_slot_saver.h`:
```cpp
class SpoolmanSlotSaver {
  public:
    // ... existing ChangeSet and detect_changes ...

    using CompletionCallback = std::function<void(bool success)>;

    explicit SpoolmanSlotSaver(MoonrakerAPI* api);

    /// Save slot changes. For Spoolman-linked slots, handles filament find-or-create.
    /// For non-linked slots, calls completion immediately (no-op).
    void save(const SlotInfo& original, const SlotInfo& edited, CompletionCallback on_complete);

  private:
    MoonrakerAPI* api_;

    void save_spool_weight(int spool_id, double weight_g, CompletionCallback on_complete);
    void find_or_create_filament(const SlotInfo& edited, int spool_id,
                                 CompletionCallback on_complete);
    void relink_spool(int spool_id, int filament_id, CompletionCallback on_complete);
};
```

The `save()` method:
1. Calls `detect_changes()`
2. If not Spoolman-linked or no changes → immediate success callback
3. If spool-level only → `update_spoolman_spool_weight()`
4. If filament-level → `get_spoolman_filaments()`, filter client-side for match on (vendor, material, color_hex)
5. Match found → `update_spoolman_spool()` with new `filament_id`
6. No match → `create_spoolman_filament()` then `update_spoolman_spool()` with new `filament_id`
7. If both → chain: filament relink first, then weight update

**Client-side filament matching:** `vendor_name == edited.brand && material == edited.material && color_hex matches edited.color_rgb` (convert RGB int to hex string for comparison).

**Step 4: Run tests**

```bash
make test && ./build/bin/helix-tests "[spoolman][slot_saver]" -v
```

**Step 5: Commit**

```bash
git add include/spoolman_slot_saver.h src/spoolman/spoolman_slot_saver.cpp tests/unit/test_spoolman_slot_saver.cpp
git commit -m "feat(spoolman): add SpoolmanSlotSaver find-or-create save flow"
```

**>> CODE REVIEW CHECKPOINT: Phase 1 complete. Review all changes before proceeding. <<**

---

## Phase 2: Edit Modal UI Restructuring

### Task 4: Add Picker Sub-View to Edit Modal XML

Add a container in the edit modal XML that holds the Spoolman picker list. Hidden by default, shown via subject binding.

**Files:**
- Modify: `ui_xml/ams_edit_modal.xml`
- Modify: `include/ui_ams_edit_modal.h`
- Modify: `src/ui/ui_ams_edit_modal.cpp`

**Reference docs:**
- `docs/devel/LVGL9_XML_GUIDE.md` — XML binding patterns
- `docs/devel/MODAL_SYSTEM.md` — Modal lifecycle

**Step 1: Add XML layout for picker view**

Add a new globally-registered subject `edit_modal_view` (0 = form, 1 = picker). Add a container alongside the existing form content:

```xml
<!-- Existing form content wrapped in a container -->
<lv_obj name="form_view" ...>
  <bind_flag_if_not_eq subject="edit_modal_view" flag="hidden" ref_value="0"/>
  <!-- ... existing vendor, material, color, remaining fields ... -->
</lv_obj>

<!-- New picker view -->
<lv_obj name="picker_view" hidden="true" ...>
  <bind_flag_if_not_eq subject="edit_modal_view" flag="hidden" ref_value="1"/>
  <!-- Loading spinner -->
  <lv_obj name="picker_loading">
    <bind_flag_if_not_eq subject="edit_picker_state" flag="hidden" ref_value="0"/>
    <lv_spinner .../>
  </lv_obj>
  <!-- Empty state -->
  <lv_obj name="picker_empty">
    <bind_flag_if_not_eq subject="edit_picker_state" flag="hidden" ref_value="1"/>
    <text_body text="No spools found in Spoolman"/>
  </lv_obj>
  <!-- Spool list (populated dynamically) -->
  <lv_obj name="picker_spool_list" scrollable="true">
    <bind_flag_if_not_eq subject="edit_picker_state" flag="hidden" ref_value="2"/>
  </lv_obj>
  <!-- Manual entry escape hatch -->
  <ui_button name="btn_manual_entry" text="Manual Entry" callback="ams_edit_manual_entry_cb"/>
</lv_obj>
```

**Step 2: Register subjects and callbacks in C++**

In `ui_ams_edit_modal.h`, add:
```cpp
lv_subject_t view_mode_subject_;        // 0=form, 1=picker
lv_subject_t picker_state_subject_;     // 0=loading, 1=empty, 2=content
std::vector<SpoolInfo> cached_spools_;  // from Spoolman fetch
```

Register `edit_modal_view` and `edit_picker_state` as `UI_MANAGED_SUBJECT_INT` so XML `bind_flag_if_*` can reference them.

Register `ams_edit_manual_entry_cb` static callback.

**Step 3: Implement view switching**

```cpp
void AmsEditModal::switch_to_picker() {
    lv_subject_set_int(&view_mode_subject_, 1);
    populate_picker();  // async spool fetch
}

void AmsEditModal::switch_to_form() {
    lv_subject_set_int(&view_mode_subject_, 0);
}

void AmsEditModal::handle_manual_entry() {
    switch_to_form();
}
```

**Step 4: Test manually**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Navigate to Multi-Filament → long-press a slot → Edit. Verify form view shows. (Picker switch not wired yet.)

**Step 5: Commit**

```bash
git add ui_xml/ams_edit_modal.xml include/ui_ams_edit_modal.h src/ui/ui_ams_edit_modal.cpp
git commit -m "feat(ams): add picker sub-view layout to edit modal"
```

---

### Task 5: Implement Picker Population (Absorb from AmsSpoolmanPicker)

Move the spool-list population logic from `AmsSpoolmanPicker::populate_spools()` into the edit modal.

**Files:**
- Modify: `src/ui/ui_ams_edit_modal.cpp`
- Modify: `include/ui_ams_edit_modal.h`
- Reference: `src/ui/ui_ams_spoolman_picker.cpp` (source of logic to absorb)

**Step 1: Implement populate_picker()**

Port from `AmsSpoolmanPicker::populate_spools()`:
```cpp
void AmsEditModal::populate_picker() {
    lv_subject_set_int(&picker_state_subject_, 0);  // loading

    std::weak_ptr<bool> guard = callback_guard_;
    api_->get_spoolman_spools(
        [this, guard](const std::vector<SpoolInfo>& spools) {
            if (guard.expired()) return;
            cached_spools_ = spools;

            if (spools.empty()) {
                lv_subject_set_int(&picker_state_subject_, 1);  // empty
                return;
            }

            lv_subject_set_int(&picker_state_subject_, 2);  // content

            lv_obj_t* list = find_widget("picker_spool_list");
            lv_obj_clean(list);  // clear previous items

            for (const auto& spool : spools) {
                // Reuse spoolman_spool_item XML component
                lv_obj_t* item = lv_xml_create(list, "spoolman_spool_item", nullptr);
                lv_obj_set_user_data(item, reinterpret_cast<void*>(spool.id));
                // ... fill labels, color swatch, checkmark for current ...
            }
        },
        [this, guard](const MoonrakerError&) {
            if (guard.expired()) return;
            lv_subject_set_int(&picker_state_subject_, 1);
        });
}
```

**Step 2: Handle spool item click**

Register `ams_edit_spool_item_cb` callback. On click:
```cpp
void AmsEditModal::handle_spool_selected(int spool_id) {
    for (const auto& spool : cached_spools_) {
        if (spool.id == spool_id) {
            // Auto-fill working_info_ from selected spool
            working_info_.spoolman_id = spool.id;
            working_info_.brand = spool.vendor;
            working_info_.material = spool.material;
            working_info_.color_name = spool.color_name;
            working_info_.color_rgb = parse_color_hex(spool.color_hex);
            working_info_.remaining_weight_g = spool.remaining_weight_g;
            working_info_.total_weight_g = spool.initial_weight_g;
            working_info_.nozzle_temp_min = spool.nozzle_temp_min;
            working_info_.nozzle_temp_max = spool.nozzle_temp_max;
            working_info_.bed_temp = spool.bed_temp_recommended;
            break;
        }
    }
    switch_to_form();
    update_ui();  // refresh form with new data
}
```

**Step 3: Test manually**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

(Will need to temporarily wire a button to `switch_to_picker()` to test.)

**Step 4: Commit**

```bash
git add include/ui_ams_edit_modal.h src/ui/ui_ams_edit_modal.cpp
git commit -m "feat(ams): absorb spoolman picker into edit modal"
```

---

### Task 6: First-View Logic and Spoolman Action Buttons

Implement the smart first-view selection and add Change Spool / Link / Unlink buttons.

**Files:**
- Modify: `ui_xml/ams_edit_modal.xml`
- Modify: `src/ui/ui_ams_edit_modal.cpp`
- Modify: `include/ui_ams_edit_modal.h`

**Step 1: Add Spoolman action buttons to form view XML**

In the form view, add below the existing fields:
```xml
<!-- Spoolman actions (only visible when Spoolman connected) -->
<lv_obj name="spoolman_actions" hidden="true">
  <bind_flag_if_eq subject="printer_has_spoolman" flag="hidden" ref_value="0"/>

  <!-- Show "Change Spool" when linked, "Link to Spoolman" when not -->
  <ui_button name="btn_change_spool" text="Change Spool"
             callback="ams_edit_change_spool_cb"/>
  <ui_button name="btn_unlink" text="Unlink" hidden="true"
             callback="ams_edit_unlink_cb"/>
</lv_obj>
```

**Step 2: Implement first-view logic in show_for_slot()**

```cpp
bool AmsEditModal::show_for_slot(lv_obj_t* parent, int slot_index,
                                 const SlotInfo& initial_info, MoonrakerAPI* api) {
    // ... existing setup ...

    // Determine first view
    bool has_spoolman = PrinterState::instance().has_spoolman();
    bool slot_empty = initial_info.material.empty() && initial_info.brand.empty();

    if (has_spoolman && slot_empty && initial_info.spoolman_id == 0) {
        switch_to_picker();  // Smart default: picker first for empty + Spoolman
    } else {
        switch_to_form();    // Otherwise: show form with current data
    }

    return true;
}
```

**Step 3: Implement button handlers**

```cpp
void AmsEditModal::handle_change_spool() {
    switch_to_picker();
}

void AmsEditModal::handle_unlink() {
    working_info_.spoolman_id = 0;
    working_info_.spool_name.clear();
    update_ui();
    update_spoolman_button_state();  // hide unlink, show "Link to Spoolman"
}
```

**Step 4: Implement update_spoolman_button_state()**

Called from `update_ui()` and `handle_unlink()`:
```cpp
void AmsEditModal::update_spoolman_button_state() {
    lv_obj_t* btn_change = find_widget("btn_change_spool");
    lv_obj_t* btn_unlink = find_widget("btn_unlink");
    if (!btn_change) return;

    if (working_info_.spoolman_id > 0) {
        // Show "Change Spool" text + unlink option
        lv_label_set_text(lv_obj_find_by_name(btn_change, "label"), "Change Spool");
        if (btn_unlink) lv_obj_remove_flag(btn_unlink, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Show "Link to Spoolman"
        lv_label_set_text(lv_obj_find_by_name(btn_change, "label"), "Link to Spoolman");
        if (btn_unlink) lv_obj_add_flag(btn_unlink, LV_OBJ_FLAG_HIDDEN);
    }
}
```

**Step 5: Test manually**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Test all four first-view combinations:
- No Spoolman + empty slot → form
- No Spoolman + has data → form
- Spoolman + empty slot → picker
- Spoolman + has data → form with "Change Spool" button

**Step 6: Commit**

```bash
git add ui_xml/ams_edit_modal.xml include/ui_ams_edit_modal.h src/ui/ui_ams_edit_modal.cpp
git commit -m "feat(ams): add first-view logic and spoolman action buttons to edit modal"
```

---

### Task 7: Wire SpoolmanSlotSaver into Edit Modal Save

Replace the simple save with the smart save that handles filament find-or-create.

**Files:**
- Modify: `src/ui/ui_ams_edit_modal.cpp`
- Modify: `include/ui_ams_edit_modal.h`
- Modify: `src/ui/ui_panel_ams.cpp`

**Step 1: Update handle_save() to use SpoolmanSlotSaver**

```cpp
void AmsEditModal::handle_save() {
    if (working_info_.spoolman_id > 0 && api_) {
        // Use smart save for Spoolman-linked slots
        auto changes = SpoolmanSlotSaver::detect_changes(original_info_, working_info_);
        if (changes.any()) {
            std::weak_ptr<bool> guard = callback_guard_;
            SpoolmanSlotSaver saver(api_);
            saver.save(original_info_, working_info_,
                [this, guard](bool success) {
                    if (guard.expired()) return;
                    if (success) {
                        fire_completion(true);
                    } else {
                        spdlog::error("[AmsEditModal] Spoolman save failed");
                        fire_completion(true);  // still save locally
                    }
                });
            return;  // async — don't hide yet
        }
    }

    // Non-spoolman or no changes: save locally immediately
    fire_completion(true);
}

void AmsEditModal::fire_completion(bool saved) {
    if (completion_callback_) {
        EditResult result;
        result.saved = saved;
        result.slot_index = slot_index_;
        result.slot_info = working_info_;
        completion_callback_(result);
    }
    hide();
}
```

**Step 2: Update AmsPanel completion callback**

In `AmsPanel::show_edit_modal()`, the completion callback stays mostly the same but now the `working_info_` may have been updated with a new `spoolman_id` from the picker flow, and Spoolman has already been updated by the saver.

**Step 3: Test manually**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Test: edit a Spoolman-linked slot, change the vendor, save. Check mock logs for filament search + spool relink.

**Step 4: Commit**

```bash
git add include/ui_ams_edit_modal.h src/ui/ui_ams_edit_modal.cpp src/ui/ui_panel_ams.cpp
git commit -m "feat(ams): wire SpoolmanSlotSaver into edit modal save flow"
```

**>> CODE REVIEW CHECKPOINT: Phase 2 complete. Review before cleanup. <<**

---

## Phase 3: Context Menu Cleanup

### Task 8: Remove "Assign Spool" from Context Menu

**Files:**
- Modify: `ui_xml/ams_context_menu.xml`
- Modify: `src/ui/ui_ams_context_menu.cpp`
- Modify: `include/ui_ams_context_menu.h`

**Step 1: Remove from XML**

Delete the `btn_spoolman` button and its `bind_flag_if_eq` from `ams_context_menu.xml`.

**Step 2: Remove from C++**

- Remove `MenuAction::SPOOLMAN` enum value (or keep for backwards compat but unused)
- Remove `handle_spoolman()` method
- Remove `ams_context_spoolman_cb` callback registration
- Remove the SPOOLMAN case from AmsPanel's action callback switch

**Step 3: Remove show_spoolman_picker from AmsPanel**

Delete `AmsPanel::show_spoolman_picker()` method and the `spoolman_picker_` member.

**Step 4: Test manually**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Context menu should show only: Load, Unload, Edit.

**Step 5: Commit**

```bash
git add ui_xml/ams_context_menu.xml src/ui/ui_ams_context_menu.cpp include/ui_ams_context_menu.h src/ui/ui_panel_ams.cpp include/ui_panel_ams.h
git commit -m "refactor(ams): remove standalone Assign Spool from context menu"
```

---

### Task 9: Remove Standalone AmsSpoolmanPicker

**Files:**
- Delete: `include/ui_ams_spoolman_picker.h`
- Delete: `src/ui/ui_ams_spoolman_picker.cpp`
- Modify: `ui_xml/spoolman_picker_modal.xml` — keep if `spoolman_spool_item` is reused, else delete
- Modify: `src/main.cpp` — remove any component/callback registration for the old picker

**Step 1: Check for other references**

Search for `AmsSpoolmanPicker`, `spoolman_picker`, and callback names to ensure nothing else uses them.

**Step 2: Remove files and references**

Delete the .h/.cpp files. Remove `#include` references. Remove callback registrations from main.cpp. Keep `spoolman_spool_item.xml` if the edit modal reuses it for picker list items.

**Step 3: Build and test**

```bash
make -j && ./build/bin/helix-tests --list-tests | grep -i spoolman
./build/bin/helix-screen --test -vv
```

Verify no build errors, no test references to removed picker.

**Step 4: Commit**

```bash
git add -u  # careful: only the removed/modified files
git commit -m "refactor(ams): remove standalone AmsSpoolmanPicker (absorbed into edit modal)"
```

---

### Task 10: Run Full Test Suite and Final Verification

**Step 1: Run all tests**

```bash
make test-run
```

**Step 2: Run spoolman-specific tests**

```bash
./build/bin/helix-tests "[spoolman]" -v
```

**Step 3: Manual smoke test**

```bash
./build/bin/helix-screen --test -vv
```

Test matrix:
- [ ] Tool changer: Edit empty slot → form shows
- [ ] Tool changer: Edit slot with data → form with data
- [ ] With Spoolman mock: Edit empty slot → picker shows first
- [ ] With Spoolman mock: Edit slot with data → form shows, has "Change Spool" button
- [ ] Click "Change Spool" → picker shows, select spool → form auto-fills
- [ ] Edit vendor on linked spool → save → check logs for filament find-or-create
- [ ] "Unlink" → spoolman_id cleared
- [ ] "Manual Entry" from picker → switches to form
- [ ] Context menu: only Load, Unload, Edit visible

**Step 4: Final commit if any fixes needed**

**>> CODE REVIEW CHECKPOINT: Full implementation complete. Final review. <<**
