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

} // namespace gcode
} // namespace helix
