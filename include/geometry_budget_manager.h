// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

namespace helix {
namespace gcode {

class GeometryBudgetManager {
  public:
    static constexpr size_t MAX_BUDGET_BYTES = 256 * 1024 * 1024;
    static constexpr int BUDGET_PERCENT = 25;
    static constexpr size_t CRITICAL_MEMORY_KB = 100 * 1024;

    struct BudgetConfig {
        int tier;
        int tube_sides;
        float simplification_tolerance;
        bool include_travels;
        size_t budget_bytes;
    };

    // Empirically measured bytes per raw gcode segment (includes simplification,
    // strip overhead, normal/color palettes). Calibrated from Pi 5 builds.
    static constexpr size_t BYTES_PER_SEG_N16 = 1300;
    static constexpr size_t BYTES_PER_SEG_N8 = 600;
    static constexpr size_t BYTES_PER_SEG_N4 = 300;

    static size_t parse_meminfo_available_kb(const std::string& content);
    size_t calculate_budget(size_t available_kb) const;
    size_t read_system_available_kb() const;
    bool is_system_memory_critical() const;
    BudgetConfig select_tier(size_t segment_count, size_t budget_bytes) const;

    enum class BudgetAction { CONTINUE, DEGRADE, ABORT };

    static constexpr float BUDGET_THRESHOLD = 0.9f;
    static constexpr size_t CHECK_INTERVAL_SEGMENTS = 5000;
    static constexpr size_t SYSTEM_CHECK_INTERVAL_SEGMENTS = 20000;

    BudgetAction check_budget(size_t current_usage_bytes, size_t budget_bytes,
                              int current_tier) const;
};

} // namespace gcode
} // namespace helix
