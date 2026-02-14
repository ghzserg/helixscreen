# Multi-Extruder Temperature State — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor `PrinterTemperatureState` from hardcoded single extruder to dynamic N-extruder support, following the `PrinterFanState` pattern.

**Architecture:** Replace static `extruder_temp_`/`extruder_target_` subjects with `unordered_map<string, ExtruderInfo>` keyed by Klipper object name. A version subject triggers UI rebuild when extruders are discovered. The first extruder ("extruder") is aliased to the legacy XML subject names for backward-compatible single-extruder display. TempControlPanel gains a segmented control for multi-extruder selection.

**Tech Stack:** C++17, LVGL 9.4 subjects/observers, Catch2 tests, Moonraker JSON API

**Design Doc:** `docs/devel/plans/2026-02-13-tool-abstraction-design.md` (Phase 1)

**Key Reference Files:**
- Target pattern: `include/printer_fan_state.h` (dynamic hardware map with version subject)
- Current code: `include/printer_temperature_state.h` (what we're replacing)
- Discovery: `include/printer_discovery.h` → `heaters()` already returns all extruder names
- Subscription: `src/api/moonraker_client.cpp:1467-1470` (already subscribes to all heaters)
- Subject macro: `include/state/subject_macros.h:56-63` (`INIT_SUBJECT_INT`)

---

## Task 1: ExtruderInfo struct and dynamic map in PrinterTemperatureState

**Files:**
- Modify: `include/printer_temperature_state.h`
- Test: `tests/unit/test_printer_temperature_char.cpp` (existing — extend)

### Step 1: Write the failing test

Add a new test section to `tests/unit/test_printer_temperature_char.cpp`:

```cpp
TEST_CASE("Multi-extruder: init_extruders creates ExtruderInfo entries",
          "[temperature][multi-extruder]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);

    // Simulate discovery of 2 extruders
    std::vector<std::string> heaters = {"extruder", "extruder1", "heater_bed"};
    temp_state.init_extruders(heaters);

    REQUIRE(temp_state.extruder_count() == 2);

    // Each extruder should have accessible subjects
    REQUIRE(temp_state.get_extruder_temp_subject("extruder") != nullptr);
    REQUIRE(temp_state.get_extruder_target_subject("extruder") != nullptr);
    REQUIRE(temp_state.get_extruder_temp_subject("extruder1") != nullptr);
    REQUIRE(temp_state.get_extruder_target_subject("extruder1") != nullptr);

    // Non-existent extruder returns nullptr
    REQUIRE(temp_state.get_extruder_temp_subject("extruder5") == nullptr);

    // Version subject should have been bumped
    REQUIRE(lv_subject_get_int(temp_state.get_extruder_version_subject()) > 0);
}
```

### Step 2: Run test to verify it fails

```bash
make test && ./build/bin/helix-tests "[multi-extruder]" -v
```

Expected: FAIL — `init_extruders()`, `extruder_count()`, overloaded `get_extruder_temp_subject(string)`, `get_extruder_version_subject()` don't exist yet.

### Step 3: Add ExtruderInfo struct and new members to header

In `include/printer_temperature_state.h`, add the struct and new methods/members:

```cpp
#include <unordered_map>
#include <memory>
#include <string>

struct ExtruderInfo {
    std::string name;           // "extruder", "extruder1", etc.
    std::string display_name;   // "Nozzle", "Nozzle 1"
    float temperature = 0.0f;   // raw float for internal tracking
    float target = 0.0f;
    std::unique_ptr<lv_subject_t> temp_subject;    // centidegrees
    std::unique_ptr<lv_subject_t> target_subject;  // centidegrees
};
```

New methods on `PrinterTemperatureState`:

```cpp
// Discovery — called after hardware detection
void init_extruders(const std::vector<std::string>& heaters);

// Per-extruder subject access (returns nullptr if not found)
lv_subject_t* get_extruder_temp_subject(const std::string& name);
lv_subject_t* get_extruder_target_subject(const std::string& name);

// Metadata
int extruder_count() const;
const std::unordered_map<std::string, ExtruderInfo>& extruders() const;
lv_subject_t* get_extruder_version_subject();
```

New members:

```cpp
std::unordered_map<std::string, ExtruderInfo> extruders_;
lv_subject_t extruder_version_{};
```

### Step 4: Implement init_extruders in .cpp

In `src/printer/printer_temperature_state.cpp`:

```cpp
void PrinterTemperatureState::init_extruders(const std::vector<std::string>& heaters) {
    // Clean up existing dynamic subjects
    for (auto& [name, info] : extruders_) {
        if (info.temp_subject) {
            lv_subject_deinit(info.temp_subject.get());
        }
        if (info.target_subject) {
            lv_subject_deinit(info.target_subject.get());
        }
    }
    extruders_.clear();

    int extruder_index = 0;
    for (const auto& heater : heaters) {
        // Only process extruder objects, skip heater_bed and heater_generic
        if (heater.rfind("extruder", 0) != 0) {
            continue;
        }

        ExtruderInfo info;
        info.name = heater;
        // "extruder" → "Nozzle", "extruder1" → "Nozzle 1", "extruder2" → "Nozzle 2"
        if (heater == "extruder") {
            info.display_name = "Nozzle";
        } else {
            // Extract number from "extruderN"
            std::string num_str = heater.substr(8); // len("extruder") = 8
            info.display_name = "Nozzle " + num_str;
        }

        info.temp_subject = std::make_unique<lv_subject_t>();
        lv_subject_init_int(info.temp_subject.get(), 0);

        info.target_subject = std::make_unique<lv_subject_t>();
        lv_subject_init_int(info.target_subject.get(), 0);

        extruders_[heater] = std::move(info);
        extruder_index++;
    }

    // Bump version subject
    lv_subject_set_int(&extruder_version_, lv_subject_get_int(&extruder_version_) + 1);

    spdlog::info("[PrinterTemperatureState] Discovered {} extruder(s)", extruders_.size());
}
```

Implement the simple accessors:

```cpp
lv_subject_t* PrinterTemperatureState::get_extruder_temp_subject(const std::string& name) {
    auto it = extruders_.find(name);
    return (it != extruders_.end()) ? it->second.temp_subject.get() : nullptr;
}

lv_subject_t* PrinterTemperatureState::get_extruder_target_subject(const std::string& name) {
    auto it = extruders_.find(name);
    return (it != extruders_.end()) ? it->second.target_subject.get() : nullptr;
}

int PrinterTemperatureState::extruder_count() const {
    return static_cast<int>(extruders_.size());
}

const std::unordered_map<std::string, ExtruderInfo>&
PrinterTemperatureState::extruders() const {
    return extruders_;
}

lv_subject_t* PrinterTemperatureState::get_extruder_version_subject() {
    return &extruder_version_;
}
```

Also init/deinit the version subject in `init_subjects()`/`deinit_subjects()`:

```cpp
// In init_subjects(), add:
INIT_SUBJECT_INT(extruder_version, 0, subjects_, register_xml);

// In deinit_subjects(), add before subjects_.deinit_all():
for (auto& [name, info] : extruders_) {
    if (info.temp_subject) lv_subject_deinit(info.temp_subject.get());
    if (info.target_subject) lv_subject_deinit(info.target_subject.get());
}
extruders_.clear();
```

### Step 5: Run test to verify it passes

```bash
make test && ./build/bin/helix-tests "[multi-extruder]" -v
```

Expected: PASS

### Step 6: Commit

```bash
git add include/printer_temperature_state.h src/printer/printer_temperature_state.cpp tests/unit/test_printer_temperature_char.cpp
git commit -m "feat(temperature): add ExtruderInfo struct and dynamic extruder map"
```

---

## Task 2: Multi-extruder status update parsing

**Files:**
- Modify: `src/printer/printer_temperature_state.cpp`
- Test: `tests/unit/test_printer_temperature_char.cpp`

### Step 1: Write the failing test

```cpp
TEST_CASE("Multi-extruder: status updates route to correct extruder",
          "[temperature][multi-extruder]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);
    temp_state.init_extruders({"extruder", "extruder1", "heater_bed"});

    SECTION("extruder0 temp update") {
        nlohmann::json status = {{"extruder", {{"temperature", 205.3}, {"target", 210.0}}}};
        temp_state.update_from_status(status);

        REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder")) == 2053);
        REQUIRE(lv_subject_get_int(temp_state.get_extruder_target_subject("extruder")) == 2100);
        // extruder1 unchanged
        REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder1")) == 0);
    }

    SECTION("extruder1 temp update") {
        nlohmann::json status = {{"extruder1", {{"temperature", 180.5}, {"target", 200.0}}}};
        temp_state.update_from_status(status);

        REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder1")) == 1805);
        REQUIRE(lv_subject_get_int(temp_state.get_extruder_target_subject("extruder1")) == 2000);
        // extruder0 unchanged
        REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder")) == 0);
    }

    SECTION("both extruders in same status update") {
        nlohmann::json status = {
            {"extruder", {{"temperature", 200.0}}},
            {"extruder1", {{"temperature", 190.0}}}
        };
        temp_state.update_from_status(status);

        REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder")) == 2000);
        REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder1")) == 1900);
    }
}
```

### Step 2: Run test to verify it fails

```bash
make test && ./build/bin/helix-tests "[multi-extruder]" -v
```

Expected: FAIL — `update_from_status()` still only checks hardcoded `status["extruder"]`.

### Step 3: Refactor update_from_status to iterate extruder map

Replace the existing extruder section of `update_from_status()` with a loop:

```cpp
void PrinterTemperatureState::update_from_status(const nlohmann::json& status) {
    // Update dynamic extruder temperatures
    for (auto& [name, info] : extruders_) {
        if (!status.contains(name)) continue;

        const auto& extruder_data = status[name];

        if (extruder_data.contains("temperature") && extruder_data["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(extruder_data, "temperature");
            info.temperature = extruder_data["temperature"].get<float>();
            lv_subject_set_int(info.temp_subject.get(), temp_centi);
            // Force notify for graph updates even if value unchanged
            lv_subject_notify(info.temp_subject.get());
        }

        if (extruder_data.contains("target") && extruder_data["target"].is_number()) {
            int target_centi = helix::units::json_to_centidegrees(extruder_data, "target");
            info.target = extruder_data["target"].get<float>();
            lv_subject_set_int(info.target_subject.get(), target_centi);
        }
    }

    // Backward compat: also update legacy static subjects from first extruder
    if (status.contains("extruder")) {
        const auto& extruder = status["extruder"];
        if (extruder.contains("temperature") && extruder["temperature"].is_number()) {
            int temp_centi = helix::units::json_to_centidegrees(extruder, "temperature");
            lv_subject_set_int(&extruder_temp_, temp_centi);
            lv_subject_notify(&extruder_temp_);
        }
        if (extruder.contains("target") && extruder["target"].is_number()) {
            int target_centi = helix::units::json_to_centidegrees(extruder, "target");
            lv_subject_set_int(&extruder_target_, target_centi);
        }
    }

    // ... bed and chamber updates remain unchanged ...
}
```

**Important:** Keep the legacy `extruder_temp_`/`extruder_target_` static subjects alive for now. They mirror the first extruder. This ensures all existing XML bindings and UI code works unchanged until we explicitly migrate consumers.

### Step 4: Run test to verify it passes

```bash
make test && ./build/bin/helix-tests "[multi-extruder]" -v
```

Expected: PASS

### Step 5: Run ALL existing temperature tests to verify no regressions

```bash
make test && ./build/bin/helix-tests "[temperature]" -v
```

Expected: All existing characterization tests still PASS. The legacy subjects still get updated from `status["extruder"]`.

### Step 6: Commit

```bash
git add src/printer/printer_temperature_state.cpp tests/unit/test_printer_temperature_char.cpp
git commit -m "feat(temperature): route Moonraker status updates to dynamic extruder map"
```

---

## Task 3: Legacy subject aliasing — first extruder mirrors static subjects

**Files:**
- Modify: `src/printer/printer_temperature_state.cpp`
- Test: `tests/unit/test_printer_temperature_char.cpp`

This task ensures the legacy `get_extruder_temp_subject()` (no args) returns the same data as `get_extruder_temp_subject("extruder")`.

### Step 1: Write the failing test

```cpp
TEST_CASE("Multi-extruder: legacy subjects mirror first extruder",
          "[temperature][multi-extruder][legacy]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);
    temp_state.init_extruders({"extruder", "extruder1", "heater_bed"});

    nlohmann::json status = {
        {"extruder", {{"temperature", 205.3}, {"target", 210.0}}},
        {"extruder1", {{"temperature", 180.0}, {"target", 195.0}}}
    };
    temp_state.update_from_status(status);

    // Legacy (no-arg) accessors should match first extruder
    REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject()) == 2053);
    REQUIRE(lv_subject_get_int(temp_state.get_extruder_target_subject()) == 2100);

    // Named accessors should also work
    REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder")) == 2053);
    REQUIRE(lv_subject_get_int(temp_state.get_extruder_temp_subject("extruder1")) == 1800);
}
```

### Step 2: Run test to verify it passes (should already pass from Task 2)

```bash
make test && ./build/bin/helix-tests "[legacy]" -v
```

Expected: PASS — Task 2 already keeps legacy subjects in sync. If it passes, this test serves as a regression guard.

### Step 3: Commit

```bash
git add tests/unit/test_printer_temperature_char.cpp
git commit -m "test(temperature): add regression tests for legacy subject aliasing"
```

---

## Task 4: Wire init_extruders into discovery flow

**Files:**
- Modify: `src/printer/printer_discovery.cpp` (in `init_subsystems_from_hardware`)
- Modify: `include/printer_state.h` (add forwarding method)
- Modify: `src/printer/printer_state.cpp`

### Step 1: Add forwarding methods to PrinterState

In `include/printer_state.h`, add alongside existing temperature accessors:

```cpp
// Multi-extruder discovery
void init_extruders(const std::vector<std::string>& heaters) {
    temperature_state_.init_extruders(heaters);
}

// Per-extruder subject access
lv_subject_t* get_extruder_temp_subject(const std::string& name) {
    return temperature_state_.get_extruder_temp_subject(name);
}
lv_subject_t* get_extruder_target_subject(const std::string& name) {
    return temperature_state_.get_extruder_target_subject(name);
}

int extruder_count() const {
    return temperature_state_.extruder_count();
}

lv_subject_t* get_extruder_version_subject() {
    return temperature_state_.get_extruder_version_subject();
}
```

### Step 2: Wire into init_subsystems_from_hardware

In `src/printer/printer_discovery.cpp`, function `init_subsystems_from_hardware()`, add after temperature sensor manager discovery (around line 105):

```cpp
// Initialize multi-extruder temperature tracking
auto& printer_state = get_printer_state();
printer_state.init_extruders(hardware.heaters());
```

### Step 3: Build and test

```bash
make -j && make test && ./build/bin/helix-tests "[temperature]" -v
```

Expected: Build succeeds, all temperature tests pass.

### Step 4: Manual smoke test

```bash
./build/bin/helix-screen --test -vv 2>&1 | grep -i "extruder"
```

Expected: Log line `[PrinterTemperatureState] Discovered 1 extruder(s)` appears (mock printer has single extruder).

### Step 5: Commit

```bash
git add include/printer_state.h src/printer/printer_state.cpp src/printer/printer_discovery.cpp
git commit -m "feat(temperature): wire multi-extruder init into hardware discovery flow"
```

---

## Task 5: TempControlPanel — dynamic heater list with segmented control

**Files:**
- Modify: `include/ui_panel_temp_control.h`
- Modify: `src/ui/ui_panel_temp_control.cpp`
- Modify: `ui_xml/nozzle_temp_panel.xml` (add segmented control placeholder)

This is the biggest UI task. The panel currently has hardcoded `nozzle_config_` and `bed_config_`. We're adding a dynamic heater list and a segmented control when multiple extruders exist.

### Step 1: Add heater abstraction to header

In `include/ui_panel_temp_control.h`, replace or augment the hardcoded configs:

```cpp
// Dynamic heater list — built from discovered extruders + bed
struct HeaterEntry {
    heater_config_t config;
    std::string klipper_name;  // "extruder", "extruder1", "heater_bed"
    ObserverGuard temp_observer;
    ObserverGuard target_observer;
};

std::vector<HeaterEntry> heaters_;
int active_heater_index_ = 0;
ObserverGuard extruder_version_observer_;
```

### Step 2: Build heater list from discovered extruders

In `src/ui/ui_panel_temp_control.cpp`, add a `rebuild_heater_list()` method:

```cpp
void TempControlPanel::rebuild_heater_list() {
    heaters_.clear();

    auto& state = get_printer_state();
    const auto& extruders = state.temperature_state().extruders();

    // Add extruders (sorted by name for stable ordering)
    std::vector<std::string> extruder_names;
    for (const auto& [name, info] : extruders) {
        extruder_names.push_back(name);
    }
    std::sort(extruder_names.begin(), extruder_names.end());

    for (const auto& name : extruder_names) {
        const auto& info = extruders.at(name);
        HeaterEntry entry;
        entry.klipper_name = name;
        entry.config = {
            .type = HEATER_NOZZLE,
            .name = info.display_name,
            .title = info.display_name + " Temperature",
            .color = theme_manager_get_color("heating_color"),
            .temp_range_max = 320.0f,
            .y_axis_increment = 80,
            .presets = {0, nozzle_pla_, nozzle_petg_, nozzle_abs_},
            .keypad_range = {0.0f, 350.0f}
        };
        heaters_.push_back(std::move(entry));
    }

    // Add bed (always last)
    HeaterEntry bed_entry;
    bed_entry.klipper_name = "heater_bed";
    bed_entry.config = bed_config_;
    heaters_.push_back(std::move(bed_entry));

    // Show segmented control only when > 1 extruder (extruder + bed = 2 heaters minimum)
    update_segmented_control();
}
```

### Step 3: Add segmented control to XML

In `ui_xml/nozzle_temp_panel.xml`, add a segmented control container at the top of the panel (hidden by default, shown when multiple extruders detected):

```xml
<lv_obj name="heater_selector" width="100%" height="content"
        style_pad_hor="#space_md" style_pad_top="#space_sm"
        flag_hidden="true">
    <ui_segmented_control name="heater_segments" width="100%"/>
</lv_obj>
```

### Step 4: Implement segmented control update

```cpp
void TempControlPanel::update_segmented_control() {
    auto* selector = lv_obj_find_by_name(panel_, "heater_selector");
    if (!selector) return;

    if (heaters_.size() <= 2) {
        // Single extruder + bed — no selector needed, hide it
        lv_obj_add_flag(selector, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(selector, LV_OBJ_FLAG_HIDDEN);

    auto* segments = lv_obj_find_by_name(panel_, "heater_segments");
    if (!segments) return;

    // Clear and rebuild segments
    lv_obj_clean(segments);
    for (size_t i = 0; i < heaters_.size(); i++) {
        // Add segment button for each heater
        // Use heaters_[i].config.name as label
    }
}
```

### Step 5: Wire observers to active heater

Replace the `TemperatureObserverBundle` with per-heater observers that activate/deactivate based on selection:

```cpp
void TempControlPanel::select_heater(int index) {
    if (index < 0 || index >= static_cast<int>(heaters_.size())) return;

    active_heater_index_ = index;
    auto& entry = heaters_[index];

    // Update display with current heater's config
    update_display_for_heater(entry);

    // Rebind observers to selected heater's subjects
    rebind_temp_observers(entry);
}
```

### Step 6: Observe extruder_version for dynamic rebuild

```cpp
// In panel init, observe version subject:
extruder_version_observer_ = observe_int_sync<TempControlPanel>(
    printer_state_.get_extruder_version_subject(), this,
    [](TempControlPanel* self, int /* version */) {
        self->rebuild_heater_list();
    });
```

### Step 7: Build and manual test

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Navigate to temperature panel. With single extruder (default mock), should look identical to today. No segmented control visible.

### Step 8: Commit

```bash
git add include/ui_panel_temp_control.h src/ui/ui_panel_temp_control.cpp ui_xml/nozzle_temp_panel.xml
git commit -m "feat(temperature): dynamic heater list with segmented control in TempControlPanel"
```

---

## Task 6: Temperature command routing — send to correct extruder

**Files:**
- Modify: `src/ui/ui_panel_temp_control.cpp`
- Test: Verify manually (GCode commands are sent via MoonrakerAPI)

### Step 1: Update temperature command to use active heater name

Currently the panel sends:
```cpp
api_->set_temperature("extruder", target);
```

Change to use the active heater's Klipper name:

```cpp
void TempControlPanel::send_temperature_command(float target) {
    if (active_heater_index_ < 0 || active_heater_index_ >= static_cast<int>(heaters_.size())) {
        return;
    }
    const auto& entry = heaters_[active_heater_index_];
    api_->set_temperature(entry.klipper_name, target);
}
```

### Step 2: Build

```bash
make -j
```

### Step 3: Commit

```bash
git add src/ui/ui_panel_temp_control.cpp
git commit -m "feat(temperature): route temp commands to active heater klipper name"
```

---

## Task 7: PrinterState forwarding — expose extruders() for UI consumers

**Files:**
- Modify: `include/printer_state.h`

### Step 1: Add `temperature_state()` accessor to PrinterState

Many UI components will need access to extruder metadata. Add a const accessor:

```cpp
const PrinterTemperatureState& temperature_state() const {
    return temperature_state_;
}
```

This gives UI code access to `extruders()`, `extruder_count()`, etc. without adding a forwarding method for every accessor.

### Step 2: Build

```bash
make -j
```

### Step 3: Commit

```bash
git add include/printer_state.h
git commit -m "feat(temperature): expose temperature_state() accessor on PrinterState"
```

---

## Task 8: Home panel — show active extruder temp (multi-extruder aware)

**Files:**
- Modify: `src/ui/ui_panel_home.cpp` (or `src/ui/panels/home_panel.cpp` — check actual path)

### Step 1: Update home panel extruder temp display

For Phase 1, the home panel continues using legacy `extruder_temp`/`extruder_target` XML subjects (which mirror the first extruder). No code changes needed for single-extruder printers.

For multi-extruder awareness (showing "T0" badge), this is deferred to Phase 2 (Tool Abstraction) per the design doc: *"Active tool's temp shown prominently. Small T0/T1 badge..."*

### Step 2: Verify no regressions

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Navigate to home panel — extruder temp should display as before.

### Step 3: Commit (only if changes were needed)

Skip if no changes — this task is a verification checkpoint.

---

## Task 9: TemperatureObserverBundle — extend for multi-extruder

**Files:**
- Modify: `include/ui/temperature_observer_bundle.h`

### Step 1: Add extruder-name-aware setup variant

The existing `TemperatureObserverBundle` hardcodes `get_extruder_temp_subject()` (no args). Add an overload or new method that accepts extruder name:

```cpp
template <typename Panel, typename TempHandler, typename TargetHandler>
void setup_for_extruder(Panel* panel, PrinterState& state,
                        const std::string& extruder_name,
                        TempHandler&& on_temp, TargetHandler&& on_target) {
    auto* temp_subj = state.get_extruder_temp_subject(extruder_name);
    auto* target_subj = state.get_extruder_target_subject(extruder_name);

    if (temp_subj) {
        nozzle_temp_observer_ = observe_int_sync<Panel>(
            temp_subj, panel, std::forward<TempHandler>(on_temp));
    }
    if (target_subj) {
        nozzle_target_observer_ = observe_int_sync<Panel>(
            target_subj, panel, std::forward<TargetHandler>(on_target));
    }
}
```

The existing `setup_sync()` continues to work unchanged for backward compat.

### Step 2: Build

```bash
make -j
```

### Step 3: Commit

```bash
git add include/ui/temperature_observer_bundle.h
git commit -m "feat(temperature): add extruder-name-aware observer bundle setup"
```

---

## Task 10: Deinit cleanup and full regression test

**Files:**
- Modify: `src/printer/printer_temperature_state.cpp` (verify deinit)
- Test: `tests/unit/test_printer_temperature_char.cpp`

### Step 1: Write deinit test

```cpp
TEST_CASE("Multi-extruder: deinit cleans up dynamic subjects",
          "[temperature][multi-extruder][lifecycle]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);
    temp_state.init_extruders({"extruder", "extruder1", "heater_bed"});

    REQUIRE(temp_state.extruder_count() == 2);

    temp_state.deinit_subjects();

    // After deinit, extruder map should be empty
    REQUIRE(temp_state.extruder_count() == 0);

    // Re-init should work cleanly (no double-free)
    temp_state.init_subjects(false);
    temp_state.init_extruders({"extruder", "heater_bed"});
    REQUIRE(temp_state.extruder_count() == 1);

    temp_state.deinit_subjects();
}

TEST_CASE("Multi-extruder: re-init with different extruder count",
          "[temperature][multi-extruder][lifecycle]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState temp_state;
    temp_state.init_subjects(false);

    // First init: 2 extruders
    temp_state.init_extruders({"extruder", "extruder1", "heater_bed"});
    REQUIRE(temp_state.extruder_count() == 2);

    // Re-init: 1 extruder (simulates reconnect to different printer)
    temp_state.init_extruders({"extruder", "heater_bed"});
    REQUIRE(temp_state.extruder_count() == 1);
    REQUIRE(temp_state.get_extruder_temp_subject("extruder1") == nullptr);

    temp_state.deinit_subjects();
}
```

### Step 2: Run ALL tests

```bash
make test && ./build/bin/helix-tests -v
```

Expected: ALL tests pass. No regressions.

### Step 3: Commit

```bash
git add tests/unit/test_printer_temperature_char.cpp
git commit -m "test(temperature): add lifecycle and deinit tests for multi-extruder"
```

---

## Task 11: Documentation

**Files:**
- Modify: `docs/devel/plans/2026-02-13-tool-abstraction-design.md` (update Phase 1 status)
- Modify: `docs/devel/ARCHITECTURE.md` or relevant doc (document new pattern)

### Step 1: Update design doc status

Change Phase 1 header to indicate completion:

```markdown
## Phase 1: Multi-Extruder Temperature State ✅
```

### Step 2: Document the new pattern

Add a brief section to architecture docs explaining:
- `ExtruderInfo` struct
- Dynamic subject map pattern
- Legacy aliasing approach
- How to access per-extruder subjects

### Step 3: Commit

```bash
git add docs/
git commit -m "docs: document multi-extruder temperature architecture"
```

---

## Summary

| Task | What | Tests |
|------|------|-------|
| 1 | ExtruderInfo struct + dynamic map | init, accessors, version subject |
| 2 | Multi-extruder status update parsing | per-extruder routing, independence |
| 3 | Legacy subject aliasing verification | regression guard |
| 4 | Wire into discovery flow | integration smoke test |
| 5 | TempControlPanel dynamic heaters + segmented control | manual UI test |
| 6 | Temperature command routing | manual verify |
| 7 | PrinterState accessor forwarding | build check |
| 8 | Home panel verification | no regressions |
| 9 | TemperatureObserverBundle extension | build check |
| 10 | Deinit lifecycle tests | init/deinit/re-init |
| 11 | Documentation | — |

**Total estimated commits:** 10-11
**Risk areas:** Task 5 (TempControlPanel) is the most complex — involves XML + observer rebinding + segmented control. Everything else is straightforward data plumbing following the existing fan pattern.
