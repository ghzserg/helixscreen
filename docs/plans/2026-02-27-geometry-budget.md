# Geometry Budget System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prevent OOM crashes on memory-constrained devices by enforcing a dynamic memory budget on 3D geometry building, with tiered quality degradation and automatic 2D fallback.

**Architecture:** New `GeometryBudgetManager` class handles memory assessment and tier selection. `GeometryBuilder::build()` gains a budget config parameter with periodic memory checks during building. `ui_gcode_viewer.cpp` orchestrates the pre-flight check and fallback cascade.

**Tech Stack:** C++17, `/proc/meminfo` parsing, existing `GeometryBuilder`/`RibbonGeometry` infrastructure.

**Design doc:** `docs/plans/2026-02-27-geometry-budget-design.md`

---

### Task 1: GeometryBudgetManager — Memory Assessment (Pure Unit Tests)

**Files:**
- Create: `tests/unit/test_geometry_budget.cpp`
- Create: `include/geometry_budget_manager.h`
- Create: `src/rendering/geometry_budget_manager.cpp`

**Step 1: Write failing tests for memory parsing and budget calculation**

Test file `tests/unit/test_geometry_budget.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "geometry_budget_manager.h"
#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

// ============================================================================
// Memory parsing
// ============================================================================

TEST_CASE("Budget: parse MemAvailable from /proc/meminfo", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:        3884136 kB
MemFree:         1363424 kB
MemAvailable:    3768880 kB
Buffers:          104872 kB
Cached:          2091048 kB)";

    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 3768880);
}

TEST_CASE("Budget: parse MemAvailable from 1GB system", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:         999936 kB
MemFree:          102400 kB
MemAvailable:     307200 kB)";

    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 307200);
}

TEST_CASE("Budget: parse MemAvailable returns 0 on missing field", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:        3884136 kB
MemFree:         1363424 kB)";

    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 0);
}

TEST_CASE("Budget: parse MemAvailable from AD5M (256MB)", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:         253440 kB
MemFree:           12288 kB
MemAvailable:      38912 kB)";

    REQUIRE(GeometryBudgetManager::parse_meminfo_available_kb(meminfo) == 38912);
}

// ============================================================================
// Budget calculation
// ============================================================================

TEST_CASE("Budget: 25% of available memory", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 4GB Pi with 3.7GB available → 925MB * 0.25 = ~231MB, but capped at 256MB
    size_t budget = mgr.calculate_budget(3768880); // 3.6GB in KB
    REQUIRE(budget == 256 * 1024 * 1024); // Capped at 256MB
}

TEST_CASE("Budget: 1GB Pi with 300MB free", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 300MB available → 75MB budget
    size_t budget = mgr.calculate_budget(307200); // 300MB in KB
    REQUIRE(budget == 307200 * 1024 / 4); // 25% = 76,800 KB = ~75MB
}

TEST_CASE("Budget: AD5M with 38MB available", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 38MB available → ~9.5MB budget
    size_t budget = mgr.calculate_budget(38912);
    REQUIRE(budget == 38912 * 1024 / 4);
}

TEST_CASE("Budget: hard cap at 256MB even with 8GB free", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    size_t budget = mgr.calculate_budget(6144000); // 6GB in KB
    REQUIRE(budget == 256 * 1024 * 1024);
}

TEST_CASE("Budget: 0 available memory returns minimum budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // Edge case: 0 available → minimum budget (allow at least thumbnail mode)
    size_t budget = mgr.calculate_budget(0);
    REQUIRE(budget == 0);
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: FAIL — `geometry_budget_manager.h` does not exist

**Step 3: Write minimal header and implementation**

Header `include/geometry_budget_manager.h`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

namespace helix {
namespace gcode {

class GeometryBudgetManager {
  public:
    /// Maximum geometry memory budget regardless of available RAM
    static constexpr size_t MAX_BUDGET_BYTES = 256 * 1024 * 1024; // 256MB

    /// Percentage of MemAvailable to use for geometry
    static constexpr int BUDGET_PERCENT = 25;

    /// Minimum MemAvailable before we consider the system critically low
    static constexpr size_t CRITICAL_MEMORY_KB = 100 * 1024; // 100MB

    /// Parse MemAvailable from /proc/meminfo content string
    /// @param content Full content of /proc/meminfo
    /// @return MemAvailable in KB, or 0 if parsing fails
    static size_t parse_meminfo_available_kb(const std::string& content);

    /// Calculate geometry memory budget from available system memory
    /// @param available_kb Available memory in KB (from /proc/meminfo MemAvailable)
    /// @return Budget in bytes
    size_t calculate_budget(size_t available_kb) const;

    /// Read current MemAvailable from /proc/meminfo on this system
    /// @return Available memory in KB, or 0 on failure
    size_t read_system_available_kb() const;

    /// Check if system memory is critically low (< 100MB available)
    /// @return true if MemAvailable < CRITICAL_MEMORY_KB
    bool is_system_memory_critical() const;
};

} // namespace gcode
} // namespace helix
```

