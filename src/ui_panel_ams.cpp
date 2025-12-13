// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_ams.h"

#include "ui_ams_slot.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_filament_path_canvas.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_panel_common.h"
#include "ui_spool_canvas.h"
#include "ui_theme.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

// Global instance pointer for XML callback access
static AmsPanel* g_ams_panel_instance = nullptr;

// Lazy registration flag - widgets and XML registered on first use
static bool s_ams_widgets_registered = false;

/**
 * @brief Register AMS widgets and XML component (lazy, called once on first use)
 *
 * Registers:
 * - spool_canvas: 3D filament spool visualization widget
 * - ams_slot: Individual slot widget with spool and status
 * - filament_path_canvas: Filament routing visualization
 * - ams_panel.xml: Main panel component
 * - ams_context_menu.xml: Slot context menu component
 */
static void ensure_ams_widgets_registered() {
    if (s_ams_widgets_registered) {
        return;
    }

    spdlog::info("[AMS Panel] Lazy-registering AMS widgets and XML components");

    // Register custom widgets (order matters - dependencies first)
    ui_spool_canvas_register();
    ui_ams_slot_register();
    ui_filament_path_canvas_register();

    // Register XML components
    lv_xml_register_component_from_file("A:ui_xml/ams_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_context_menu.xml");

    s_ams_widgets_registered = true;
    spdlog::debug("[AMS Panel] Widget and XML registration complete");
}

// ============================================================================
// XML Event Callback Wrappers (for <event_cb> elements in XML)
// ============================================================================

static void on_unload_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    if (g_ams_panel_instance) {
        g_ams_panel_instance->handle_unload();
    }
}

static void on_reset_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    if (g_ams_panel_instance) {
        g_ams_panel_instance->handle_reset();
    }
}

static void on_bypass_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    if (g_ams_panel_instance) {
        g_ams_panel_instance->handle_bypass_toggle();
    }
}

// ============================================================================
// Construction
// ============================================================================

AmsPanel::AmsPanel(PrinterState& printer_state, MoonrakerAPI* api) : PanelBase(printer_state, api) {
    spdlog::debug("[AmsPanel] Constructed");
}

// ============================================================================
// PanelBase Interface
// ============================================================================

void AmsPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // AmsState handles all subject registration centrally
    // We just ensure it's initialized before panel creation
    AmsState::instance().init_subjects(true);

    // NOTE: Backend creation is handled by:
    // - main.cpp (mock mode at startup)
    // - AmsState::init_backend_from_capabilities() (real printer connection)
    // Panel should NOT create backends - it just observes the existing one.

    // Register observers for state changes
    gates_version_observer_ = ObserverGuard(AmsState::instance().get_gates_version_subject(),
                                            on_gates_version_changed, this);

    action_observer_ =
        ObserverGuard(AmsState::instance().get_ams_action_subject(), on_action_changed, this);

    current_gate_observer_ = ObserverGuard(AmsState::instance().get_current_gate_subject(),
                                           on_current_gate_changed, this);

    // Gate count observer for dynamic slot creation
    gate_count_observer_ =
        ObserverGuard(AmsState::instance().get_gate_count_subject(), on_gate_count_changed, this);

    // Path state observers for filament path visualization
    path_segment_observer_ = ObserverGuard(AmsState::instance().get_path_filament_segment_subject(),
                                           on_path_state_changed, this);
    path_topology_observer_ = ObserverGuard(AmsState::instance().get_path_topology_subject(),
                                            on_path_state_changed, this);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized via AmsState + observers registered", get_name());
}

void AmsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    // Call base class to store panel_ and parent_screen_
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    spdlog::debug("[{}] Setting up...", get_name());

    // Use standard overlay panel setup (header bar, responsive padding)
    ui_overlay_panel_setup_standard(panel_, parent_screen_, "overlay_header", "overlay_content");

    // Setup UI components
    setup_slots();
    setup_action_buttons();
    setup_status_display();
    setup_path_canvas();

    // Initial UI sync from backend state
    refresh_slots();

    spdlog::debug("[{}] Setup complete!", get_name());
}

