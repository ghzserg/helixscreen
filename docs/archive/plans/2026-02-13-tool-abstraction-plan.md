# Phase 2: Tool Abstraction Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Introduce a `ToolState` singleton that models physical print heads (tools), each owning hardware references (extruder, fan, offsets), so the UI can display per-tool temperatures and identify the active tool.

**Architecture:** `ToolState` is a thin UI-facing singleton with LVGL subjects. It is populated from `AmsBackendToolChanger`'s parsed data for toolchanger printers, or creates one implicit tool for single-extruder printers. It does NOT subscribe to Moonraker directly — data flows through the existing backend. The Home panel and Print Status overlay gain active-tool awareness.

**Tech Stack:** C++17, LVGL 9.4 subjects, Catch2 tests, LVGL XML declarative UI

**Design doc:** `docs/devel/plans/2026-02-13-tool-abstraction-design.md` — Phase 2 section

**Worktree:** `.worktrees/multi-extruder-temps` (branch `feature/multi-extruder-temps`, continues Phase 1 work)

---

## Context for Implementers

### Key files to understand before starting

| File | Why |
|------|-----|
| `include/printer_fan_state.h` | Template pattern: SubjectManager, init/deinit, version subject |
| `include/printer_temperature_state.h` | Phase 1 work: ExtruderInfo struct, dynamic map |
| `src/printer/ams_backend_toolchanger.cpp` | Already parses tool data from Klipper — ToolState reads from here |
| `include/ams_backend.h` | `AmsBackend` base class with `get_system_info()`, `get_current_tool()` |
| `src/printer/ams_state.cpp:298-348` | `init_backend_from_hardware()` — creates and starts backend |
| `src/printer/printer_discovery.cpp:86-118` | `init_subsystems_from_hardware()` — where ToolState init goes |
| `include/printer_discovery.h:224-232` | Tool name discovery from Klipper objects |

### Data flow

```
Klipper → Moonraker → AmsBackendToolChanger (parse_tool_state/parse_toolchanger_state)
                                ↓ EVENT_STATE_CHANGED
                           AmsState (event callback)
                                ↓ (new) forward to ToolState
                           ToolState (update subjects)
                                ↓ lv_subject observers
                           UI Panels (Home, PrintStatus, TempControl)
```

For **single-extruder printers** (no toolchanger): `ToolState::init_tools()` creates one implicit `ToolInfo{0, "T0", "extruder", nullopt, "fan"}`. No backend involvement.

### Existing AmsBackendToolChanger parsed data

Already available via `AmsBackend` interface:
- `get_current_tool()` → int (-1 if none)
- `get_system_info().current_tool` → same
- `parse_tool_state()` extracts: `active`, `mounted`, `gcode_x/y/z_offset`
- `parse_toolchanger_state()` extracts: `status`, `tool_number`, `tool_numbers`, `tool_names`
- `tool_names_` member has the Klipper tool names ("T0", "T1")

Currently parsed but **NOT stored**: extruder name per tool, fan name per tool (logged but discarded). These need to be captured.

---

## Task 1: ToolInfo struct and DetectState enum

**Files:**
- Create: `include/tool_state.h`
- Test: `tests/unit/test_tool_state.cpp`

**Step 1: Write the failing test**

Create `tests/unit/test_tool_state.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ToolInfo: default construction", "[tool][tool-state]") {
    ToolInfo info;
    REQUIRE(info.index == 0);
    REQUIRE(info.name == "T0");
    REQUIRE(info.extruder_name.has_value());
    REQUIRE(info.extruder_name.value() == "extruder");
    REQUIRE_FALSE(info.heater_name.has_value());
    REQUIRE_FALSE(info.fan_name.has_value());
    REQUIRE(info.gcode_x_offset == 0.0f);
    REQUIRE(info.gcode_y_offset == 0.0f);
    REQUIRE(info.gcode_z_offset == 0.0f);
    REQUIRE_FALSE(info.active);
    REQUIRE_FALSE(info.mounted);
    REQUIRE(info.detect_state == DetectState::UNAVAILABLE);
}

TEST_CASE("ToolInfo: effective_heater prefers heater_name", "[tool][tool-state]") {
    ToolInfo info;
    info.extruder_name = "extruder1";
    info.heater_name = "heater_generic nozzle_swap";

    REQUIRE(info.effective_heater() == "heater_generic nozzle_swap");
}

TEST_CASE("ToolInfo: effective_heater falls back to extruder_name", "[tool][tool-state]") {
    ToolInfo info;
    info.extruder_name = "extruder1";
    // No heater_name set

    REQUIRE(info.effective_heater() == "extruder1");
}

TEST_CASE("ToolInfo: effective_heater fallback when nothing set", "[tool][tool-state]") {
    ToolInfo info;
    info.extruder_name = std::nullopt;

    REQUIRE(info.effective_heater() == "extruder");
}

TEST_CASE("DetectState: enum values", "[tool][tool-state]") {
    REQUIRE(static_cast<int>(DetectState::PRESENT) == 0);
    REQUIRE(static_cast<int>(DetectState::ABSENT) == 1);
    REQUIRE(static_cast<int>(DetectState::UNAVAILABLE) == 2);
}
```