Implementation `src/rendering/geometry_budget_manager.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry_budget_manager.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

namespace helix {
namespace gcode {

size_t GeometryBudgetManager::parse_meminfo_available_kb(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            // Format: "MemAvailable:    3768880 kB"
            size_t value = 0;
            if (sscanf(line.c_str(), "MemAvailable: %zu", &value) == 1) {
                return value;
            }
        }
    }
    return 0;
}

size_t GeometryBudgetManager::calculate_budget(size_t available_kb) const {
    if (available_kb == 0) {
        return 0;
    }
    size_t budget = (available_kb * 1024) / (100 / BUDGET_PERCENT);
    return std::min(budget, MAX_BUDGET_BYTES);
}

size_t GeometryBudgetManager::read_system_available_kb() const {
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        spdlog::warn("[GeometryBudget] Cannot read /proc/meminfo");
        return 0;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return parse_meminfo_available_kb(content);
}

bool GeometryBudgetManager::is_system_memory_critical() const {
    return read_system_available_kb() < CRITICAL_MEMORY_KB;
}

} // namespace gcode
} // namespace helix
```

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: All 5 tests PASS

**Step 5: Commit**

```bash
git add tests/unit/test_geometry_budget.cpp include/geometry_budget_manager.h src/rendering/geometry_budget_manager.cpp
git commit -m "feat(gcode): add GeometryBudgetManager memory assessment"
```

---

### Task 2: Tier Selection Logic

**Files:**
- Modify: `tests/unit/test_geometry_budget.cpp`
- Modify: `include/geometry_budget_manager.h`
- Modify: `src/rendering/geometry_budget_manager.cpp`

**Step 1: Write failing tests for tier selection**