void AmsPanel::on_activate() {
    spdlog::debug("[{}] Activated - syncing from backend", get_name());

    // Sync state when panel becomes visible
    AmsState::instance().sync_from_backend();
    refresh_slots();
}

void AmsPanel::on_deactivate() {
    spdlog::debug("[{}] Deactivated", get_name());
    // Note: UI destruction is handled by NavigationManager close callback
    // registered in get_global_ams_panel()
}

void AmsPanel::clear_panel_reference() {
    // Clear all widget references before LVGL object deletion
    panel_ = nullptr;
    parent_screen_ = nullptr;
    slot_grid_ = nullptr;
    path_canvas_ = nullptr;
    context_menu_ = nullptr;
    context_menu_slot_ = -1;
    current_slot_count_ = 0;

    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        slot_widgets_[i] = nullptr;
    }

    // Clear observer guards (they reference deleted widgets)
    gates_version_observer_.reset();
    action_observer_.reset();
    current_gate_observer_.reset();
    gate_count_observer_.reset();
    path_segment_observer_.reset();
    path_topology_observer_.reset();

    // Reset subjects_initialized_ so observers are recreated on next access
    subjects_initialized_ = false;

    // Clear global instance pointer to prevent callbacks from using stale pointer
    g_ams_panel_instance = nullptr;

    spdlog::debug("[AMS Panel] Cleared all widget references");
}

// ============================================================================
// Setup Helpers
// ============================================================================

void AmsPanel::setup_slots() {
    slot_grid_ = lv_obj_find_by_name(panel_, "slot_grid");
    if (!slot_grid_) {
        spdlog::warn("[{}] slot_grid not found in XML", get_name());
        return;
    }

    // Get initial gate count and create slots
    int gate_count = lv_subject_get_int(AmsState::instance().get_gate_count_subject());
    create_slots(gate_count);
}

void AmsPanel::create_slots(int count) {
    if (!slot_grid_) {
        return;
    }

    // Clamp to reasonable range
    if (count < 0) {
        count = 0;
    }
    if (count > MAX_VISIBLE_SLOTS) {
        spdlog::warn("[{}] Clamping gate_count {} to max {}", get_name(), count, MAX_VISIBLE_SLOTS);
        count = MAX_VISIBLE_SLOTS;
    }

    // Skip if unchanged
    if (count == current_slot_count_) {
        return;
    }

    spdlog::debug("[{}] Creating {} slots (was {})", get_name(), count, current_slot_count_);

    // Delete existing slots
    for (int i = 0; i < current_slot_count_; ++i) {
        if (slot_widgets_[i]) {
            lv_obj_delete(slot_widgets_[i]);
            slot_widgets_[i] = nullptr;
        }
    }

    // Create new slots via XML system (widget handles its own sizing/appearance)
    for (int i = 0; i < count; ++i) {
        lv_obj_t* slot = static_cast<lv_obj_t*>(lv_xml_create(slot_grid_, "ams_slot", nullptr));
        if (!slot) {
            spdlog::error("[{}] Failed to create ams_slot for index {}", get_name(), i);
            continue;
        }

        // Configure slot index (triggers reactive binding setup)
        ui_ams_slot_set_index(slot, i);

        // Store reference and setup click handler
        slot_widgets_[i] = slot;
        lv_obj_set_user_data(slot, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(slot, on_slot_clicked, LV_EVENT_CLICKED, this);
    }

    current_slot_count_ = count;
    spdlog::info("[{}] Created {} slot widgets", get_name(), count);
}

void AmsPanel::on_gate_count_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self || !self->panel_) {
        return;
    }

    int new_count = lv_subject_get_int(subject);
    spdlog::debug("[AmsPanel] Gate count changed to {}", new_count);
    self->create_slots(new_count);
}

void AmsPanel::setup_action_buttons() {
    // Register XML event callbacks for buttons
    // These callbacks are referenced in ams_panel.xml via <event_cb> elements
    lv_xml_register_event_cb(nullptr, "ams_unload_clicked_cb", on_unload_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_reset_clicked_cb", on_reset_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_bypass_clicked_cb", on_bypass_clicked_xml);

    // Store panel pointer for static callbacks to access
    g_ams_panel_instance = this;

    // Show/hide bypass button based on backend support
    update_bypass_button_visibility();

    spdlog::debug("[{}] Action button callbacks registered", get_name());
}