**Step 2: Run test to verify it fails**

```bash
cd /Users/pbrown/Code/Printing/helixscreen/.worktrees/multi-extruder-temps && make test
```
Expected: FAIL — `tool_state.h` doesn't exist

**Step 3: Write the header**

Create `include/tool_state.h`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

/**
 * @brief Detection pin state for a physical tool
 *
 * Maps to klipper-toolchanger's tool.detect_state values.
 */
enum class DetectState {
    PRESENT = 0,     ///< Detection pin confirms tool is mounted
    ABSENT = 1,      ///< Detection pin confirms tool is NOT mounted
    UNAVAILABLE = 2, ///< No detection pin configured
};

/**
 * @brief Information about a physical print head (tool)
 *
 * A tool owns hardware references: extruder, heater, fan, and offsets.
 * Single-extruder printers have one implicit tool.
 * Toolchanger printers have N tools populated from Klipper's tool objects.
 *
 * Hardware references are all optional and independent to handle different configs:
 * - Normal toolchanger: per-tool extruder + fan
 * - Nozzle-swap system: shared extruder motor, per-tool heater_generic
 * - Single extruder: extruder="extruder", fan="fan"
 * - IDEX: per-tool extruder + fan
 */
struct ToolInfo {
    int index = 0;                                ///< Tool index (0, 1, 2...)
    std::string name = "T0";                      ///< Klipper tool name
    std::optional<std::string> extruder_name = "extruder"; ///< Associated extruder
    std::optional<std::string> heater_name;        ///< Standalone heater (nozzle-swap)
    std::optional<std::string> fan_name;           ///< Per-tool fan
    float gcode_x_offset = 0.0f;
    float gcode_y_offset = 0.0f;
    float gcode_z_offset = 0.0f;
    bool active = false;                           ///< Currently selected tool?
    bool mounted = false;                          ///< Detection pin says present?
    DetectState detect_state = DetectState::UNAVAILABLE;

    /**
     * @brief Get the effective heater name for temperature display
     *
     * Prefers heater_name (standalone heater for nozzle-swap systems),
     * falls back to extruder_name, then to "extruder" as last resort.
     */
    [[nodiscard]] std::string effective_heater() const {
        if (heater_name) return *heater_name;
        if (extruder_name) return *extruder_name;
        return "extruder";
    }
};
```

**Step 4: Run test to verify it passes**

```bash
make test && ./build/bin/helix-tests "[tool-state]" --verbosity high
```
Expected: PASS — 5 test cases

**Step 5: Commit**

```bash
git add include/tool_state.h tests/unit/test_tool_state.cpp
git commit -m "feat(tool): add ToolInfo struct and DetectState enum"
```

---

## Task 2: ToolState singleton — init/deinit subjects

**Files:**
- Modify: `include/tool_state.h`
- Create: `src/printer/tool_state.cpp`
- Modify: `tests/unit/test_tool_state.cpp`

**Step 1: Write the failing tests**

Add to `tests/unit/test_tool_state.cpp`:

```cpp
#include "test_helpers.h" // for lv_init_safe()
#include "lvgl/lvgl.h"

TEST_CASE("ToolState: singleton access", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    auto& ts2 = ToolState::instance();
    REQUIRE(&ts == &ts2);
}

TEST_CASE("ToolState: init_subjects creates subjects", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    REQUIRE(ts.get_active_tool_subject() != nullptr);
    REQUIRE(ts.get_tool_count_subject() != nullptr);
    REQUIRE(ts.get_tools_version_subject() != nullptr);

    // Initial values
    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == 0);
    REQUIRE(lv_subject_get_int(ts.get_tool_count_subject()) == 0);
    REQUIRE(lv_subject_get_int(ts.get_tools_version_subject()) == 0);

    ts.deinit_subjects();
}

TEST_CASE("ToolState: double init is safe", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);
    ts.init_subjects(false); // Should be a no-op

    REQUIRE(ts.get_active_tool_subject() != nullptr);
    ts.deinit_subjects();
}

TEST_CASE("ToolState: deinit then re-init", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);
    ts.deinit_subjects();
    ts.init_subjects(false);

    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == 0);
    ts.deinit_subjects();
}
```

**Step 2: Run test to verify it fails**

```bash
make test
```
Expected: FAIL — `ToolState` class not defined

**Step 3: Write the implementation**

Add to `include/tool_state.h` (after ToolInfo):

```cpp
#include "subject_managed_panel.h" // for SubjectManager, INIT_SUBJECT_INT

#include "lvgl/lvgl.h"

#include <vector>

/**
 * @brief Singleton managing tool state and LVGL subjects
 *
 * Provides UI-facing subjects for active tool, tool count, and version.
 * Populated from AmsBackendToolChanger for toolchanger printers,
 * or creates one implicit tool for single-extruder printers.
 *
 * NOT a Moonraker subscriber — data flows through existing backends.
 */
