// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_panel_widgets.cpp
 * @brief Implementation of PanelWidgetsOverlay
 *
 * Dynamically creates toggle rows for each panel widget.
 * Hardware-gated widgets that aren't detected show as disabled with "Not detected".
 */

#include "ui_settings_panel_widgets.h"

#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_toast_manager.h"

#include "config.h"
#include "panel_widget_manager.h"
#include "static_panel_registry.h"
#include "theme_manager.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
namespace helix::settings {

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<PanelWidgetsOverlay> g_panel_widgets_overlay;

PanelWidgetsOverlay& get_panel_widgets_overlay() {
    if (!g_panel_widgets_overlay) {
        g_panel_widgets_overlay = std::make_unique<PanelWidgetsOverlay>();
        StaticPanelRegistry::instance().register_destroy("PanelWidgetsOverlay",
                                                         []() { g_panel_widgets_overlay.reset(); });
    }
    return *g_panel_widgets_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PanelWidgetsOverlay::PanelWidgetsOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

PanelWidgetsOverlay::~PanelWidgetsOverlay() {
    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void PanelWidgetsOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }
    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void PanelWidgetsOverlay::register_callbacks() {
    spdlog::debug("[{}] Callbacks registered", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* PanelWidgetsOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "panel_widgets_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void PanelWidgetsOverlay::show(lv_obj_t* parent_screen) {
    spdlog::debug("[{}] show() called", get_name());

    parent_screen_ = parent_screen;

    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }

    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    NavigationManager::instance().push_overlay(overlay_root_);
}

// ============================================================================
// LIFECYCLE
// ============================================================================

void PanelWidgetsOverlay::on_activate() {
    OverlayBase::on_activate();
    changes_made_ = false;

    // Create fresh config instance and populate toggle list
    widget_config_ = std::make_unique<helix::PanelWidgetConfig>("home", *Config::get_instance());
    widget_config_->load();

    // Auto-disable hardware-gated widgets whose hardware isn't detected.
    // Prevents stale enabled=true entries from counting toward the max widget limit.
    {
        const auto& entries = widget_config_->entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].enabled) {
                continue;
            }
            const auto* def = helix::find_widget_def(entries[i].id);
            if (def && def->hardware_gate_subject) {
                lv_subject_t* gate = lv_xml_get_subject(nullptr, def->hardware_gate_subject);
                if (!gate || lv_subject_get_int(gate) <= 0) {
                    widget_config_->set_enabled(i, false);
                    changes_made_ = true;
                    spdlog::debug("[{}] Auto-disabled '{}' (hardware not detected)", get_name(),
                                  entries[i].id);
                }
            }
        }
    }

    populate_widget_list();
}

void PanelWidgetsOverlay::on_deactivate() {
    OverlayBase::on_deactivate();

    reset_drag_state();
    widget_list_ = nullptr;

    if (changes_made_ && widget_config_) {
        spdlog::info("[{}] Saving widget config changes", get_name());
        widget_config_->save();

        // Notify the widget manager so registered panels rebuild
        helix::PanelWidgetManager::instance().notify_config_changed("home");
    }

    widget_config_.reset();
}

// ============================================================================
// WIDGET LIST POPULATION
// ============================================================================

/// Maximum number of widgets that can be enabled at once
static constexpr size_t MAX_ENABLED_WIDGETS = 10;

