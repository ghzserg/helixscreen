// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_mini_status.h"

#include "ui_fonts.h"
#include "ui_nav_manager.h"
#include "ui_observer_guard.h"
#include "ui_panel_ams.h"
#include "ui_panel_ams_overview.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "observer_factory.h"
#include "theme_manager.h"
#include "ui/ams_drawing_utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

// ============================================================================
// Layout constants
// ============================================================================

/** Minimum bar width in pixels (prevents bars from becoming invisible) */
static constexpr int32_t MIN_BAR_WIDTH_PX = 3;

/** Maximum bar width in pixels (prevents bars from becoming too wide) */
static constexpr int32_t MAX_BAR_WIDTH_PX = 16;

/** Border radius for bar corners in pixels (very rounded appearance) */
static constexpr int32_t BAR_BORDER_RADIUS_PX = 8;

// ============================================================================
// Per-widget user data
// ============================================================================

/** Magic number to identify ams_mini_status widgets ("AMS1" as ASCII) */
static constexpr uint32_t AMS_MINI_STATUS_MAGIC = 0x414D5331;

/**
 * @brief Per-slot data stored for each bar
 */
struct SlotBarData {
    ams_draw::SlotColumn col; // Shared slot column (container, bar_bg, bar_fill, status_line)
    uint32_t color_rgb = 0x808080;
    int fill_pct = 100;
    bool present = false;                           // Filament present in slot
    bool loaded = false;                            // Filament loaded to toolhead
    bool has_error = false;                         // Slot is in error/blocked state
    SlotError::Severity severity = SlotError::INFO; // Error severity level
};

/**
 * @brief Per-unit row info for multi-unit stacked display
 */
struct UnitRowInfo {
    int first_slot = 0; // Index into slots[] array
    int slot_count = 0;
    lv_obj_t* row_container = nullptr; // Row container for this unit's bars
};

/**
 * @brief User data stored on each ams_mini_status widget
 */
struct AmsMiniStatusData {
    uint32_t magic = AMS_MINI_STATUS_MAGIC;
    int32_t height = 32;
    int slot_count = 0;
    int max_visible = AMS_MINI_STATUS_MAX_VISIBLE;

    // Multi-unit support
    int unit_count = 0;       // Number of AMS units (0 or 1 = single row, 2+ = stacked rows)
    UnitRowInfo unit_rows[8]; // Max 8 units

    // Child objects
    lv_obj_t* container = nullptr;      // Main container
    lv_obj_t* bars_container = nullptr; // Container for slot bars
    lv_obj_t* overflow_label = nullptr; // "+N" overflow indicator

    // Per-slot data
    SlotBarData slots[AMS_MINI_STATUS_MAX_VISIBLE];

    // Auto-binding observer (observe AmsState slots_version subject)
    // Uses ObserverGuard for RAII lifecycle management
    // Note: slots_version is always bumped after slot_count changes, so one observer suffices
    ObserverGuard slots_version_observer;
};

// Static registry for safe cleanup
static std::unordered_map<lv_obj_t*, AmsMiniStatusData*> s_registry;

static AmsMiniStatusData* get_data(lv_obj_t* obj) {
    auto it = s_registry.find(obj);
    return (it != s_registry.end()) ? it->second : nullptr;
}

// Forward declarations for internal functions
static void rebuild_bars(AmsMiniStatusData* data);
static void sync_from_ams_state(AmsMiniStatusData* data);

// ============================================================================
// Internal helpers
// ============================================================================

/** Helper to style a SlotBarData using shared drawing utils */
static void apply_slot_style(SlotBarData* slot) {
    ams_draw::BarStyleParams params;
    params.color_rgb = slot->color_rgb;
    params.fill_pct = slot->fill_pct;
    params.is_present = slot->present;
    params.is_loaded = slot->loaded;
    params.has_error = slot->has_error;
    params.severity = slot->severity;
    ams_draw::style_slot_bar(slot->col, params, BAR_BORDER_RADIUS_PX);
}

/**
 * @brief Create or get a unit row container for multi-unit stacked layout
 */
