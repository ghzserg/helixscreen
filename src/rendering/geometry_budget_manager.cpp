// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry_budget_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace helix {
namespace gcode {

size_t GeometryBudgetManager::parse_meminfo_available_kb(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            size_t value = 0;
            if (sscanf(line.c_str(), "MemAvailable: %zu", &value) == 1) {
                return value;
            }
        }
    }
    return 0;
}

size_t GeometryBudgetManager::calculate_budget(size_t available_kb) const {
    if (available_kb == 0)
        return 0;
    size_t budget = (available_kb * 1024) / (100 / BUDGET_PERCENT);
    return std::min(budget, MAX_BUDGET_BYTES);
}

size_t GeometryBudgetManager::read_system_available_kb() const {
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        spdlog::warn("[GeometryBudget] Cannot read /proc/meminfo");
        return 0;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parse_meminfo_available_kb(content);
}

bool GeometryBudgetManager::is_system_memory_critical() const {
    return read_system_available_kb() < CRITICAL_MEMORY_KB;
}

GeometryBudgetManager::BudgetConfig GeometryBudgetManager::select_tier(size_t segment_count,
                                                                       size_t budget_bytes) const {
    if (budget_bytes == 0) {
        spdlog::info("[GeometryBudget] Zero budget — thumbnail only (tier 5)");
        return {.tier = 5,
                .tube_sides = 0,
                .simplification_tolerance = 0.0f,
                .include_travels = false,
                .budget_bytes = 0};
    }
    if (segment_count == 0) {
        return {.tier = 1,
                .tube_sides = 16,
                .simplification_tolerance = 0.01f,
                .include_travels = true,
                .budget_bytes = budget_bytes};
    }

    size_t est_n16 = segment_count * BYTES_PER_SEG_N16;
    size_t est_n8 = segment_count * BYTES_PER_SEG_N8;
    size_t est_n4 = segment_count * BYTES_PER_SEG_N4;

    if (est_n16 < budget_bytes) {
        spdlog::info("[GeometryBudget] Tier 1 (full): est {}MB / {}MB budget",
                     est_n16 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 1,
                .tube_sides = 16,
                .simplification_tolerance = 0.01f,
                .include_travels = true,
                .budget_bytes = budget_bytes};
    }
    if (est_n8 < budget_bytes) {
        spdlog::info("[GeometryBudget] Tier 2 (medium): est {}MB / {}MB budget",
                     est_n8 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 2,
                .tube_sides = 8,
                .simplification_tolerance = 0.2f,
                .include_travels = true,
                .budget_bytes = budget_bytes};
    }
    if (est_n4 < budget_bytes) {
        spdlog::info("[GeometryBudget] Tier 3 (low): est {}MB / {}MB budget",
                     est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 3,
                .tube_sides = 4,
                .simplification_tolerance = 1.0f,
                .include_travels = false,
                .budget_bytes = budget_bytes};
    }
    if (est_n4 < budget_bytes * 2) {
        spdlog::info("[GeometryBudget] Tier 3 (aggressive): est {}MB / {}MB budget",
                     est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024));
        return {.tier = 3,
                .tube_sides = 4,
                .simplification_tolerance = 2.0f,
                .include_travels = false,
                .budget_bytes = budget_bytes};
    }

    spdlog::info("[GeometryBudget] Tier 4 (2D fallback): est {}MB exceeds {}MB budget",
                 est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024));
    return {.tier = 4,
            .tube_sides = 0,
            .simplification_tolerance = 0.0f,
            .include_travels = false,
            .budget_bytes = budget_bytes};
}

GeometryBudgetManager::BudgetAction GeometryBudgetManager::check_budget(size_t current_usage_bytes,
                                                                        size_t budget_bytes,
                                                                        int current_tier) const {
    if (budget_bytes == 0)
        return BudgetAction::ABORT;

    float usage_ratio = static_cast<float>(current_usage_bytes) / budget_bytes;
    if (usage_ratio < BUDGET_THRESHOLD)
        return BudgetAction::CONTINUE;

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

} // namespace gcode
} // namespace helix