/// Static callback for switch toggle events.
static void on_widget_toggle_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PanelWidgetsOverlay] on_widget_toggle_changed");

    auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);

    // Index is stored in the switch's user_data as a size_t cast to void*
    auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(sw));

    if (!get_panel_widgets_overlay().handle_widget_toggled(index, checked)) {
        // Toggle was rejected (e.g. max limit) — revert the switch state
        if (checked) {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

bool PanelWidgetsOverlay::handle_widget_toggled(size_t index, bool enabled) {
    if (!widget_config_) {
        return false;
    }

    // Enforce max enabled widget limit (only count widgets whose hardware is available,
    // since hardware-gated widgets with no detected hardware can't be toggled off)
    if (enabled) {
        size_t enabled_count = 0;
        for (const auto& entry : widget_config_->entries()) {
            if (!entry.enabled) {
                continue;
            }
            const auto* def = helix::find_widget_def(entry.id);
            if (def && def->hardware_gate_subject) {
                lv_subject_t* gate = lv_xml_get_subject(nullptr, def->hardware_gate_subject);
                if (!gate || lv_subject_get_int(gate) <= 0) {
                    continue; // Hardware not detected — don't count
                }
            }
            ++enabled_count;
        }
        if (enabled_count >= MAX_ENABLED_WIDGETS) {
            spdlog::warn("[{}] Cannot enable more than {} widgets", get_name(),
                         MAX_ENABLED_WIDGETS);
            ToastManager::instance().show(ToastSeverity::WARNING,
                                          lv_tr("Maximum of 10 widgets can be enabled at once"),
                                          3000);
            return false;
        }
    }

    widget_config_->set_enabled(index, enabled);
    changes_made_ = true;
    spdlog::debug("[{}] Widget index {} toggled to {}", get_name(), index,
                  enabled ? "enabled" : "disabled");
    return true;
}

void PanelWidgetsOverlay::populate_widget_list() {
    widget_list_ = lv_obj_find_by_name(overlay_root_, "widget_list");
    if (!widget_list_) {
        spdlog::error("[{}] widget_list not found", get_name());
        return;
    }

    lv_obj_clean(widget_list_);
    reset_drag_state();

    if (!widget_config_) {
        return;
    }

    const auto& entries = widget_config_->entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const auto* def = helix::find_widget_def(entry.id);
        if (!def) {
            spdlog::warn("[{}] No widget def for id '{}'", get_name(), entry.id);
            continue;
        }

        create_widget_row(widget_list_, entry, *def, i);
    }

    spdlog::debug("[{}] Populated {} widget rows", get_name(), entries.size());
}

void PanelWidgetsOverlay::create_widget_row(lv_obj_t* parent, const helix::PanelWidgetEntry& entry,
                                            const helix::PanelWidgetDef& def, size_t index) {
    // Check hardware gate (if the widget has one)
    bool hw_available = true;
    if (def.hardware_gate_subject) {
        lv_subject_t* gate_subject = lv_xml_get_subject(nullptr, def.hardware_gate_subject);
        if (gate_subject) {
            int value = lv_subject_get_int(gate_subject);
            hw_available = (value > 0);
        } else {
            hw_available = false;
            spdlog::trace("[{}] Hardware gate subject '{}' not found for '{}'", get_name(),
                          def.hardware_gate_subject, def.id);
        }
    }

    // Build label text — append "(not detected)" when hardware unavailable
    std::string label_text;
    if (hw_available) {
        label_text = lv_tr(def.display_name);
    } else {
        label_text = fmt::format("{} ({})", lv_tr(def.display_name), lv_tr("not detected"));
    }

    const char* icon_variant = hw_available ? "secondary" : "muted";

    // Create row from XML component
    const char* attrs[] = {"label",        label_text.c_str(),
                           "label_tag",    def.translation_tag ? def.translation_tag : "",
                           "description",  lv_tr(def.description),
                           "icon",         def.icon,
                           "icon_variant", icon_variant,
                           nullptr};

    auto* row = static_cast<lv_obj_t*>(lv_xml_create(parent, "panel_widget_row", attrs));
    if (!row) {
        spdlog::error("[{}] Failed to create panel_widget_row for '{}'", get_name(), def.id);
        return;
    }

    // Wire drag events on the row (not in XML — rows are dynamic, each needs unique state)
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, on_drag_handle_event, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_event_cb(row, on_drag_handle_event, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(row, on_drag_handle_event, LV_EVENT_RELEASED, nullptr);

    // Make drag handle non-clickable so it doesn't steal events
    auto* handle = lv_obj_find_by_name(row, "drag_handle");
    if (handle) {
        lv_obj_remove_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    }

    // Configure switch state from runtime data
    auto* sw = lv_obj_find_by_name(row, "toggle");
    if (sw) {
        if (!hw_available) {
            lv_obj_add_state(sw, LV_STATE_DISABLED);
        } else if (entry.enabled) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }

        lv_obj_set_user_data(sw, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(sw, on_widget_toggle_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    }
}

// ============================================================================
// DRAG-TO-REORDER
// ============================================================================

/// Static event callback for row drag interactions. Routes to overlay instance methods.
/// Ignores events originating from the toggle switch to avoid interfering with toggling.
void PanelWidgetsOverlay::on_drag_handle_event(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PanelWidgetsOverlay] on_drag_handle_event");

    auto code = lv_event_get_code(e);
    auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));

    // Ignore events that bubbled up from the toggle switch
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    bool from_switch = (target != row && lv_obj_check_type(target, &lv_switch_class));

    if (!from_switch) {
        auto& overlay = get_panel_widgets_overlay();

        switch (code) {
        case LV_EVENT_LONG_PRESSED:
            overlay.handle_drag_start(row);
            break;
        case LV_EVENT_PRESSING:
            if (overlay.drag_active_) {
                overlay.handle_drag_move();
            }
            break;
        case LV_EVENT_RELEASED:
            if (overlay.drag_active_) {
                overlay.handle_drag_end();
            }
            break;
        default:
            break;
        }
    }

    LVGL_SAFE_EVENT_CB_END();
}

