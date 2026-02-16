# Probe Management System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Full probe management — detection of all common probe types, dedicated probe overlay with type-specific controls, safe Klipper config editing with include resolution, and a consolidated "Detected Hardware" wizard step.

**Architecture:** Expand `ProbeSensorManager` to detect Cartographer/Beacon/Tap/Klicky. Build a `KlipperConfigEditor` for safe targeted config file edits with include resolution and backup/restore. Create a new probe overlay with swappable type-specific panels. Replace the narrow wizard probe step with a broader hardware detection step.

**Tech Stack:** C++17, LVGL 9.4 XML, Catch2, Moonraker JSON-RPC + file API, spdlog

**Design doc:** `docs/devel/plans/2026-02-15-probe-management-design.md`

**Status:** Phases 1-2 complete (on `feature/probe-management` branch). Phases 3-5 remaining.

**Key docs to read before starting:**
- `docs/devel/LVGL9_XML_GUIDE.md` — XML widget patterns, bindings, event callbacks
- `docs/devel/UI_CONTRIBUTOR_GUIDE.md` — Layout tokens, breakpoints, component patterns
- `docs/devel/MODAL_SYSTEM.md` — Overlay patterns (probe overlay follows this)
- `include/probe_sensor_types.h` — Current probe type enum and config structs
- `src/sensors/probe_sensor_manager.cpp` — Current detection logic
- `src/ui/ui_wizard.cpp` — Wizard step array and skip logic
- `/Users/pbrown/Code/Printing/kalico/klippy/configfile.py` — Klipper config format reference

---

## Phase 1: Expanded Probe Detection ✅ COMPLETE

### Task 1.1: Add new probe type enum values

**Files:**
- Modify: `include/probe_sensor_types.h:17-22`
- Test: `tests/unit/test_probe_sensor_manager.cpp`

**Step 1: Write failing tests for new type string conversions**

Add to `tests/unit/test_probe_sensor_manager.cpp` in the existing type helper test section:

```cpp
TEST_CASE("Probe type string conversions - new types", "[probe][types]") {
    SECTION("cartographer type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::CARTOGRAPHER) == "cartographer");
    }
    SECTION("beacon type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::BEACON) == "beacon");
    }
    SECTION("tap type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::TAP) == "tap");
    }
    SECTION("klicky type to string") {
        REQUIRE(probe_type_to_string(ProbeSensorType::KLICKY) == "klicky");
    }
    SECTION("cartographer from string") {
        REQUIRE(probe_type_from_string("cartographer") == ProbeSensorType::CARTOGRAPHER);
    }
    SECTION("beacon from string") {
        REQUIRE(probe_type_from_string("beacon") == ProbeSensorType::BEACON);
    }
    SECTION("tap from string") {
        REQUIRE(probe_type_from_string("tap") == ProbeSensorType::TAP);
    }
    SECTION("klicky from string") {
        REQUIRE(probe_type_from_string("klicky") == ProbeSensorType::KLICKY);
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[probe][types]" -v`
Expected: Compilation errors — `CARTOGRAPHER`, `BEACON`, `TAP`, `KLICKY` not defined.

**Step 3: Add enum values and update string converters**

In `include/probe_sensor_types.h`, add to `ProbeSensorType` enum:

```cpp
enum class ProbeSensorType {
    STANDARD = 1,       ///< Standard probe (Klipper "probe" section)
    BLTOUCH = 2,        ///< BLTouch probe
    SMART_EFFECTOR = 3, ///< Duet Smart Effector
    EDDY_CURRENT = 4,   ///< Generic eddy current probe (probe_eddy_current)
    CARTOGRAPHER = 5,   ///< Cartographer 3D scanning/contact probe
    BEACON = 6,         ///< Beacon eddy current probe
    TAP = 7,            ///< Voron Tap nozzle-contact probe
    KLICKY = 8,         ///< Klicky magnetic probe (macro-based)
};
```

Update `probe_type_to_string()` switch with new cases.
Update `probe_type_from_string()` with new string checks.

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[probe][types]" -v`
Expected: All PASS.

**Step 5: Commit**

```bash
git add include/probe_sensor_types.h tests/unit/test_probe_sensor_manager.cpp
git commit -m "feat(probe): add Cartographer, Beacon, Tap, Klicky probe type enums"
```

---

### Task 1.2: Expand parse_klipper_name() for new probe types

**Files:**
- Modify: `src/sensors/probe_sensor_manager.cpp:427-459`
- Modify: `include/probe_sensor_manager.h` (add macro heuristic method)
- Test: `tests/unit/test_probe_sensor_manager.cpp`

**Step 1: Write failing tests for new detection**

```cpp
TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - discovery of new probe types",
                 "[probe][discovery]") {
    SECTION("Discovers cartographer object") {
        // Cartographer registers as "cartographer" in Klipper objects
        std::vector<std::string> objects = {"cartographer"};
        mgr().discover(objects);
        REQUIRE(mgr().has_sensors());
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::CARTOGRAPHER);
    }

    SECTION("Discovers beacon object") {
        // Beacon registers as "beacon" in Klipper objects
        std::vector<std::string> objects = {"beacon"};
        mgr().discover(objects);
        REQUIRE(mgr().has_sensors());
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::BEACON);
    }

    SECTION("Discovers eddy current as cartographer via model object") {
        // Some Cartographer setups show as probe_eddy_current + cartographer model object
        std::vector<std::string> objects = {"probe_eddy_current carto", "cartographer"};
        mgr().discover(objects);
        REQUIRE(mgr().sensor_count() >= 1);
        // Should detect as CARTOGRAPHER, not generic EDDY_CURRENT
        auto configs = mgr().get_sensors();
        bool found_carto = false;
        for (const auto& c : configs) {
            if (c.type == ProbeSensorType::CARTOGRAPHER) found_carto = true;
        }
        REQUIRE(found_carto);
    }

    SECTION("Discovers beacon model object") {
        std::vector<std::string> objects = {"probe_eddy_current beacon", "beacon model default"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        bool found_beacon = false;
        for (const auto& c : configs) {
            if (c.type == ProbeSensorType::BEACON) found_beacon = true;
        }
        REQUIRE(found_beacon);
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[probe][discovery]" -v`
Expected: FAIL — `cartographer` and `beacon` not recognized by `parse_klipper_name()`.

