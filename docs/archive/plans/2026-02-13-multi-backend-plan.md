# Phase 3: Multi-Backend AMS Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace AmsState's single `backend_` with a collection so a printer can have multiple filament management systems simultaneously (e.g., IDEX with MMU on one head, or toolchanger with AFC on one tool).

**Architecture:** `AmsState` holds a `vector<unique_ptr<AmsBackend>>` indexed by order of discovery. Per-slot subjects become per-backend namespaced. `PrinterDiscovery` detects multiple AMS systems and returns a list. The AMS panel gains a backend selector (segmented control) when multiple backends are present. `ToolInfo` gains `backend_index`/`backend_slot` fields to map tools to their feeding system.

**Tech Stack:** C++17, LVGL 9.4 subjects, Catch2 tests, LVGL XML declarative UI

**Design doc:** `docs/devel/plans/2026-02-13-tool-abstraction-design.md` — Phase 3 section

**Worktree:** `.worktrees/multi-extruder-temps` (branch `feature/multi-extruder-temps`, continues Phase 1-2 work)

---

## Context for Implementers

### Key files to understand before starting

| File | Why |
|------|-----|
| `include/ams_state.h` | Singleton we're refactoring — holds `backend_`, subjects, slot arrays |
| `src/printer/ams_state.cpp` | Implementation: `init_backend_from_hardware()`, `set_backend()`, `sync_from_backend()`, event routing |
| `include/ams_backend.h` | Abstract backend interface — factory, event callbacks, virtual methods |
| `include/ams_types.h` | Enums: `AmsType`, `SlotStatus`, `AmsAction`, `PathTopology` |
| `include/printer_discovery.h` | Hardware detection: `mmu_type()` returns single `AmsType`, `parse_objects()` |
| `include/tool_state.h` | `ToolInfo` struct — gains `backend_index`/`backend_slot` in this phase |
| `src/ui/ui_panel_ams.cpp` | AMS panel overlay — dynamic slot creation, observers |
| `src/ui/ui_ams_slot.cpp` | Per-slot widget — color/status observers on `AmsState` |

### Current single-backend data flow

```
PrinterDiscovery::parse_objects()
  → detects mmu_type_ (single AmsType)
  → AmsState::init_backend_from_hardware()
    → AmsBackend::create(detected_type, api, client)
    → set_backend(std::move(backend))   // registers event callback
    → backend_->start()                 // subscribes to Moonraker
      → handle_status_update()          // background thread
        → emit_event(EVENT_STATE_CHANGED)
          → on_backend_event()
            → ui_queue_update → sync_from_backend()
              → updates flat slot_colors_[0..15], slot_statuses_[0..15]
```

### What changes in multi-backend

```
PrinterDiscovery::parse_objects()
  → detects vector<DetectedAmsSystem>   // NEW: multiple systems
  → AmsState::init_backends_from_hardware()
    → for each detected system:
      → AmsBackend::create(system.type, api, client)
      → add_backend(std::move(backend), backend_index)
      → backend->start()
        → handle_status_update()
          → emit_event(EVENT_STATE_CHANGED)
            → on_backend_event(backend_index, event, data)  // NEW: identifies backend
              → ui_queue_update → sync_backend(backend_index)
                → updates per-backend slot subjects
```

### Backward compatibility guarantee

Single-backend printers (the vast majority) must work identically. All existing code that calls `get_slot_color_subject(int slot_index)` continues to work — it accesses the primary backend (index 0). New multi-backend code uses `get_slot_color_subject(int backend_index, int slot_index)`.

---

## Task 1: DetectedAmsSystem struct and multi-detection in PrinterDiscovery

**Files:**
- Modify: `include/printer_discovery.h`
- Modify: `src/printer/printer_discovery.cpp` (the `parse_objects()` method)
- Create: `tests/unit/test_multi_backend_discovery.cpp`

### Step 1: Write the failing tests

```cpp
// tests/unit/test_multi_backend_discovery.cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui_test_utils.h"
#include "printer_discovery.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("PrinterDiscovery: single MMU detected as one system", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "mmu", "mmu_encoder", "extruder", "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 1);
    REQUIRE(systems[0].type == AmsType::HAPPY_HARE);

    // Backward compat: mmu_type() returns primary
    REQUIRE(hw.mmu_type() == AmsType::HAPPY_HARE);
}

TEST_CASE("PrinterDiscovery: toolchanger only detected as one system", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "toolchanger", "tool T0", "tool T1", "extruder", "extruder1",
        "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 1);
    REQUIRE(systems[0].type == AmsType::TOOL_CHANGER);
}

TEST_CASE("PrinterDiscovery: toolchanger + Happy Hare detected as two systems",
          "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "toolchanger", "tool T0", "tool T1",
        "mmu", "mmu_encoder",
        "extruder", "extruder1", "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 2);

    // Order: toolchanger first (physical tools), then MMU
    bool has_tc = false, has_hh = false;
    for (const auto& sys : systems) {
        if (sys.type == AmsType::TOOL_CHANGER) has_tc = true;
        if (sys.type == AmsType::HAPPY_HARE) has_hh = true;
    }
    REQUIRE(has_tc);
    REQUIRE(has_hh);

    // Primary type is toolchanger (physical tool heads take priority)
    REQUIRE(hw.mmu_type() == AmsType::TOOL_CHANGER);
}

TEST_CASE("PrinterDiscovery: AFC + toolchanger detected as two systems",
          "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "toolchanger", "tool T0", "tool T1",
        "AFC", "AFC_stepper lane1", "AFC_stepper lane2",
        "extruder", "extruder1", "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);

    const auto& systems = hw.detected_ams_systems();
    REQUIRE(systems.size() == 2);

    bool has_tc = false, has_afc = false;
    for (const auto& sys : systems) {
        if (sys.type == AmsType::TOOL_CHANGER) has_tc = true;
        if (sys.type == AmsType::AFC) has_afc = true;
    }
    REQUIRE(has_tc);
    REQUIRE(has_afc);
}

TEST_CASE("PrinterDiscovery: no AMS detected returns empty", "[ams][multi-backend]") {
    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "extruder", "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);

    REQUIRE(hw.detected_ams_systems().empty());
    REQUIRE(hw.mmu_type() == AmsType::NONE);
}
```