void AmsPanel::setup_status_display() {
    // Status display is handled reactively via bind_text in XML
    // Just verify the elements exist
    lv_obj_t* status_label = lv_obj_find_by_name(panel_, "status_label");
    if (status_label) {
        spdlog::debug("[{}] Status label found - bound to ams_action_detail", get_name());
    }
}

void AmsPanel::setup_path_canvas() {
    path_canvas_ = lv_obj_find_by_name(panel_, "path_canvas");
    if (!path_canvas_) {
        spdlog::warn("[{}] path_canvas not found in XML", get_name());
        return;
    }

    // Set gate click callback to trigger filament load
    ui_filament_path_canvas_set_gate_callback(path_canvas_, on_path_gate_clicked, this);

    // Initial configuration from backend
    update_path_canvas_from_backend();

    spdlog::debug("[{}] Path canvas setup complete", get_name());
}

void AmsPanel::update_path_canvas_from_backend() {
    if (!path_canvas_) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    // Get system info for gate count and topology
    AmsSystemInfo info = backend->get_system_info();

    // Set gate count from backend
    ui_filament_path_canvas_set_gate_count(path_canvas_, info.total_gates);

    // Set topology from backend
    PathTopology topology = backend->get_topology();
    ui_filament_path_canvas_set_topology(path_canvas_, static_cast<int>(topology));

    // Set active gate
    ui_filament_path_canvas_set_active_gate(path_canvas_, info.current_gate);

    // Set filament segment position
    PathSegment segment = backend->get_filament_segment();
    ui_filament_path_canvas_set_filament_segment(path_canvas_, static_cast<int>(segment));

    // Set error segment if any
    PathSegment error_seg = backend->infer_error_segment();
    ui_filament_path_canvas_set_error_segment(path_canvas_, static_cast<int>(error_seg));

    // Set filament color from current gate's filament
    if (info.current_gate >= 0) {
        GateInfo gate_info = backend->get_gate_info(info.current_gate);
        ui_filament_path_canvas_set_filament_color(path_canvas_, gate_info.color_rgb);
    }

    spdlog::trace("[{}] Path canvas updated: gates={}, topology={}, active={}, segment={}",
                  get_name(), info.total_gates, static_cast<int>(topology), info.current_gate,
                  static_cast<int>(segment));
}

// ============================================================================
// Public API
// ============================================================================

void AmsPanel::refresh_slots() {
    if (!panel_ || !subjects_initialized_) {
        return;
    }

    update_slot_colors();

    // Update current gate highlight
    int current_gate = lv_subject_get_int(AmsState::instance().get_current_gate_subject());
    update_current_gate_highlight(current_gate);
}

// ============================================================================
// UI Update Handlers
// ============================================================================