**Step 3: Implement expanded detection**

In `probe_sensor_manager.cpp`, update `parse_klipper_name()`:

```cpp
bool ProbeSensorManager::parse_klipper_name(const std::string& klipper_name,
                                            std::string& sensor_name, ProbeSensorType& type) const {
    // Cartographer probe
    if (klipper_name == "cartographer") {
        sensor_name = "cartographer";
        type = ProbeSensorType::CARTOGRAPHER;
        return true;
    }

    // Beacon probe
    if (klipper_name == "beacon") {
        sensor_name = "beacon";
        type = ProbeSensorType::BEACON;
        return true;
    }

    // Standard probe
    if (klipper_name == "probe") {
        sensor_name = "probe";
        type = ProbeSensorType::STANDARD;
        return true;
    }

    // BLTouch
    if (klipper_name == "bltouch") {
        sensor_name = "bltouch";
        type = ProbeSensorType::BLTOUCH;
        return true;
    }

    // Smart Effector
    if (klipper_name == "smart_effector") {
        sensor_name = "smart_effector";
        type = ProbeSensorType::SMART_EFFECTOR;
        return true;
    }

    // Eddy current probe: "probe_eddy_current <name>"
    const std::string eddy_prefix = "probe_eddy_current ";
    if (klipper_name.rfind(eddy_prefix, 0) == 0 && klipper_name.size() > eddy_prefix.size()) {
        sensor_name = klipper_name.substr(eddy_prefix.size());
        type = ProbeSensorType::EDDY_CURRENT;
        return true;
    }

    return false;
}
```

Also add a post-discovery pass in `discover()` that upgrades EDDY_CURRENT to CARTOGRAPHER or BEACON if the corresponding named object was also found in the object list. This handles the case where a Cartographer shows up as both `probe_eddy_current carto` and `cartographer`.

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[probe][discovery]" -v`
Expected: All PASS.

**Step 5: Commit**

```bash
git add src/sensors/probe_sensor_manager.cpp include/probe_sensor_manager.h tests/unit/test_probe_sensor_manager.cpp
git commit -m "feat(probe): detect Cartographer, Beacon, and eddy current probe variants"
```

---

### Task 1.3: Add Klicky and Tap detection via macro heuristics

**Files:**
- Modify: `src/sensors/probe_sensor_manager.cpp`
- Modify: `include/probe_sensor_manager.h`
- Test: `tests/unit/test_probe_sensor_manager.cpp`

**Context:** Klicky and Tap don't register special Klipper objects. Klicky is detected via ATTACH_PROBE/DOCK_PROBE macros. Tap is detected by its `[probe]` section being configured for nozzle contact (detected via macro names like `TAP_VERSION` or config heuristics).

**Step 1: Write failing tests**

```cpp
TEST_CASE_METHOD(ProbeSensorTestFixture, "ProbeSensorManager - macro-based probe detection",
                 "[probe][discovery][macros]") {
    SECTION("Detects Klicky from macros") {
        std::vector<std::string> objects = {"probe", "gcode_macro ATTACH_PROBE",
                                            "gcode_macro DOCK_PROBE"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs.size() == 1);
        REQUIRE(configs[0].type == ProbeSensorType::KLICKY);
    }

    SECTION("Detects Klicky from alternate macro names") {
        std::vector<std::string> objects = {"probe", "gcode_macro _Probe_Deploy",
                                            "gcode_macro _Probe_Stow"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::KLICKY);
    }

    SECTION("Standard probe without Klicky macros stays STANDARD") {
        std::vector<std::string> objects = {"probe"};
        mgr().discover(objects);
        auto configs = mgr().get_sensors();
        REQUIRE(configs[0].type == ProbeSensorType::STANDARD);
    }
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[probe][discovery][macros]" -v`
Expected: FAIL — Klicky macros not checked.

**Step 3: Implement macro heuristic detection**

Add a post-discovery refinement pass in `discover()` that checks the object list for macro names:

```cpp
void ProbeSensorManager::refine_probe_types(const std::vector<std::string>& objects) {
    // Build a set of macro names for fast lookup
    std::unordered_set<std::string> macros;
    for (const auto& obj : objects) {
        if (obj.rfind("gcode_macro ", 0) == 0) {
            macros.insert(obj.substr(12)); // Strip "gcode_macro " prefix
        }
    }

    for (auto& [name, config] : sensors_) {
        // Klicky: standard probe + ATTACH_PROBE/DOCK_PROBE or _Probe_Deploy/_Probe_Stow
        if (config.type == ProbeSensorType::STANDARD) {
            bool has_klicky = (macros.count("ATTACH_PROBE") && macros.count("DOCK_PROBE")) ||
                              (macros.count("_Probe_Deploy") && macros.count("_Probe_Stow"));
            if (has_klicky) {
                config.type = ProbeSensorType::KLICKY;
                spdlog::debug("[ProbeSensor] Refined {} to Klicky (macro heuristic)", name);
            }
        }
    }
}
```

Call `refine_probe_types(objects)` at the end of `discover()`.

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[probe][discovery]" -v`
Expected: All PASS.

**Step 5: Commit**