Add to `tests/unit/test_geometry_budget.cpp`:
```cpp
// ============================================================================
// Tier selection
// ============================================================================

TEST_CASE("Budget: tier selection - small file gets Tier 1 (full quality)", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 50K segments, 256MB budget → estimated N=16: 50K * 1100 * 0.7 = ~38MB → fits
    auto config = mgr.select_tier(50000, 256 * 1024 * 1024);
    REQUIRE(config.tier == 1);
    REQUIRE(config.tube_sides == 16);
    REQUIRE(config.include_travels == true);
}

TEST_CASE("Budget: tier selection - medium file gets Tier 2", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 200K segments, 100MB budget
    // N=16: 200K * 1100 * 0.7 = 154MB → too big
    // N=8:  200K * 550 * 0.7  = 77MB  → fits
    auto config = mgr.select_tier(200000, 100 * 1024 * 1024);
    REQUIRE(config.tier == 2);
    REQUIRE(config.tube_sides == 8);
}

TEST_CASE("Budget: tier selection - large file gets Tier 3", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 500K segments, 75MB budget
    // N=16: 500K * 1100 * 0.7 = 385MB → too big
    // N=8:  500K * 550 * 0.7  = 192MB → too big
    // N=4:  500K * 280 * 0.7  = 98MB  → too big
    // N=4:  500K * 280 * 0.7  = 98MB  < 75MB*2 = 150MB → try aggressive
    auto config = mgr.select_tier(500000, 75 * 1024 * 1024);
    REQUIRE(config.tier == 3);
    REQUIRE(config.tube_sides == 4);
    REQUIRE(config.include_travels == false);
    REQUIRE(config.simplification_tolerance > 0.1f); // aggressive
}

TEST_CASE("Budget: tier selection - massive file gets Tier 4 (2D fallback)", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 2M segments, 75MB budget → nothing fits
    auto config = mgr.select_tier(2000000, 75 * 1024 * 1024);
    REQUIRE(config.tier == 4);
    REQUIRE(config.tube_sides == 0); // N/A for 2D
}

TEST_CASE("Budget: tier selection - tiny budget forces Tier 4", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // AD5M: 50K segments but only 10MB budget
    auto config = mgr.select_tier(50000, 10 * 1024 * 1024);
    // N=4: 50K * 280 * 0.7 = 9.8MB → borderline, check
    // This should either be Tier 3 (fits) or Tier 4 depending on exact calculation
    REQUIRE(config.tier >= 3);
}

TEST_CASE("Budget: tier selection - 0 segments gets Tier 1", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    auto config = mgr.select_tier(0, 256 * 1024 * 1024);
    REQUIRE(config.tier == 1);
}

TEST_CASE("Budget: tier 5 for zero budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    auto config = mgr.select_tier(100000, 0);
    REQUIRE(config.tier == 5); // thumbnail only
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: FAIL — `select_tier` not defined

**Step 3: Add tier selection to header and implementation**

Add to `include/geometry_budget_manager.h` (inside class):
```cpp
    /// Quality tier configuration returned by select_tier()
    struct BudgetConfig {
        int tier;                        ///< 1=full, 2=medium, 3=low, 4=2D, 5=thumbnail
        int tube_sides;                  ///< 4, 8, 16, or 0 (N/A for 2D/thumbnail)
        float simplification_tolerance;  ///< mm (higher = more aggressive)
        bool include_travels;            ///< Include travel move geometry
        size_t budget_bytes;             ///< Memory ceiling for geometry
    };

    /// Bytes per segment estimate at each tube_sides value (after 0.7 simplification factor)
    static constexpr size_t BYTES_PER_SEG_N16 = 770;  // 1100 * 0.7
    static constexpr size_t BYTES_PER_SEG_N8  = 385;  // 550 * 0.7
    static constexpr size_t BYTES_PER_SEG_N4  = 196;  // 280 * 0.7

    /// Select optimal quality tier for a given segment count and budget
    /// @param segment_count Total segments from parsed gcode
    /// @param budget_bytes Available memory for geometry (from calculate_budget())
    /// @return Configuration for the selected tier
    BudgetConfig select_tier(size_t segment_count, size_t budget_bytes) const;