/// Animation exec callback: sets the Y position of the dragged row
static void drag_anim_y_cb(void* var, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t*>(var), value);
}

/// Animation completed callback: finalizes the drag operation
static void drag_anim_completed_cb(lv_anim_t* anim) {
    (void)anim;
    get_panel_widgets_overlay().finalize_drag();
}

void PanelWidgetsOverlay::handle_drag_start(lv_obj_t* row) {
    if (!widget_list_ || !row) {
        return;
    }

    drag_row_ = row;
    drag_from_index_ = lv_obj_get_index(row);

    spdlog::debug("[{}] Drag started at index {}", get_name(), drag_from_index_);

    // Record the row's current screen position and height
    lv_area_t row_coords;
    lv_obj_get_coords(row, &row_coords);
    drag_start_y_ = row_coords.y1;
    drag_row_height_ = row_coords.y2 - row_coords.y1;

    // Compute offset from pointer to row top so the row doesn't jump
    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        drag_offset_y_ = point.y - row_coords.y1;
    } else {
        drag_offset_y_ = drag_row_height_ / 2;
    }

    // Create an invisible placeholder at the row's current index
    drag_placeholder_ = lv_obj_create(widget_list_);
    lv_obj_set_width(drag_placeholder_, LV_PCT(100));
    lv_obj_set_height(drag_placeholder_, drag_row_height_);
    lv_obj_set_style_bg_opa(drag_placeholder_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drag_placeholder_, 0, 0);
    lv_obj_set_style_pad_all(drag_placeholder_, 0, 0);
    lv_obj_remove_flag(drag_placeholder_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(drag_placeholder_, drag_from_index_);

    // Float the row out of flex layout (keeps its current screen position)
    lv_obj_add_flag(row, LV_OBJ_FLAG_FLOATING);

    // Set the row's position relative to parent to match its screen position
    lv_area_t list_coords;
    lv_obj_get_coords(widget_list_, &list_coords);
    lv_obj_set_y(row, drag_start_y_ - list_coords.y1);

    // "Lifted" visual style: shadow + slight opacity
    lv_obj_set_style_shadow_width(row, 12, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_30, 0);
    lv_obj_set_style_shadow_spread(row, 2, 0);
    lv_obj_set_style_shadow_color(row, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(row, theme_manager_get_color("card_bg"), 0);

    // Disable user-input scrolling during drag (auto-scroll handles it programmatically)
    lv_obj_remove_flag(widget_list_, LV_OBJ_FLAG_SCROLLABLE);

    // Start auto-scroll timer (runs every 30ms to scroll when near edges)
    if (!drag_scroll_timer_) {
        drag_scroll_timer_ = lv_timer_create(drag_scroll_timer_cb, 30, this);
    }
    drag_scroll_speed_ = 0;

    drag_active_ = true;
}

void PanelWidgetsOverlay::drag_scroll_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<PanelWidgetsOverlay*>(lv_timer_get_user_data(timer));
    if (!self || !self->drag_active_ || self->drag_scroll_speed_ == 0 || !self->drag_row_ ||
        !self->widget_list_) {
        return;
    }

    // Clamp scroll to content bounds + one row of padding (enough to drop at end)
    int32_t scroll_y = lv_obj_get_scroll_y(self->widget_list_);
    int32_t content_h = lv_obj_get_scroll_bottom(self->widget_list_) + scroll_y +
                        lv_obj_get_height(self->widget_list_);
    int32_t visible_h = lv_obj_get_height(self->widget_list_);
    int32_t max_scroll = content_h - visible_h + self->drag_row_height_;
    if (max_scroll < 0) {
        max_scroll = 0;
    }

    // Don't scroll past bounds
    int32_t new_scroll = scroll_y + self->drag_scroll_speed_;
    if (new_scroll < 0 || (self->drag_scroll_speed_ < 0 && scroll_y <= 0)) {
        self->drag_scroll_speed_ = 0;
        return;
    }
    if (new_scroll > max_scroll || (self->drag_scroll_speed_ > 0 && scroll_y >= max_scroll)) {
        self->drag_scroll_speed_ = 0;
        return;
    }

    // Temporarily re-enable scrolling on widget_list for the programmatic scroll
    lv_obj_add_flag(self->widget_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_by(self->widget_list_, 0, -self->drag_scroll_speed_, LV_ANIM_OFF);
    lv_obj_remove_flag(self->widget_list_, LV_OBJ_FLAG_SCROLLABLE);

    // Trigger a drag move to update row position and placeholder
    self->handle_drag_move();
}

void PanelWidgetsOverlay::update_drag_auto_scroll() {
    if (!drag_active_ || !widget_list_) {
        drag_scroll_speed_ = 0;
        return;
    }

    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        drag_scroll_speed_ = 0;
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Get the visible area of the widget list
    lv_area_t content_coords;
    lv_obj_get_coords(widget_list_, &content_coords);

    // Edge zone: pixels from the edge where scrolling starts
    static constexpr int32_t EDGE_ZONE = 60;
    // Max scroll speed in pixels per timer tick (30ms)
    static constexpr int32_t MAX_SCROLL_SPEED = 16;

    int32_t dist_from_top = point.y - content_coords.y1;
    int32_t dist_from_bottom = content_coords.y2 - point.y;

    if (dist_from_top < EDGE_ZONE && dist_from_top >= 0) {
        // Scroll up — speed proportional to closeness to edge
        drag_scroll_speed_ = -MAX_SCROLL_SPEED * (EDGE_ZONE - dist_from_top) / EDGE_ZONE;
    } else if (dist_from_bottom < EDGE_ZONE && dist_from_bottom >= 0) {
        // Scroll down — speed proportional to closeness to edge
        drag_scroll_speed_ = MAX_SCROLL_SPEED * (EDGE_ZONE - dist_from_bottom) / EDGE_ZONE;
    } else {
        drag_scroll_speed_ = 0;
    }
}

void PanelWidgetsOverlay::handle_drag_move() {
    if (!drag_active_ || !drag_row_ || !widget_list_ || !drag_placeholder_) {
        return;
    }

    // Get the current pointer position (screen coordinates)
    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    // Convert pointer Y to parent-relative Y. Allow half a row height overshoot
    // at top and bottom so the drag center can pass above/below the first/last item.
    lv_area_t list_coords;
    lv_obj_get_coords(widget_list_, &list_coords);
    int32_t new_y = point.y - drag_offset_y_ - list_coords.y1;
    int32_t list_content_h = lv_obj_get_height(widget_list_);
    int32_t overshoot = drag_row_height_ / 2;
    new_y = LV_CLAMP(-overshoot, new_y, list_content_h - drag_row_height_ + overshoot);

    // Move the floating row to track the finger
    lv_obj_set_y(drag_row_, new_y);

    // Determine the center Y of the dragged row in screen coordinates
    int32_t drag_center_y = list_coords.y1 + new_y + drag_row_height_ / 2;

    // Find which index the dragged row center is over (compare against siblings)
    int32_t placeholder_index = lv_obj_get_index(drag_placeholder_);
    uint32_t child_count = lv_obj_get_child_count(widget_list_);

    // Track first and last real rows for edge-case detection
    int32_t first_real_index = -1;
    int32_t first_real_top_y = 0;
    int32_t last_real_index = -1;
    int32_t last_real_bottom_y = 0;

    for (uint32_t i = 0; i < child_count; ++i) {
        lv_obj_t* child = lv_obj_get_child(widget_list_, static_cast<int32_t>(i));
        if (child == drag_row_ || child == drag_placeholder_) {
            continue;
        }

        lv_area_t coords;
        lv_obj_get_coords(child, &coords);
        int32_t child_mid_y = (coords.y1 + coords.y2) / 2;
        int32_t child_index = lv_obj_get_index(child);

        if (first_real_index < 0) {
            first_real_index = child_index;
            first_real_top_y = coords.y1;
        }
        last_real_index = child_index;
        last_real_bottom_y = coords.y2;

        // Move placeholder toward the dragged row's center position
        if ((child_index < placeholder_index && drag_center_y < child_mid_y) ||
            (child_index > placeholder_index && drag_center_y > child_mid_y)) {
            lv_obj_move_to_index(drag_placeholder_, child_index);
            break;
        }
    }

    // If the drag center is above all real rows, move placeholder to the beginning
    if (first_real_index >= 0 && drag_center_y < first_real_top_y &&
        placeholder_index >= first_real_index) {
        int32_t target = (first_real_index > 0) ? first_real_index - 1 : 0;
        lv_obj_move_to_index(drag_placeholder_, target);
    }

    // If the drag center is below all real rows, move placeholder to the end
    if (last_real_index >= 0 && drag_center_y > last_real_bottom_y &&
        placeholder_index <= last_real_index) {
        lv_obj_move_to_index(drag_placeholder_, last_real_index + 1);
    }

    // Update auto-scroll speed based on pointer proximity to edges
    update_drag_auto_scroll();
}

void PanelWidgetsOverlay::handle_drag_end() {
    if (!drag_active_ || !drag_row_ || !widget_list_ || !drag_placeholder_) {
        reset_drag_state();
        return;
    }

    int32_t to_index = lv_obj_get_index(drag_placeholder_);

    // Animate the row to the placeholder's position, then finalize
    lv_area_t placeholder_coords;
    lv_obj_get_coords(drag_placeholder_, &placeholder_coords);

    lv_area_t list_coords;
    lv_obj_get_coords(widget_list_, &list_coords);

    int32_t current_y = lv_obj_get_y(drag_row_);
    int32_t final_y = placeholder_coords.y1 - list_coords.y1;

    spdlog::debug("[{}] Drag ended: {} -> {}", get_name(), drag_from_index_, to_index);

    if (current_y == final_y) {
        finalize_drag();
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, drag_row_);
    lv_anim_set_values(&anim, current_y, final_y);
    lv_anim_set_duration(&anim, 150);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, drag_anim_y_cb);
    lv_anim_set_completed_cb(&anim, drag_anim_completed_cb);
    lv_anim_start(&anim);
}