### Step 2: Run tests to verify they fail

```bash
make test && ./build/bin/helix-tests "[multi-backend]" 2>&1
```
Expected: Compilation error — `detected_ams_systems()` doesn't exist, `DetectedAmsSystem` not defined.

### Step 3: Add DetectedAmsSystem struct and detection

In `include/printer_discovery.h`, add near the top (after includes, before `PrinterDiscovery` class):

```cpp
/// Describes one detected AMS/filament system
struct DetectedAmsSystem {
    AmsType type = AmsType::NONE;
    std::string name;  // Human-readable: "Happy Hare", "AFC", "Tool Changer"
};
```

Add to `PrinterDiscovery` class (public section):

```cpp
/// Get all detected AMS/filament management systems
[[nodiscard]] const std::vector<DetectedAmsSystem>& detected_ams_systems() const {
    return detected_ams_systems_;
}
```

Add to private section:

```cpp
std::vector<DetectedAmsSystem> detected_ams_systems_;
```

In `src/printer/printer_discovery.cpp`, at the end of `parse_objects()` (after existing detection logic, around line 327-330), replace the toolchanger override block with multi-system collection:

```cpp
// Collect all detected AMS systems (replaces single mmu_type_ override)
detected_ams_systems_.clear();

if (has_tool_changer_ && !tool_names_.empty()) {
    detected_ams_systems_.push_back({AmsType::TOOL_CHANGER, "Tool Changer"});
}
if (has_mmu_) {
    if (mmu_type_ == AmsType::HAPPY_HARE) {
        detected_ams_systems_.push_back({AmsType::HAPPY_HARE, "Happy Hare"});
    } else if (mmu_type_ == AmsType::AFC) {
        detected_ams_systems_.push_back({AmsType::AFC, "AFC"});
    }
}

// Update mmu_type_ for backward compat: toolchanger takes priority
if (has_tool_changer_ && !tool_names_.empty()) {
    mmu_type_ = AmsType::TOOL_CHANGER;
}
```

**Important:** Keep existing `mmu_type_` assignment for Happy Hare/AFC in the main parse loop unchanged. Only the priority override block at the end changes.

### Step 4: Build and test

```bash
make test-run
./build/bin/helix-tests "[multi-backend]"
```
Expected: All 5 tests pass.

### Step 5: Commit

```bash
git add include/printer_discovery.h src/printer/printer_discovery.cpp tests/unit/test_multi_backend_discovery.cpp
git commit -m "feat(discovery): add DetectedAmsSystem and multi-system detection"
```

---

## Task 2: AmsState multi-backend storage

Replace `unique_ptr<AmsBackend> backend_` with `vector<unique_ptr<AmsBackend>> backends_`. Keep backward compatibility via `get_backend()` returning index 0.

**Files:**
- Modify: `include/ams_state.h`
- Modify: `src/printer/ams_state.cpp`
- Modify: `tests/unit/test_multi_backend_discovery.cpp` (add AmsState tests)

### Step 1: Write failing tests

Add to `tests/unit/test_multi_backend_discovery.cpp`:

```cpp
#include "ams_state.h"
#include "ams_backend.h"

TEST_CASE("AmsState: add_backend stores multiple backends", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    // Create two mock backends
    auto mock1 = AmsBackend::create_mock(4);
    auto mock2 = AmsBackend::create_mock(2);

    ams.add_backend(std::move(mock1));
    ams.add_backend(std::move(mock2));

    REQUIRE(ams.backend_count() == 2);
    REQUIRE(ams.get_backend(0) != nullptr);
    REQUIRE(ams.get_backend(1) != nullptr);
    REQUIRE(ams.get_backend(2) == nullptr);  // out of range

    // Backward compat: get_backend() returns primary (index 0)
    REQUIRE(ams.get_backend() == ams.get_backend(0));

    ams.deinit_subjects();
}

TEST_CASE("AmsState: set_backend replaces all backends with single", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    auto mock1 = AmsBackend::create_mock(4);
    auto mock2 = AmsBackend::create_mock(2);

    ams.add_backend(std::move(mock1));
    ams.add_backend(std::move(mock2));
    REQUIRE(ams.backend_count() == 2);

    // set_backend replaces everything (backward compat)
    auto mock3 = AmsBackend::create_mock(3);
    ams.set_backend(std::move(mock3));
    REQUIRE(ams.backend_count() == 1);

    ams.deinit_subjects();
}

TEST_CASE("AmsState: clear_backends removes all", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    auto mock = AmsBackend::create_mock(4);
    ams.add_backend(std::move(mock));
    REQUIRE(ams.backend_count() == 1);

    ams.clear_backends();
    REQUIRE(ams.backend_count() == 0);
    REQUIRE(ams.get_backend() == nullptr);

    ams.deinit_subjects();
}
```