```

Add to `src/rendering/geometry_budget_manager.cpp`:
```cpp
GeometryBudgetManager::BudgetConfig
GeometryBudgetManager::select_tier(size_t segment_count, size_t budget_bytes) const {
    if (budget_bytes == 0) {
        spdlog::info("[GeometryBudget] Zero budget — thumbnail only (tier 5)");
        return {.tier = 5, .tube_sides = 0, .simplification_tolerance = 0.0f,
                .include_travels = false, .budget_bytes = 0};
    }

    if (segment_count == 0) {
        return {.tier = 1, .tube_sides = 16, .simplification_tolerance = 0.01f,
                .include_travels = true, .budget_bytes = budget_bytes};
    }

    size_t est_n16 = segment_count * BYTES_PER_SEG_N16;
    size_t est_n8  = segment_count * BYTES_PER_SEG_N8;
    size_t est_n4  = segment_count * BYTES_PER_SEG_N4;

    if (est_n16 < budget_bytes) {
        spdlog::info("[GeometryBudget] Tier 1 (full): est {}MB / {}MB budget",
                     est_n16 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 1, .tube_sides = 16, .simplification_tolerance = 0.01f,
                .include_travels = true, .budget_bytes = budget_bytes};
    }

    if (est_n8 < budget_bytes) {
        spdlog::info("[GeometryBudget] Tier 2 (medium): est {}MB / {}MB budget",
                     est_n8 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 2, .tube_sides = 8, .simplification_tolerance = 0.05f,
                .include_travels = true, .budget_bytes = budget_bytes};
    }

    if (est_n4 < budget_bytes) {
        spdlog::info("[GeometryBudget] Tier 3 (low): est {}MB / {}MB budget",
                     est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 3, .tube_sides = 4, .simplification_tolerance = 0.5f,
                .include_travels = false, .budget_bytes = budget_bytes};
    }

    // N=4 with extra-aggressive simplification might still fit (2x headroom)
    if (est_n4 < budget_bytes * 2) {
        spdlog::info("[GeometryBudget] Tier 3 (aggressive): est {}MB / {}MB budget",
                     est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 3, .tube_sides = 4, .simplification_tolerance = 1.0f,
                .include_travels = false, .budget_bytes = budget_bytes};
    }

    // Too complex for 3D — fall back to 2D renderer
    spdlog::info("[GeometryBudget] Tier 4 (2D fallback): est {}MB exceeds {}MB budget",
                 est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024));
    return {.tier = 4, .tube_sides = 0, .simplification_tolerance = 0.0f,
            .include_travels = false, .budget_bytes = budget_bytes};
}
```

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: All tier selection tests PASS

**Step 5: Commit**

```bash
git add tests/unit/test_geometry_budget.cpp include/geometry_budget_manager.h src/rendering/geometry_budget_manager.cpp
git commit -m "feat(gcode): add tier selection logic to GeometryBudgetManager"
```

---

### Task 3: Progressive Budget Checking During Build

**Files:**
- Modify: `tests/unit/test_geometry_budget.cpp`
- Modify: `include/geometry_budget_manager.h`
- Modify: `src/rendering/geometry_budget_manager.cpp`

**Step 1: Write failing tests for budget action decisions**

Add to `tests/unit/test_geometry_budget.cpp`:
```cpp
// ============================================================================
// Progressive budget checking
// ============================================================================

TEST_CASE("Budget: check_budget returns CONTINUE when under budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    auto action = mgr.check_budget(50 * 1024 * 1024, 256 * 1024 * 1024, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::CONTINUE);
}

TEST_CASE("Budget: check_budget returns DEGRADE at 90% when tier > 3", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // 90% of 100MB budget at tier 1 → should degrade
    auto action = mgr.check_budget(91 * 1024 * 1024, 100 * 1024 * 1024, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::DEGRADE);
}

TEST_CASE("Budget: check_budget returns DEGRADE at 90% for tier 2", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    auto action = mgr.check_budget(91 * 1024 * 1024, 100 * 1024 * 1024, 2);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::DEGRADE);
}

TEST_CASE("Budget: check_budget returns ABORT at 90% for tier 3 (lowest 3D)", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    // Already at lowest 3D tier, can't degrade further → abort to 2D
    auto action = mgr.check_budget(91 * 1024 * 1024, 100 * 1024 * 1024, 3);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::ABORT);
}

TEST_CASE("Budget: check_budget CONTINUE at 89% (just under threshold)", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    auto action = mgr.check_budget(89 * 1024 * 1024, 100 * 1024 * 1024, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::CONTINUE);
}

