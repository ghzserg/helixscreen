// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_print_select_file_sorter.h"
#include <algorithm>

namespace helix::ui {

void PrintSelectFileSorter::sort_by(SortColumn column) {
    if (column == current_column_) {
        current_direction_ = (current_direction_ == SortDirection::ASCENDING)
                                 ? SortDirection::DESCENDING
                                 : SortDirection::ASCENDING;
    } else {
        current_column_ = column;
        current_direction_ = SortDirection::ASCENDING;
    }
}

void PrintSelectFileSorter::apply_sort(std::vector<PrintFileData>& files) {
    auto sort_column = current_column_;
    auto sort_direction = current_direction_;

    std::sort(files.begin(), files.end(),
              [sort_column, sort_direction](const PrintFileData& a, const PrintFileData& b) {
                  // Directories always sort to top
                  if (a.is_dir != b.is_dir) {
                      return a.is_dir;
                  }

                  bool result = false;

                  switch (sort_column) {
                  case SortColumn::FILENAME:
                      result = a.filename < b.filename;
                      break;
                  case SortColumn::SIZE:
                      result = a.file_size_bytes < b.file_size_bytes;
                      break;
                  case SortColumn::MODIFIED:
                      result = a.modified_timestamp < b.modified_timestamp;
                      break;
                  case SortColumn::PRINT_TIME:
                      result = a.print_time_minutes < b.print_time_minutes;
                      break;
                  case SortColumn::FILAMENT:
                      result = a.filament_grams < b.filament_grams;
                      break;
                  }

                  if (sort_direction == SortDirection::DESCENDING) {
                      result = !result;
                  }

                  return result;
              });
}

} // namespace helix::ui