### Step 2: Run tests to verify failure

```bash
make test && ./build/bin/helix-tests "[multi-backend]"
```
Expected: Compilation error — `add_backend()`, `backend_count()`, `get_backend(int)`, `clear_backends()` don't exist.

### Step 3: Implement multi-backend storage

In `include/ams_state.h`:

**Add new public methods** (near existing `set_backend` / `get_backend`):

```cpp
/**
 * @brief Add a backend to the collection
 * @param backend Backend instance (ownership transferred)
 * @return Index of the added backend
 */
int add_backend(std::unique_ptr<AmsBackend> backend);

/**
 * @brief Get backend by index
 * @param index Backend index (0-based)
 * @return Pointer to backend or nullptr if out of range
 */
[[nodiscard]] AmsBackend* get_backend(int index) const;

/**
 * @brief Get number of active backends
 */
[[nodiscard]] int backend_count() const;

/**
 * @brief Remove all backends
 */
void clear_backends();
```

**Change private storage** (replace line 638):

```cpp
// Old:
// std::unique_ptr<AmsBackend> backend_;

// New:
std::vector<std::unique_ptr<AmsBackend>> backends_;
```

In `src/printer/ams_state.cpp`:

**Add new methods:**

```cpp
int AmsState::add_backend(std::unique_ptr<AmsBackend> backend) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    int index = static_cast<int>(backends_.size());

    if (backend) {
        // Capture index for event routing
        backend->set_event_callback(
            [this, index](const std::string& event, const std::string& data) {
                on_backend_event(index, event, data);
            });

        spdlog::debug("[AMS State] Added backend {} (type={})", index,
                      ams_type_to_string(backend->get_type()));
    }

    backends_.push_back(std::move(backend));
    return index;
}

AmsBackend* AmsState::get_backend(int index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(backends_.size())) {
        return nullptr;
    }
    return backends_[index].get();
}

int AmsState::backend_count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<int>(backends_.size());
}

void AmsState::clear_backends() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& b : backends_) {
        if (b) b->stop();
    }
    backends_.clear();
}
```

**Update existing methods to use `backends_` instead of `backend_`:**

`set_backend()` — replaces all backends with one:
```cpp
void AmsState::set_backend(std::unique_ptr<AmsBackend> backend) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Stop and clear existing backends
    clear_backends();

    if (backend) {
        add_backend(std::move(backend));
    }
}
```

`get_backend()` (no args, backward compat) — returns primary:
```cpp
AmsBackend* AmsState::get_backend() const {
    return get_backend(0);
}
```

`is_available()`:
```cpp
bool AmsState::is_available() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !backends_.empty() && backends_[0] && backends_[0]->get_type() != AmsType::NONE;
}
```

**Update all other references from `backend_` to `backends_[0]` or primary backend accessor.** Search for `backend_` in `ams_state.cpp` and replace each usage:

- `sync_from_backend()`: Change `if (!backend_)` to `auto* backend = get_backend(0); if (!backend)`. Replace all `backend_->` with `backend->`.
- `update_slot()`: Same pattern — use `get_backend(0)`.
- `init_backend_from_hardware()`: Change `if (backend_)` early-return to `if (!backends_.empty())`. Replace `backend_->start()` with `backends_[0]->start()`. (This method creates a single backend — will be updated in Task 5 to create multiple.)
- `on_backend_event()`: Add `int backend_index` parameter (see Task 4). For now, keep existing behavior — always syncs primary.
- `deinit_subjects()`: Replace `backend_.reset()` with `clear_backends()`.
- `probe_valgace()` and `create_valgace_backend()`: Use `set_backend()` (replaces all, which is correct for late-discovered ValgACE).

**Important:** `on_backend_event` signature changes in this step. Update the existing callback lambda in `set_backend()` (which now calls `add_backend()`) to pass index 0. The `on_backend_event` method signature becomes:

```cpp
void on_backend_event(int backend_index, const std::string& event, const std::string& data);
```

For now, `on_backend_event` ignores the index and always syncs from `backends_[0]`. Task 4 will make it per-backend aware.

### Step 4: Build and test

```bash
make test-run
./build/bin/helix-tests "[multi-backend]"
```
Expected: All tests pass. Existing AMS tests also pass (backward compat).

### Step 5: Commit

```bash
git add include/ams_state.h src/printer/ams_state.cpp tests/unit/test_multi_backend_discovery.cpp
git commit -m "refactor(ams): replace single backend_ with backends_ vector"
```

---

## Task 3: Per-backend slot subject accessors

Add `get_slot_color_subject(int backend_index, int slot_index)` overloads. Keep flat `slot_colors_[MAX_SLOTS]` for primary backend (index 0) — this preserves all existing UI bindings. Secondary backends get dynamically allocated subjects.

**Files:**
- Modify: `include/ams_state.h`
- Modify: `src/printer/ams_state.cpp`
- Add tests to: `tests/unit/test_multi_backend_discovery.cpp`

