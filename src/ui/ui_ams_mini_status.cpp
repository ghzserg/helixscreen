// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_mini_status.h"

#include "ui_fonts.h"
#include "ui_nav_manager.h"
#include "ui_observer_guard.h"
#include "ui_panel_ams.h"
#include "ui_theme.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl/src/xml/lv_xml_parser.h"
#include "lvgl/src/xml/parsers/lv_xml_obj_parser.h"
#include "observer_factory.h"

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
    lv_obj_t* slot_container = nullptr; // Wrapper for bar + status line (column flex)
    lv_obj_t* bar_bg = nullptr;         // Background outline container
    lv_obj_t* bar_fill = nullptr;       // Fill portion (colored)
    lv_obj_t* status_line = nullptr;    // Bottom line BELOW bar (green=loaded, red=error only)
    uint32_t color_rgb = 0x808080;
    int fill_pct = 100;
    bool present = false;   // Filament present in slot
    bool loaded = false;    // Filament loaded to toolhead
    bool has_error = false; // Slot is in error/blocked state
};

static lv_color_t lighten_color(lv_color_t c, uint8_t amt) {
    return lv_color_make((c.red + amt > 255) ? 255 : c.red + amt,
                         (c.green + amt > 255) ? 255 : c.green + amt,
                         (c.blue + amt > 255) ? 255 : c.blue + amt);
}

/**
 * @brief User data stored on each ams_mini_status widget
 */
struct AmsMiniStatusData {
    uint32_t magic = AMS_MINI_STATUS_MAGIC;
    int32_t height = 32;
    int slot_count = 0;
    int max_visible = AMS_MINI_STATUS_MAX_VISIBLE;

    // Child objects
    lv_obj_t* container = nullptr;      // Main container
    lv_obj_t* bars_container = nullptr; // Container for slot bars
    lv_obj_t* overflow_label = nullptr; // "+N" overflow indicator

    // Per-slot data
    SlotBarData slots[AMS_MINI_STATUS_MAX_VISIBLE];