class ToolState {
  public:
    static ToolState& instance();

    // Non-copyable
    ToolState(const ToolState&) = delete;
    ToolState& operator=(const ToolState&) = delete;

    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    // Subject accessors
    lv_subject_t* get_active_tool_subject() { return &active_tool_subject_; }
    lv_subject_t* get_tool_count_subject() { return &tool_count_subject_; }
    lv_subject_t* get_tools_version_subject() { return &tools_version_subject_; }

  private:
    ToolState() = default;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    lv_subject_t active_tool_subject_{};
    lv_subject_t tool_count_subject_{};
    lv_subject_t tools_version_subject_{};
};
```

Create `src/printer/tool_state.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_state.h"

#include "spdlog/spdlog.h"

ToolState& ToolState::instance() {
    static ToolState instance;
    return instance;
}

void ToolState::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        spdlog::debug("[ToolState] Subjects already initialized, skipping");
        return;
    }

    spdlog::trace("[ToolState] Initializing subjects (register_xml={})", register_xml);

    INIT_SUBJECT_INT(active_tool, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(tool_count, 0, subjects_, register_xml);
    INIT_SUBJECT_INT(tools_version, 0, subjects_, register_xml);

    subjects_initialized_ = true;
    spdlog::trace("[ToolState] Subjects initialized");
}

void ToolState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[ToolState] Deinitializing subjects");
    subjects_.deinit_all();
    subjects_initialized_ = false;
}
```

**Step 4: Run test to verify it passes**

```bash
make test && ./build/bin/helix-tests "[tool-state]" --verbosity high
```
Expected: PASS — 9 test cases

**Step 5: Commit**

```bash
git add include/tool_state.h src/printer/tool_state.cpp tests/unit/test_tool_state.cpp
git commit -m "feat(tool): add ToolState singleton with subject lifecycle"
```

---

## Task 3: init_tools — populate from discovery

**Files:**
- Modify: `include/tool_state.h`
- Modify: `src/printer/tool_state.cpp`
- Modify: `tests/unit/test_tool_state.cpp`

This task implements `init_tools()` which creates `ToolInfo` entries from `PrinterDiscovery` data.

**Step 1: Write the failing tests**

Add to `tests/unit/test_tool_state.cpp`:

```cpp
#include "printer_discovery.h"

TEST_CASE("ToolState: init_tools with no tools creates implicit tool", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // No toolchanger, no tools — should create implicit T0
    helix::PrinterDiscovery hw;
    ts.init_tools(hw);

    REQUIRE(ts.tool_count() == 1);
    REQUIRE(lv_subject_get_int(ts.get_tool_count_subject()) == 1);

    const auto& tools = ts.tools();
    REQUIRE(tools.size() == 1);
    REQUIRE(tools[0].index == 0);
    REQUIRE(tools[0].name == "T0");
    REQUIRE(tools[0].extruder_name.value() == "extruder");
    REQUIRE(tools[0].active == true); // Implicit tool is always active

    ts.deinit_subjects();
}

TEST_CASE("ToolState: init_tools with toolchanger creates N tools", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // Simulate discovery of 3 tools
    helix::PrinterDiscovery hw;
    // Parse objects that include "toolchanger" and "tool T0", "tool T1", "tool T2"
    // plus corresponding extruders
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1", "tool T2",
                              "extruder", "extruder1", "extruder2",
                              "heater_bed", "gcode_move"};
    hw.parse_objects(objects);

    ts.init_tools(hw);

    REQUIRE(ts.tool_count() == 3);
    REQUIRE(lv_subject_get_int(ts.get_tool_count_subject()) == 3);

    const auto& tools = ts.tools();
    REQUIRE(tools[0].name == "T0");
    REQUIRE(tools[1].name == "T1");
    REQUIRE(tools[2].name == "T2");
    // Extruder mapping: T0→extruder, T1→extruder1, T2→extruder2
    REQUIRE(tools[0].extruder_name.value() == "extruder");
    REQUIRE(tools[1].extruder_name.value() == "extruder1");
    REQUIRE(tools[2].extruder_name.value() == "extruder2");

    // Version bumped
    REQUIRE(lv_subject_get_int(ts.get_tools_version_subject()) == 1);

    ts.deinit_subjects();
}

TEST_CASE("ToolState: active_tool accessors", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    ts.init_tools(hw); // Creates implicit T0

    REQUIRE(ts.active_tool() != nullptr);
    REQUIRE(ts.active_tool()->name == "T0");
    REQUIRE(ts.active_tool_index() == 0);

    ts.deinit_subjects();
}

