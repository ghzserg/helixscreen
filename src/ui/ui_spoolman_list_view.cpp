// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_spoolman_list_view.h"

#include "ui_spool_canvas.h"

#include "format_utils.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>

namespace helix::ui {

// ============================================================================
// Helpers
// ============================================================================

/**
 * Parse a hex color string (with or without '#' prefix) into an lv_color_t.
 * Returns fallback_color if the string is empty or unparseable.
 */
static lv_color_t parse_spool_color(const std::string& color_hex, lv_color_t fallback_color) {
    if (color_hex.empty()) {
        return fallback_color;
    }

    const char* hex = color_hex.c_str();
    if (hex[0] == '#') {
        hex++;
    }

    unsigned int color_val = 0;
    if (sscanf(hex, "%x", &color_val) == 1) {
        return lv_color_hex(color_val);
    }
    return fallback_color;
}

// ============================================================================
// Destruction
// ============================================================================

SpoolmanListView::~SpoolmanListView() {
    cleanup();
}

// ============================================================================
// Setup / Cleanup
// ============================================================================

bool SpoolmanListView::setup(lv_obj_t* container) {
    if (!container) {
        spdlog::error("[SpoolmanListView] Cannot setup - null container");
        return false;
    }

    container_ = container;
    spdlog::trace("[SpoolmanListView] Setup complete");
    return true;
}

void SpoolmanListView::cleanup() {
    pool_.clear();
    pool_indices_.clear();
    container_ = nullptr;
    leading_spacer_ = nullptr;
    trailing_spacer_ = nullptr;
    visible_start_ = -1;
    visible_end_ = -1;
    total_items_ = 0;
    cached_row_height_ = 0;
    cached_row_gap_ = 0;
    spdlog::debug("[SpoolmanListView] cleanup()");
}

// ============================================================================
// Pool Initialization
// ============================================================================

void SpoolmanListView::init_pool() {
    if (!container_ || !pool_.empty()) {
        return;
    }

    spdlog::debug("[SpoolmanListView] Creating {} row widgets", POOL_SIZE);

    pool_.reserve(POOL_SIZE);
    pool_indices_.resize(POOL_SIZE, -1);

    for (int i = 0; i < POOL_SIZE; i++) {
        lv_obj_t* row =
            static_cast<lv_obj_t*>(lv_xml_create(container_, "spoolman_spool_row", nullptr));

        if (row) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            pool_.push_back(row);
        }
    }

    spdlog::debug("[SpoolmanListView] Pool initialized with {} rows", pool_.size());
}

