// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "print_file_data.h"
#include <vector>

namespace helix::ui {

enum class SortColumn { FILENAME, SIZE, MODIFIED, PRINT_TIME, FILAMENT };
enum class SortDirection { ASCENDING, DESCENDING };

/**
 * @brief Handles sorting of print file lists with directory-first ordering.
 *
 * Supports sorting by filename, size, modified date, print time, and filament usage.
 * Directories always appear before files regardless of sort column.
 */
class PrintSelectFileSorter {
public:
    PrintSelectFileSorter() = default;

    /**
     * @brief Set sort column. Toggles direction if same column, else resets to ASCENDING.
     */
    void sort_by(SortColumn column);

    /**
     * @brief Apply current sort settings to a file list.
     */
    void apply_sort(std::vector<PrintFileData>& files);

    SortColumn current_column() const { return current_column_; }
    SortDirection current_direction() const { return current_direction_; }

private:
    SortColumn current_column_ = SortColumn::MODIFIED;
    SortDirection current_direction_ = SortDirection::DESCENDING;
};

} // namespace helix::ui