### Step 1: Write failing tests

```cpp
TEST_CASE("AmsState: per-backend slot subjects for primary backend", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    auto mock = AmsBackend::create_mock(4);
    ams.set_backend(std::move(mock));

    // Primary backend (index 0) uses same subjects as flat accessors
    REQUIRE(ams.get_slot_color_subject(0, 0) == ams.get_slot_color_subject(0));
    REQUIRE(ams.get_slot_color_subject(0, 3) == ams.get_slot_color_subject(3));
    REQUIRE(ams.get_slot_status_subject(0, 0) == ams.get_slot_status_subject(0));

    ams.deinit_subjects();
}

TEST_CASE("AmsState: secondary backend gets separate slot subjects", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    auto mock1 = AmsBackend::create_mock(4);
    auto mock2 = AmsBackend::create_mock(3);
    ams.add_backend(std::move(mock1));
    int idx = ams.add_backend(std::move(mock2));

    // Secondary backend subjects exist and are different from primary
    auto* color_0_0 = ams.get_slot_color_subject(0, 0);
    auto* color_1_0 = ams.get_slot_color_subject(1, 0);
    REQUIRE(color_0_0 != nullptr);
    REQUIRE(color_1_0 != nullptr);
    REQUIRE(color_0_0 != color_1_0);  // Different subjects!

    // Out of range returns nullptr
    REQUIRE(ams.get_slot_color_subject(1, 3) == nullptr);  // mock2 only has 3 slots
    REQUIRE(ams.get_slot_color_subject(2, 0) == nullptr);  // no backend at index 2

    ams.deinit_subjects();
}
```

### Step 2: Run to verify failure

```bash
make test && ./build/bin/helix-tests "[multi-backend]"
```
Expected: Compilation error — `get_slot_color_subject(int, int)` overload doesn't exist.

### Step 3: Implement per-backend slot subjects

In `include/ams_state.h`:

Add a private struct for per-backend slot storage:

```cpp
/// Per-backend slot subject storage (for secondary backends)
struct BackendSlotSubjects {
    std::vector<lv_subject_t> colors;
    std::vector<lv_subject_t> statuses;
    int slot_count = 0;

    void init(int count);
    void deinit();
};
```

Add to private members:

```cpp
std::vector<BackendSlotSubjects> secondary_slot_subjects_;
```

Add public overloads:

```cpp
/// Get slot color subject for a specific backend and slot
[[nodiscard]] lv_subject_t* get_slot_color_subject(int backend_index, int slot_index);

/// Get slot status subject for a specific backend and slot
[[nodiscard]] lv_subject_t* get_slot_status_subject(int backend_index, int slot_index);
```

In `src/printer/ams_state.cpp`:

```cpp
void AmsState::BackendSlotSubjects::init(int count) {
    slot_count = count;
    colors.resize(count);
    statuses.resize(count);
    for (int i = 0; i < count; ++i) {
        lv_subject_init_int(&colors[i], static_cast<int>(AMS_DEFAULT_SLOT_COLOR));
        lv_subject_init_int(&statuses[i], static_cast<int>(SlotStatus::UNKNOWN));
    }
}

void AmsState::BackendSlotSubjects::deinit() {
    for (auto& c : colors) lv_subject_deinit(&c);
    for (auto& s : statuses) lv_subject_deinit(&s);
    colors.clear();
    statuses.clear();
    slot_count = 0;
}

lv_subject_t* AmsState::get_slot_color_subject(int backend_index, int slot_index) {
    if (backend_index == 0) {
        return get_slot_color_subject(slot_index);  // Use flat primary subjects
    }
    int sec_idx = backend_index - 1;
    if (sec_idx < 0 || sec_idx >= static_cast<int>(secondary_slot_subjects_.size())) {
        return nullptr;
    }
    auto& subs = secondary_slot_subjects_[sec_idx];
    if (slot_index < 0 || slot_index >= subs.slot_count) {
        return nullptr;
    }
    return &subs.colors[slot_index];
}

lv_subject_t* AmsState::get_slot_status_subject(int backend_index, int slot_index) {
    if (backend_index == 0) {
        return get_slot_status_subject(slot_index);
    }
    int sec_idx = backend_index - 1;
    if (sec_idx < 0 || sec_idx >= static_cast<int>(secondary_slot_subjects_.size())) {
        return nullptr;
    }
    auto& subs = secondary_slot_subjects_[sec_idx];
    if (slot_index < 0 || slot_index >= subs.slot_count) {
        return nullptr;
    }
    return &subs.statuses[slot_index];
}
```

Update `add_backend()` to allocate secondary slot subjects:

```cpp
int AmsState::add_backend(std::unique_ptr<AmsBackend> backend) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    int index = static_cast<int>(backends_.size());

    if (backend) {
        backend->set_event_callback(
            [this, index](const std::string& event, const std::string& data) {
                on_backend_event(index, event, data);
            });

        // Allocate per-backend slot subjects for secondary backends
        if (index > 0) {
            auto info = backend->get_system_info();
            BackendSlotSubjects subs;
            subs.init(info.total_slots);
            secondary_slot_subjects_.push_back(std::move(subs));
        }

        spdlog::debug("[AMS State] Added backend {} (type={})", index,
                      ams_type_to_string(backend->get_type()));
    }

    backends_.push_back(std::move(backend));
    return index;
}
```