void AmsPanel::update_slot_colors() {
    int gate_count = lv_subject_get_int(AmsState::instance().get_gate_count_subject());
    AmsBackend* backend = AmsState::instance().get_backend();

    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        if (!slot_widgets_[i]) {
            continue;
        }

        if (i >= gate_count) {
            // Hide slots beyond configured count
            lv_obj_add_flag(slot_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(slot_widgets_[i], LV_OBJ_FLAG_HIDDEN);

        // Get gate color from AmsState subject
        lv_subject_t* color_subject = AmsState::instance().get_gate_color_subject(i);
        if (color_subject) {
            uint32_t rgb = static_cast<uint32_t>(lv_subject_get_int(color_subject));
            lv_color_t color = lv_color_hex(rgb);

            // Find color swatch within slot
            lv_obj_t* swatch = lv_obj_find_by_name(slot_widgets_[i], "color_swatch");
            if (swatch) {
                lv_obj_set_style_bg_color(swatch, color, 0);
            }
        }

        // Update material label and fill level from backend gate info
        if (backend) {
            GateInfo gate_info = backend->get_gate_info(i);
            lv_obj_t* material_label = lv_obj_find_by_name(slot_widgets_[i], "material_label");
            if (material_label) {
                if (!gate_info.material.empty()) {
                    lv_label_set_text(material_label, gate_info.material.c_str());
                } else {
                    lv_label_set_text(material_label, "---");
                }
            }

            // Set fill level from Spoolman weight data
            if (gate_info.total_weight_g > 0.0f) {
                float fill_level = gate_info.remaining_weight_g / gate_info.total_weight_g;
                ui_ams_slot_set_fill_level(slot_widgets_[i], fill_level);
            }
        }

        // Update status indicator
        update_slot_status(i);
    }
}

void AmsPanel::update_slot_status(int gate_index) {
    if (gate_index < 0 || gate_index >= MAX_VISIBLE_SLOTS || !slot_widgets_[gate_index]) {
        return;
    }

    lv_subject_t* status_subject = AmsState::instance().get_gate_status_subject(gate_index);
    if (!status_subject) {
        return;
    }

    auto status = static_cast<GateStatus>(lv_subject_get_int(status_subject));

    // Find status indicator icon within slot
    lv_obj_t* status_icon = lv_obj_find_by_name(slot_widgets_[gate_index], "status_icon");
    if (!status_icon) {
        return;
    }

    // Update icon based on status
    switch (status) {
    case GateStatus::EMPTY:
        // Show empty indicator
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_30, 0);
        break;

    case GateStatus::AVAILABLE:
    case GateStatus::FROM_BUFFER:
        // Show filament available
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case GateStatus::LOADED:
        // Show loaded (highlighted)
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case GateStatus::BLOCKED:
        // Show error state
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case GateStatus::UNKNOWN:
    default:
        lv_obj_add_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void AmsPanel::update_action_display(AmsAction action) {
    // Action display is handled via bind_text to ams_action_detail
    // This method can add visual feedback (progress indicators, etc.)

    lv_obj_t* progress = lv_obj_find_by_name(panel_, "action_progress");
    if (!progress) {
        return;
    }

    bool show_progress = (action == AmsAction::LOADING || action == AmsAction::UNLOADING ||
                          action == AmsAction::SELECTING || action == AmsAction::RESETTING);

    if (show_progress) {
        lv_obj_remove_flag(progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(progress, LV_OBJ_FLAG_HIDDEN);
    }
}

void AmsPanel::update_current_gate_highlight(int gate_index) {
    // Remove highlight from all slots (set border opacity to 0)
    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        if (slot_widgets_[i]) {
            lv_obj_remove_state(slot_widgets_[i], LV_STATE_CHECKED);
            lv_obj_set_style_border_opa(slot_widgets_[i], LV_OPA_0, 0);
        }
    }

    // Add highlight to current gate (show border)
    if (gate_index >= 0 && gate_index < MAX_VISIBLE_SLOTS && slot_widgets_[gate_index]) {
        lv_obj_add_state(slot_widgets_[gate_index], LV_STATE_CHECKED);
        lv_obj_set_style_border_opa(slot_widgets_[gate_index], LV_OPA_100, 0);
    }

    // Update the "Currently Loaded" card in the right column
    update_current_loaded_display(gate_index);
}

void AmsPanel::update_current_loaded_display(int gate_index) {
    if (!panel_) {
        return;
    }

    // Find the "Currently Loaded" card elements
    lv_obj_t* current_swatch = lv_obj_find_by_name(panel_, "current_swatch");
    lv_obj_t* current_material = lv_obj_find_by_name(panel_, "current_material");
    lv_obj_t* current_slot_label = lv_obj_find_by_name(panel_, "current_slot_label");

    AmsBackend* backend = AmsState::instance().get_backend();
    bool filament_loaded =
        lv_subject_get_int(AmsState::instance().get_filament_loaded_subject()) != 0;

    // Check for bypass mode (gate_index == -2)
    if (gate_index == -2 && backend && backend->is_bypass_active()) {
        // Bypass mode active - show bypass state
        if (current_swatch) {
            lv_obj_set_style_bg_color(current_swatch, lv_color_hex(0x888888), 0);
            lv_obj_set_style_border_color(current_swatch, lv_color_hex(0x888888), 0);
        }

        if (current_material) {
            lv_label_set_text(current_material, "External");
        }

        if (current_slot_label) {
            lv_label_set_text(current_slot_label, "Bypass");
        }

        // Update bypass button state
        update_bypass_button_state();

        // Update path canvas bypass state
        if (path_canvas_) {
            ui_filament_path_canvas_set_bypass_active(path_canvas_, true);
        }
    } else if (gate_index >= 0 && filament_loaded && backend) {
        // Filament is loaded - show the loaded gate info
        GateInfo gate_info = backend->get_gate_info(gate_index);

        // Set swatch color
        if (current_swatch) {
            lv_color_t color = lv_color_hex(gate_info.color_rgb);
            lv_obj_set_style_bg_color(current_swatch, color, 0);
            lv_obj_set_style_border_color(current_swatch, color, 0);
        }

        // Set material name
        if (current_material) {
            if (!gate_info.material.empty()) {
                lv_label_set_text(current_material, gate_info.material.c_str());
            } else {
                lv_label_set_text(current_material, "Filament");
            }
        }

        // Set slot label (1-based for user display)
        if (current_slot_label) {
            char buf[16];
            snprintf(buf, sizeof(buf), "Slot %d", gate_index + 1);
            lv_label_set_text(current_slot_label, buf);
        }

        // Clear bypass state on path canvas
        if (path_canvas_) {
            ui_filament_path_canvas_set_bypass_active(path_canvas_, false);
        }
    } else {
        // No filament loaded - show empty state
        if (current_swatch) {
            lv_obj_set_style_bg_color(current_swatch, lv_color_hex(0x505050), 0);
            lv_obj_set_style_border_color(current_swatch, lv_color_hex(0x505050), 0);
        }

        if (current_material) {
            lv_label_set_text(current_material, "---");
        }

        if (current_slot_label) {
            lv_label_set_text(current_slot_label, "None");
        }

        // Clear bypass state on path canvas
        if (path_canvas_) {
            ui_filament_path_canvas_set_bypass_active(path_canvas_, false);
        }
    }
}

// ============================================================================
// Event Callbacks
// ============================================================================

void AmsPanel::on_path_gate_clicked(int gate_index, void* user_data) {
    auto* self = static_cast<AmsPanel*>(user_data);
    if (!self) {
        return;
    }

    spdlog::info("[AmsPanel] Path gate {} clicked - triggering load", gate_index);

    // Trigger filament load for the clicked gate
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Check if backend is busy
    AmsSystemInfo info = backend->get_system_info();
    if (info.action != AmsAction::IDLE && info.action != AmsAction::ERROR) {
        NOTIFY_WARNING("AMS is busy: {}", ams_action_to_string(info.action));
        return;
    }

    AmsError error = backend->load_filament(gate_index);
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Load failed: {}", error.user_msg);
    }
}

void AmsPanel::on_slot_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_slot_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        lv_obj_t* slot = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto slot_index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(slot)));
        self->handle_slot_tap(slot_index);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_unload_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_unload_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_unload();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_reset_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_reset_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_reset();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Observer Callbacks