    // Auto-binding observers (observe AmsState subjects)
    // Uses ObserverGuard for RAII lifecycle management
    ObserverGuard slot_count_observer;    // Triggers when number of slots changes
    ObserverGuard slots_version_observer; // Triggers when slot status/color changes
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

/** Height of the status indicator line at bottom of slot */
static constexpr int32_t STATUS_LINE_HEIGHT_PX = 3;

/** Gap between filament bar and status line underneath */
static constexpr int32_t STATUS_LINE_GAP_PX = 2;

/** Update a single slot bar's appearance (colors, opacity, status line) */
static void update_slot_bar(SlotBarData* slot) {
    if (!slot->bar_bg || !slot->bar_fill)
        return;

    // Background: outline only - opacity varies by state
    // Empty slots get very dim "ghosted" outline, present slots get normal outline
    lv_obj_set_style_bg_opa(slot->bar_bg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(slot->bar_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(slot->bar_bg, ui_theme_get_color("text_secondary"), LV_PART_MAIN);

    if (slot->present) {
        // Normal visibility for slots with filament
        lv_obj_set_style_border_opa(slot->bar_bg, LV_OPA_50, LV_PART_MAIN);
    } else {
        // Ghosted/dim outline for empty slots
        lv_obj_set_style_border_opa(slot->bar_bg, LV_OPA_20, LV_PART_MAIN);
    }

    // Fill: colored portion from bottom, filling up within bar_bg
    if (slot->present && slot->fill_pct > 0) {
        lv_color_t base_color = lv_color_hex(slot->color_rgb);
        lv_color_t light_color = lighten_color(base_color, 50);

        lv_obj_set_style_bg_color(slot->bar_fill, light_color, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(slot->bar_fill, base_color, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(slot->bar_fill, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slot->bar_fill, LV_OPA_COVER, LV_PART_MAIN);

        // Use percentage height relative to parent's content area
        // This ensures fill stays within bar_bg's borders
        lv_obj_set_height(slot->bar_fill, LV_PCT(slot->fill_pct));
        lv_obj_align(slot->bar_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_remove_flag(slot->bar_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(slot->bar_fill, LV_OBJ_FLAG_HIDDEN);
    }

    // Status line BELOW bar_bg: green=loaded, red=error only
    // Empty slots get NO status line (just ghosted outline)
    if (slot->status_line) {
        if (slot->has_error) {
            // Red - slot is in error/blocked state
            lv_obj_set_style_bg_color(slot->status_line, ui_theme_get_color("error_color"),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(slot->status_line, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_remove_flag(slot->status_line, LV_OBJ_FLAG_HIDDEN);
        } else if (slot->loaded) {
            // Green - filament loaded to toolhead from this lane
            lv_obj_set_style_bg_color(slot->status_line, ui_theme_get_color("success_color"),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(slot->status_line, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_remove_flag(slot->status_line, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Present but not loaded, or empty - hide status line
            lv_obj_add_flag(slot->status_line, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/** Rebuild the bars based on slot_count and max_visible */
static void rebuild_bars(AmsMiniStatusData* data) {
    if (!data || !data->bars_container)
        return;

    int visible_count = std::min(data->slot_count, data->max_visible);
    int overflow_count = data->slot_count - visible_count;

    // Calculate bar width to fit within container
    lv_obj_update_layout(data->container);
    int32_t container_width = lv_obj_get_content_width(data->container);
    int32_t container_height = lv_obj_get_content_height(data->container);

    int32_t gap = ui_theme_get_spacing("space_xxs"); // Responsive 2-4px gap

    // Use 90% of container width for bars (leave 10% margin for centering)
    int32_t total_bar_space = (container_width * 90) / 100;
    int32_t total_gaps = (visible_count > 1) ? (visible_count - 1) * gap : 0;
    int32_t bar_width = (total_bar_space - total_gaps) / std::max(1, visible_count);
    bar_width = std::clamp(bar_width, MIN_BAR_WIDTH_PX, MAX_BAR_WIDTH_PX);

    // Calculate bar height - use container height if data->height is 0 (XML-based responsive)
    int32_t effective_height = data->height > 0 ? data->height : container_height;
    if (effective_height < 20) {
        effective_height = 32; // Minimum fallback height
    }
    int32_t total_slot_height = (effective_height * 2) / 3;
    int32_t bar_height = total_slot_height - STATUS_LINE_HEIGHT_PX - STATUS_LINE_GAP_PX;

    // Create/update bars
    for (int i = 0; i < AMS_MINI_STATUS_MAX_VISIBLE; i++) {
        SlotBarData* slot = &data->slots[i];

        if (i < visible_count) {
            // Show this slot
            if (!slot->slot_container) {
                // Create slot container (column flex: bar on top, status line below)
                slot->slot_container = lv_obj_create(data->bars_container);
                lv_obj_remove_flag(slot->slot_container, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(slot->slot_container,
                                LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
                lv_obj_set_style_bg_opa(slot->slot_container, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(slot->slot_container, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(slot->slot_container, 0, LV_PART_MAIN);
                lv_obj_set_flex_flow(slot->slot_container, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(slot->slot_container, LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                lv_obj_set_style_pad_row(slot->slot_container, STATUS_LINE_GAP_PX, LV_PART_MAIN);

                // Create bar background (outline container)
                slot->bar_bg = lv_obj_create(slot->slot_container);
                lv_obj_remove_flag(slot->bar_bg, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(slot->bar_bg, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
                lv_obj_set_style_border_width(slot->bar_bg, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(slot->bar_bg, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(slot->bar_bg, BAR_BORDER_RADIUS_PX, LV_PART_MAIN);

                // Create fill inside bar_bg
                slot->bar_fill = lv_obj_create(slot->bar_bg);
                lv_obj_remove_flag(slot->bar_fill, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(slot->bar_fill, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
                lv_obj_set_style_border_width(slot->bar_fill, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(slot->bar_fill, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(slot->bar_fill, BAR_BORDER_RADIUS_PX, LV_PART_MAIN);
                lv_obj_set_width(slot->bar_fill, LV_PCT(100));

                // Create status line as sibling BELOW bar_bg (green=loaded, red=error only)
                slot->status_line = lv_obj_create(slot->slot_container);
                lv_obj_remove_flag(slot->status_line, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(slot->status_line,
                                LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
                lv_obj_set_style_border_width(slot->status_line, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(slot->status_line, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(slot->status_line, BAR_BORDER_RADIUS_PX / 2, LV_PART_MAIN);
            }

            // Set sizes
            lv_obj_set_size(slot->slot_container, bar_width, total_slot_height);
            lv_obj_set_size(slot->bar_bg, bar_width, bar_height);
            lv_obj_set_size(slot->status_line, bar_width, STATUS_LINE_HEIGHT_PX);

            lv_obj_remove_flag(slot->slot_container, LV_OBJ_FLAG_HIDDEN);
            update_slot_bar(slot);
        } else {
            // Hide this slot
            if (slot->slot_container) {
                lv_obj_add_flag(slot->slot_container, LV_OBJ_FLAG_HIDDEN);
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
            // Release observers before delete to prevent destructor from calling
            // lv_observer_remove() on potentially destroyed subjects during shutdown
            data->slot_count_observer.release();
            data->slots_version_observer.release();
        }
        // data automatically freed when unique_ptr goes out of scope
        s_registry.erase(it);
    }
}

/** Click callback to open AMS panel */
static void on_click(lv_event_t* e) {
    (void)e;
    spdlog::debug("[AmsMiniStatus] Clicked - opening AMS panel");

    // Get or create the AMS panel and push it as an overlay
    auto& ams_panel = get_global_ams_panel();
    if (!ams_panel.are_subjects_initialized()) {
        ams_panel.init_subjects();
    }
    lv_obj_t* panel_obj = ams_panel.get_panel();
    if (panel_obj) {
        ui_nav_push_overlay(panel_obj);
    }
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
    lv_obj_set_style_pad_column(container, ui_theme_get_spacing("space_xs"), LV_PART_MAIN);

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
    lv_obj_set_style_pad_column(data_ptr->bars_container, ui_theme_get_spacing("space_xxs"),
                                LV_PART_MAIN);
    lv_obj_set_size(data_ptr->bars_container, LV_SIZE_CONTENT, height);

    // Create overflow label (hidden by default) - use responsive font
    data_ptr->overflow_label = lv_label_create(container);
    lv_obj_add_flag(data_ptr->overflow_label, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
    lv_label_set_text(data_ptr->overflow_label, "+0");
    lv_obj_set_style_text_color(data_ptr->overflow_label, ui_theme_get_color("text_secondary"),
                                LV_PART_MAIN);
    const char* font_xs_name = lv_xml_get_const(nullptr, "font_xs");
    const lv_font_t* font_xs =
        font_xs_name ? lv_xml_get_font(nullptr, font_xs_name) : &noto_sans_12;
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

    // Auto-bind to AmsState: observe slot_count and slots_version changes
    // This makes the widget self-updating - no external wiring needed
    // Using observer factory for type-safe lambda observers
    using helix::ui::observe_int_sync;

    lv_subject_t* slot_count_subject = AmsState::instance().get_slot_count_subject();
    if (slot_count_subject) {
        data->slot_count_observer = observe_int_sync<AmsMiniStatusData>(
            slot_count_subject, data,
            [](AmsMiniStatusData* d, int /* count */) { sync_from_ams_state(d); });
        spdlog::debug("[AmsMiniStatus] Auto-bound to AmsState slot_count subject");

        // Sync initial state if AMS already has data
        int current_slot_count = lv_subject_get_int(slot_count_subject);
        if (current_slot_count > 0) {
            sync_from_ams_state(data);
        }
    }

    // Also observe slots_version for status/color changes (not just count changes)
    lv_subject_t* slots_version_subject = AmsState::instance().get_slots_version_subject();
    if (slots_version_subject) {
        data->slots_version_observer = observe_int_sync<AmsMiniStatusData>(
            slots_version_subject, data,
            [](AmsMiniStatusData* d, int /* version */) { sync_from_ams_state(d); });
        spdlog::debug("[AmsMiniStatus] Auto-bound to AmsState slots_version subject");
    }

    spdlog::debug("[AmsMiniStatus] Created (height={})", height);
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

    update_slot_bar(slot);
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

    // Populate each slot from backend slot info
    for (int i = 0; i < slot_count && i < AMS_MINI_STATUS_MAX_VISIBLE; ++i) {
        SlotInfo slot = backend->get_slot_info(i);

        // Calculate fill percentage from weight data
        int fill_pct = 100;
        if (slot.total_weight_g > 0) {
            fill_pct = static_cast<int>((slot.remaining_weight_g / slot.total_weight_g) * 100.0f);
            fill_pct = std::clamp(fill_pct, 0, 100);
        }

        bool present = (slot.status != SlotStatus::EMPTY && slot.status != SlotStatus::UNKNOWN);
        bool loaded = (slot.status == SlotStatus::LOADED);
        bool has_error = (slot.status == SlotStatus::BLOCKED);

        SlotBarData* slot_bar = &data->slots[i];
        slot_bar->color_rgb = slot.color_rgb;
        slot_bar->fill_pct = fill_pct;
        slot_bar->present = present;
        slot_bar->loaded = loaded;
        slot_bar->has_error = has_error;
    }

    rebuild_bars(data);
    spdlog::debug("[AmsMiniStatus] Synced from AmsState: {} slots", slot_count);
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
    lv_obj_set_style_pad_column(container, ui_theme_get_spacing("space_xs"), LV_PART_MAIN);

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
    lv_obj_set_style_pad_column(data_ptr->bars_container, ui_theme_get_spacing("space_xxs"),
                                LV_PART_MAIN);
    // Fill parent height (responsive)
    lv_obj_set_size(data_ptr->bars_container, LV_SIZE_CONTENT, LV_PCT(100));

    // Create overflow label (hidden by default)
    data_ptr->overflow_label = lv_label_create(container);
    lv_obj_add_flag(data_ptr->overflow_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(data_ptr->overflow_label, "+0");
    lv_obj_set_style_text_color(data_ptr->overflow_label, ui_theme_get_color("text_secondary"),
                                LV_PART_MAIN);
    const char* font_xs_name = lv_xml_get_const(nullptr, "font_xs");
    const lv_font_t* font_xs =
        font_xs_name ? lv_xml_get_font(nullptr, font_xs_name) : &noto_sans_12;
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

    // Auto-bind to AmsState
    using helix::ui::observe_int_sync;

    lv_subject_t* slot_count_subject = AmsState::instance().get_slot_count_subject();
    if (slot_count_subject) {
        data->slot_count_observer = observe_int_sync<AmsMiniStatusData>(
            slot_count_subject, data,
            [](AmsMiniStatusData* d, int /* count */) { sync_from_ams_state(d); });

        int current_slot_count = lv_subject_get_int(slot_count_subject);
        if (current_slot_count > 0) {
            sync_from_ams_state(data);
        }
    }

    lv_subject_t* slots_version_subject = AmsState::instance().get_slots_version_subject();
    if (slots_version_subject) {
        data->slots_version_observer = observe_int_sync<AmsMiniStatusData>(
            slots_version_subject, data,
            [](AmsMiniStatusData* d, int /* version */) { sync_from_ams_state(d); });
    }

    spdlog::debug("[AmsMiniStatus] Created via XML (responsive height)");
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
    spdlog::debug("[AmsMiniStatus] Registered ams_mini_status XML widget");
}