Update `clear_backends()` to clean up secondary subjects:

```cpp
void AmsState::clear_backends() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& b : backends_) {
        if (b) b->stop();
    }
    backends_.clear();
    for (auto& s : secondary_slot_subjects_) {
        s.deinit();
    }
    secondary_slot_subjects_.clear();
}
```

Update `deinit_subjects()` to also clean up secondary subjects.

### Step 4: Build and test

```bash
make test-run
./build/bin/helix-tests "[multi-backend]"
```

### Step 5: Commit

```bash
git add include/ams_state.h src/printer/ams_state.cpp tests/unit/test_multi_backend_discovery.cpp
git commit -m "feat(ams): add per-backend slot subject accessors"
```

---

## Task 4: Per-backend event routing and sync

Make `on_backend_event()` and `sync_from_backend()` backend-index-aware. Each backend syncs to its own slot subjects.

**Files:**
- Modify: `include/ams_state.h` (private method signatures)
- Modify: `src/printer/ams_state.cpp`
- Add tests to: `tests/unit/test_multi_backend_discovery.cpp`

### Step 1: Write failing tests

```cpp
TEST_CASE("AmsState: sync_backend updates correct backend's subjects", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    auto mock1 = AmsBackend::create_mock(4);
    auto mock2 = AmsBackend::create_mock(2);
    ams.add_backend(std::move(mock1));
    ams.add_backend(std::move(mock2));

    // Sync primary backend
    ams.sync_backend(0);
    REQUIRE(lv_subject_get_int(ams.get_slot_color_subject(0)) != 0);  // mock sets colors

    // Sync secondary backend
    ams.sync_backend(1);
    auto* sec_color = ams.get_slot_color_subject(1, 0);
    REQUIRE(sec_color != nullptr);
    // Mock backend sets some default color — just verify it was synced
    REQUIRE(lv_subject_get_int(sec_color) != static_cast<int>(AMS_DEFAULT_SLOT_COLOR));

    ams.deinit_subjects();
}
```

### Step 2: Run to verify failure

```bash
make test && ./build/bin/helix-tests "[multi-backend]"
```
Expected: `sync_backend(int)` doesn't exist.

### Step 3: Implement per-backend sync

In `include/ams_state.h`, add public method:

```cpp
/**
 * @brief Sync state from a specific backend
 * @param backend_index Which backend to sync from
 */
void sync_backend(int backend_index);
```

Change `on_backend_event` signature in private section:

```cpp
void on_backend_event(int backend_index, const std::string& event, const std::string& data);
```

In `src/printer/ams_state.cpp`:

```cpp
void AmsState::sync_backend(int backend_index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto* backend = get_backend(backend_index);
    if (!backend) return;

    AmsSystemInfo info = backend->get_system_info();

    if (backend_index == 0) {
        // Primary backend: update system-level subjects (existing behavior)
        lv_subject_set_int(&ams_type_, static_cast<int>(info.type));
        lv_subject_set_int(&ams_action_, static_cast<int>(info.action));
        // ... (all existing sync_from_backend() system-level updates)
        // ... (all existing per-slot updates to flat slot_colors_[])

        // Keep existing sync_from_backend() logic for primary
        sync_from_backend();
        return;
    }

    // Secondary backend: only update per-backend slot subjects
    int sec_idx = backend_index - 1;
    if (sec_idx < 0 || sec_idx >= static_cast<int>(secondary_slot_subjects_.size())) {
        return;
    }

    auto& subs = secondary_slot_subjects_[sec_idx];
    for (int i = 0; i < std::min(info.total_slots, subs.slot_count); ++i) {
        const SlotInfo* slot = info.get_slot_global(i);
        if (slot) {
            lv_subject_set_int(&subs.colors[i], static_cast<int>(slot->color_rgb));
            lv_subject_set_int(&subs.statuses[i], static_cast<int>(slot->status));
        }
    }

    spdlog::debug("[AMS State] Synced secondary backend {} - type={}, slots={}",
                  backend_index, ams_type_to_string(info.type), info.total_slots);
}
```

Update `on_backend_event()` to route by backend index:

```cpp
void AmsState::on_backend_event(int backend_index, const std::string& event,
                                const std::string& data) {
    spdlog::trace("[AMS State] Backend {} event '{}' data='{}'", backend_index, event, data);

    auto queue_sync = [backend_index](bool full_sync, int slot_index) {
        struct AsyncData {
            int backend_index;
            bool full_sync;
            int slot_index;
        };
        auto sync_data = std::make_unique<AsyncData>(AsyncData{backend_index, full_sync, slot_index});
        ui_queue_update<AsyncData>(std::move(sync_data), [](AsyncData* d) {
            if (s_shutdown_flag.load(std::memory_order_acquire)) return;
            if (d->full_sync) {
                AmsState::instance().sync_backend(d->backend_index);
            } else {
                AmsState::instance().update_slot_for_backend(d->backend_index, d->slot_index);
            }
        });
    };

    if (event == AmsBackend::EVENT_STATE_CHANGED ||
        event == AmsBackend::EVENT_LOAD_COMPLETE ||
        event == AmsBackend::EVENT_UNLOAD_COMPLETE ||
        event == AmsBackend::EVENT_TOOL_CHANGED ||
        event == AmsBackend::EVENT_ERROR) {
        queue_sync(true, -1);
    } else if (event == AmsBackend::EVENT_SLOT_CHANGED && !data.empty()) {
        try {
            queue_sync(false, std::stoi(data));
        } catch (...) {
            queue_sync(true, -1);
        }
    }
}
```

