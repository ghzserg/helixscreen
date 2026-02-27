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

    static size_t parse_meminfo_available_kb(const std::string& content);
    size_t calculate_budget(size_t available_kb) const;
    size_t read_system_available_kb() const;
    bool is_system_memory_critical() const;
};

} // namespace gcode
} // namespace helix