TEST_CASE("ToolState: re-init with different tool count", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // First: implicit tool
    helix::PrinterDiscovery hw1;
    ts.init_tools(hw1);
    REQUIRE(ts.tool_count() == 1);

    // Second: toolchanger with 2 tools
    helix::PrinterDiscovery hw2;
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1",
                              "extruder", "extruder1", "heater_bed", "gcode_move"};
    hw2.parse_objects(objects);
    ts.init_tools(hw2);
    REQUIRE(ts.tool_count() == 2);

    // Version bumped twice (once per init_tools call)
    REQUIRE(lv_subject_get_int(ts.get_tools_version_subject()) == 2);

    ts.deinit_subjects();
}
```

**Step 2: Run test to verify it fails**

```bash
make test
```
Expected: FAIL — `init_tools()`, `tool_count()`, `tools()`, `active_tool()`, `active_tool_index()` not defined

**Step 3: Write the implementation**

Add to `include/tool_state.h` (public section):

```cpp
    /**
     * @brief Initialize tools from hardware discovery
     *
     * For toolchanger printers: creates one ToolInfo per discovered tool.
     * For single-extruder printers: creates one implicit tool.
     * Safe to call multiple times (clears and rebuilds).
     *
     * @param hardware Discovery data with tool names and heater list
     */
    void init_tools(const helix::PrinterDiscovery& hardware);

    // Accessors
    [[nodiscard]] const std::vector<ToolInfo>& tools() const { return tools_; }
    [[nodiscard]] const ToolInfo* active_tool() const;
    [[nodiscard]] int active_tool_index() const { return active_tool_index_; }
    [[nodiscard]] int tool_count() const { return static_cast<int>(tools_.size()); }
```

Add to `include/tool_state.h` (private section):

```cpp
    std::vector<ToolInfo> tools_;
    int active_tool_index_ = 0;
```

Add to `src/printer/tool_state.cpp`:

```cpp
#include "printer_discovery.h"

void ToolState::init_tools(const helix::PrinterDiscovery& hardware) {
    tools_.clear();

    const auto& tool_names = hardware.tool_names();
    const auto& heaters = hardware.heaters();

    if (tool_names.empty() || !hardware.has_tool_changer()) {
        // Single-extruder: one implicit tool
        ToolInfo tool;
        tool.index = 0;
        tool.name = "T0";
        tool.extruder_name = "extruder";
        tool.fan_name = "fan";
        tool.active = true;
        tools_.push_back(std::move(tool));

        spdlog::info("[ToolState] Created implicit single tool (T0)");
    } else {
        // Toolchanger: one ToolInfo per discovered tool
        // Build set of known extruders for mapping
        std::vector<std::string> extruder_names;
        for (const auto& h : heaters) {
            if (h == "extruder" ||
                (h.size() > 8 && h.rfind("extruder", 0) == 0 && std::isdigit(h[8]))) {
                extruder_names.push_back(h);
            }
        }
        // Sort for consistent T0→extruder, T1→extruder1 mapping
        std::sort(extruder_names.begin(), extruder_names.end());

        for (int i = 0; i < static_cast<int>(tool_names.size()); ++i) {
            ToolInfo tool;
            tool.index = i;
            tool.name = tool_names[i];

            // Map tool index to extruder name (T0→extruder, T1→extruder1, ...)
            if (i < static_cast<int>(extruder_names.size())) {
                tool.extruder_name = extruder_names[i];
            }

            tools_.push_back(std::move(tool));
        }

        spdlog::info("[ToolState] Created {} tools from toolchanger discovery", tools_.size());
    }

    active_tool_index_ = 0;

    // Update subjects
    if (subjects_initialized_) {
        lv_subject_set_int(&tool_count_subject_, static_cast<int>(tools_.size()));
        lv_subject_set_int(&active_tool_subject_, active_tool_index_);
        lv_subject_set_int(&tools_version_subject_,
                           lv_subject_get_int(&tools_version_subject_) + 1);
    }
}

const ToolInfo* ToolState::active_tool() const {
    if (active_tool_index_ >= 0 &&
        active_tool_index_ < static_cast<int>(tools_.size())) {
        return &tools_[active_tool_index_];
    }
    return nullptr;
}
```

Also update `deinit_subjects()` to clear tools:

```cpp
void ToolState::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    spdlog::debug("[ToolState] Deinitializing subjects");
    tools_.clear();
    active_tool_index_ = 0;
    subjects_.deinit_all();
    subjects_initialized_ = false;
}
```

**Step 4: Run test to verify it passes**

```bash
make test && ./build/bin/helix-tests "[tool-state]" --verbosity high
```
Expected: PASS — 13 test cases

**Step 5: Commit**

```bash
git add include/tool_state.h src/printer/tool_state.cpp tests/unit/test_tool_state.cpp
git commit -m "feat(tool): add init_tools with discovery-based tool creation"
```

---

## Task 4: update_from_status — tool state updates

**Files:**
- Modify: `include/tool_state.h`
- Modify: `src/printer/tool_state.cpp`
- Modify: `tests/unit/test_tool_state.cpp`

This task adds `update_from_status()` which processes Moonraker status JSON for toolchanger and per-tool state. This method will be called from `AmsState`'s event handler when the toolchanger backend reports changes.

**Step 1: Write the failing tests**

Add to `tests/unit/test_tool_state.cpp`:

```cpp
TEST_CASE("ToolState: update_from_status sets active tool", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1",
                              "extruder", "extruder1", "heater_bed", "gcode_move"};
    hw.parse_objects(objects);
    ts.init_tools(hw);

    // Simulate toolchanger selecting T1
    nlohmann::json status = {
        {"toolchanger", {{"tool_number", 1}, {"status", "ready"}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.active_tool_index() == 1);
    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == 1);
    REQUIRE(ts.active_tool() != nullptr);
    REQUIRE(ts.active_tool()->name == "T1");

    ts.deinit_subjects();
}