void SpoolmanListView::create_spacers() {
    if (!container_) {
        return;
    }

    if (!leading_spacer_) {
        leading_spacer_ = lv_obj_create(container_);
        lv_obj_remove_style_all(leading_spacer_);
        lv_obj_remove_flag(leading_spacer_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(leading_spacer_, lv_pct(100));
        lv_obj_set_height(leading_spacer_, 0);
    }

    if (!trailing_spacer_) {
        trailing_spacer_ = lv_obj_create(container_);
        lv_obj_remove_style_all(trailing_spacer_);
        lv_obj_remove_flag(trailing_spacer_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(trailing_spacer_, lv_pct(100));
        lv_obj_set_height(trailing_spacer_, 0);
    }
}

// ============================================================================
// Row Configuration
// ============================================================================

void SpoolmanListView::configure_row(lv_obj_t* row, const SpoolInfo& spool, int active_spool_id) {
    if (!row) {
        return;
    }

    // Store spool ID in user_data for click handling
    lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(spool.id)));

    // Update 3D spool canvas
    lv_obj_t* canvas = lv_obj_find_by_name(row, "spool_canvas");
    if (canvas) {
        lv_color_t color =
            parse_spool_color(spool.color_hex, theme_manager_get_color("text_muted"));
        ui_spool_canvas_set_color(canvas, color);

        float fill_level = static_cast<float>(spool.remaining_percent()) / 100.0f;
        ui_spool_canvas_set_fill_level(canvas, fill_level);
        ui_spool_canvas_redraw(canvas);
    }

    // Update spool ID label
    lv_obj_t* id_label = lv_obj_find_by_name(row, "spool_id_label");
    if (id_label) {
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "#%d", spool.id);
        lv_label_set_text(id_label, id_buf);
    }

    // Update spool name (Material - Color)
    lv_obj_t* name_label = lv_obj_find_by_name(row, "spool_name");
    if (name_label) {
        lv_label_set_text(name_label, spool.display_name().c_str());
    }

    // Update vendor
    lv_obj_t* vendor_label = lv_obj_find_by_name(row, "spool_vendor");
    if (vendor_label) {
        const char* vendor = spool.vendor.empty() ? "Unknown" : spool.vendor.c_str();
        lv_label_set_text(vendor_label, vendor);
    }

    // Update weight
    lv_obj_t* weight_label = lv_obj_find_by_name(row, "weight_text");
    if (weight_label) {
        char weight_buf[32];
        snprintf(weight_buf, sizeof(weight_buf), "%.0fg", spool.remaining_weight_g);
        lv_label_set_text(weight_label, weight_buf);
    }

    // Update percentage
    lv_obj_t* percent_label = lv_obj_find_by_name(row, "percent_text");
    if (percent_label) {
        char percent_buf[16];
        helix::fmt::format_percent(static_cast<int>(spool.remaining_percent()), percent_buf,
                                   sizeof(percent_buf));
        lv_label_set_text(percent_label, percent_buf);
    }

    // Low stock warning
    lv_obj_t* low_stock_icon = lv_obj_find_by_name(row, "low_stock_indicator");
    if (low_stock_icon) {
        lv_obj_set_flag(low_stock_icon, LV_OBJ_FLAG_HIDDEN, !spool.is_low());
    }

    // Active spool: show checkmark + highlight row with checked state
    bool is_active = (spool.id == active_spool_id);
    lv_obj_t* active_icon = lv_obj_find_by_name(row, "active_indicator");
    if (active_icon) {
        lv_obj_set_flag(active_icon, LV_OBJ_FLAG_HIDDEN, !is_active);
    }
    lv_obj_set_state(row, LV_STATE_CHECKED, is_active);

    // Show the row
    lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================================
// Population / Visibility
// ============================================================================

void SpoolmanListView::populate(const std::vector<SpoolInfo>& spools, int active_spool_id) {
    if (!container_) {
        return;
    }

    spdlog::debug("[SpoolmanListView] Populating with {} spools", spools.size());

    // Initialize pool on first call
    if (pool_.empty()) {
        init_pool();
    }

    // Create spacers if needed
    create_spacers();

    // Cache row dimensions on first populate
    if (cached_row_height_ == 0 && !pool_.empty() && !spools.empty()) {
        lv_obj_t* row = pool_[0];
        configure_row(row, spools[0], active_spool_id);
        lv_obj_update_layout(container_);

        cached_row_height_ = lv_obj_get_height(row);
        cached_row_gap_ = lv_obj_get_style_pad_row(container_, LV_PART_MAIN);

        spdlog::debug("[SpoolmanListView] Cached row dimensions: height={} gap={}",
                      cached_row_height_, cached_row_gap_);
    }

    // Reset visible range and scroll
    visible_start_ = -1;
    visible_end_ = -1;

    lv_obj_scroll_to_y(container_, 0, LV_ANIM_OFF);

    // Update visible rows
    update_visible(spools, active_spool_id);

    spdlog::debug("[SpoolmanListView] Populated: {} spools, pool size {}", spools.size(),
                  pool_.size());
}