static lv_obj_t* ensure_unit_row(AmsMiniStatusData* data, int unit_index) {
    UnitRowInfo* row = &data->unit_rows[unit_index];
    if (!row->row_container) {
        row->row_container = ams_draw::create_transparent_container(data->bars_container);
        lv_obj_set_flex_flow(row->row_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row->row_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row->row_container, theme_manager_get_spacing("space_xxs"),
                                    LV_PART_MAIN);
        lv_obj_set_size(row->row_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    return row->row_container;
}

/** Rebuild the bars based on slot_count, max_visible, and unit_count */
static void rebuild_bars(AmsMiniStatusData* data) {
    if (!data || !data->bars_container)
        return;

    int visible_count = std::min(data->slot_count, data->max_visible);
    int overflow_count = data->slot_count - visible_count;

    // Calculate dimensions from container
    lv_obj_update_layout(data->container);
    int32_t container_width = lv_obj_get_content_width(data->container);
    int32_t container_height = lv_obj_get_content_height(data->container);

    int32_t gap = theme_manager_get_spacing("space_xxs");

    // Calculate bar height - use container height if data->height is 0 (XML-based responsive)
    int32_t effective_height = data->height > 0 ? data->height : container_height;
    if (effective_height < 20) {
        effective_height = 32; // Minimum fallback height
    }

    bool is_multi_unit = (data->unit_count >= 2);

    if (is_multi_unit) {
        // === Multi-unit stacked layout ===
        // bars_container becomes a column, each unit gets its own row

        lv_obj_set_flex_flow(data->bars_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(data->bars_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(data->bars_container, gap, LV_PART_MAIN);
        // Reset column padding (not used in column flow)
        lv_obj_set_style_pad_column(data->bars_container, 0, LV_PART_MAIN);

        // Reduce bar height per row to fit stacked rows
        int32_t row_gap_total = (data->unit_count - 1) * gap;
        int32_t available_height = effective_height - row_gap_total;
        int32_t per_row_height = available_height / data->unit_count;
        if (per_row_height < 12)
            per_row_height = 12; // Minimum per-row height

        int32_t total_slot_height = (per_row_height * 2) / 3;
        int32_t bar_height =
            total_slot_height - ams_draw::STATUS_LINE_HEIGHT_PX - ams_draw::STATUS_LINE_GAP_PX;
        if (bar_height < 6)
            bar_height = 6;

        // Delete any row containers beyond current unit_count
        for (int u = data->unit_count; u < 8; ++u) {
            if (data->unit_rows[u].row_container) {
                lv_obj_delete(data->unit_rows[u].row_container);
                data->unit_rows[u].row_container = nullptr;
            }
        }

        // Process each unit row
        for (int u = 0; u < data->unit_count && u < 8; ++u) {
            UnitRowInfo* row_info = &data->unit_rows[u];
            lv_obj_t* row = ensure_unit_row(data, u);

            // Calculate bar width for this row based on its slot count
            int row_slots =
                std::min(row_info->slot_count, data->max_visible - row_info->first_slot);
            if (row_slots < 0)
                row_slots = 0;

            int32_t bar_width = ams_draw::calc_bar_width(container_width, row_slots, gap,
                                                         MIN_BAR_WIDTH_PX, MAX_BAR_WIDTH_PX, 90);

            // Create/update slots in this unit row
            for (int s = 0; s < row_info->slot_count; ++s) {
                int global_idx = row_info->first_slot + s;
                if (global_idx >= AMS_MINI_STATUS_MAX_VISIBLE)
                    break;

                SlotBarData* slot = &data->slots[global_idx];

                if (global_idx < visible_count) {
                    if (!slot->col.container) {
                        // Create new slot column in this row
                        slot->col = ams_draw::create_slot_column(row, bar_width, bar_height,
                                                                 BAR_BORDER_RADIUS_PX);
                    } else if (lv_obj_get_parent(slot->col.container) != row) {
                        // Reparent slot container into correct unit row
                        lv_obj_set_parent(slot->col.container, row);
                    }

                    lv_obj_remove_flag(slot->col.container, LV_OBJ_FLAG_HIDDEN);
                    apply_slot_style(slot);
                } else {
                    if (slot->col.container) {
                        lv_obj_add_flag(slot->col.container, LV_OBJ_FLAG_HIDDEN);
                    }
                }
            }

            lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        }

        spdlog::debug("[AmsMiniStatus] Multi-unit layout: {} units, {} total slots",
                      data->unit_count, visible_count);

    } else {
        // === Single-unit layout (original behavior) ===

        // Clean up any leftover unit row containers from a previous multi-unit state
        // Reparent any slot containers back to bars_container first
        for (int u = 0; u < 8; ++u) {
            if (data->unit_rows[u].row_container) {
                // Move children back to bars_container before deleting the row
                for (int i = 0; i < AMS_MINI_STATUS_MAX_VISIBLE; ++i) {
                    SlotBarData* slot = &data->slots[i];
                    if (slot->col.container && lv_obj_get_parent(slot->col.container) ==
                                                   data->unit_rows[u].row_container) {
                        lv_obj_set_parent(slot->col.container, data->bars_container);
                    }
                }
                lv_obj_delete(data->unit_rows[u].row_container);
                data->unit_rows[u].row_container = nullptr;
            }
        }

        // Restore bars_container to row flex flow
        lv_obj_set_flex_flow(data->bars_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(data->bars_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(data->bars_container, gap, LV_PART_MAIN);
        lv_obj_set_style_pad_row(data->bars_container, 0, LV_PART_MAIN);

        int32_t total_slot_height = (effective_height * 2) / 3;
        int32_t bar_height =
            total_slot_height - ams_draw::STATUS_LINE_HEIGHT_PX - ams_draw::STATUS_LINE_GAP_PX;

        // Use 90% of container width for bars (leave 10% margin for centering)
        int32_t bar_width = ams_draw::calc_bar_width(container_width, visible_count, gap,
                                                     MIN_BAR_WIDTH_PX, MAX_BAR_WIDTH_PX, 90);

        // Create/update bars
        for (int i = 0; i < AMS_MINI_STATUS_MAX_VISIBLE; i++) {
            SlotBarData* slot = &data->slots[i];

            if (i < visible_count) {
                if (!slot->col.container) {
                    slot->col = ams_draw::create_slot_column(data->bars_container, bar_width,
                                                             bar_height, BAR_BORDER_RADIUS_PX);
                }

                lv_obj_remove_flag(slot->col.container, LV_OBJ_FLAG_HIDDEN);
                apply_slot_style(slot);
            } else {
                if (slot->col.container) {
                    lv_obj_add_flag(slot->col.container, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // Update overflow label
    if (data->overflow_label) {
        if (overflow_count > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "+%d", overflow_count);
            lv_label_set_text(data->overflow_label, buf);
            lv_obj_remove_flag(data->overflow_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(data->overflow_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Force layout recalculation for flex centering
    lv_obj_update_layout(data->container);

    // Hide entire widget if no slots
    if (data->slot_count <= 0) {
        lv_obj_add_flag(data->container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(data->container, LV_OBJ_FLAG_HIDDEN);
    }
}

/** Cleanup callback when widget is deleted */
static void on_delete(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto it = s_registry.find(obj);
    if (it != s_registry.end()) {
        std::unique_ptr<AmsMiniStatusData> data(it->second);
        if (data) {
            // Release observer before delete to prevent destructor from calling
            // lv_observer_remove() on potentially destroyed subjects during shutdown
            data->slots_version_observer.release();
        }
        // data automatically freed when unique_ptr goes out of scope
        s_registry.erase(it);
    }
}

/** Click callback to open AMS panel (routes to overview for multi-unit) */
static void on_click(lv_event_t* e) {
    (void)e;
    spdlog::debug("[AmsMiniStatus] Clicked - navigating to AMS panel");
    navigate_to_ams_panel();
}

// ============================================================================
// Public API
// ============================================================================

lv_obj_t* ui_ams_mini_status_create(lv_obj_t* parent, int32_t height) {
    if (!parent || height <= 0) {
        return nullptr;
    }

    // Create main container
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

    // Fill parent width, content height
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);

    // Use flex layout to center children (bars_container + overflow_label) horizontally
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);

    // Create user data
    auto data_ptr = std::make_unique<AmsMiniStatusData>();
    data_ptr->height = height;
    data_ptr->container = container;

    // Create bars container (holds the slot bars)
    data_ptr->bars_container = lv_obj_create(container);
    lv_obj_remove_flag(data_ptr->bars_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(data_ptr->bars_container, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
    lv_obj_set_style_bg_opa(data_ptr->bars_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(data_ptr->bars_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(data_ptr->bars_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(data_ptr->bars_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(data_ptr->bars_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(data_ptr->bars_container, theme_manager_get_spacing("space_xxs"),
                                LV_PART_MAIN);
    lv_obj_set_size(data_ptr->bars_container, LV_SIZE_CONTENT, height);

    // Create overflow label (hidden by default) - use responsive font
    data_ptr->overflow_label = lv_label_create(container);
    lv_obj_add_flag(data_ptr->overflow_label, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
    lv_label_set_text(data_ptr->overflow_label, "+0");
    lv_obj_set_style_text_color(data_ptr->overflow_label, theme_manager_get_color("text_muted"),
                                LV_PART_MAIN);
    const lv_font_t* font_xs = theme_manager_get_font("font_xs");
    if (!font_xs)
        font_xs = &noto_sans_12;
    lv_obj_set_style_text_font(data_ptr->overflow_label, font_xs, LV_PART_MAIN);
    lv_obj_add_flag(data_ptr->overflow_label, LV_OBJ_FLAG_HIDDEN);

    // Register and set up cleanup
    AmsMiniStatusData* data = data_ptr.release();
    s_registry[container] = data;
    lv_obj_add_event_cb(container, on_delete, LV_EVENT_DELETE, nullptr);

    // Make clickable to open AMS panel
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(container, on_click, LV_EVENT_CLICKED, nullptr);

    // Initially hidden (no slots)
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);

    // Auto-bind to AmsState: observe slots_version changes
    // slots_version is always bumped after slot_count changes, so one observer suffices
    // This makes the widget self-updating - no external wiring needed
    using helix::ui::observe_int_sync;

    lv_subject_t* slots_version_subject = AmsState::instance().get_slots_version_subject();
    if (slots_version_subject) {
        // Capture container (lv_obj_t*) instead of data pointer to prevent
        // use-after-free when deferred callback executes after widget deletion.
        // The registry lookup acts as a validity check. (fixes #83)
        data->slots_version_observer = observe_int_sync<lv_obj_t>(
            slots_version_subject, container, [](lv_obj_t* obj, int /* version */) {
                auto* d = get_data(obj);
                if (d)
                    sync_from_ams_state(d);
            });

        // Sync initial state if AMS already has data
        lv_subject_t* slot_count_subject = AmsState::instance().get_slot_count_subject();
        if (slot_count_subject && lv_subject_get_int(slot_count_subject) > 0) {
            sync_from_ams_state(data);
        }
        spdlog::debug("[AmsMiniStatus] Auto-bound to AmsState slots_version subject");
    }

    spdlog::trace("[AmsMiniStatus] Created (height={})", height);
    return container;
}

void ui_ams_mini_status_set_slot_count(lv_obj_t* obj, int slot_count) {
    auto* data = get_data(obj);
    if (!data)
        return;

    slot_count = std::max(0, slot_count);
    if (data->slot_count == slot_count)
        return;

    data->slot_count = slot_count;
    rebuild_bars(data);

    spdlog::debug("[AmsMiniStatus] slot_count={}", slot_count);
}

void ui_ams_mini_status_set_max_visible(lv_obj_t* obj, int max_visible) {
    auto* data = get_data(obj);
    if (!data)
        return;

    max_visible = std::max(1, std::min(AMS_MINI_STATUS_MAX_VISIBLE, max_visible));
    if (data->max_visible == max_visible)
        return;

    data->max_visible = max_visible;
    rebuild_bars(data);
}

void ui_ams_mini_status_set_slot(lv_obj_t* obj, int slot_index, uint32_t color_rgb, int fill_pct,
                                 bool present) {
    auto* data = get_data(obj);
    if (!data || slot_index < 0 || slot_index >= AMS_MINI_STATUS_MAX_VISIBLE)
        return;

    SlotBarData* slot = &data->slots[slot_index];
    slot->color_rgb = color_rgb;
    slot->fill_pct = std::clamp(fill_pct, 0, 100);
    slot->present = present;

    apply_slot_style(slot);
}

/** Timer callback for deferred refresh */
static void deferred_refresh_cb(lv_timer_t* timer) {
    lv_obj_t* container = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
    if (!container) {
        lv_timer_delete(timer);
        return;
    }

    auto* data = get_data(container);
    if (data) {
        rebuild_bars(data);
        spdlog::debug("[AmsMiniStatus] Deferred refresh complete");
    }
    lv_timer_delete(timer);
}

void ui_ams_mini_status_refresh(lv_obj_t* obj) {
    auto* data = get_data(obj);
    if (!data || !data->container)
        return;

    // Check if we have valid dimensions yet
    lv_obj_update_layout(data->bars_container);
    int32_t width = lv_obj_get_content_width(data->bars_container);

    if (width > 0) {
        // We have dimensions - rebuild immediately
        rebuild_bars(data);
    } else {
        // Container still has zero width (likely just unhidden)
        // Defer refresh to next LVGL tick when layout will be recalculated
        lv_timer_t* timer = lv_timer_create(deferred_refresh_cb, 1, data->container);
        lv_timer_set_repeat_count(timer, 1);
        spdlog::debug("[AmsMiniStatus] Deferring refresh (container has zero width)");
    }
}

bool ui_ams_mini_status_is_valid(lv_obj_t* obj) {
    auto* data = get_data(obj);
    return data && data->magic == AMS_MINI_STATUS_MAGIC;
}

// ============================================================================
// Auto-binding to AmsState
// ============================================================================

/**
 * @brief Sync widget state from AmsState backend
 *
 * Reads slot count and per-slot info from AmsState and updates the widget.
 * Called on initial creation and when slot_count changes.
 */
static void sync_from_ams_state(AmsMiniStatusData* data) {
    if (!data)
        return;

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        // No backend - hide widget
        data->slot_count = 0;
        rebuild_bars(data);
        return;
    }

    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    data->slot_count = slot_count;

    // Get multi-unit info from system info
    AmsSystemInfo info = backend->get_system_info();
    data->unit_count = static_cast<int>(info.units.size());
    for (int u = 0; u < data->unit_count && u < 8; ++u) {
        data->unit_rows[u].first_slot = info.units[u].first_slot_global_index;
        data->unit_rows[u].slot_count = info.units[u].slot_count;
    }
    // Clear any stale unit rows beyond current count
    for (int u = data->unit_count; u < 8; ++u) {
        data->unit_rows[u].first_slot = 0;
        data->unit_rows[u].slot_count = 0;
    }

    // Populate each slot from backend slot info
    for (int i = 0; i < slot_count && i < AMS_MINI_STATUS_MAX_VISIBLE; ++i) {
        SlotInfo slot = backend->get_slot_info(i);

        SlotBarData* slot_bar = &data->slots[i];
        slot_bar->color_rgb = slot.color_rgb;
        slot_bar->fill_pct = ams_draw::fill_percent_from_slot(slot, 0);
        slot_bar->present = slot.is_present();
        slot_bar->loaded = (slot.status == SlotStatus::LOADED);
        slot_bar->has_error = (slot.status == SlotStatus::BLOCKED || slot.error.has_value());
        slot_bar->severity = slot.error.has_value() ? slot.error->severity : SlotError::INFO;
    }

    rebuild_bars(data);
    spdlog::trace("[AmsMiniStatus] Synced from AmsState: {} slots", slot_count);
}

// Observer callbacks migrated to lambda observers in ui_ams_mini_status_create()

// ============================================================================
// XML Widget Registration
// ============================================================================

/**
 * @brief XML create callback - creates widget with responsive sizing
 */
static void* ui_ams_mini_status_xml_create(lv_xml_parser_state_t* state, const char** /* attrs */) {
    void* parent = lv_xml_state_get_parent(state);

    // Create main container that fills parent
    lv_obj_t* container = lv_obj_create((lv_obj_t*)parent);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

    // Fill parent by default (can be overridden by XML attrs)
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));

    // Use flex layout to center children
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, theme_manager_get_spacing("space_xs"), LV_PART_MAIN);

    // Create user data with height=0 (will be set dynamically)
    auto data_ptr = std::make_unique<AmsMiniStatusData>();
    data_ptr->height = 0; // Will be calculated from parent
    data_ptr->container = container;

    // Create bars container (holds the slot bars)
    data_ptr->bars_container = lv_obj_create(container);
    lv_obj_remove_flag(data_ptr->bars_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(data_ptr->bars_container, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(data_ptr->bars_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(data_ptr->bars_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(data_ptr->bars_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(data_ptr->bars_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(data_ptr->bars_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(data_ptr->bars_container, theme_manager_get_spacing("space_xxs"),
                                LV_PART_MAIN);
    // Fill parent height (responsive)
    lv_obj_set_size(data_ptr->bars_container, LV_SIZE_CONTENT, LV_PCT(100));

    // Create overflow label (hidden by default)
    data_ptr->overflow_label = lv_label_create(container);
    lv_obj_add_flag(data_ptr->overflow_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(data_ptr->overflow_label, "+0");
    lv_obj_set_style_text_color(data_ptr->overflow_label, theme_manager_get_color("text_muted"),
                                LV_PART_MAIN);
    const lv_font_t* font_xs = theme_manager_get_font("font_xs");
    if (!font_xs)
        font_xs = &noto_sans_12;
    lv_obj_set_style_text_font(data_ptr->overflow_label, font_xs, LV_PART_MAIN);
    lv_obj_add_flag(data_ptr->overflow_label, LV_OBJ_FLAG_HIDDEN);

    // Register and set up cleanup
    AmsMiniStatusData* data = data_ptr.release();
    s_registry[container] = data;
    lv_obj_add_event_cb(container, on_delete, LV_EVENT_DELETE, nullptr);

    // Make clickable to open AMS panel
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(container, on_click, LV_EVENT_CLICKED, nullptr);

    // Initially hidden (no slots)
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);

    // Auto-bind to AmsState: observe slots_version changes
    // slots_version is always bumped after slot_count changes, so one observer suffices
    using helix::ui::observe_int_sync;

    lv_subject_t* slots_version_subject = AmsState::instance().get_slots_version_subject();
    if (slots_version_subject) {
        // Capture container (lv_obj_t*) instead of data pointer to prevent
        // use-after-free when deferred callback executes after widget deletion.
        // The registry lookup acts as a validity check. (fixes #83)
        data->slots_version_observer = observe_int_sync<lv_obj_t>(
            slots_version_subject, container, [](lv_obj_t* obj, int /* version */) {
                auto* d = get_data(obj);
                if (d)
                    sync_from_ams_state(d);
            });

        // Sync initial state if AMS already has data
        lv_subject_t* slot_count_subject = AmsState::instance().get_slot_count_subject();
        if (slot_count_subject && lv_subject_get_int(slot_count_subject) > 0) {
            sync_from_ams_state(data);
        }
    }

    spdlog::trace("[AmsMiniStatus] Created via XML (responsive height)");
    return container;
}

/**
 * @brief XML apply callback - handles XML attributes
 */
static void ui_ams_mini_status_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    // Apply standard object attributes (width, height, align, etc.)
    lv_xml_obj_apply(state, attrs);
}

void ui_ams_mini_status_init(void) {
    lv_xml_register_widget("ams_mini_status", ui_ams_mini_status_xml_create,
                           ui_ams_mini_status_xml_apply);
    spdlog::trace("[AmsMiniStatus] Registered ams_mini_status XML widget");
}