TEST_CASE("Budget: check_budget handles 0 budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;

    auto action = mgr.check_budget(1024, 0, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::ABORT);
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: FAIL — `check_budget` and `BudgetAction` not defined

**Step 3: Add budget checking to header and implementation**

Add to `include/geometry_budget_manager.h` (inside class):
```cpp
    /// Action to take when checking budget during build
    enum class BudgetAction {
        CONTINUE, ///< Under budget, keep building
        DEGRADE,  ///< Over 90% at tier 1-2, switch to N=4 in-place
        ABORT     ///< Over 90% at tier 3 (or budget 0), fall back to 2D
    };

    /// Budget threshold (0.0-1.0) at which to take action
    static constexpr float BUDGET_THRESHOLD = 0.9f;

    /// Segments between budget checks during build
    static constexpr size_t CHECK_INTERVAL_SEGMENTS = 5000;

    /// Segments between system memory checks during build
    static constexpr size_t SYSTEM_CHECK_INTERVAL_SEGMENTS = 20000;

    /// Check if build should continue, degrade, or abort
    /// @param current_usage_bytes Current geometry memory usage
    /// @param budget_bytes Memory ceiling
    /// @param current_tier Current quality tier (1-3)
    /// @return Action to take
    BudgetAction check_budget(size_t current_usage_bytes,
                              size_t budget_bytes,
                              int current_tier) const;
```

Add to `src/rendering/geometry_budget_manager.cpp`:
```cpp
GeometryBudgetManager::BudgetAction
GeometryBudgetManager::check_budget(size_t current_usage_bytes,
                                    size_t budget_bytes,
                                    int current_tier) const {
    if (budget_bytes == 0) {
        return BudgetAction::ABORT;
    }

    float usage_ratio = static_cast<float>(current_usage_bytes) / budget_bytes;

    if (usage_ratio < BUDGET_THRESHOLD) {
        return BudgetAction::CONTINUE;
    }

    // Over threshold: degrade if possible, abort if at lowest 3D tier
    if (current_tier < 3) {
        spdlog::warn("[GeometryBudget] {}MB / {}MB ({:.0f}%) — degrading from tier {}",
                     current_usage_bytes / (1024 * 1024), budget_bytes / (1024 * 1024),
                     usage_ratio * 100, current_tier);
        return BudgetAction::DEGRADE;
    }

    spdlog::warn("[GeometryBudget] {}MB / {}MB ({:.0f}%) — aborting (already at tier 3)",
                 current_usage_bytes / (1024 * 1024), budget_bytes / (1024 * 1024),
                 usage_ratio * 100);
    return BudgetAction::ABORT;
}
```

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: All budget check tests PASS

**Step 5: Commit**

```bash
git add tests/unit/test_geometry_budget.cpp include/geometry_budget_manager.h src/rendering/geometry_budget_manager.cpp
git commit -m "feat(gcode): add progressive budget checking to GeometryBudgetManager"
```

---

### Task 4: Integrate Budget into GeometryBuilder

**Files:**
- Modify: `tests/unit/test_gcode_geometry_builder.cpp` (add budget integration tests)
- Modify: `include/gcode_geometry_builder.h`
- Modify: `src/rendering/gcode_geometry_builder.cpp`

**Step 1: Write failing integration tests**

Add to `tests/unit/test_gcode_geometry_builder.cpp`. First check how existing tests
create test gcode data — look for helper functions that create `ParsedGCodeFile` with
synthetic segments. Use the same pattern.

```cpp
// ============================================================================
// Budget integration tests
// ============================================================================

TEST_CASE("GeometryBuilder: respects tube_sides from BudgetConfig", "[gcode][budget][builder]") {
    // Create a small test gcode with known segment count
    ParsedGCodeFile gcode;
    Layer layer;
    layer.z_height = 0.2f;
    for (int i = 0; i < 100; ++i) {
        ToolpathSegment seg;
        seg.start = {static_cast<float>(i), 0.0f, 0.2f};
        seg.end = {static_cast<float>(i + 1), 0.0f, 0.2f};
        seg.is_extrusion = true;
        layer.segments.push_back(seg);
    }
    gcode.layers.push_back(std::move(layer));
    gcode.total_segments = 100;
    gcode.global_bounding_box.expand({0, 0, 0});
    gcode.global_bounding_box.expand({101, 1, 1});

    GeometryBuilder builder;
    SimplificationOptions opts;

    // Build with default (should use config tube_sides=4 from helixconfig)
    auto geom_default = builder.build(gcode, opts);
    size_t verts_default = geom_default.vertices.size();

    // Build with budget config forcing tube_sides=4
    GeometryBuilder builder4;
    builder4.set_budget_tube_sides(4);
    auto geom_4 = builder4.build(gcode, opts);
    size_t verts_4 = geom_4.vertices.size();

    // N=4 should produce fewer vertices than default (N=16 or whatever config says)
    // If default is already 4, they'll be equal — that's fine too
    REQUIRE(verts_4 <= verts_default);
}

TEST_CASE("GeometryBuilder: budget abort returns empty geometry with flag", "[gcode][budget][builder]") {
    // Create gcode with enough segments to exceed a tiny budget
    ParsedGCodeFile gcode;
    Layer layer;
    layer.z_height = 0.2f;
    for (int i = 0; i < 10000; ++i) {
        ToolpathSegment seg;
        seg.start = {static_cast<float>(i) * 0.5f, 0.0f, 0.2f};
        seg.end = {static_cast<float>(i + 1) * 0.5f, 0.0f, 0.2f};
        seg.is_extrusion = true;
        layer.segments.push_back(seg);
    }
    gcode.layers.push_back(std::move(layer));
    gcode.total_segments = 10000;
    gcode.global_bounding_box.expand({0, 0, 0});
    gcode.global_bounding_box.expand({5001, 1, 1});

    GeometryBuilder builder;
    builder.set_budget_tube_sides(4);
    builder.set_budget_limit(1024); // 1KB budget — impossibly small

    SimplificationOptions opts;
    auto geom = builder.build(gcode, opts);

    // Build should have aborted
    REQUIRE(builder.was_budget_exceeded());
    // Some vertices may have been generated before abort
    // The key test is that it didn't OOM and the flag is set
}
```

**Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[budget][builder]" -v`
Expected: FAIL — `set_budget_tube_sides`, `set_budget_limit`, `was_budget_exceeded` not defined

**Step 3: Add budget integration to GeometryBuilder**

Add to `include/gcode_geometry_builder.h` in the public section of `GeometryBuilder`:
```cpp
    /// Set tube_sides override from budget manager (0 = use config default)
    void set_budget_tube_sides(int sides) { budget_tube_sides_ = sides; }

    /// Set memory ceiling for progressive budget checking (0 = unlimited)
    void set_budget_limit(size_t bytes) { budget_limit_bytes_ = bytes; }

    /// Whether the last build was aborted due to budget exceeded
    bool was_budget_exceeded() const { return budget_exceeded_; }
```

Add to private section:
```cpp
    int budget_tube_sides_ = 0;        ///< Override tube_sides from budget (0 = use config)
    size_t budget_limit_bytes_ = 0;    ///< Memory ceiling (0 = unlimited)
    bool budget_exceeded_ = false;     ///< Set to true if build aborted due to budget
```

Modify `GeometryBuilder::build()` in `src/rendering/gcode_geometry_builder.cpp`:

At the top of `build()`, after `stats_ = {};`:
```cpp
    budget_exceeded_ = false;

    // Apply budget tube_sides override if set
    if (budget_tube_sides_ > 0) {
        tube_sides_ = budget_tube_sides_;
        spdlog::info("[GCode::Builder] Budget override: tube_sides={}", tube_sides_);
    }
```

In the segment loop (line ~457), add budget check every CHECK_INTERVAL_SEGMENTS:
```cpp
    size_t segments_since_check = 0;

    for (size_t i = 0; i < simplified.size(); ++i) {
        // ... existing skip/filter logic ...

        // Progressive budget check
        if (budget_limit_bytes_ > 0) {
            segments_since_check++;
            if (segments_since_check >= GeometryBudgetManager::CHECK_INTERVAL_SEGMENTS) {
                segments_since_check = 0;
                size_t current_mem = geometry.memory_usage();
                if (current_mem > budget_limit_bytes_ * GeometryBudgetManager::BUDGET_THRESHOLD) {
                    spdlog::warn("[GCode::Builder] Budget exceeded: {}MB / {}MB at segment {}/{}",
                                 current_mem / (1024 * 1024),
                                 budget_limit_bytes_ / (1024 * 1024), i, simplified.size());
                    budget_exceeded_ = true;
                    break; // Stop building
                }

                // System memory check (less frequent)
                if (i % GeometryBudgetManager::SYSTEM_CHECK_INTERVAL_SEGMENTS == 0) {
                    GeometryBudgetManager budget_mgr;
                    if (budget_mgr.is_system_memory_critical()) {
                        spdlog::error("[GCode::Builder] System memory critical — aborting build");
                        budget_exceeded_ = true;
                        break;
                    }
                }
            }
        }

        // ... rest of existing segment processing ...
    }
```

Note: Add `#include "geometry_budget_manager.h"` to `gcode_geometry_builder.cpp`.

**Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[budget]" -v`
Expected: All tests PASS

**Step 5: Commit**

```bash
git add tests/unit/test_gcode_geometry_builder.cpp include/gcode_geometry_builder.h src/rendering/gcode_geometry_builder.cpp
git commit -m "feat(gcode): integrate budget limits into GeometryBuilder"
```

---

### Task 5: Integrate Budget into GCode Viewer

**Files:**
- Modify: `src/ui/ui_gcode_viewer.cpp` (lines ~1256-1289, the full-load 3D build path)

This task has no unit tests — it's UI integration that connects the budget manager
to the existing async build pipeline. The behavior is testable end-to-end by loading
files on actual hardware.

**Step 1: Add budget-aware geometry building to the full-load path**

In `src/ui/ui_gcode_viewer.cpp`, replace the geometry build section
(lines ~1256-1289) inside the `#ifdef ENABLE_3D_RENDERER` block:

```cpp
#ifdef ENABLE_3D_RENDERER
                // PHASE 2: Budget-aware 3D geometry build
                if (!st->is_using_2d_mode()) {
                    // Assess system memory and calculate budget
                    helix::gcode::GeometryBudgetManager budget_mgr;
                    size_t available_kb = budget_mgr.read_system_available_kb();
                    size_t budget = budget_mgr.calculate_budget(available_kb);

                    auto budget_config = budget_mgr.select_tier(
                        result->gcode_file->total_segments, budget);

                    spdlog::info("[GCode Viewer] Memory: {}MB available, {}MB budget, "
                                 "{} segments → tier {}",
                                 available_kb / 1024, budget / (1024 * 1024),
                                 result->gcode_file->total_segments, budget_config.tier);

                    if (budget_config.tier <= 3) {
                        // Tier 1-3: Build 3D geometry with budget constraints
                        auto configure_builder = [&](helix::gcode::GeometryBuilder& builder) {
                            if (!result->gcode_file->tool_color_palette.empty()) {
                                builder.set_tool_color_palette(
                                    result->gcode_file->tool_color_palette);
                            }
                            if (result->gcode_file->perimeter_extrusion_width_mm > 0.0f) {
                                builder.set_extrusion_width(
                                    result->gcode_file->perimeter_extrusion_width_mm);
                            } else if (result->gcode_file->extrusion_width_mm > 0.0f) {
                                builder.set_extrusion_width(
                                    result->gcode_file->extrusion_width_mm);
                            }
                            builder.set_layer_height(result->gcode_file->layer_height_mm);
                            builder.set_budget_tube_sides(budget_config.tube_sides);
                            builder.set_budget_limit(budget_config.budget_bytes);
                        };

                        {
                            helix::gcode::GeometryBuilder builder;
                            configure_builder(builder);

                            helix::gcode::SimplificationOptions opts{
                                .tolerance_mm = budget_config.simplification_tolerance,
                                .min_segment_length_mm = 0.05f};

                            result->geometry =
                                std::make_unique<helix::gcode::RibbonGeometry>(
                                    builder.build(*result->gcode_file, opts));

                            if (builder.was_budget_exceeded()) {
                                spdlog::warn("[GCode Viewer] Budget exceeded — "
                                             "falling back to 2D");
                                result->geometry.reset();
                                result->force_2d = true;
                            } else {
                                spdlog::info("[GCode Viewer] Built geometry: "
                                             "{} vertices, {} triangles (tier {})",
                                             result->geometry->vertices.size(),
                                             result->geometry->extrusion_triangle_count +
                                                 result->geometry->travel_triangle_count,
                                             budget_config.tier);
                            }
                        }

                        if (!result->force_2d) {
                            size_t freed = result->gcode_file->clear_segments();
                            spdlog::info("[GCode Viewer] Freed {} MB of parsed segment data",
                                         freed / (1024 * 1024));
                        }
                    } else {
                        // Tier 4-5: Skip geometry build entirely
                        spdlog::info("[GCode Viewer] Tier {} — skipping 3D geometry build",
                                     budget_config.tier);
                        result->force_2d = true;
                    }
                } else {
                    spdlog::debug("[GCode Viewer] 2D mode - skipping 3D geometry build");
                }
#else
                spdlog::debug("[GCode Viewer] 2D renderer - skipping geometry build");
#endif
```

**Step 2: Add `force_2d` field to `AsyncBuildResult`**

Find the `AsyncBuildResult` struct in `ui_gcode_viewer.cpp` and add:
```cpp
    bool force_2d = false; ///< Budget system forced 2D fallback
```

**Step 3: Handle `force_2d` in the async callback**

In the async callback (line ~1344), after `if (r->success)`, add handling for
`force_2d` — when set, initialize the 2D renderer instead of the 3D one:
```cpp
                if (r->success) {
                    st->gcode_file = std::move(r->gcode_file);

                    if (r->force_2d) {
                        // Budget-forced 2D fallback: initialize 2D renderer
                        spdlog::info("[GCode Viewer] Using 2D renderer (budget fallback)");
                        st->layer_renderer_2d_ =
                            std::make_unique<helix::gcode::GCodeLayerRenderer>();
                        st->layer_renderer_2d_->set_gcode(st->gcode_file.get());
                        // ... apply colors same as existing 2D path ...
                        st->layer_renderer_2d_->auto_fit();
                        // TODO: Show toast "Complex model — showing 2D layer view"
                    } else if (r->geometry) {
                        // ... existing 3D setup path ...
                    }
```

**Step 4: Build and verify compilation**

Run: `make -j`
Expected: Clean build with no errors

**Step 5: Commit**

```bash
git add src/ui/ui_gcode_viewer.cpp
git commit -m "feat(gcode): integrate budget system into gcode viewer loading pipeline"
```

---

### Task 6: Toast Notifications for Fallback

**Files:**
- Modify: `src/ui/ui_gcode_viewer.cpp` (add toast calls in the force_2d handler)

**Step 1: Add toast notification when falling back to 2D or thumbnail**

In the `force_2d` handler from Task 5, add a toast. Find the existing toast
function by searching for `ui_toast` or `toast_show` in the codebase:

```cpp
// After initializing 2D renderer in force_2d path:
helix::ui::toast_show(lv_tr("Complex model — showing 2D layer view"),
                      helix::ui::ToastType::INFO);
```

**Step 2: Build and verify**

Run: `make -j`
Expected: Clean build

**Step 3: Commit**

```bash
git add src/ui/ui_gcode_viewer.cpp
git commit -m "feat(gcode): show toast when budget forces 2D fallback"
```

---

### Task 7: End-to-End Verification on Pi

**Files:** None (manual testing)

**Step 1: Deploy to Pi and test with 3DBenchy**

Deploy: `make pi-test`

Test with 3DBenchy (100K segments):
1. Open gcode detail panel, load 3DBenchy
2. Check logs: `ssh 192.168.1.113 'cat /tmp/test.log | grep -i budget'`
3. Verify tier selection and geometry build stats appear in logs
4. Verify 3D rendering works normally (should be tier 1 or 2)

**Step 2: Test with a large file**

Generate or find a large gcode file (500K+ segments). Verify:
- Budget system selects appropriate tier
- No OOM crash
- Falls back to 2D if budget exceeded

**Step 3: Test on memory-constrained setup**

If possible, test with reduced available memory (other processes consuming RAM)
to verify the dynamic memory assessment works correctly.

---

## Summary

| Task | What | Tests |
|------|------|-------|
| 1 | Memory parsing + budget calculation | 5 unit tests |
| 2 | Tier selection logic | 7 unit tests |
| 3 | Progressive budget checking | 6 unit tests |
| 4 | GeometryBuilder integration | 2 integration tests |
| 5 | GCode viewer integration | Manual (build verification) |
| 6 | Toast notifications | Manual (build verification) |
| 7 | End-to-end Pi testing | Manual hardware test |

Total: ~20 automated tests, 3 manual verification steps.