// ============================================================================

void AmsPanel::on_gates_version_changed(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    spdlog::debug("[AmsPanel] Gates version changed - refreshing slots");
    self->refresh_slots();
}

void AmsPanel::on_action_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    auto action = static_cast<AmsAction>(lv_subject_get_int(subject));
    spdlog::debug("[AmsPanel] Action changed: {}", ams_action_to_string(action));
    self->update_action_display(action);
}

void AmsPanel::on_current_gate_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    int gate = lv_subject_get_int(subject);
    spdlog::debug("[AmsPanel] Current gate changed: {}", gate);
    self->update_current_gate_highlight(gate);

    // Also update path canvas when current gate changes
    self->update_path_canvas_from_backend();
}

void AmsPanel::on_path_state_changed(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    spdlog::debug("[AmsPanel] Path state changed - updating path canvas");
    self->update_path_canvas_from_backend();
}

// ============================================================================
// Action Handlers
// ============================================================================

void AmsPanel::handle_slot_tap(int slot_index) {
    spdlog::info("[{}] Slot {} tapped", get_name(), slot_index);

    // Validate slot index against configured gate count
    int gate_count = lv_subject_get_int(AmsState::instance().get_gate_count_subject());
    if (slot_index < 0 || slot_index >= gate_count) {
        spdlog::warn("[{}] Invalid slot index {} (gate_count={})", get_name(), slot_index,
                     gate_count);
        return;
    }

    // Show context menu near the tapped slot
    if (slot_index >= 0 && slot_index < MAX_VISIBLE_SLOTS && slot_widgets_[slot_index]) {
        show_context_menu(slot_index, slot_widgets_[slot_index]);
    }
}