TEST_CASE("ToolState: update_from_status sets mounted state", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1",
                              "extruder", "extruder1", "heater_bed", "gcode_move"};
    hw.parse_objects(objects);
    ts.init_tools(hw);

    // Simulate T0 mounted, T1 not
    nlohmann::json status = {
        {"tool T0", {{"mounted", true}, {"active", true}}},
        {"tool T1", {{"mounted", false}, {"active", false}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.tools()[0].mounted == true);
    REQUIRE(ts.tools()[0].active == true);
    REQUIRE(ts.tools()[1].mounted == false);
    REQUIRE(ts.tools()[1].active == false);

    ts.deinit_subjects();
}

TEST_CASE("ToolState: update_from_status parses offsets", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1",
                              "extruder", "extruder1", "heater_bed", "gcode_move"};
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {
        {"tool T1", {{"gcode_x_offset", 25.5}, {"gcode_y_offset", -1.2}, {"gcode_z_offset", 0.05}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.tools()[1].gcode_x_offset == Catch::Approx(25.5f));
    REQUIRE(ts.tools()[1].gcode_y_offset == Catch::Approx(-1.2f));
    REQUIRE(ts.tools()[1].gcode_z_offset == Catch::Approx(0.05f));

    ts.deinit_subjects();
}

TEST_CASE("ToolState: update_from_status with no tools is safe", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    // No init_tools called — should not crash
    nlohmann::json status = {
        {"toolchanger", {{"tool_number", 0}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.tool_count() == 0);
    ts.deinit_subjects();
}

TEST_CASE("ToolState: update_from_status tool_number -1 means no tool", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1",
                              "extruder", "extruder1", "heater_bed", "gcode_move"};
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {
        {"toolchanger", {{"tool_number", -1}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.active_tool_index() == -1);
    REQUIRE(ts.active_tool() == nullptr);
    REQUIRE(lv_subject_get_int(ts.get_active_tool_subject()) == -1);

    ts.deinit_subjects();
}
```

**Step 2: Run test to verify it fails**

```bash
make test
```
Expected: FAIL — `update_from_status()` not defined

**Step 3: Write the implementation**

Add to `include/tool_state.h` (public section):

```cpp
    /**
     * @brief Update tool state from Moonraker status JSON
     *
     * Processes "toolchanger" and "tool T*" objects from status updates.
     * Called when AmsBackendToolChanger reports state changes.
     *
     * @param status JSON object containing toolchanger/tool state
     */
    void update_from_status(const nlohmann::json& status);
```

Add to `src/printer/tool_state.cpp`:

```cpp
void ToolState::update_from_status(const nlohmann::json& status) {
    if (tools_.empty()) {
        return;
    }

    bool state_changed = false;

    // Parse toolchanger object (active tool selection)
    if (status.contains("toolchanger") && status["toolchanger"].is_object()) {
        const auto& tc = status["toolchanger"];

        if (tc.contains("tool_number") && tc["tool_number"].is_number_integer()) {
            int tool_num = tc["tool_number"].get<int>();
            if (tool_num != active_tool_index_) {
                active_tool_index_ = tool_num;
                if (subjects_initialized_) {
                    lv_subject_set_int(&active_tool_subject_, active_tool_index_);
                }
                spdlog::debug("[ToolState] Active tool changed to {}", tool_num);
                state_changed = true;
            }
        }
    }

    // Parse individual tool objects
    for (auto& tool : tools_) {
        std::string key = "tool " + tool.name;
        if (!status.contains(key) || !status[key].is_object()) {
            continue;
        }

        const auto& td = status[key];

        if (td.contains("active") && td["active"].is_boolean()) {
            tool.active = td["active"].get<bool>();
        }
        if (td.contains("mounted") && td["mounted"].is_boolean()) {
            tool.mounted = td["mounted"].get<bool>();
        }
        if (td.contains("detect_state") && td["detect_state"].is_string()) {
            std::string ds = td["detect_state"].get<std::string>();
            if (ds == "present") tool.detect_state = DetectState::PRESENT;
            else if (ds == "absent") tool.detect_state = DetectState::ABSENT;
            else tool.detect_state = DetectState::UNAVAILABLE;
        }
        if (td.contains("gcode_x_offset") && td["gcode_x_offset"].is_number()) {
            tool.gcode_x_offset = td["gcode_x_offset"].get<float>();
        }
        if (td.contains("gcode_y_offset") && td["gcode_y_offset"].is_number()) {
            tool.gcode_y_offset = td["gcode_y_offset"].get<float>();
        }
        if (td.contains("gcode_z_offset") && td["gcode_z_offset"].is_number()) {
            tool.gcode_z_offset = td["gcode_z_offset"].get<float>();
        }

        // Capture extruder/fan names if provided (first time from initial status)
        if (td.contains("extruder") && td["extruder"].is_string()) {
            tool.extruder_name = td["extruder"].get<std::string>();
        }
        if (td.contains("fan") && td["fan"].is_string()) {
            tool.fan_name = td["fan"].get<std::string>();
        }

        state_changed = true;
        spdlog::trace("[ToolState] Updated tool {}: active={}, mounted={}", tool.name,
                      tool.active, tool.mounted);
    }

    if (state_changed && subjects_initialized_) {
        lv_subject_set_int(&tools_version_subject_,
                           lv_subject_get_int(&tools_version_subject_) + 1);
    }
}
```

**Step 4: Run test to verify it passes**

```bash
make test && ./build/bin/helix-tests "[tool-state]" --verbosity high
```
Expected: PASS — 18 test cases

**Step 5: Commit**

```bash
git add include/tool_state.h src/printer/tool_state.cpp tests/unit/test_tool_state.cpp
git commit -m "feat(tool): add update_from_status for tool state parsing"
```

---

## Task 5: Wire ToolState into application lifecycle

**Files:**
- Modify: `src/printer/printer_discovery.cpp` — add `ToolState::init_tools()` call
- Modify: `src/application/application.cpp` — add `ToolState::init_subjects()` / `deinit_subjects()`
- Modify: `src/printer/ams_state.cpp` — forward toolchanger updates to ToolState

This task integrates ToolState into the application startup and status update flow.

**Step 1: Find init_subjects/deinit_subjects calls to add ToolState alongside**

Search for where `PrinterState` and `PrinterFanState` subjects are initialized:

```bash
grep -n "init_subjects\|deinit_subjects" src/application/application.cpp | head -20
```

The pattern is: `init_subjects()` in app startup, `deinit_subjects()` in shutdown.

**Step 2: Add ToolState lifecycle**

In `src/application/application.cpp`, find where `PrinterState::instance().init_subjects()` is called and add after it:

```cpp
#include "tool_state.h"

// In startup (after PrinterState init_subjects):
ToolState::instance().init_subjects();

// In shutdown (before PrinterState deinit_subjects):
ToolState::instance().deinit_subjects();
```

**Step 3: Wire into discovery**

In `src/printer/printer_discovery.cpp`, in `init_subsystems_from_hardware()`, add after `printer_state.init_extruders()`:

```cpp
#include "tool_state.h"

// After printer_state.init_extruders(hardware.heaters()):
ToolState::instance().init_tools(hardware);
```

**Step 4: Forward toolchanger updates to ToolState**

In `src/printer/ams_state.cpp`, find the event callback where `AmsBackendToolChanger` state changes are processed. The backend emits `EVENT_STATE_CHANGED` which `AmsState` handles. In that handler, forward the status to ToolState.

Alternatively, add a `register_notify_update` callback in `init_backend_from_hardware` that forwards tool-related status updates to ToolState.

The cleanest approach: in `MoonrakerManager::process_notifications()` (or in PrinterState's `update_from_notification`), extract toolchanger/tool data and forward to ToolState:

Find where `get_printer_state().update_from_notification(notification)` is called and add:

```cpp
// After printer state update:
ToolState::instance().update_from_status(params);
```

This ensures ToolState receives the same raw Moonraker data without depending on the AMS backend.

**Step 5: Build and test**

```bash
make -j && make test-run
```
Expected: All tests pass, builds clean

**Step 6: Commit**

```bash
git add src/printer/printer_discovery.cpp src/application/application.cpp
# Include any other modified files
git commit -m "feat(tool): wire ToolState into application lifecycle and status updates"
```

---

## Task 6: Capture extruder/fan names in AmsBackendToolChanger

**Files:**
- Modify: `src/printer/ams_backend_toolchanger.cpp`
- Modify: `include/ams_backend_toolchanger.h` (if needed for storage)

Currently `parse_tool_state()` receives `extruder` and `fan` fields from Klipper but only logs them (discards). ToolState's `update_from_status()` will capture these from the raw Moonraker JSON, but we need to ensure the subscription includes tool objects.

**Step 1: Add tool objects to Moonraker subscription**

In `src/api/moonraker_client.cpp`, in `complete_discovery_subscription()`, add:

```cpp
// All discovered tool objects (for toolchanger support)
// "tool T0", "tool T1", etc. + "toolchanger"
for (const auto& tool_name : hardware_.tool_names()) {
    subscription_objects["tool " + tool_name] = nullptr;
}
if (hardware_.has_tool_changer()) {
    subscription_objects["toolchanger"] = nullptr;
}
```

This ensures Moonraker sends updates for tool objects, which currently aren't subscribed to.

**Step 2: Build and test**

```bash
make -j && make test-run
```

**Step 3: Commit**

```bash
git add src/api/moonraker_client.cpp
git commit -m "fix(moonraker): subscribe to toolchanger and tool objects"
```

---

## Task 7: Home Panel — active tool badge

**Files:**
- Modify: `src/ui/panels/home_panel.h` (if new members needed)
- Modify: `src/ui/panels/home_panel.cpp`
- Modify: `ui_xml/home_panel.xml` (for tool badge visibility binding)

**Design:** When `tool_count > 1`, show a small "T0"/"T1" badge next to the nozzle temperature readout on the home screen. Single-extruder: no badge, no visual change.

**Step 1: Add tool count observer to HomePanel**

In `home_panel.cpp`, add observer for `ToolState::get_tool_count_subject()` and `ToolState::get_active_tool_subject()`. When tool count > 1, update a string subject with the active tool name (e.g., "T0").

```cpp
#include "tool_state.h"

// In observer setup:
auto& tool_state = ToolState::instance();
tool_badge_observer_ = observe_int_sync<HomePanel>(
    tool_state.get_active_tool_subject(), this,
    [](HomePanel* self, int tool_idx) {
        self->update_tool_badge(tool_idx);
    });
```

**Step 2: Add tool badge subject**

```cpp
// New subject for tool badge text (empty when single tool)
lv_subject_t tool_badge_subject_;
char tool_badge_buf_[8] = {};

// In init_subjects():
UI_MANAGED_SUBJECT_STRING(tool_badge_subject_, tool_badge_buf_, "", "tool_badge_text", subjects_);
```

**Step 3: Update tool badge helper**

```cpp
void HomePanel::update_tool_badge(int tool_idx) {
    auto& tool_state = ToolState::instance();
    if (tool_state.tool_count() <= 1) {
        tool_badge_buf_[0] = '\0';
    } else {
        const auto* tool = tool_state.active_tool();
        if (tool) {
            std::snprintf(tool_badge_buf_, sizeof(tool_badge_buf_), "%s", tool->name.c_str());
        }
    }
    if (subjects_initialized_) {
        lv_subject_copy_string(&tool_badge_subject_, tool_badge_buf_);
    }
}
```

**Step 4: Add badge to XML**

In `ui_xml/home_panel.xml`, add a small text element near the nozzle temp display:

```xml
<text_small name="tool_badge" bind_text="tool_badge_text"
            style_text_color="#text_secondary"/>
```

Use `bind_flag_if_eq` to hide when badge text is empty, or just let the empty string render as nothing.

**Step 5: Build and run visually**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Verify: Single-extruder mock → no badge visible. (Toolchanger testing deferred to real hardware or mock toolchanger.)

**Step 6: Commit**

```bash
git add src/ui/panels/home_panel.h src/ui/panels/home_panel.cpp ui_xml/home_panel.xml
git commit -m "feat(home): show active tool badge for multi-tool printers"
```

---

## Task 8: Print Status Overlay — active tool in nozzle label

**Files:**
- Modify: `src/ui/overlays/print_status_overlay.h` (if new members)
- Modify: `src/ui/overlays/print_status_overlay.cpp`

**Design:** When multiple tools are present, prefix the nozzle temperature label with the active tool name (e.g., "T0: 205 / 210°C" instead of "205 / 210°C"). Single-tool: unchanged.

**Step 1: Add tool awareness to PrintStatusPanel**

```cpp
#include "tool_state.h"

// Add observer for active tool changes
ObserverGuard active_tool_observer_;

// In setup:
auto& tool_state = ToolState::instance();
active_tool_observer_ = observe_int_sync<PrintStatusPanel>(
    tool_state.get_active_tool_subject(), this,
    [](PrintStatusPanel* self, int) { self->on_temperature_changed(); });
```

**Step 2: Modify temperature formatting**

In `on_temperature_changed()`, when tool count > 1, prefix nozzle temp with tool name:

```cpp
auto& tool_state = ToolState::instance();
if (tool_state.tool_count() > 1) {
    const auto* tool = tool_state.active_tool();
    if (tool) {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%s: ", tool->name.c_str());
        size_t prefix_len = std::strlen(nozzle_temp_buf_);
        format_temperature_pair(centi_to_degrees(nozzle_current_),
                                centi_to_degrees(nozzle_target_),
                                nozzle_temp_buf_ + prefix_len,
                                sizeof(nozzle_temp_buf_) - prefix_len);
    }
} else {
    format_temperature_pair(centi_to_degrees(nozzle_current_),
                            centi_to_degrees(nozzle_target_),
                            nozzle_temp_buf_, sizeof(nozzle_temp_buf_));
}
```

**Step 3: Build and verify**

```bash
make -j && make test-run
```

**Step 4: Commit**

```bash
git add src/ui/overlays/print_status_overlay.h src/ui/overlays/print_status_overlay.cpp
git commit -m "feat(print-status): show active tool name in nozzle temperature label"
```

---

## Task 9: TempControlPanel — route commands to active tool's extruder

**Files:**
- Modify: `src/ui/ui_panel_temp_control.cpp`

**Design:** When a toolchanger is active, TempControlPanel's extruder selector from Phase 1 should sync with the active tool. When the user selects a different extruder in the temp panel, it should also select the corresponding tool. When a tool change happens externally, the temp panel should switch to show that tool's extruder.

**Step 1: Add tool-aware extruder selection**

In `setup_nozzle_panel()`, observe `ToolState::get_active_tool_subject()`:

```cpp
#include "tool_state.h"

// When active tool changes, switch to that tool's extruder
auto& tool_state = ToolState::instance();
if (tool_state.tool_count() > 1) {
    // Observe active tool changes
    active_tool_observer_ = observe_int_sync<TempControlPanel>(
        tool_state.get_active_tool_subject(), this,
        [](TempControlPanel* self, int tool_idx) {
            auto& ts = ToolState::instance();
            const auto* tool = ts.active_tool();
            if (tool && tool->extruder_name) {
                self->select_extruder(*tool->extruder_name);
            }
        });
}
```

**Step 2: Build and verify**

```bash
make -j && make test-run
```

**Step 3: Commit**

```bash
git add src/ui/ui_panel_temp_control.cpp
git commit -m "feat(temp-control): sync extruder selection with active tool"
```

---

## Task 10: Documentation and lifecycle tests

**Files:**
- Modify: `docs/devel/plans/2026-02-13-tool-abstraction-design.md` — update Phase 2 status
- Create or modify tests for ToolState lifecycle edge cases

**Step 1: Add lifecycle edge case tests**

```cpp
TEST_CASE("ToolState: update_from_status captures extruder name from Klipper",
          "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = {"toolchanger", "tool T0", "tool T1",
                              "extruder", "extruder1", "heater_bed", "gcode_move"};
    hw.parse_objects(objects);
    ts.init_tools(hw);

    // Klipper sends extruder association in tool status
    nlohmann::json status = {
        {"tool T0", {{"extruder", "extruder"}, {"fan", "part_fan_T0"}}},
        {"tool T1", {{"extruder", "extruder1"}, {"fan", "part_fan_T1"}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.tools()[0].extruder_name.value() == "extruder");
    REQUIRE(ts.tools()[0].fan_name.value() == "part_fan_T0");
    REQUIRE(ts.tools()[1].extruder_name.value() == "extruder1");
    REQUIRE(ts.tools()[1].fan_name.value() == "part_fan_T1");

    ts.deinit_subjects();
}

TEST_CASE("ToolState: detect_state parsed from status", "[tool][tool-state]") {
    lv_init_safe();
    auto& ts = ToolState::instance();
    ts.deinit_subjects();
    ts.init_subjects(false);

    helix::PrinterDiscovery hw;
    nlohmann::json objects = {"toolchanger", "tool T0",
                              "extruder", "heater_bed", "gcode_move"};
    hw.parse_objects(objects);
    ts.init_tools(hw);

    nlohmann::json status = {
        {"tool T0", {{"detect_state", "present"}}}
    };
    ts.update_from_status(status);

    REQUIRE(ts.tools()[0].detect_state == DetectState::PRESENT);

    ts.deinit_subjects();
}
```

**Step 2: Update design doc status**

In `docs/devel/plans/2026-02-13-tool-abstraction-design.md`, change the Phase 2 section header or add a status note:

```markdown
## Phase 2: Tool Abstraction ✅ Complete
```

**Step 3: Build, test, commit**

```bash
make test-run
git add tests/unit/test_tool_state.cpp docs/devel/plans/2026-02-13-tool-abstraction-design.md
git commit -m "test(tool): add lifecycle and edge case tests; update design doc status"
```

---

## Summary

| Task | What | Files | Commit |
|------|------|-------|--------|
| 1 | ToolInfo struct + DetectState enum | `include/tool_state.h`, test | `feat(tool): add ToolInfo struct` |
| 2 | ToolState singleton + subjects | `include/tool_state.h`, `src/printer/tool_state.cpp`, test | `feat(tool): add ToolState singleton` |
| 3 | init_tools from discovery | Same + `printer_discovery.h` include | `feat(tool): add init_tools` |
| 4 | update_from_status parsing | Same | `feat(tool): add update_from_status` |
| 5 | Wire into app lifecycle | `application.cpp`, `printer_discovery.cpp` | `feat(tool): wire into lifecycle` |
| 6 | Fix tool object subscription | `moonraker_client.cpp` | `fix(moonraker): subscribe to tool objects` |
| 7 | Home panel tool badge | `home_panel.*`, `home_panel.xml` | `feat(home): active tool badge` |
| 8 | Print status tool label | `print_status_overlay.*` | `feat(print-status): tool name in nozzle label` |
| 9 | TempControl tool-extruder sync | `ui_panel_temp_control.cpp` | `feat(temp-control): sync with active tool` |
| 10 | Tests + docs | tests, design doc | `test(tool): lifecycle tests + doc update` |
