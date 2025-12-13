// Copyright 2025 HelixScreen
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

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

// ============================================================================
// Layout constants
// ============================================================================

/** Minimum fill height in pixels (ensures visibility when present) */
static constexpr int32_t MIN_FILL_HEIGHT_PX = 2;

/** Minimum bar width in pixels (prevents bars from becoming invisible) */
static constexpr int32_t MIN_BAR_WIDTH_PX = 3;

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
    bool has_error = false; // Gate is in error/blocked state
};

/**
 * @brief User data stored on each ams_mini_status widget
 */
struct AmsMiniStatusData {
    uint32_t magic = AMS_MINI_STATUS_MAGIC;
    int32_t height = 32;
    int gate_count = 0;
    int max_visible = AMS_MINI_STATUS_MAX_VISIBLE;

    // Child objects
    lv_obj_t* container = nullptr;      // Main container
    lv_obj_t* bars_container = nullptr; // Container for slot bars
    lv_obj_t* overflow_label = nullptr; // "+N" overflow indicator

    // Per-slot data
    SlotBarData slots[AMS_MINI_STATUS_MAX_VISIBLE];

    // Auto-binding observers (observe AmsState subjects)
    // Uses ObserverGuard for RAII lifecycle management
    ObserverGuard gate_count_observer;    // Triggers when number of gates changes
    ObserverGuard gates_version_observer; // Triggers when gate status/color changes
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
static void on_ams_gate_count_changed(lv_observer_t* observer, lv_subject_t* subject);
static void on_ams_gates_version_changed(lv_observer_t* observer, lv_subject_t* subject);

// ============================================================================
// Internal helpers
// ============================================================================

/** Height of the status indicator line at bottom of slot */
static constexpr int32_t STATUS_LINE_HEIGHT_PX = 3;

/** Gap between filament bar and status line underneath */
static constexpr int32_t STATUS_LINE_GAP_PX = 2;

/** Update a single slot bar's appearance */
static void update_slot_bar(SlotBarData* slot, int32_t bar_height) {
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
        lv_obj_set_style_bg_color(slot->bar_fill, lv_color_hex(slot->color_rgb), LV_PART_MAIN);
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
            // Red - gate is in error/blocked state
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

/** Rebuild the bars based on gate_count and max_visible */
static void rebuild_bars(AmsMiniStatusData* data) {
    if (!data || !data->bars_container)
        return;

    int visible_count = std::min(data->gate_count, data->max_visible);
    int overflow_count = data->gate_count - visible_count;

    // Calculate bar width to fit within parent, leaving room for centering
    // Get parent dimensions first
    lv_obj_t* parent = lv_obj_get_parent(data->container);
    if (parent) {
        lv_obj_update_layout(parent);
    }
    int32_t parent_width = parent ? lv_obj_get_content_width(parent) : 100;

    int32_t gap = ui_theme_get_spacing("space_xxs"); // Responsive 2-4px gap

    // Use 70% of parent width for all bars combined, then divide by bar count
    int32_t total_bar_space = (parent_width * 70) / 100;
    int32_t total_gaps = (visible_count > 1) ? (visible_count - 1) * gap : 0;
    int32_t bar_width = (total_bar_space - total_gaps) / std::max(1, visible_count);
    bar_width = std::max(MIN_BAR_WIDTH_PX, bar_width);

    // Calculate bar height (2/3 of container, minus space for status line + gap)
    int32_t total_slot_height = (data->height * 2) / 3;
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
            update_slot_bar(slot, bar_height);
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
            char buf[8];
            snprintf(buf, sizeof(buf), "+%d", overflow_count);
            lv_label_set_text(data->overflow_label, buf);
            lv_obj_remove_flag(data->overflow_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(data->overflow_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Center the container within its parent
    lv_obj_t* center_parent = lv_obj_get_parent(data->container);
    if (center_parent) {
        lv_obj_update_layout(center_parent);
    }
    lv_obj_center(data->container);

    // Hide entire widget if no gates
    if (data->gate_count <= 0) {
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
        AmsMiniStatusData* data = it->second;
        if (data) {
            // Release observers before delete to prevent destructor from calling
            // lv_observer_remove() on potentially destroyed subjects during shutdown
            data->gate_count_observer.release();
            data->gates_version_observer.release();
        }
        delete data;
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

    // Size to content and center within parent
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(container);

    // Create user data
    auto* data = new AmsMiniStatusData();
    data->height = height;
    data->container = container;

    // Create bars container (holds the slot bars)
    data->bars_container = lv_obj_create(container);
    lv_obj_remove_flag(data->bars_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(data->bars_container, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
    lv_obj_set_style_bg_opa(data->bars_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(data->bars_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(data->bars_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(data->bars_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(data->bars_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(data->bars_container, ui_theme_get_spacing("space_xxs"),
                                LV_PART_MAIN);
    lv_obj_set_size(data->bars_container, LV_SIZE_CONTENT, height);

    // Create overflow label (hidden by default) - use responsive font
    data->overflow_label = lv_label_create(container);
    lv_obj_add_flag(data->overflow_label, LV_OBJ_FLAG_EVENT_BUBBLE); // Pass clicks to parent
    lv_label_set_text(data->overflow_label, "+0");
    lv_obj_set_style_text_color(data->overflow_label, ui_theme_get_color("text_secondary"),
                                LV_PART_MAIN);
    const char* font_xs_name = lv_xml_get_const(nullptr, "font_xs");
    const lv_font_t* font_xs =
        font_xs_name ? lv_xml_get_font(nullptr, font_xs_name) : &noto_sans_12;
    lv_obj_set_style_text_font(data->overflow_label, font_xs, LV_PART_MAIN);
    lv_obj_add_flag(data->overflow_label, LV_OBJ_FLAG_HIDDEN);

    // Register and set up cleanup
    s_registry[container] = data;
    lv_obj_add_event_cb(container, on_delete, LV_EVENT_DELETE, nullptr);

    // Make clickable to open AMS panel
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(container, on_click, LV_EVENT_CLICKED, nullptr);

    // Initially hidden (no gates)
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);

    // Auto-bind to AmsState: observe gate_count and gates_version changes
    // This makes the widget self-updating - no external wiring needed
    lv_subject_t* gate_count_subject = AmsState::instance().get_gate_count_subject();
    if (gate_count_subject) {
        data->gate_count_observer =
            ObserverGuard(gate_count_subject, on_ams_gate_count_changed, container);
        spdlog::debug("[AmsMiniStatus] Auto-bound to AmsState gate_count subject");

        // Sync initial state if AMS already has data
        int current_gate_count = lv_subject_get_int(gate_count_subject);
        if (current_gate_count > 0) {
            sync_from_ams_state(data);
        }
    }

    // Also observe gates_version for status/color changes (not just count changes)
    lv_subject_t* gates_version_subject = AmsState::instance().get_gates_version_subject();
    if (gates_version_subject) {
        data->gates_version_observer =
            ObserverGuard(gates_version_subject, on_ams_gates_version_changed, container);
        spdlog::debug("[AmsMiniStatus] Auto-bound to AmsState gates_version subject");
    }

    spdlog::debug("[AmsMiniStatus] Created (height={})", height);
    return container;
}

void ui_ams_mini_status_set_gate_count(lv_obj_t* obj, int gate_count) {
    auto* data = get_data(obj);
    if (!data)
        return;

    gate_count = std::max(0, gate_count);
    if (data->gate_count == gate_count)
        return;

    data->gate_count = gate_count;
    rebuild_bars(data);

    spdlog::debug("[AmsMiniStatus] gate_count={}", gate_count);
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

    update_slot_bar(slot, data->height);
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
 * Reads gate count and per-gate info from AmsState and updates the widget.
 * Called on initial creation and when gate_count changes.
 */
static void sync_from_ams_state(AmsMiniStatusData* data) {
    if (!data)
        return;

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        // No backend - hide widget
        data->gate_count = 0;
        rebuild_bars(data);
        return;
    }

    int gate_count = lv_subject_get_int(AmsState::instance().get_gate_count_subject());
    data->gate_count = gate_count;

    // Populate each slot from backend gate info
    for (int i = 0; i < gate_count && i < AMS_MINI_STATUS_MAX_VISIBLE; ++i) {
        GateInfo gate = backend->get_gate_info(i);

        // Calculate fill percentage from weight data
        int fill_pct = 100;
        if (gate.total_weight_g > 0) {
            fill_pct = static_cast<int>((gate.remaining_weight_g / gate.total_weight_g) * 100.0f);
            fill_pct = std::clamp(fill_pct, 0, 100);
        }

        bool present = (gate.status != GateStatus::EMPTY && gate.status != GateStatus::UNKNOWN);
        bool loaded = (gate.status == GateStatus::LOADED);
        bool has_error = (gate.status == GateStatus::BLOCKED);

        SlotBarData* slot = &data->slots[i];
        slot->color_rgb = gate.color_rgb;
        slot->fill_pct = fill_pct;
        slot->present = present;
        slot->loaded = loaded;
        slot->has_error = has_error;
    }

    rebuild_bars(data);
    spdlog::debug("[AmsMiniStatus] Synced from AmsState: {} gates", gate_count);
}

/**
 * @brief Observer callback for AmsState gate_count changes
 *
 * Automatically updates the widget when AMS backend reports gate count changes.
 */
static void on_ams_gate_count_changed(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    lv_obj_t* container = static_cast<lv_obj_t*>(lv_observer_get_user_data(observer));
    if (!container)
        return;

    auto* data = get_data(container);
    if (!data)
        return;

    sync_from_ams_state(data);
}

/**
 * @brief Observer callback for AmsState gates_version changes
 *
 * Automatically updates the widget when gate status or color changes.
 * This is separate from gate_count because status changes don't change the count.
 */
static void on_ams_gates_version_changed(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    lv_obj_t* container = static_cast<lv_obj_t*>(lv_observer_get_user_data(observer));
    if (!container)
        return;

    auto* data = get_data(container);
    if (!data)
        return;

    sync_from_ams_state(data);
}