void PanelWidgetsOverlay::finalize_drag() {
    if (!drag_row_ || !drag_placeholder_ || !widget_list_) {
        reset_drag_state();
        return;
    }

    int32_t to_index = lv_obj_get_index(drag_placeholder_);

    // Re-enable scrollable so lv_obj_get_scroll_y returns the real value
    lv_obj_add_flag(widget_list_, LV_OBJ_FLAG_SCROLLABLE);

    // Remove floating flag so the row returns to flex layout
    lv_obj_remove_flag(drag_row_, LV_OBJ_FLAG_FLOATING);

    // Clear elevated visual styles
    lv_obj_set_style_shadow_width(drag_row_, 0, 0);
    lv_obj_set_style_shadow_opa(drag_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(drag_row_, 0, 0);
    lv_obj_set_style_bg_opa(drag_row_, LV_OPA_TRANSP, 0);

    // Move the row to the placeholder's position BEFORE deleting the placeholder.
    // With placeholder still present, to_index is valid (N+1 children).
    // If we deleted first, to_index could exceed the child count and silently fail.
    lv_obj_move_to_index(drag_row_, to_index);

    // Now delete the placeholder
    lv_obj_delete(drag_placeholder_);
    drag_placeholder_ = nullptr;

    // Clamp config index — placeholder index can exceed entry count when at the end
    int32_t config_to = to_index;
    int32_t max_config_index =
        widget_config_ ? static_cast<int32_t>(widget_config_->entries().size()) - 1 : 0;
    if (config_to > max_config_index) {
        config_to = max_config_index;
    }

    // Apply the reorder to the config if position changed
    if (drag_from_index_ >= 0 && config_to != drag_from_index_ && widget_config_) {
        widget_config_->reorder(static_cast<size_t>(drag_from_index_),
                                static_cast<size_t>(config_to));
        changes_made_ = true;
        spdlog::info("[{}] Widget reordered: {} -> {}", get_name(), drag_from_index_, config_to);

        // Tell reset_drag_state NOT to re-enable SCROLLABLE — we handle it after clamping
        skip_scroll_restore_ = true;
        reset_drag_state();

        // Force layout pass, then clamp scroll to content bounds (placeholder removal
        // may have reduced content height, leaving us over-scrolled)
        lv_obj_update_layout(widget_list_);
        int32_t scroll_bottom_now = lv_obj_get_scroll_bottom(widget_list_);
        if (scroll_bottom_now < 0) {
            int32_t scroll_now = lv_obj_get_scroll_y(widget_list_);
            int32_t clamped = scroll_now + scroll_bottom_now;
            if (clamped < 0)
                clamped = 0;
            lv_obj_scroll_to_y(widget_list_, clamped, LV_ANIM_OFF);
        }

        // Re-enable SCROLLABLE after clamping so LVGL doesn't see over-scroll
        lv_obj_add_flag(widget_list_, LV_OBJ_FLAG_SCROLLABLE);
        lv_anim_delete(widget_list_, nullptr);

        // Update each row's switch user_data to reflect the new config order
        uint32_t child_count = lv_obj_get_child_count(widget_list_);
        for (uint32_t i = 0; i < child_count; ++i) {
            lv_obj_t* row = lv_obj_get_child(widget_list_, static_cast<int32_t>(i));
            lv_obj_t* sw = lv_obj_find_by_name(row, "toggle");
            if (sw) {
                lv_obj_set_user_data(sw, reinterpret_cast<void*>(static_cast<size_t>(i)));
            }
        }
        return;
    }

    reset_drag_state();
}

void PanelWidgetsOverlay::reset_drag_state() {
    // Clean up floating flag and elevated styles if row still exists
    if (drag_row_) {
        lv_obj_remove_flag(drag_row_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_style_shadow_width(drag_row_, 0, 0);
        lv_obj_set_style_shadow_opa(drag_row_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_spread(drag_row_, 0, 0);
        lv_obj_set_style_bg_opa(drag_row_, LV_OPA_TRANSP, 0);
    }

    // Delete placeholder if it still exists
    if (drag_placeholder_) {
        lv_obj_delete(drag_placeholder_);
        drag_placeholder_ = nullptr;
    }

    // Cancel any in-progress animation on the drag row
    if (drag_row_) {
        lv_anim_delete(drag_row_, drag_anim_y_cb);
    }

    // Re-enable scrolling unless caller will handle it (finalize_drag reorder path)
    if (widget_list_ && !skip_scroll_restore_) {
        lv_obj_add_flag(widget_list_, LV_OBJ_FLAG_SCROLLABLE);
    }
    skip_scroll_restore_ = false;

    // Stop auto-scroll timer
    if (drag_scroll_timer_) {
        lv_timer_delete(drag_scroll_timer_);
        drag_scroll_timer_ = nullptr;
    }
    drag_scroll_speed_ = 0;

    drag_active_ = false;
    drag_from_index_ = -1;
    drag_row_ = nullptr;
    drag_start_y_ = 0;
    drag_row_height_ = 0;
    drag_offset_y_ = 0;
}

} // namespace helix::settings