void AmsPanel::handle_unload() {
    spdlog::info("[{}] Unload requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    AmsError error = backend->unload_filament();
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Unload failed: {}", error.user_msg);
    }
}

void AmsPanel::handle_reset() {
    spdlog::info("[{}] Reset requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    AmsError error = backend->reset();
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Reset failed: {}", error.user_msg);
    }
}

void AmsPanel::handle_bypass_toggle() {
    spdlog::info("[{}] Bypass toggle requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Check if hardware sensor controls bypass (button should be disabled, but check anyway)
    AmsSystemInfo info = backend->get_system_info();
    if (info.has_hardware_bypass_sensor) {
        NOTIFY_WARNING("Bypass controlled by sensor");
        spdlog::warn("[{}] Bypass toggle blocked - hardware sensor controls bypass", get_name());
        return;
    }

    // Check current bypass state and toggle
    bool currently_bypassed = backend->is_bypass_active();
    AmsError error;

    if (currently_bypassed) {
        error = backend->disable_bypass();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO("Bypass disabled");
        }
    } else {
        error = backend->enable_bypass();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO("Bypass enabled");
        }
    }

    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Bypass toggle failed: {}", error.user_msg);
    }

    // Update button label
    update_bypass_button_state();
}

void AmsPanel::update_bypass_button_visibility() {
    if (!panel_) {
        spdlog::debug("[{}] update_bypass_button_visibility: panel_ is null", get_name());
        return;
    }

    lv_obj_t* btn_bypass = lv_obj_find_by_name(panel_, "btn_bypass");
    if (!btn_bypass) {
        spdlog::debug("[{}] update_bypass_button_visibility: btn_bypass not found", get_name());
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        AmsSystemInfo info = backend->get_system_info();
        spdlog::debug(
            "[{}] update_bypass_button_visibility: supports_bypass={}, has_hardware_sensor={}",
            get_name(), info.supports_bypass, info.has_hardware_bypass_sensor);
        if (info.supports_bypass) {
            lv_obj_remove_flag(btn_bypass, LV_OBJ_FLAG_HIDDEN);

            // Disable button if hardware sensor controls bypass (auto-detect mode)
            if (info.has_hardware_bypass_sensor) {
                lv_obj_add_state(btn_bypass, LV_STATE_DISABLED);
                spdlog::info("[{}] Bypass button disabled (hardware sensor controls bypass)",
                             get_name());
            } else {
                lv_obj_remove_state(btn_bypass, LV_STATE_DISABLED);
            }

            // Force parent layout update to make button visible
            lv_obj_t* parent = lv_obj_get_parent(btn_bypass);
            if (parent) {
                lv_obj_invalidate(parent);
                lv_obj_update_layout(parent);
            }
            update_bypass_button_state();
            spdlog::info("[{}] Bypass button shown (backend supports bypass)", get_name());
        } else {
            lv_obj_add_flag(btn_bypass, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        spdlog::debug("[{}] update_bypass_button_visibility: no backend", get_name());
        lv_obj_add_flag(btn_bypass, LV_OBJ_FLAG_HIDDEN);
    }
}

void AmsPanel::update_bypass_button_state() {
    if (!panel_) {
        return;
    }

    lv_obj_t* bypass_label = lv_obj_find_by_name(panel_, "bypass_label");
    if (!bypass_label) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    AmsSystemInfo info = backend->get_system_info();
    bool bypass_active = backend->is_bypass_active();

    // Different text based on hardware sensor vs virtual bypass
    if (info.has_hardware_bypass_sensor) {
        // Hardware sensor mode - show current state (button is disabled)
        if (bypass_active) {
            lv_label_set_text(bypass_label, "Bypass Active");
        } else {
            lv_label_set_text(bypass_label, "Bypass Inactive");
        }
    } else {
        // Virtual bypass mode - show action (button is clickable)
        if (bypass_active) {
            lv_label_set_text(bypass_label, "Disable Bypass");
        } else {
            lv_label_set_text(bypass_label, "Enable Bypass");
        }
    }
}

void AmsPanel::handle_context_load() {
    if (context_menu_slot_ < 0) {
        return;
    }

    // Capture slot before hiding menu (hide_context_menu resets context_menu_slot_)
    int slot_to_load = context_menu_slot_;
    spdlog::info("[{}] Context menu: Load from slot {}", get_name(), slot_to_load);
    hide_context_menu();

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Check if backend is busy
    AmsSystemInfo info = backend->get_system_info();
    if (info.action != AmsAction::IDLE && info.action != AmsAction::ERROR) {
        NOTIFY_WARNING("AMS is busy: {}", ams_action_to_string(info.action));
        return;
    }

    AmsError error = backend->load_filament(slot_to_load);
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Load failed: {}", error.user_msg);
    }
}

void AmsPanel::handle_context_unload() {
    if (context_menu_slot_ < 0) {
        return;
    }

    spdlog::info("[{}] Context menu: Unload slot {}", get_name(), context_menu_slot_);
    hide_context_menu();

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    AmsError error = backend->unload_filament();
    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Unload failed: {}", error.user_msg);
    }
}