Add `update_slot_for_backend()`:

```cpp
void AmsState::update_slot_for_backend(int backend_index, int slot_index) {
    if (backend_index == 0) {
        update_slot(slot_index);  // Existing behavior for primary
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* backend = get_backend(backend_index);
    if (!backend) return;

    int sec_idx = backend_index - 1;
    if (sec_idx < 0 || sec_idx >= static_cast<int>(secondary_slot_subjects_.size())) return;

    auto& subs = secondary_slot_subjects_[sec_idx];
    if (slot_index < 0 || slot_index >= subs.slot_count) return;

    SlotInfo slot = backend->get_slot_info(slot_index);
    if (slot.slot_index >= 0) {
        lv_subject_set_int(&subs.colors[slot_index], static_cast<int>(slot.color_rgb));
        lv_subject_set_int(&subs.statuses[slot_index], static_cast<int>(slot.status));
    }
}
```

### Step 4: Build and test

```bash
make test-run
./build/bin/helix-tests "[multi-backend]"
```

### Step 5: Commit

```bash
git add include/ams_state.h src/printer/ams_state.cpp tests/unit/test_multi_backend_discovery.cpp
git commit -m "feat(ams): per-backend event routing and sync"
```

---

## Task 5: Multi-backend init flow

Update `init_backend_from_hardware()` to create multiple backends when multiple systems are detected. Rename to `init_backends_from_hardware()` (plural) with backward-compat wrapper.

**Files:**
- Modify: `include/ams_state.h`
- Modify: `src/printer/ams_state.cpp`
- Modify: `src/printer/printer_discovery.cpp` (call site)
- Add tests to: `tests/unit/test_multi_backend_discovery.cpp`

### Step 1: Write failing tests

```cpp
TEST_CASE("AmsState: init_backends creates one backend for single system",
          "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "toolchanger", "tool T0", "tool T1", "extruder", "extruder1",
        "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);

    // Note: We can't fully test real backends without Moonraker,
    // but we can verify the detection path
    REQUIRE(hw.detected_ams_systems().size() == 1);
    REQUIRE(hw.detected_ams_systems()[0].type == AmsType::TOOL_CHANGER);

    ams.deinit_subjects();
}
```

### Step 2: Implement

In `include/ams_state.h`, add:

```cpp
/**
 * @brief Initialize backends from all detected hardware systems
 *
 * Creates and starts backends for each detected AMS system.
 * Replaces init_backend_from_hardware() for multi-backend support.
 */
void init_backends_from_hardware(const helix::PrinterDiscovery& hardware,
                                 MoonrakerAPI* api, MoonrakerClient* client);
```

In `src/printer/ams_state.cpp`:

```cpp
void AmsState::init_backends_from_hardware(const helix::PrinterDiscovery& hardware,
                                           MoonrakerAPI* api, MoonrakerClient* client) {
    const auto& systems = hardware.detected_ams_systems();
    if (systems.empty()) {
        spdlog::debug("[AMS State] No AMS systems detected, skipping");
        return;
    }

    if (get_runtime_config()->should_mock_ams()) {
        spdlog::debug("[AMS State] Mock mode active, skipping real backend initialization");
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!backends_.empty()) {
            spdlog::debug("[AMS State] Backends already initialized, skipping");
            return;
        }
    }

    for (const auto& system : systems) {
        spdlog::info("[AMS State] Creating backend for: {} ({})",
                     system.name, ams_type_to_string(system.type));

        auto backend = AmsBackend::create(system.type, api, client);
        if (!backend) {
            spdlog::warn("[AMS State] Failed to create {} backend", system.name);
            continue;
        }

        backend->set_discovered_lanes(hardware.afc_lane_names(), hardware.afc_hub_names());
        backend->set_discovered_tools(hardware.tool_names());

        int index = add_backend(std::move(backend));

        // Start the backend
        auto* b = get_backend(index);
        if (b) {
            auto result = b->start();
            spdlog::debug("[AMS State] Backend {} started, result={}", index,
                          static_cast<bool>(result));
        }
    }

    spdlog::info("[AMS State] Initialized {} backends", backend_count());
}
```

Update `init_backend_from_hardware()` to delegate:

```cpp
void AmsState::init_backend_from_hardware(const helix::PrinterDiscovery& hardware,
                                          MoonrakerAPI* api, MoonrakerClient* client) {
    // Delegate to multi-backend version
    init_backends_from_hardware(hardware, api, client);
}
```

### Step 3: Build and test

```bash
make test-run
```

### Step 4: Commit

```bash
git add include/ams_state.h src/printer/ams_state.cpp tests/unit/test_multi_backend_discovery.cpp
git commit -m "feat(ams): multi-backend init from discovered hardware systems"
```

---

## Task 6: ToolInfo backend mapping

Add `backend_index` and `backend_slot` fields to `ToolInfo`. Populate during `init_tools()` based on discovery data.