void SpoolmanListView::update_visible(const std::vector<SpoolInfo>& spools, int active_spool_id) {
    if (!container_ || pool_.empty() || spools.empty()) {
        for (auto* row : pool_) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
        if (leading_spacer_)
            lv_obj_set_height(leading_spacer_, 0);
        if (trailing_spacer_)
            lv_obj_set_height(trailing_spacer_, 0);
        visible_start_ = -1;
        visible_end_ = -1;
        total_items_ = 0;
        return;
    }

    int32_t scroll_y = lv_obj_get_scroll_y(container_);
    int32_t viewport_height = lv_obj_get_height(container_);

    int total_rows = static_cast<int>(spools.size());

    int row_height = cached_row_height_ > 0 ? cached_row_height_ : 56;
    int row_gap = cached_row_gap_;
    int row_stride = row_height + row_gap;

    // Calculate visible range with buffer
    int first_visible = std::max(0, static_cast<int>(scroll_y / row_stride) - BUFFER_ROWS);
    int last_visible = std::min(
        total_rows, static_cast<int>((scroll_y + viewport_height) / row_stride) + 1 + BUFFER_ROWS);

    // Force re-render if total item count changed (e.g. filter applied)
    bool data_changed = (total_rows != total_items_);

    // Skip if unchanged (unless data set size changed)
    if (!data_changed && first_visible == visible_start_ && last_visible == visible_end_) {
        return;
    }

    total_items_ = total_rows;

    spdlog::trace(
        "[SpoolmanListView] Rendering rows {}-{} of {} (scroll_y={} viewport={} data_changed={})",
        first_visible, last_visible, total_rows, scroll_y, viewport_height, data_changed);

    // Update leading spacer
    int leading_height = first_visible * row_stride;
    if (leading_spacer_) {
        lv_obj_set_height(leading_spacer_, leading_height);
        lv_obj_move_to_index(leading_spacer_, 0);
    }

    // Update trailing spacer
    int trailing_height = (total_rows - last_visible) * row_stride;
    if (trailing_spacer_) {
        lv_obj_set_height(trailing_spacer_, std::max(0, trailing_height));
    }

    // Mark all pool slots as available
    std::fill(pool_indices_.begin(), pool_indices_.end(), static_cast<ssize_t>(-1));

    // Assign pool rows to visible indices
    size_t pool_idx = 0;
    for (int spool_idx = first_visible; spool_idx < last_visible && pool_idx < pool_.size();
         spool_idx++, pool_idx++) {
        lv_obj_t* row = pool_[pool_idx];
        configure_row(row, spools[spool_idx], active_spool_id);
        pool_indices_[pool_idx] = spool_idx;

        // Position after leading spacer
        lv_obj_move_to_index(row, static_cast<int>(pool_idx) + 1);
    }

    // Hide unused pool rows
    for (; pool_idx < pool_.size(); pool_idx++) {
        lv_obj_add_flag(pool_[pool_idx], LV_OBJ_FLAG_HIDDEN);
        pool_indices_[pool_idx] = -1;
    }

    visible_start_ = first_visible;
    visible_end_ = last_visible;
}

void SpoolmanListView::refresh_content(const std::vector<SpoolInfo>& spools, int active_spool_id) {
    if (!container_ || pool_.empty() || visible_start_ < 0) {
        return;
    }

    for (size_t i = 0; i < pool_.size(); i++) {
        ssize_t spool_idx = pool_indices_[i];
        if (spool_idx >= 0 && static_cast<size_t>(spool_idx) < spools.size()) {
            configure_row(pool_[i], spools[spool_idx], active_spool_id);
        }
    }
}

void SpoolmanListView::update_active_indicators(const std::vector<SpoolInfo>& spools,
                                                int active_spool_id) {
    if (!container_ || pool_.empty()) {
        return;
    }

    for (size_t i = 0; i < pool_.size(); i++) {
        ssize_t spool_idx = pool_indices_[i];
        if (spool_idx < 0 || static_cast<size_t>(spool_idx) >= spools.size()) {
            continue;
        }

        lv_obj_t* row = pool_[i];
        bool is_active = (spools[spool_idx].id == active_spool_id);

        lv_obj_set_state(row, LV_STATE_CHECKED, is_active);

        lv_obj_t* active_icon = lv_obj_find_by_name(row, "active_indicator");
        if (active_icon) {
            lv_obj_set_flag(active_icon, LV_OBJ_FLAG_HIDDEN, !is_active);
        }
    }

    spdlog::debug("[SpoolmanListView] Updated active indicators (active={})", active_spool_id);
}

} // namespace helix::ui