void AmsPanel::handle_context_edit() {
    if (context_menu_slot_ < 0) {
        return;
    }

    spdlog::info("[{}] Context menu: Edit slot {}", get_name(), context_menu_slot_);
    hide_context_menu();

    // TODO: Phase 3 - Open edit modal with Spoolman integration
    NOTIFY_INFO("Edit feature coming in Phase 3");
}

// ============================================================================
// Context Menu Management
// ============================================================================

void AmsPanel::show_context_menu(int slot_index, lv_obj_t* near_widget) {
    // Hide any existing context menu first
    hide_context_menu();

    if (!parent_screen_ || !near_widget) {
        return;
    }

    // Store which slot the menu is for
    context_menu_slot_ = slot_index;

    // Create context menu from XML component
    context_menu_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "ams_context_menu", nullptr));
    if (!context_menu_) {
        spdlog::error("[{}] Failed to create context menu", get_name());
        return;
    }

    // Find and wire up callbacks
    lv_obj_t* backdrop = lv_obj_find_by_name(context_menu_, "context_backdrop");
    lv_obj_t* menu_card = lv_obj_find_by_name(context_menu_, "context_menu");
    lv_obj_t* btn_load = lv_obj_find_by_name(context_menu_, "btn_load");
    lv_obj_t* btn_unload = lv_obj_find_by_name(context_menu_, "btn_unload");
    lv_obj_t* btn_edit = lv_obj_find_by_name(context_menu_, "btn_edit");

    if (backdrop) {
        lv_obj_add_event_cb(backdrop, on_context_backdrop_clicked, LV_EVENT_CLICKED, this);
    }
    if (btn_load) {
        lv_obj_add_event_cb(btn_load, on_context_load_clicked, LV_EVENT_CLICKED, this);
    }
    if (btn_unload) {
        lv_obj_add_event_cb(btn_unload, on_context_unload_clicked, LV_EVENT_CLICKED, this);
    }
    if (btn_edit) {
        lv_obj_add_event_cb(btn_edit, on_context_edit_clicked, LV_EVENT_CLICKED, this);
    }

    // Position the menu card near the tapped widget
    if (menu_card) {
        // Update layout to get accurate dimensions before positioning
        lv_obj_update_layout(menu_card);

        // Get the position of the slot widget in screen coordinates
        lv_point_t slot_pos;
        lv_obj_get_coords(near_widget, (lv_area_t*)&slot_pos);

        // Position menu to the right of the slot, or left if near edge
        int32_t screen_width = lv_obj_get_width(parent_screen_);
        int32_t menu_width = lv_obj_get_width(menu_card);
        int32_t slot_center_x = slot_pos.x + lv_obj_get_width(near_widget) / 2;
        int32_t slot_center_y = slot_pos.y + lv_obj_get_height(near_widget) / 2;

        int32_t menu_x = slot_center_x + 20; // Position to the right
        if (menu_x + menu_width > screen_width - 10) {
            menu_x = slot_center_x - menu_width - 20; // Position to the left instead
        }

        // Vertical: center on the slot
        int32_t menu_y = slot_center_y - lv_obj_get_height(menu_card) / 2;

        // Clamp to screen bounds
        int32_t screen_height = lv_obj_get_height(parent_screen_);
        if (menu_y < 10)
            menu_y = 10;
        if (menu_y + lv_obj_get_height(menu_card) > screen_height - 10) {
            menu_y = screen_height - lv_obj_get_height(menu_card) - 10;
        }

        lv_obj_set_pos(menu_card, menu_x, menu_y);
    }

    spdlog::debug("[{}] Context menu shown for slot {}", get_name(), slot_index);
}