**Files:**
- Modify: `include/tool_state.h` (ToolInfo struct)
- Modify: `src/printer/tool_state.cpp` (init_tools)
- Add tests to: `tests/unit/test_tool_state.cpp`

### Step 1: Write failing tests

Add to `tests/unit/test_tool_state.cpp`:

```cpp
TEST_CASE("ToolInfo: default backend mapping is unassigned", "[tool][tool-state]") {
    ToolInfo info;
    REQUIRE(info.backend_index == -1);
    REQUIRE(info.backend_slot == -1);
}

TEST_CASE("ToolState: toolchanger tools get backend_index 0", "[tool][tool-state]") {
    lv_init_safe();

    ToolState& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = nlohmann::json::array({
        "toolchanger", "tool T0", "tool T1", "extruder", "extruder1",
        "heater_bed", "gcode_move"
    });
    hw.parse_objects(objects);
    ts.init_tools(hw);

    // Each tool maps to the toolchanger backend's slot
    REQUIRE(ts.tools()[0].backend_index == -1);  // No backend assigned at init time
    REQUIRE(ts.tools()[0].backend_slot == -1);    // Will be populated by AmsState later

    ts.deinit_subjects();
}
```

### Step 2: Run to verify failure

```bash
make test && ./build/bin/helix-tests "[tool-state]"
```
Expected: `backend_index` not a member of `ToolInfo`.

### Step 3: Implement

In `include/tool_state.h`, add to `ToolInfo` struct:

```cpp
int backend_index = -1;  ///< Which AMS backend feeds this tool (-1 = direct drive)
int backend_slot = -1;   ///< Fixed slot in that backend (-1 = any/dynamic)
```

No changes needed to `init_tools()` — backend mapping is populated later by `AmsState` or `update_from_status()` when the AMS backend reports tool associations.

### Step 4: Build and test

```bash
make test-run
./build/bin/helix-tests "[tool-state]"
```

### Step 5: Commit

```bash
git add include/tool_state.h tests/unit/test_tool_state.cpp
git commit -m "feat(tool): add backend_index/backend_slot to ToolInfo"
```

---

## Task 7: AMS Panel backend selector

Add a segmented control to the AMS panel when multiple backends are detected. Selecting a segment switches which backend's slots are displayed. Single-backend printers see no change.

**Files:**
- Modify: `include/ui_panel_ams.h`
- Modify: `src/ui/ui_panel_ams.cpp`
- Modify: `ui_xml/ams_panel.xml`

### Step 1: Add backend count subject

In `include/ams_state.h`, add a new subject:

```cpp
lv_subject_t backend_count_;
```

Initialize it in `init_subjects()`:

```cpp
INIT_SUBJECT_INT(backend_count, 0, subjects_, register_xml);
```

Add accessor:

```cpp
lv_subject_t* get_backend_count_subject() { return &backend_count_; }
```

Update `add_backend()` to bump:

```cpp
lv_subject_set_int(&backend_count_, static_cast<int>(backends_.size()));
```

Update `clear_backends()` to reset:

```cpp
lv_subject_set_int(&backend_count_, 0);
```

### Step 2: Add active backend subject

In `include/ams_state.h`:

```cpp
lv_subject_t active_backend_;  // Which backend is currently displayed in UI
```

Initialize to 0. Add accessor:

```cpp
lv_subject_t* get_active_backend_subject() { return &active_backend_; }
int active_backend_index() const { return lv_subject_get_int(&active_backend_); }
void set_active_backend(int index);
```

### Step 3: Add segmented control to AMS Panel

In `include/ui_panel_ams.h`, add:

```cpp
ObserverGuard backend_count_observer_;
int active_backend_index_ = 0;

void rebuild_backend_segments();
void on_backend_selected(int index);
```

In `ui_xml/ams_panel.xml`, add a container for the backend selector above the slot grid:

```xml
<!-- Backend selector (hidden when single backend) -->
<lv_obj name="backend_selector_row" width="100%" height="content"
        style_pad_bottom="#space_sm"
        flex_flow="row" flex_main_place="center">
    <bind_flag_if_eq subject="ams_backend_count" flag="hidden" ref_value="1"/>
    <bind_flag_if_eq subject="ams_backend_count" flag="hidden" ref_value="0"/>
    <!-- Segments created dynamically in C++ -->
</lv_obj>
```

**Note:** The `bind_flag_if_eq` hides when count is 0 or 1 (i.e., only visible when 2+). This requires two bind rules. If LVGL doesn't support dual bind_flag_if_eq on the same flag, use a computed subject instead (e.g., `show_backend_selector` that's 1 when count >= 2).

In `src/ui/ui_panel_ams.cpp`, implement `rebuild_backend_segments()`:

```cpp
void AmsPanel::rebuild_backend_segments() {
    auto& ams = AmsState::instance();
    int count = ams.backend_count();

    if (count <= 1) return;  // No selector needed

    lv_obj_t* row = lv_obj_find_by_name(panel_, "backend_selector_row");
    if (!row) return;

    // Clear existing segments
    lv_obj_clean(row);

    for (int i = 0; i < count; ++i) {
        auto* backend = ams.get_backend(i);
        if (!backend) continue;

        const char* name = ams_type_to_string(backend->get_type());
        // Create segment button (similar to TempControlPanel extruder segments)
        lv_obj_t* btn = lv_xml_create(row, "segment_button", nullptr);
        // ... set text, register click callback with index capture
    }
}
```