```bash
git add src/sensors/probe_sensor_manager.cpp include/probe_sensor_manager.h tests/unit/test_probe_sensor_manager.cpp
git commit -m "feat(probe): detect Klicky probe via macro heuristics"
```

---

### Task 1.4: Add probe type display strings

**Files:**
- Modify: `include/probe_sensor_types.h`
- Test: `tests/unit/test_probe_sensor_manager.cpp`

**Step 1: Write failing test**

```cpp
TEST_CASE("Probe type display strings", "[probe][types]") {
    REQUIRE(probe_type_to_display_string(ProbeSensorType::CARTOGRAPHER) == "Cartographer");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::BEACON) == "Beacon");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::TAP) == "Voron Tap");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::KLICKY) == "Klicky");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::BLTOUCH) == "BLTouch");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::EDDY_CURRENT) == "Eddy Current");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::STANDARD) == "Probe");
    REQUIRE(probe_type_to_display_string(ProbeSensorType::SMART_EFFECTOR) == "Smart Effector");
}
```

**Step 2: Run test — expect compile error (function doesn't exist)**

**Step 3: Add `probe_type_to_display_string()` to `probe_sensor_types.h`**

```cpp
[[nodiscard]] inline std::string probe_type_to_display_string(ProbeSensorType type) {
    switch (type) {
    case ProbeSensorType::STANDARD: return "Probe";
    case ProbeSensorType::BLTOUCH: return "BLTouch";
    case ProbeSensorType::SMART_EFFECTOR: return "Smart Effector";
    case ProbeSensorType::EDDY_CURRENT: return "Eddy Current";
    case ProbeSensorType::CARTOGRAPHER: return "Cartographer";
    case ProbeSensorType::BEACON: return "Beacon";
    case ProbeSensorType::TAP: return "Voron Tap";
    case ProbeSensorType::KLICKY: return "Klicky";
    default: return "Unknown Probe";
    }
}
```

**Step 4: Run tests — all pass**

**Step 5: Commit**

```bash
git add include/probe_sensor_types.h tests/unit/test_probe_sensor_manager.cpp
git commit -m "feat(probe): add human-readable display strings for all probe types"
```

---

## Phase 2: Klipper Config Editor ✅ COMPLETE

### Task 2.1: Config file structure parser

**Files:**
- Create: `include/klipper_config_editor.h`
- Create: `src/system/klipper_config_editor.cpp`
- Create: `tests/unit/test_klipper_config_editor.cpp`

This is the core parser that understands Klipper's INI-like format. No Moonraker integration yet — pure text parsing.

**Step 1: Write failing tests for section parsing**

```cpp
// tests/unit/test_klipper_config_editor.cpp
#include "../catch_amalgamated.hpp"
#include "klipper_config_editor.h"

using namespace helix::system;

TEST_CASE("KlipperConfigEditor - section parsing", "[config][parser]") {
    KlipperConfigEditor editor;

    SECTION("Finds simple section") {
        std::string content = "[printer]\nkinematics: corexy\n\n[probe]\npin: PA1\nz_offset: 1.5\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.sections.count("probe") == 1);
        REQUIRE(result.sections["probe"].line_start > 0);
    }

    SECTION("Handles section with space in name") {
        std::string content = "[bed_mesh default]\nversion: 1\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.sections.count("bed_mesh default") == 1);
    }

    SECTION("Finds key within section") {
        std::string content = "[probe]\npin: PA1\nz_offset: 1.5\nsamples: 3\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "z_offset");
        REQUIRE(key.has_value());
        REQUIRE(key->value == "1.5");
    }

    SECTION("Handles both : and = delimiters") {
        std::string content = "[probe]\npin: PA1\nz_offset = 1.5\n";
        auto result = editor.parse_structure(content);
        auto key1 = result.find_key("probe", "pin");
        auto key2 = result.find_key("probe", "z_offset");
        REQUIRE(key1->delimiter == ":");
        REQUIRE(key2->delimiter == "=");
    }

    SECTION("Skips multi-line values correctly") {
        std::string content =
            "[gcode_macro START]\ngcode:\n    G28\n    G1 Z10\n\n[probe]\npin: PA1\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "pin");
        REQUIRE(key.has_value());
        REQUIRE(key->value == "PA1");
    }

    SECTION("Identifies SAVE_CONFIG boundary") {
        std::string content =
            "[probe]\npin: PA1\n\n"
            "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
            "#*# DO NOT EDIT THIS BLOCK OR BELOW.\n"
            "#*#\n"
            "#*# [probe]\n"
            "#*# z_offset = 1.234\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.save_config_line > 0);
    }

    SECTION("Preserves comments") {
        std::string content = "# My printer config\n[probe]\n# Z offset for BLTouch\nz_offset: 1.5\n";
        auto result = editor.parse_structure(content);
        // Comments should be tracked but not treated as keys
        auto key = result.find_key("probe", "z_offset");
        REQUIRE(key.has_value());
    }

    SECTION("Detects include directives") {
        std::string content = "[include hardware/*.cfg]\n[include macros.cfg]\n[printer]\nkinematics: corexy\n";
        auto result = editor.parse_structure(content);
        REQUIRE(result.includes.size() == 2);
        REQUIRE(result.includes[0] == "hardware/*.cfg");
        REQUIRE(result.includes[1] == "macros.cfg");
    }

    SECTION("Option names are lowercased") {
        std::string content = "[probe]\nZ_Offset: 1.5\n";
        auto result = editor.parse_structure(content);
        auto key = result.find_key("probe", "z_offset");
        REQUIRE(key.has_value());
    }
}
```

**Step 2: Run tests — compile error, files don't exist**

**Step 3: Implement `KlipperConfigEditor` header and structure parser**

Create `include/klipper_config_editor.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace helix::system {

/// A key-value pair within a config section
struct ConfigKey {
    std::string name;         ///< Key name (lowercased)
    std::string value;        ///< Raw value string
    std::string delimiter;    ///< ":" or "=" — preserved for round-trip fidelity
    int line_number = 0;      ///< Line number in the file (0-indexed)
    bool is_multiline = false;
    int end_line = 0;         ///< Last line of value (for multi-line)
};

/// A parsed config section
struct ConfigSection {
    std::string name;
    int line_start = 0;       ///< Line of [section] header
    int line_end = 0;         ///< Last line before next section or EOF
    std::vector<ConfigKey> keys;
};

/// Result of parsing a config file's structure
struct ConfigStructure {
    std::map<std::string, ConfigSection> sections;
    std::vector<std::string> includes;  ///< [include ...] paths found
    int save_config_line = -1;          ///< Line of SAVE_CONFIG marker (-1 = none)
    int total_lines = 0;

    /// Find a key within a section
    std::optional<ConfigKey> find_key(const std::string& section,
                                      const std::string& key) const;
};

/// Which file a section was found in (for include resolution)
struct SectionLocation {
    std::string file_path;    ///< Relative path within config root
    ConfigSection section;
};

/// Safe, targeted editor for Klipper config files
///
/// Understands Klipper's INI-like format (ported from Kalico's configfile.py):
/// - [section] headers with spaces allowed
/// - : and = delimiters (preserved)
/// - Multi-line values (indented continuation lines, empty lines allowed)
/// - # and ; comments (preserved, never stripped)
/// - [include ...] directives with glob support
/// - #*# SAVE_CONFIG boundary
class KlipperConfigEditor {
  public:
    /// Parse the structure of a config file (sections, keys, line ranges)
    /// Does NOT follow includes — call resolve_includes() for that.
    ConfigStructure parse_structure(const std::string& content) const;

    /// Set a value for an existing key within a file's content
    /// Returns modified content, or std::nullopt if key not found
    std::optional<std::string> set_value(const std::string& content,
                                         const std::string& section,
                                         const std::string& key,
                                         const std::string& new_value) const;

    /// Add a new key to an existing section
    /// Returns modified content, or std::nullopt if section not found
    std::optional<std::string> add_key(const std::string& content,
                                       const std::string& section,
                                       const std::string& key,
                                       const std::string& value,
                                       const std::string& delimiter = ":") const;

    /// Comment out a key (prefix with #) — safer than deleting
    /// Returns modified content, or std::nullopt if key not found
    std::optional<std::string> remove_key(const std::string& content,
                                          const std::string& section,
                                          const std::string& key) const;
};

} // namespace helix::system
```

Create `src/system/klipper_config_editor.cpp` with the `parse_structure()` implementation. The parser should:
- Split content into lines
- Track current section as it scans line by line
- Detect `[section]` headers via regex or simple bracket matching
- Detect `[include path]` directives
- Detect key-value pairs (look for `:` or `=` outside of indented lines)
- Track multi-line values (indented continuation lines)
- Detect `#*# <--- SAVE_CONFIG` marker
- Lowercase key names (not section names)
- Skip comment-only lines for key detection but preserve line numbers

**Step 4: Run tests — all pass**

**Step 5: Commit**

```bash
git add include/klipper_config_editor.h src/system/klipper_config_editor.cpp tests/unit/test_klipper_config_editor.cpp
git commit -m "feat(config): add Klipper config structure parser"
```

---

### Task 2.2: Config value editing (set_value, add_key, remove_key)

**Files:**
- Modify: `src/system/klipper_config_editor.cpp`
- Test: `tests/unit/test_klipper_config_editor.cpp`

**Step 1: Write failing tests**

```cpp
TEST_CASE("KlipperConfigEditor - value editing", "[config][editor]") {
    KlipperConfigEditor editor;

    SECTION("set_value replaces existing value") {
        std::string content = "[probe]\npin: PA1\nz_offset: 1.5\nsamples: 3\n";
        auto result = editor.set_value(content, "probe", "samples", "5");
        REQUIRE(result.has_value());
        REQUIRE(result->find("samples: 5") != std::string::npos);
        // Other values unchanged
        REQUIRE(result->find("pin: PA1") != std::string::npos);
        REQUIRE(result->find("z_offset: 1.5") != std::string::npos);
    }

    SECTION("set_value preserves delimiter style") {
        std::string content = "[probe]\nz_offset = 1.5\n";
        auto result = editor.set_value(content, "probe", "z_offset", "2.0");
        REQUIRE(result.has_value());
        REQUIRE(result->find("z_offset = 2.0") != std::string::npos);
    }

    SECTION("set_value preserves comments") {
        std::string content = "[probe]\n# Important comment\nz_offset: 1.5  # inline comment\n";
        auto result = editor.set_value(content, "probe", "z_offset", "2.0");
        REQUIRE(result.has_value());
        REQUIRE(result->find("# Important comment") != std::string::npos);
    }

    SECTION("set_value returns nullopt for missing key") {
        std::string content = "[probe]\npin: PA1\n";
        auto result = editor.set_value(content, "probe", "samples", "5");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("set_value returns nullopt for missing section") {
        std::string content = "[printer]\nkinematics: corexy\n";
        auto result = editor.set_value(content, "probe", "pin", "PA1");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("add_key adds to end of section") {
        std::string content = "[probe]\npin: PA1\nz_offset: 1.5\n\n[printer]\n";
        auto result = editor.add_key(content, "probe", "samples", "3");
        REQUIRE(result.has_value());
        REQUIRE(result->find("samples: 3") != std::string::npos);
        // Should be in [probe] section, before [printer]
        auto samples_pos = result->find("samples: 3");
        auto printer_pos = result->find("[printer]");
        REQUIRE(samples_pos < printer_pos);
    }

    SECTION("remove_key comments out the line") {
        std::string content = "[probe]\npin: PA1\nsamples: 3\nz_offset: 1.5\n";
        auto result = editor.remove_key(content, "probe", "samples");
        REQUIRE(result.has_value());
        REQUIRE(result->find("#samples: 3") != std::string::npos);
        // Other keys untouched
        REQUIRE(result->find("pin: PA1") != std::string::npos);
        REQUIRE(result->find("z_offset: 1.5") != std::string::npos);
    }
}
```

**Step 2: Run tests — FAIL**

**Step 3: Implement set_value, add_key, remove_key**

All three work by:
1. Call `parse_structure()` to find the target key's line number
2. Split content into lines
3. Make the targeted edit on the specific line(s)
4. Rejoin lines and return

Key principle: **only modify the specific line(s), preserve everything else byte-for-byte.**

**Step 4: Run tests — all pass**

**Step 5: Commit**

```bash
git add src/system/klipper_config_editor.cpp tests/unit/test_klipper_config_editor.cpp
git commit -m "feat(config): add set_value, add_key, remove_key for targeted config edits"
```

---

### Task 2.3: Include resolution

**Files:**
- Modify: `include/klipper_config_editor.h`
- Modify: `src/system/klipper_config_editor.cpp`
- Test: `tests/unit/test_klipper_config_editor.cpp`

**Step 1: Write failing tests**

```cpp
TEST_CASE("KlipperConfigEditor - include resolution", "[config][includes]") {
    KlipperConfigEditor editor;

    SECTION("Resolves simple include") {
        // Simulate file contents as a map (for unit testing without Moonraker)
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include hardware.cfg]\n[printer]\nkinematics: corexy\n";
        files["hardware.cfg"] = "[probe]\npin: PA1\nz_offset: 1.5\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        REQUIRE(result["probe"].file_path == "hardware.cfg");
    }

    SECTION("Resolves nested includes") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include hardware/main.cfg]\n[printer]\nkinematics: corexy\n";
        files["hardware/main.cfg"] = "[include probe.cfg]\n[stepper_x]\nstep_pin: PA1\n";
        files["hardware/probe.cfg"] = "[probe]\npin: PB6\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        REQUIRE(result["probe"].file_path == "hardware/probe.cfg");
    }

    SECTION("Detects circular includes") {
        std::map<std::string, std::string> files;
        files["a.cfg"] = "[include b.cfg]\n";
        files["b.cfg"] = "[include a.cfg]\n";

        auto result = editor.resolve_includes(files, "a.cfg");
        // Should not infinite loop — returns whatever it found
        REQUIRE(true); // Just verifying it terminates
    }

    SECTION("Caps recursion depth") {
        std::map<std::string, std::string> files;
        files["a.cfg"] = "[include b.cfg]\n";
        files["b.cfg"] = "[include c.cfg]\n";
        files["c.cfg"] = "[include d.cfg]\n";
        files["d.cfg"] = "[include e.cfg]\n";
        files["e.cfg"] = "[include f.cfg]\n";
        files["f.cfg"] = "[include g.cfg]\n"; // depth 6 — should be capped at 5
        files["g.cfg"] = "[probe]\npin: PA1\n";

        auto result = editor.resolve_includes(files, "a.cfg");
        // Probe in g.cfg should NOT be found (too deep)
        REQUIRE(result.count("probe") == 0);
    }

    SECTION("Handles missing included file gracefully") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include nonexistent.cfg]\n[printer]\nkinematics: corexy\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("printer") == 1); // Still parses what it can
    }

    SECTION("Resolves relative paths from including file") {
        std::map<std::string, std::string> files;
        files["printer.cfg"] = "[include hardware/sensors.cfg]\n";
        files["hardware/sensors.cfg"] = "[include probe.cfg]\n"; // relative to hardware/
        files["hardware/probe.cfg"] = "[probe]\npin: PA1\n";

        auto result = editor.resolve_includes(files, "printer.cfg");
        REQUIRE(result.count("probe") == 1);
        REQUIRE(result["probe"].file_path == "hardware/probe.cfg");
    }
}
```

**Step 2: Run tests — FAIL**

**Step 3: Implement resolve_includes()**

The method signature for unit testing takes a `std::map<std::string, std::string>` (filename → content) and a starting filename. For production use, a separate method will use Moonraker's file API to populate this map.

```cpp
/// Resolve all includes and build a section → file mapping
/// @param files Map of filename → content (for unit testing)
/// @param root_file Starting file
/// @param max_depth Maximum include recursion depth (default 5)
/// @return Map of section_name → SectionLocation
std::map<std::string, SectionLocation> resolve_includes(
    const std::map<std::string, std::string>& files,
    const std::string& root_file,
    int max_depth = 5) const;
```

Implementation:
1. Parse root file structure
2. For each `[include ...]` directive, resolve the path relative to the current file's directory
3. For glob patterns, match against available filenames in the map
4. Recursively parse included files (tracking visited set for cycle detection)
5. Build section → SectionLocation map
6. If a section appears in multiple files, keep the last one (matching Klipper's behavior)

**Step 4: Run tests — all pass**

**Step 5: Commit**

```bash
git add include/klipper_config_editor.h src/system/klipper_config_editor.cpp tests/unit/test_klipper_config_editor.cpp
git commit -m "feat(config): add include resolution with cycle detection and depth cap"
```

---

### Task 2.4: Moonraker integration for config editing

**Files:**
- Modify: `include/klipper_config_editor.h`
- Modify: `src/system/klipper_config_editor.cpp`
- Modify: `include/moonraker_api.h` (if new methods needed)

This task wires the editor to Moonraker's file API for real file operations.

**Step 1: Add async methods that use Moonraker file API**

```cpp
/// Load all config files from printer via Moonraker and resolve includes
/// @param api Moonraker API instance
/// @param on_complete Callback with section map, or error string
void load_config_files(
    MoonrakerAPI& api,
    std::function<void(std::map<std::string, SectionLocation>)> on_complete,
    std::function<void(const std::string& error)> on_error);

/// Edit a value and write back to the correct file with backup
/// @param api Moonraker API instance
/// @param section Config section name (e.g., "probe")
/// @param key Key to edit (e.g., "samples")
/// @param new_value New value string
/// @param on_success Called after successful write
/// @param on_error Called on failure (backup auto-restored)
void edit_value(
    MoonrakerAPI& api,
    const std::string& section,
    const std::string& key,
    const std::string& new_value,
    std::function<void()> on_success,
    std::function<void(const std::string& error)> on_error);
```

**Step 2: Implement load_config_files()**

1. Call `api.list_files("config", "", true, ...)` to get all config files
2. Download `printer.cfg` via `api.download_file("config", "printer.cfg", ...)`
3. Parse structure, find includes
4. Download each included file
5. Call `resolve_includes()` with the collected file map

**Step 3: Implement edit_value()**

1. Find which file contains the section via the resolved section map
2. Create backup: `api.copy_file("config/file.cfg", "config/file.cfg.helix_backup", ...)`
3. Download current file content
4. Call `set_value()` for the targeted edit
5. Validate structure of the result (re-parse to check it's still valid)
6. Upload modified content: `api.upload_file("config", "file.cfg", modified_content, ...)`

**Step 4: Commit**

```bash
git add include/klipper_config_editor.h src/system/klipper_config_editor.cpp
git commit -m "feat(config): wire Klipper config editor to Moonraker file API"
```

---

### Task 2.5: Post-edit health check and auto-revert

**Files:**
- Modify: `src/system/klipper_config_editor.cpp`

**Step 1: Implement restart-and-monitor flow**

After a config edit is written:

1. Send `FIRMWARE_RESTART` via `api.send_gcode("FIRMWARE_RESTART", ...)`
2. Start a 15-second timer
3. Watch for Klipper reconnection (MoonrakerAPI connection state)
4. **Success path**: Klipper reconnects within timeout → delete `.helix_backup` files → call `on_success`
5. **Failure path**: Timeout or Klipper reports config error → restore all `.helix_backup` files → send another `FIRMWARE_RESTART` → call `on_error("Config change reverted: Klipper failed to start")`

**Step 2: Add backup cleanup method**

```cpp
/// Delete all .helix_backup files in config root
void cleanup_backups(MoonrakerAPI& api, std::function<void()> on_complete);

/// Restore all .helix_backup files to their originals
void restore_backups(MoonrakerAPI& api, std::function<void()> on_complete,
                     std::function<void(const std::string&)> on_error);
```

**Step 3: Commit**

```bash
git add src/system/klipper_config_editor.cpp include/klipper_config_editor.h
git commit -m "feat(config): add post-edit health check with automatic backup restore"
```

---

## Phase 3: Probe Overlay

### Task 3.1: Create probe overlay skeleton

**Files:**
- Create: `include/ui_probe_overlay.h`
- Create: `src/ui/overlays/ui_probe_overlay.cpp`
- Create: `ui_xml/probe_overlay.xml`
- Modify: `src/application/application.cpp` (register overlay)
- Modify: `src/ui/panels/ui_panel_advanced.cpp` or calibration menu (add entry point)

**Step 1: Create the overlay class following OverlayBase pattern**

Follow the exact pattern from `BedMeshPanel` / `InputShaperPanel`:
- Singleton with `get_global_probe_overlay()`
- `StaticPanelRegistry` destruction registration
- `init_subjects()`, `register_callbacks()`, `create()`, `on_activate()`, `on_deactivate()`

**Step 2: Create minimal XML layout**

```xml
<!-- ui_xml/probe_overlay.xml -->
<lv_obj name="probe_overlay" style_bg_color="#surface" style_size="fill"
        style_pad_all="#space_md" style_flex_flow="column"
        style_flex_main_place="start" style_flex_cross_place="center"
        style_row_gap="#space_md">

    <!-- Header: Probe identity + status -->
    <lv_obj name="probe_header" style_size="fill_h_content"
            style_bg_color="#card_bg" style_radius="#radius_lg"
            style_pad_all="#space_md" style_flex_flow="column"
            style_row_gap="#space_sm">

        <text_heading name="probe_title" bind_text="probe_display_name"/>
        <text_body name="probe_subtitle" bind_text="probe_type_label"/>

        <lv_obj style_size="fill_h_content" style_flex_flow="row"
                style_column_gap="#space_lg">
            <text_body text="Status:"/>
            <probe_indicator/>
            <text_body text="Z Offset:"/>
            <text_body name="probe_offset" bind_text="probe_z_offset_display"/>
        </lv_obj>
    </lv_obj>

    <!-- Middle: Type-specific panel placeholder -->
    <lv_obj name="probe_type_panel" style_size="fill_h_content"/>

    <!-- Bottom: Universal actions -->
    <lv_obj name="probe_actions" style_size="fill_h_content"
            style_flex_flow="column" style_row_gap="#space_sm">

        <lv_obj name="btn_probe_accuracy" style_size="fill_h_content"
                style_bg_color="#card_bg" style_radius="#radius_md"
                style_pad_all="#space_md">
            <text_body text="Probe Accuracy Test"/>
            <event_cb trigger="clicked" callback="on_probe_accuracy"/>
        </lv_obj>

        <lv_obj name="btn_zoffset" style_size="fill_h_content"
                style_bg_color="#card_bg" style_radius="#radius_md"
                style_pad_all="#space_md">
            <text_body text="Z-Offset Calibration"/>
            <event_cb trigger="clicked" callback="on_zoffset_cal"/>
        </lv_obj>

        <lv_obj name="btn_bedmesh" style_size="fill_h_content"
                style_bg_color="#card_bg" style_radius="#radius_md"
                style_pad_all="#space_md">
            <text_body text="Bed Mesh"/>
            <event_cb trigger="clicked" callback="on_bed_mesh"/>
        </lv_obj>
    </lv_obj>
</lv_obj>
```

**Step 3: Register in application and calibration menu**

Add to the advanced/calibration panel's menu as a new row that opens the probe overlay.

**Step 4: Verify it builds and renders**

Run: `make -j && ./build/bin/helix-screen --test -vv`
Navigate to the probe overlay — should show the skeleton layout.

**Step 5: Commit**

```bash
git add include/ui_probe_overlay.h src/ui/overlays/ui_probe_overlay.cpp ui_xml/probe_overlay.xml src/application/application.cpp
git commit -m "feat(probe): add probe overlay skeleton with status header and action buttons"
```

---

### Task 3.2: Wire probe status subjects to overlay

**Files:**
- Modify: `src/ui/overlays/ui_probe_overlay.cpp`
- Modify: `src/sensors/probe_sensor_manager.cpp` (add new subjects if needed)

**Step 1: Add new subjects for display**

The overlay needs:
- `probe_display_name` (string subject) — e.g., "Cartographer" or "BLTouch"
- `probe_type_label` (string subject) — e.g., "Eddy Current Scanning Probe"
- `probe_z_offset_display` (string subject) — formatted offset like "-0.425mm"

These should be updated in `ProbeSensorManager` when a probe is discovered.

**Step 2: Bind existing subjects**

Wire the existing `probe_triggered`, `probe_last_z`, `probe_z_offset` subjects from `ProbeSensorManager` to the overlay XML via observer bindings.

**Step 3: Implement PROBE_ACCURACY action**

On button press:
1. Send `PROBE_ACCURACY` GCode
2. Parse the results from Klipper's response (standard deviation, range, max, min)
3. Display results inline below the button

**Step 4: Wire navigation shortcuts**

"Z-Offset Calibration" → `ui_nav_push_overlay()` to existing z-offset overlay
"Bed Mesh" → `ui_nav_push_overlay()` to existing bed mesh overlay

**Step 5: Commit**

```bash
git add src/ui/overlays/ui_probe_overlay.cpp src/sensors/probe_sensor_manager.cpp include/probe_sensor_manager.h
git commit -m "feat(probe): wire probe status subjects and universal actions to overlay"
```

---

### Task 3.3: BLTouch type-specific panel

**Files:**
- Create: `ui_xml/components/probe_bltouch_panel.xml`
- Modify: `src/ui/overlays/ui_probe_overlay.cpp`

**Step 1: Create BLTouch panel XML**

Buttons for Deploy, Stow, Reset, Self-Test. Output mode selector.

**Step 2: Register event callbacks**

- `on_bltouch_deploy` → `BLTOUCH_DEBUG COMMAND=pin_down`
- `on_bltouch_stow` → `BLTOUCH_DEBUG COMMAND=pin_up`
- `on_bltouch_reset` → `BLTOUCH_DEBUG COMMAND=reset`
- `on_bltouch_selftest` → `BLTOUCH_DEBUG COMMAND=self_test`
- `on_bltouch_output_mode` → `SET_BLTOUCH OUTPUT_MODE=5V` or `OD`

**Step 3: Load this panel when probe type is BLTOUCH**

In the overlay's `on_activate()`, check probe type and create the appropriate type panel component into the `probe_type_panel` container.

**Step 4: Test manually**

Run: `make -j && ./build/bin/helix-screen --test -vv`
(BLTouch won't be detected in test mode unless mocked, but verify the panel renders if forced)

**Step 5: Commit**

```bash
git add ui_xml/components/probe_bltouch_panel.xml src/ui/overlays/ui_probe_overlay.cpp
git commit -m "feat(probe): add BLTouch control panel (deploy/stow/self-test/output mode)"
```

---

### Task 3.4: Cartographer type-specific panel

**Files:**
- Create: `ui_xml/components/probe_cartographer_panel.xml`
- Modify: `src/ui/overlays/ui_probe_overlay.cpp`

**Step 1: Create Cartographer panel XML**

- Model selector dropdown (populated from Klipper's cartographer model list)
- Coil temperature readout
- Calibrate button
- Touch Calibrate button (for contact mode Cartographer)

**Step 2: Register event callbacks**

- `on_carto_model_select` → `CARTOGRAPHER_MODEL_SELECT NAME=<selected>`
- `on_carto_calibrate` → `CARTOGRAPHER_CALIBRATE`
- `on_carto_touch_cal` → `CARTOGRAPHER_TOUCH_CALIBRATE`

**Step 3: Query cartographer status for model list and temp**

Use `query_configfile()` or Moonraker object query to get available models and current temperature.

**Step 4: Commit**

```bash
git add ui_xml/components/probe_cartographer_panel.xml src/ui/overlays/ui_probe_overlay.cpp
git commit -m "feat(probe): add Cartographer control panel (model select, calibrate, temp)"
```

---

### Task 3.5: Beacon type-specific panel

**Files:**
- Create: `ui_xml/components/probe_beacon_panel.xml`
- Modify: `src/ui/overlays/ui_probe_overlay.cpp`

**Step 1: Create Beacon panel XML**

- Model selector dropdown
- Temperature compensation status indicator
- Sensor temperature readout
- Calibrate / Auto-Calibrate buttons

**Step 2: Register callbacks**

- `on_beacon_model_select` → `BEACON_MODEL_SELECT NAME=<selected>`
- `on_beacon_calibrate` → `BEACON_CALIBRATE`
- `on_beacon_auto_cal` → `BEACON_AUTO_CALIBRATE`

**Step 3: Commit**

```bash
git add ui_xml/components/probe_beacon_panel.xml src/ui/overlays/ui_probe_overlay.cpp
git commit -m "feat(probe): add Beacon control panel (model select, temp comp, calibrate)"
```

---

### Task 3.6: Generic/Klicky fallback panel

**Files:**
- Create: `ui_xml/components/probe_generic_panel.xml`
- Modify: `src/ui/overlays/ui_probe_overlay.cpp`

**Step 1: Create generic panel**

For Standard, Smart Effector, Tap, and unknown probes — just type identification. For Klicky, add Deploy (ATTACH_PROBE) and Dock (DOCK_PROBE) macro buttons.

**Step 2: Commit**

```bash
git add ui_xml/components/probe_generic_panel.xml src/ui/overlays/ui_probe_overlay.cpp
git commit -m "feat(probe): add generic and Klicky probe panels"
```

---

## Phase 4: Probe Config Editing via Overlay

### Task 4.1: Wire config editor to probe overlay

**Files:**
- Modify: `src/ui/overlays/ui_probe_overlay.cpp`

**Step 1: Add editable fields to probe overlay**

For probes that support config editing, add fields below the type-specific panel:

- x_offset, y_offset (number inputs)
- samples (number input, min 1)
- speed (number input, > 0)
- sample_retract_dist (number input, > 0)
- samples_tolerance (number input, >= 0)
- samples_tolerance_retries (number input, >= 0)

Display current values from `query_configfile()`. On change, use `KlipperConfigEditor::edit_value()` with the full safety flow.

**Step 2: Add confirmation dialog before config edit**

Use `ui_modal_show_confirmation()` pattern:
"This will modify your Klipper config and restart the firmware. A backup will be created automatically. Continue?"

**Step 3: Add status feedback**

Show progress: "Creating backup..." → "Writing config..." → "Restarting firmware..." → "Success" or "Failed, config reverted"

**Step 4: Commit**

```bash
git add src/ui/overlays/ui_probe_overlay.cpp
git commit -m "feat(probe): wire config editor for probe settings (offsets, samples, speed)"
```

---

## Phase 5: Wizard Detected Hardware Step

### Task 5.1: Create Detected Hardware wizard step

**Files:**
- Create: `include/ui_wizard_detected_hardware.h`
- Create: `src/ui/ui_wizard_detected_hardware.cpp`
- Create: `ui_xml/wizard_detected_hardware.xml`
- Modify: `src/ui/ui_wizard.cpp` (replace steps 7 + 10)

**Step 1: Create the wizard step class**

Follow the pattern of existing wizard steps (e.g., `ui_wizard_probe_sensor_select.cpp`):
- `should_skip()` — skip if no interesting hardware detected (no probe, no AMS, no LEDs, no accelerometer)
- `create()` — build the UI from XML
- Populate sections dynamically based on what `ProbeSensorManager`, `AmsState`, LED detection, and accelerometer detection find

**Step 2: Create XML layout**

Sections for each hardware category, each only visible if hardware detected (use `bind_flag_if_eq` on count subjects):

- Bed Probe section: probe type icon + name
- Filament System section: AMS/ERCF type + slot count
- LEDs section: detected LED chain names
- Accelerometer section: ADXL345/LIS2DW name

"Not seeing something? Check your Klipper config." help text at bottom.

**Step 3: Update wizard step array**

In `ui_wizard.cpp`:
- Replace step 7 (AMS) and step 10 (probe sensor) with a single "detected hardware" step
- Update step indices and skip flag logic
- Remove `wizard_ams_identify` and `wizard_probe_sensor_select` from the step array
- Add `wizard_detected_hardware` in their place

**Step 4: Update skip logic**

```cpp
// Skip detected hardware step if nothing interesting found
bool should_skip() const {
    bool has_probe = ProbeSensorManager::instance().has_sensors();
    bool has_ams = AmsState::instance().has_ams();
    bool has_leds = /* LED detection check */;
    bool has_accel = /* accelerometer detection check */;
    return !has_probe && !has_ams && !has_leds && !has_accel;
}
```

**Step 5: Register new XML component**

Add `lv_xml_component_register_from_file()` call in `main.cpp` for `wizard_detected_hardware`.

**Step 6: Test the wizard flow**

Run: `make -j && ./build/bin/helix-screen --test -vv`
Trigger the wizard, verify the detected hardware step appears with correct sections.

**Step 7: Commit**

```bash
git add include/ui_wizard_detected_hardware.h src/ui/ui_wizard_detected_hardware.cpp ui_xml/wizard_detected_hardware.xml src/ui/ui_wizard.cpp src/main.cpp
git commit -m "feat(wizard): replace AMS + probe steps with consolidated Detected Hardware step"
```

---

## Summary

| Phase | Tasks | Estimated Commits |
|-------|-------|-------------------|
| 1. Probe Detection | 1.1–1.4 | 4 |
| 2. Config Editor | 2.1–2.5 | 5 |
| 3. Probe Overlay | 3.1–3.6 | 6 |
| 4. Config Editing UI | 4.1 | 1 |
| 5. Wizard Step | 5.1 | 1 |
| **Total** | **17 tasks** | **~17 commits** |

**Dependencies:**
- Phase 1 (detection) is independent — can start immediately
- Phase 2 (config editor) is independent — can run in parallel with Phase 1
- Phase 3 (overlay) depends on Phase 1 (needs new probe types)
- Phase 4 (config editing UI) depends on Phase 2 + Phase 3
- Phase 5 (wizard) depends on Phase 1 (needs expanded detection)

**Parallelization opportunity:** Phase 1 and Phase 2 can run in parallel since they're independent.