void AmsPanel::hide_context_menu() {
    if (context_menu_) {
        lv_obj_delete(context_menu_);
        context_menu_ = nullptr;
        context_menu_slot_ = -1;
        spdlog::debug("[{}] Context menu hidden", get_name());
    }
}

// ============================================================================
// Context Menu Callbacks
// ============================================================================

void AmsPanel::on_context_backdrop_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_context_backdrop_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->hide_context_menu();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_context_load_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_context_load_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_context_load();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_context_unload_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_context_unload_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_context_unload();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void AmsPanel::on_context_edit_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_context_edit_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->handle_context_edit();
    }
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<AmsPanel> g_ams_panel;
static lv_obj_t* s_ams_panel_obj = nullptr;

void destroy_ams_panel_ui() {
    if (s_ams_panel_obj) {
        spdlog::info("[AMS Panel] Destroying panel UI to free memory");

        // Unregister close callback BEFORE deleting to prevent double-invocation
        // (e.g., if destroy called manually while panel is in overlay stack)
        NavigationManager::instance().unregister_overlay_close_callback(s_ams_panel_obj);

        // Clear the panel_ reference in AmsPanel before deleting
        if (g_ams_panel) {
            g_ams_panel->clear_panel_reference();
        }

        lv_obj_delete(s_ams_panel_obj);
        s_ams_panel_obj = nullptr;

        // Note: Widget registrations remain (LVGL doesn't support unregistration)
        // Note: g_ams_panel C++ object stays for state preservation
    }
}

AmsPanel& get_global_ams_panel() {
    if (!g_ams_panel) {
        g_ams_panel = std::make_unique<AmsPanel>(get_printer_state(), nullptr);
    }

    // Lazy create the panel UI if not yet created
    if (!s_ams_panel_obj && g_ams_panel) {
        // Ensure widgets and XML are registered
        ensure_ams_widgets_registered();

        // Create the panel on the active screen
        lv_obj_t* screen = lv_scr_act();
        s_ams_panel_obj = static_cast<lv_obj_t*>(lv_xml_create(screen, "ams_panel", nullptr));

        if (s_ams_panel_obj) {
            // Initialize subjects if needed
            if (!g_ams_panel->are_subjects_initialized()) {
                g_ams_panel->init_subjects();
            }

            // Setup the panel
            g_ams_panel->setup(s_ams_panel_obj, screen);
            lv_obj_add_flag(s_ams_panel_obj, LV_OBJ_FLAG_HIDDEN); // Hidden by default

            // Register close callback to destroy UI when overlay is closed
            NavigationManager::instance().register_overlay_close_callback(
                s_ams_panel_obj, []() { destroy_ams_panel_ui(); });

            spdlog::info("[AMS Panel] Lazy-created panel UI with close callback");
        } else {
            spdlog::error("[AMS Panel] Failed to create panel from XML");
        }
    }

    return *g_ams_panel;
}