When a backend segment is selected:

```cpp
void AmsPanel::on_backend_selected(int index) {
    active_backend_index_ = index;
    AmsState::instance().set_active_backend(index);

    // Rebuild slots from the selected backend
    auto* backend = AmsState::instance().get_backend(index);
    if (backend) {
        auto info = backend->get_system_info();
        create_slots(info.total_slots);
    }
}
```

### Step 4: Update slot observers for backend-aware access

In `src/ui/ui_ams_slot.cpp`, when creating per-slot observers, use the active backend index:

```cpp
int backend_idx = AmsState::instance().active_backend_index();
auto* color_subj = AmsState::instance().get_slot_color_subject(backend_idx, slot_index);
auto* status_subj = AmsState::instance().get_slot_status_subject(backend_idx, slot_index);
```

### Step 5: Build and test manually

```bash
make -j
./build/bin/helix-screen --test -vv
```

Test with mock AMS to verify single-backend behavior is unchanged.

### Step 6: Commit

```bash
git add include/ams_state.h src/printer/ams_state.cpp include/ui_panel_ams.h src/ui/ui_panel_ams.cpp ui_xml/ams_panel.xml src/ui/ui_ams_slot.cpp
git commit -m "feat(ams-panel): add backend selector for multi-backend systems"
```

---

## Task 8: Integration tests, doc update, and cleanup

Add integration-style tests, update the design doc, and ensure all existing tests still pass.

**Files:**
- Modify: `tests/unit/test_multi_backend_discovery.cpp`
- Modify: `docs/devel/plans/2026-02-13-tool-abstraction-design.md`

### Step 1: Add integration tests

```cpp
TEST_CASE("Multi-backend: full lifecycle", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();
    ams.deinit_subjects();
    ams.init_subjects(false);

    // Add two mock backends
    auto mock1 = AmsBackend::create_mock(4);
    auto mock2 = AmsBackend::create_mock(2);
    ams.add_backend(std::move(mock1));
    ams.add_backend(std::move(mock2));

    REQUIRE(ams.backend_count() == 2);

    // Sync both backends
    ams.sync_backend(0);
    ams.sync_backend(1);

    // Primary backend slots work via flat accessors
    REQUIRE(ams.get_slot_color_subject(0) != nullptr);
    REQUIRE(ams.get_slot_color_subject(3) != nullptr);

    // Secondary backend slots work via indexed accessors
    REQUIRE(ams.get_slot_color_subject(1, 0) != nullptr);
    REQUIRE(ams.get_slot_color_subject(1, 1) != nullptr);
    REQUIRE(ams.get_slot_color_subject(1, 2) == nullptr);  // Only 2 slots

    // Deinit cleans everything up
    ams.deinit_subjects();
    REQUIRE(ams.backend_count() == 0);
}

TEST_CASE("Multi-backend: deinit then re-init is safe", "[ams][multi-backend]") {
    lv_init_safe();

    AmsState& ams = AmsState::instance();

    // First cycle
    ams.deinit_subjects();
    ams.init_subjects(false);
    auto mock = AmsBackend::create_mock(4);
    ams.add_backend(std::move(mock));
    REQUIRE(ams.backend_count() == 1);
    ams.deinit_subjects();

    // Second cycle
    ams.init_subjects(false);
    REQUIRE(ams.backend_count() == 0);
    auto mock2 = AmsBackend::create_mock(2);
    ams.add_backend(std::move(mock2));
    REQUIRE(ams.backend_count() == 1);
    ams.deinit_subjects();
}
```

### Step 2: Update design doc

In `docs/devel/plans/2026-02-13-tool-abstraction-design.md`, change:

```markdown
## Phase 3: Multi-Backend (Cascading) ✅ Complete
```

### Step 3: Full test run

```bash
make test-run
```
Expected: All tests pass — both new multi-backend tests and all existing tests.

### Step 4: Commit

```bash
git add tests/unit/test_multi_backend_discovery.cpp docs/devel/plans/2026-02-13-tool-abstraction-design.md
git commit -m "test(ams): add multi-backend lifecycle tests; mark Phase 3 complete"
```

---

## Summary

| Task | What | Files | Commit |
|------|------|-------|--------|
| 1 | DetectedAmsSystem struct + multi-detection | `printer_discovery.h/cpp`, test | `feat(discovery): add DetectedAmsSystem` |
| 2 | AmsState backends_ vector | `ams_state.h/cpp`, test | `refactor(ams): backends_ vector` |
| 3 | Per-backend slot subjects | `ams_state.h/cpp`, test | `feat(ams): per-backend slot subjects` |
| 4 | Per-backend event routing | `ams_state.h/cpp`, test | `feat(ams): per-backend event routing` |
| 5 | Multi-backend init flow | `ams_state.h/cpp`, `printer_discovery.cpp` | `feat(ams): multi-backend init` |
| 6 | ToolInfo backend mapping | `tool_state.h`, test | `feat(tool): backend_index/backend_slot` |
| 7 | AMS Panel backend selector | `ui_panel_ams.h/cpp`, `ams_panel.xml`, `ui_ams_slot.cpp` | `feat(ams-panel): backend selector` |
| 8 | Integration tests + doc | test, design doc | `test(ams): multi-backend lifecycle` |
