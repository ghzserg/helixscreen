// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_ams.h"

#include "ui_ams_detail.h"
#include "ui_ams_device_operations_overlay.h"
#include "ui_ams_dryer_card.h"
#include "ui_ams_slot.h"
#include "ui_ams_slot_layout.h"
#include "ui_endless_spool_arrows.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_filament_path_canvas.h"
#include "ui_fonts.h"
#include "ui_hsv_picker.h"
#include "ui_icon.h"
#include "ui_nav_manager.h"
#include "ui_panel_common.h"
#include "ui_spool_canvas.h"
#include "ui_step_progress.h"
#include "ui_temperature_utils.h"
#include "ui_utils.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_constants.h"
#include "app_globals.h"
#include "color_utils.h"
#include "config.h"
#include "filament_database.h"
#include "moonraker_api.h"
#include "observer_factory.h"
#include "printer_detector.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "ui/ams_drawing_utils.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <sstream>
#include <unordered_map>

using namespace helix;

// Global instance pointer for XML callback access (atomic for safety during destruction)
static std::atomic<AmsPanel*> g_ams_panel_instance{nullptr};

// Default slot width for endless arrows canvas (when layout not yet computed)
static constexpr int32_t DEFAULT_SLOT_WIDTH = 80;

// Logo path mapping moved to AmsState::get_logo_path()

// Voron printer check moved to PrinterDetector::is_voron_printer()

// Lazy registration flag - widgets and XML registered on first use
static bool s_ams_widgets_registered = false;

// Forward declarations for XML event callbacks (defined later in file)
static void on_unload_clicked_xml(lv_event_t* e);
static void on_reset_clicked_xml(lv_event_t* e);
static void on_bypass_clicked_xml(lv_event_t* e);
static void on_bypass_toggled_xml(lv_event_t* e);
static void on_settings_clicked_xml(lv_event_t* e);
// Dryer card callbacks now handled by helix::ui::AmsDryerCard class
// Context menu and spoolman picker callbacks are now in extracted classes

// Edit modal and color picker callbacks now handled by helix::ui::AmsEditModal class

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
    ui_endless_spool_arrows_register();

    // Register XML event callbacks BEFORE registering XML components
    // (callbacks must exist when XML parser encounters <event_cb> elements)
    lv_xml_register_event_cb(nullptr, "ams_unload_clicked_cb", on_unload_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_reset_clicked_cb", on_reset_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_bypass_clicked_cb", on_bypass_clicked_xml);
    lv_xml_register_event_cb(nullptr, "ams_bypass_toggled_cb", on_bypass_toggled_xml);
    lv_xml_register_event_cb(nullptr, "on_ams_panel_settings_clicked", on_settings_clicked_xml);

    // Register dryer card callbacks BEFORE XML parsing (callbacks must exist when parser sees them)
    helix::ui::AmsDryerCard::register_callbacks_static();

    // Register AMS device operations overlay callbacks BEFORE XML parsing
    helix::ui::get_ams_device_operations_overlay().register_callbacks();

    // Context menu callbacks registered by helix::ui::AmsContextMenu class
    // Edit modal and color picker callbacks registered by helix::ui::AmsEditModal class

    // Register XML components (dryer card must be registered before ams_panel since it's used
    // there)
    lv_xml_register_component_from_file("A:ui_xml/ams_dryer_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/dryer_presets_modal.xml");
    // NOTE: Old AMS settings panels removed - Device Operations overlay is registered in
    // xml_registration.cpp
    lv_xml_register_component_from_file("A:ui_xml/components/ams_unit_detail.xml");
    lv_xml_register_component_from_file("A:ui_xml/components/ams_loaded_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_panel.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_context_menu.xml");
    lv_xml_register_component_from_file("A:ui_xml/spoolman_spool_item.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_edit_modal.xml");
    lv_xml_register_component_from_file("A:ui_xml/ams_loading_error_modal.xml");
    // NOTE: color_picker.xml is registered at startup in xml_registration.cpp

    s_ams_widgets_registered = true;
    spdlog::debug("[AMS Panel] Widget and XML registration complete");
}

// ============================================================================
// XML Event Callback Wrappers (for <event_cb> elements in XML)
// ============================================================================

static void on_unload_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_unload();
    }
}

static void on_reset_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_reset();
    }
}

static void on_bypass_clicked_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_bypass_toggle();
    }
}

static void on_bypass_toggled_xml(lv_event_t* e) {
    LV_UNUSED(e);
    AmsPanel* panel = g_ams_panel_instance.load();
    if (panel) {
        panel->handle_bypass_toggle();
    }
}

static void on_settings_clicked_xml(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_settings_clicked");
    LV_UNUSED(e);

    spdlog::info("[AmsPanel] Opening AMS Device Operations overlay");

    auto& overlay = helix::ui::get_ams_device_operations_overlay();
    if (!overlay.are_subjects_initialized()) {
        overlay.init_subjects();
        overlay.register_callbacks();
    }

    // Create if needed, then show
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* parent = lv_obj_get_screen(target);
    overlay.show(parent);

    LVGL_SAFE_EVENT_CB_END();
}

// Dryer card callbacks now handled by helix::ui::AmsDryerCard class
// Context menu callbacks handled by helix::ui::AmsContextMenu class
// Edit modal callbacks handled by helix::ui::AmsEditModal class

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
    // Using observer factory for action and slot_count; others use traditional callbacks
    using helix::ui::observe_int_sync;

    slots_version_observer_ = ObserverGuard(AmsState::instance().get_slots_version_subject(),
                                            on_slots_version_changed, this);

    action_observer_ = observe_int_sync<AmsPanel>(
        AmsState::instance().get_ams_action_subject(), this, [](AmsPanel* self, int action_int) {
            if (!self->subjects_initialized_ || !self->panel_)
                return;
            auto action = static_cast<AmsAction>(action_int);
            spdlog::debug("[AmsPanel] Action changed: {} (prev={})", ams_action_to_string(action),
                          ams_action_to_string(self->prev_ams_action_));

            // Detect LOADING -> IDLE or LOADING -> ERROR transition for post-load cooling
            // Also turn off heater if load fails, to prevent leaving heater on indefinitely
            if (self->prev_ams_action_ == AmsAction::LOADING &&
                (action == AmsAction::IDLE || action == AmsAction::ERROR)) {
                self->handle_load_complete();
            }

            // IMPORTANT: Call update_action_display BEFORE updating prev_ams_action_
            // so that operation type detection can compare old vs new action
            self->update_action_display(action);

            self->prev_ams_action_ = action;
        });

    current_slot_observer_ = ObserverGuard(AmsState::instance().get_current_slot_subject(),
                                           on_current_slot_changed, this);

    // Slot count observer for dynamic slot creation (non-scoped mode only)
    slot_count_observer_ = observe_int_sync<AmsPanel>(
        AmsState::instance().get_slot_count_subject(), this, [](AmsPanel* self, int new_count) {
            if (!self->panel_)
                return;
            // When scoped to a unit, on_activate() handles slot creation with correct offsets.
            // Don't let the global slot count observer override that.
            if (self->scoped_unit_index_ >= 0)
                return;
            spdlog::debug("[AmsPanel] Slot count changed to {}", new_count);
            self->create_slots(new_count);
        });

    // Path state observers for filament path visualization
    path_segment_observer_ = ObserverGuard(AmsState::instance().get_path_filament_segment_subject(),
                                           on_path_state_changed, this);
    path_topology_observer_ = ObserverGuard(AmsState::instance().get_path_topology_subject(),
                                            on_path_state_changed, this);

    // Extruder temperature observer for preheat completion detection
    extruder_temp_observer_ =
        observe_int_sync<AmsPanel>(printer_state_.get_active_extruder_temp_subject(), this,
                                   [](AmsPanel* self, int /*temp_centi*/) {
                                       // Check if a pending load can proceed now that temp has
                                       // changed
                                       self->check_pending_load();
                                   });

    // Backend count observer for multi-backend selector
    backend_count_observer_ = observe_int_sync<AmsPanel>(
        AmsState::instance().get_backend_count_subject(), this,
        [](AmsPanel* self, int /*count*/) { self->rebuild_backend_selector(); });

    // Observe external spool color changes to reactively update bypass in path canvas.
    // NOTE: set_external_spool_info() calls lv_subject_set_int() directly (not via
    // ui_queue_update) which is safe because all current callers are on the LVGL thread.
    // If callers from background threads are added, those must use ui_queue_update().
    external_spool_observer_ = observe_int_sync<AmsPanel>(
        AmsState::instance().get_external_spool_color_subject(), this,
        [](AmsPanel* self, int color_int) {
            if (!self->path_canvas_)
                return;
            bool has_spool = color_int != 0;
            ui_filament_path_canvas_set_bypass_has_spool(self->path_canvas_, has_spool);
            if (has_spool) {
                ui_filament_path_canvas_set_bypass_color(self->path_canvas_,
                                                         static_cast<uint32_t>(color_int));
            }
        });

    // UI module subjects are now encapsulated in their respective classes:
    // - helix::ui::AmsEditModal
    // - helix::ui::AmsColorPicker

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
    setup_system_header();
    setup_slots();
    setup_action_buttons();
    setup_status_display();
    setup_path_canvas();
    setup_step_progress();

    // Setup endless spool arrows (before dryer card as it's in slot area)
    setup_endless_arrows();

    // Setup dryer card (extracted module)
    if (!dryer_card_) {
        dryer_card_ = std::make_unique<helix::ui::AmsDryerCard>();
    }
    dryer_card_->setup(panel_);

    // Initial UI sync from backend state
    refresh_slots();

    spdlog::debug("[{}] Setup complete!", get_name());
}

void AmsPanel::on_activate() {
    spdlog::debug("[{}] Activated - syncing from backend", get_name());

    // Sync state when panel becomes visible
    AmsState::instance().sync_from_backend();

    // Create/recreate slots based on scope
    if (scoped_unit_index_ >= 0) {
        // Scoped: show only this unit's slots
        auto* backend = AmsState::instance().get_backend();
        if (backend) {
            AmsSystemInfo info = backend->get_system_info();
            if (scoped_unit_index_ < static_cast<int>(info.units.size())) {
                int unit_slots = info.units[scoped_unit_index_].slot_count;
                spdlog::info("[{}] Scoped to unit {} with {} slots", get_name(), scoped_unit_index_,
                             unit_slots);
                create_slots(unit_slots);
                setup_system_header();
            }
        }

        // Hide elements that don't apply to a single-unit scoped view:
        // path canvas (hub/bypass/toolhead routing), bypass toggle, dryer card
        lv_obj_t* path_container = lv_obj_find_by_name(panel_, "path_container");
        if (path_container)
            lv_obj_add_flag(path_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* bypass_row = lv_obj_find_by_name(panel_, "bypass_row");
        if (bypass_row)
            lv_obj_add_flag(bypass_row, LV_OBJ_FLAG_HIDDEN);
        // In scoped view, force-hide dryer (system-level feature, not per-unit)
        lv_obj_t* dryer_card = lv_obj_find_by_name(panel_, "dryer_card");
        if (dryer_card)
            lv_obj_add_flag(dryer_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Non-scoped: show all system slots
        int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
        if (slot_count != current_slot_count_) {
            create_slots(slot_count);
        }
        setup_system_header();

        // Restore elements hidden by scoped view
        lv_obj_t* path_container = lv_obj_find_by_name(panel_, "path_container");
        if (path_container)
            lv_obj_remove_flag(path_container, LV_OBJ_FLAG_HIDDEN);
        // bypass_row visibility managed by bind_flag_if_eq on ams_supports_bypass subject
        // dryer_card visibility managed by bind_flag_if_eq on dryer_supported subject
    }

    refresh_slots();

    // Sync step progress with current action (in case we reopened mid-operation)
    auto action =
        static_cast<AmsAction>(lv_subject_get_int(AmsState::instance().get_ams_action_subject()));
    update_step_progress(action);

    // If we're in a UI-managed preheat, restore visual feedback
    if (pending_load_slot_ >= 0 && pending_load_target_temp_ > 0) {
        show_preheat_feedback(pending_load_slot_, pending_load_target_temp_);
    }

    // Sync Spoolman active spool with currently loaded slot
    sync_spoolman_active_spool();

    // Start Spoolman polling for slot weight updates
    AmsState::instance().start_spoolman_polling();
}

void AmsPanel::sync_spoolman_active_spool() {
    if (!api_) {
        return;
    }

    int current_slot = lv_subject_get_int(AmsState::instance().get_current_slot_subject());
    if (current_slot < 0) {
        return; // No active slot
    }

    auto* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    SlotInfo slot_info = backend->get_slot_info(current_slot);
    if (slot_info.spoolman_id > 0) {
        spdlog::debug("[{}] Syncing Spoolman: slot {} → spool ID {}", get_name(), current_slot,
                      slot_info.spoolman_id);
        api_->set_active_spool(
            slot_info.spoolman_id, []() {},
            [](const MoonrakerError& err) {
                spdlog::warn("[AmsPanel] Failed to sync active spool: {}", err.message);
            });
    }
}

void AmsPanel::on_deactivate() {
    AmsState::instance().stop_spoolman_polling();

    spdlog::debug("[{}] Deactivated", get_name());
    // Note: UI destruction is handled by NavigationManager close callback
    // registered in get_global_ams_panel()
}

void AmsPanel::clear_panel_reference() {
    // Reset extracted UI modules (they handle their own RAII cleanup)
    dryer_card_.reset();
    context_menu_.reset();
    edit_modal_.reset();
    error_modal_.reset();

    // Clear observer guards BEFORE clearing widget pointers (they reference widgets)
    // NOTE: Keep extruder_temp_observer_ alive so check_pending_load() runs while
    // panel is closed - it doesn't touch widgets, just checks temp and calls backend.
    slots_version_observer_.reset();
    action_observer_.reset();
    current_slot_observer_.reset();
    slot_count_observer_.reset();
    path_segment_observer_.reset();
    path_topology_observer_.reset();
    backend_count_observer_.reset();
    external_spool_observer_.reset();
    // extruder_temp_observer_ intentionally NOT reset - needed for preheat completion

    // Don't cancel preheat or clear pending load state when panel closes.
    // User initiated a load operation - let it complete even if they navigate away.
    prev_ams_action_ = AmsAction::IDLE;

    // Now clear all widget references
    panel_ = nullptr;
    parent_screen_ = nullptr;
    slot_grid_ = nullptr;
    detail_widgets_ = AmsDetailWidgets{};
    path_canvas_ = nullptr;
    endless_arrows_ = nullptr;
    step_progress_ = nullptr;
    step_progress_container_ = nullptr;
    current_slot_count_ = 0;

    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        slot_widgets_[i] = nullptr;
        label_widgets_[i] = nullptr;
    }

    // Reset subjects_initialized_ so observers are recreated on next access
    subjects_initialized_ = false;

    // Clear global instance pointer to prevent callbacks from using stale pointer
    g_ams_panel_instance.store(nullptr);

    spdlog::debug("[AMS Panel] Cleared all widget references");
}

void AmsPanel::set_unit_scope(int unit_index) {
    spdlog::info("[AmsPanel] Setting unit scope to {}", unit_index);
    scoped_unit_index_ = unit_index;
}

void AmsPanel::clear_unit_scope() {
    spdlog::debug("[AmsPanel] Clearing unit scope");
    scoped_unit_index_ = -1;
}

// ============================================================================
// Setup Helpers
// ============================================================================

void AmsPanel::setup_system_header() {
    // Find the system logo image in the header
    lv_obj_t* system_logo = lv_obj_find_by_name(panel_, "system_logo");
    if (!system_logo) {
        spdlog::warn("[{}] system_logo not found in XML", get_name());
        return;
    }

    // Get AMS system info from backend
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        spdlog::debug("[{}] No backend, hiding logo", get_name());
        lv_obj_add_flag(system_logo, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const auto& info = backend->get_system_info();

    // When scoped to a unit, show unit-specific name and logo
    if (scoped_unit_index_ >= 0 && scoped_unit_index_ < static_cast<int>(info.units.size())) {
        const AmsUnit& unit = info.units[scoped_unit_index_];

        // Try unit-specific logo first, fall back to system logo
        ams_draw::apply_logo(system_logo, unit, info);

        // Override the header title with unit name
        lv_obj_t* title_label = lv_obj_find_by_name(panel_, "system_name");
        if (title_label) {
            std::string display_name = ams_draw::get_unit_display_name(unit, scoped_unit_index_);
            lv_label_set_text(title_label, display_name.c_str());
        }

        spdlog::info("[{}] Scoped to unit {}: '{}'", get_name(), scoped_unit_index_, unit.name);
        return;
    }

    // Default: show system-level logo (existing behavior)
    ams_draw::apply_logo(system_logo, info.type_name);
}

void AmsPanel::rebuild_backend_selector() {
    if (!panel_) {
        return;
    }

    lv_obj_t* row = lv_obj_find_by_name(panel_, "backend_selector_row");
    if (!row) {
        return;
    }

    auto& ams = AmsState::instance();
    int count = ams.backend_count();

    if (count <= 1) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);

    // Clear existing children
    while (lv_obj_get_child_count(row) > 0) {
        lv_obj_delete(lv_obj_get_child(row, 0));
    }

    for (int i = 0; i < count; ++i) {
        auto* backend = ams.get_backend(i);
        if (!backend) {
            continue;
        }

        std::string label = ams_type_to_string(backend->get_type());

        // Create a button-like segment for each backend
        lv_obj_t* btn = lv_obj_create(row);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(btn, 8, 0);
        lv_obj_set_style_pad_left(btn, 12, 0);
        lv_obj_set_style_pad_right(btn, 12, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        if (i == active_backend_idx_) {
            lv_obj_set_style_bg_color(btn, theme_manager_get_color("primary"), 0);
        } else {
            lv_obj_set_style_bg_color(btn, theme_manager_get_color("elevated_bg"), 0);
        }

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label.c_str());
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, theme_manager_get_font("font_small"), 0);

        // Store index and add click handler (dynamic buttons are a documented exception)
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto* btn_obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
                int idx =
                    static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn_obj)));
                AmsPanel* panel = g_ams_panel_instance.load();
                if (panel) {
                    panel->on_backend_segment_selected(idx);
                }
            },
            LV_EVENT_CLICKED, nullptr);
    }

    spdlog::debug("[AmsPanel] Backend selector rebuilt with {} segments (active={})", count,
                  active_backend_idx_);
}

void AmsPanel::on_backend_segment_selected(int index) {
    if (index == active_backend_idx_) {
        return;
    }

    active_backend_idx_ = index;
    AmsState::instance().set_active_backend(index);

    // Rebuild selector to update visual highlight
    rebuild_backend_selector();

    // Sync the selected backend and recreate slots
    AmsState::instance().sync_backend(index);

    auto* backend = AmsState::instance().get_backend(index);
    if (backend) {
        auto info = backend->get_system_info();
        create_slots(info.total_slots);

        // Update system header (logo + name)
        setup_system_header();

        // Update path visualization for this backend
        update_path_canvas_from_backend();
    }

    spdlog::info("[AmsPanel] Switched to backend {} ({})", index,
                 backend ? ams_type_to_string(backend->get_type()) : "null");
}

void AmsPanel::setup_slots() {
    lv_obj_t* unit_detail = lv_obj_find_by_name(panel_, "unit_detail");
    if (!unit_detail) {
        spdlog::warn("[{}] unit_detail not found in XML", get_name());
        return;
    }

    detail_widgets_ = ams_detail_find_widgets(unit_detail);
    slot_grid_ = detail_widgets_.slot_grid; // Keep for path canvas sync

    spdlog::debug("[{}] setup_slots: widgets resolved, slot creation deferred to on_activate()",
                  get_name());
}

void AmsPanel::create_slots(int count) {
    (void)count; // Slot count determined by shared helper from backend

    // Destroy existing
    ams_detail_destroy_slots(detail_widgets_, slot_widgets_, current_slot_count_);

    // Determine unit index for scoped views
    int unit_index = scoped_unit_index_;

    // Create new slots
    auto result = ams_detail_create_slots(detail_widgets_, slot_widgets_, MAX_VISIBLE_SLOTS,
                                          unit_index, on_slot_clicked, this);

    current_slot_count_ = result.slot_count;

    // Labels overlay for 5+ slots
    ams_detail_update_labels(detail_widgets_, slot_widgets_, result.slot_count, result.layout);

    // Update path canvas sizing
    if (path_canvas_) {
        ui_filament_path_canvas_set_slot_overlap(path_canvas_, result.layout.overlap);
        ui_filament_path_canvas_set_slot_width(path_canvas_, result.layout.slot_width);
    }

    spdlog::info("[{}] Created {} slot widgets via shared helpers", get_name(), result.slot_count);

    // Update tray
    ams_detail_update_tray(detail_widgets_);
}

// on_slot_count_changed migrated to lambda in init_subjects()

void AmsPanel::setup_action_buttons() {
    // Store panel pointer for static callbacks to access
    // (Callbacks are registered earlier in ensure_ams_widgets_registered())
    g_ams_panel_instance.store(this);

    // Hide settings button when backend has no device sections (e.g. tool changers)
    auto* backend = AmsState::instance().get_backend(0);
    lv_obj_t* btn_settings = lv_obj_find_by_name(panel_, "btn_settings");
    if (btn_settings && backend) {
        auto sections = backend->get_device_sections();
        if (sections.empty()) {
            lv_obj_add_flag(btn_settings, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(btn_settings, LV_OBJ_FLAG_HIDDEN);
        }
    }

    spdlog::debug("[{}] Action buttons ready (callbacks registered during widget init)",
                  get_name());
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

    // Set slot click callback (panel-specific)
    ui_filament_path_canvas_set_slot_callback(path_canvas_, on_path_slot_clicked, this);

    // Set bypass spool click callback (opens edit modal for external spool)
    ui_filament_path_canvas_set_bypass_callback(path_canvas_, on_bypass_spool_clicked, this);

    // Configure from backend using shared helper
    ams_detail_setup_path_canvas(path_canvas_, slot_grid_, scoped_unit_index_, false);

    spdlog::debug("[{}] Path canvas setup complete", get_name());
}

void AmsPanel::update_path_canvas_from_backend() {
    ams_detail_setup_path_canvas(path_canvas_, slot_grid_, scoped_unit_index_, false);
}

void AmsPanel::setup_endless_arrows() {
    endless_arrows_ = lv_obj_find_by_name(panel_, "endless_arrows");
    if (!endless_arrows_) {
        spdlog::warn("[{}] endless_arrows not found in XML - skipping", get_name());
        return;
    }

    spdlog::info("[{}] Found endless_arrows widget", get_name());

    // Initial configuration from backend
    update_endless_arrows_from_backend();

    spdlog::info("[{}] Endless spool arrows setup complete", get_name());
}

void AmsPanel::update_endless_arrows_from_backend() {
    if (!endless_arrows_) {
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        ui_endless_spool_arrows_clear(endless_arrows_);
        lv_obj_add_flag(endless_arrows_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Check if endless spool is supported
    auto capabilities = backend->get_endless_spool_capabilities();
    if (!capabilities.supported) {
        ui_endless_spool_arrows_clear(endless_arrows_);
        lv_obj_add_flag(endless_arrows_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Get the endless spool configuration
    auto configs = backend->get_endless_spool_config();
    if (configs.empty()) {
        ui_endless_spool_arrows_clear(endless_arrows_);
        lv_obj_add_flag(endless_arrows_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Check if any backup is configured
    bool has_any_backup = false;
    for (const auto& config : configs) {
        if (config.backup_slot >= 0) {
            has_any_backup = true;
            break;
        }
    }

    if (!has_any_backup) {
        spdlog::trace("[{}] No endless spool backups configured - hiding arrows", get_name());
        ui_endless_spool_arrows_clear(endless_arrows_);
        lv_obj_add_flag(endless_arrows_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    spdlog::trace("[{}] Endless spool has {} configs with backups", get_name(), configs.size());

    // Build backup slots array
    int backup_slots[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    int slot_count = 0;
    for (const auto& config : configs) {
        if (config.slot_index >= 0 && config.slot_index < 16) {
            backup_slots[config.slot_index] = config.backup_slot;
            slot_count = std::max(slot_count, config.slot_index + 1);
        }
    }

    // Get slot width and overlap from current layout
    int32_t slot_width = DEFAULT_SLOT_WIDTH;
    int32_t overlap = 0;
    if (slot_grid_) {
        lv_obj_t* slot_area = lv_obj_get_parent(slot_grid_);
        if (slot_area) {
            lv_obj_update_layout(slot_area);
            int32_t available_width = lv_obj_get_content_width(slot_area);
            auto layout = calculate_ams_slot_layout(available_width, slot_count);
            slot_width = layout.slot_width > 0 ? layout.slot_width : DEFAULT_SLOT_WIDTH;
            overlap = layout.overlap;
        }
    }

    // Update canvas
    ui_endless_spool_arrows_set_slot_count(endless_arrows_, slot_count);
    ui_endless_spool_arrows_set_slot_width(endless_arrows_, slot_width);
    ui_endless_spool_arrows_set_slot_overlap(endless_arrows_, overlap);
    ui_endless_spool_arrows_set_config(endless_arrows_, backup_slots, slot_count);

    // Show the canvas
    lv_obj_remove_flag(endless_arrows_, LV_OBJ_FLAG_HIDDEN);

    spdlog::debug("[{}] Endless arrows updated with {} slots", get_name(), slot_count);
}

void AmsPanel::setup_step_progress() {
    step_progress_container_ = lv_obj_find_by_name(panel_, "progress_stepper_container");
    if (!step_progress_container_) {
        spdlog::warn("[{}] progress_stepper_container not found in XML", get_name());
        return;
    }

    // Create initial step progress widget (fresh load by default)
    recreate_step_progress_for_operation(StepOperationType::LOAD_FRESH);

    // Container is hidden by default (via XML)
    spdlog::debug("[{}] Step progress widget created", get_name());
}

void AmsPanel::recreate_step_progress_for_operation(StepOperationType op_type) {
    if (!step_progress_container_) {
        return;
    }

    // Delete existing step progress widget if any
    if (step_progress_) {
        lv_obj_del(step_progress_);
        step_progress_ = nullptr;
    }

    current_operation_type_ = op_type;

    // Get capabilities from backend for dynamic labels
    TipMethod tip_method = TipMethod::CUT;
    bool supports_purge = false;
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        AmsSystemInfo info = backend->get_system_info();
        tip_method = info.tip_method;
        supports_purge = info.supports_purge;
    }
    const char* tip_step_label = tip_method_step_label(tip_method);

    // Define steps based on operation type and capabilities
    // NOTE: No "Complete" step - operation just finishes and stepper hides
    switch (op_type) {
    case StepOperationType::LOAD_FRESH: {
        if (supports_purge) {
            // Fresh load with purge: Heat → Feed → Purge
            ui_step_t steps[] = {
                {"Heat nozzle", StepState::Pending},
                {"Feed filament", StepState::Pending},
                {"Purge", StepState::Pending},
            };
            current_step_count_ = 3;
            step_progress_ = ui_step_progress_create(step_progress_container_, steps, 3, false,
                                                     "ams_step_progress");
        } else {
            // Fresh load without purge: Heat → Feed
            ui_step_t steps[] = {
                {"Heat nozzle", StepState::Pending},
                {"Feed filament", StepState::Pending},
            };
            current_step_count_ = 2;
            step_progress_ = ui_step_progress_create(step_progress_container_, steps, 2, false,
                                                     "ams_step_progress");
        }
        break;
    }
    case StepOperationType::LOAD_SWAP: {
        if (supports_purge) {
            // Swap load with purge: Heat → Cut/Tip → Feed → Purge
            ui_step_t steps[] = {
                {"Heat nozzle", StepState::Pending},
                {tip_step_label, StepState::Pending},
                {"Feed filament", StepState::Pending},
                {"Purge", StepState::Pending},
            };
            current_step_count_ = 4;
            step_progress_ = ui_step_progress_create(step_progress_container_, steps, 4, false,
                                                     "ams_step_progress");
        } else {
            // Swap load without purge: Heat → Cut/Tip → Feed
            ui_step_t steps[] = {
                {"Heat nozzle", StepState::Pending},
                {tip_step_label, StepState::Pending},
                {"Feed filament", StepState::Pending},
            };
            current_step_count_ = 3;
            step_progress_ = ui_step_progress_create(step_progress_container_, steps, 3, false,
                                                     "ams_step_progress");
        }
        break;
    }
    case StepOperationType::UNLOAD: {
        // Unload: Heat → Cut/Tip → Retract
        ui_step_t steps[] = {
            {"Heat nozzle", StepState::Pending},
            {tip_step_label, StepState::Pending},
            {"Retract", StepState::Pending},
        };
        current_step_count_ = 3;
        step_progress_ =
            ui_step_progress_create(step_progress_container_, steps, 3, false, "ams_step_progress");
        break;
    }
    }

    if (!step_progress_) {
        spdlog::error("[{}] Failed to create step progress widget for op_type={}", get_name(),
                      static_cast<int>(op_type));
    } else {
        spdlog::debug("[{}] Created step progress: {} steps for op_type={}", get_name(),
                      current_step_count_, static_cast<int>(op_type));
    }
}

int AmsPanel::get_step_index_for_action(AmsAction action, StepOperationType op_type) {
    // Map action to step index based on operation type
    // No "Complete" step - stepper just hides when IDLE
    switch (op_type) {
    case StepOperationType::LOAD_FRESH:
        // With purge (3 steps): Heat(0) → Feed(1) → Purge(2)
        // Without purge (2 steps): Heat(0) → Feed(1)
        switch (action) {
        case AmsAction::HEATING:
            return 0;
        case AmsAction::LOADING:
            return 1;
        case AmsAction::PURGING:
            return 2; // Only if supports_purge
        case AmsAction::IDLE:
            return -1; // Hide stepper
        default:
            return -1;
        }

    case StepOperationType::LOAD_SWAP:
        // With purge (4 steps): Heat(0) → Cut(1) → Feed(2) → Purge(3)
        // Without purge (3 steps): Heat(0) → Cut(1) → Feed(2)
        switch (action) {
        case AmsAction::HEATING:
            return 0;
        case AmsAction::CUTTING:
        case AmsAction::FORMING_TIP:
        case AmsAction::UNLOADING:
            return 1; // Cut/tip and retract phase
        case AmsAction::LOADING:
            return 2;
        case AmsAction::PURGING:
            return 3; // Only if supports_purge
        case AmsAction::IDLE:
            return -1; // Hide stepper
        default:
            return -1;
        }

    case StepOperationType::UNLOAD:
        // 3 steps: Heat(0) → Cut/Tip(1) → Retract(2)
        switch (action) {
        case AmsAction::HEATING:
            return 0;
        case AmsAction::CUTTING:
        case AmsAction::FORMING_TIP:
            return 1;
        case AmsAction::UNLOADING:
            return 2;
        case AmsAction::IDLE:
            return -1; // Hide stepper
        default:
            return -1;
        }
    }
    return -1;
}

void AmsPanel::start_operation(StepOperationType op_type, int target_slot) {
    spdlog::info("[AmsPanel] Starting operation: type={}, target_slot={}",
                 static_cast<int>(op_type), target_slot);

    // Store target for pulse animation
    target_load_slot_ = target_slot;

    // Set pending target slot early so the target slot pulses during preheat
    AmsState::instance().set_pending_target_slot(target_slot);

    // Set ams_action to HEATING immediately - this triggers XML binding to hide buttons
    // (Important for UI-managed preheat where backend hasn't started yet)
    AmsState::instance().set_action(AmsAction::HEATING);

    // Create step progress with correct steps for this operation type
    recreate_step_progress_for_operation(op_type);

    // Show step progress immediately
    if (step_progress_container_) {
        lv_obj_remove_flag(step_progress_container_, LV_OBJ_FLAG_HIDDEN);
    }

    AmsBackend* backend = AmsState::instance().get_backend();

    // NOTE: Slot pulse animation is now subject-driven (in ui_ams_slot.cpp).
    // Each slot auto-pulses when ams_action indicates an active operation and
    // current_slot matches its index. No explicit pulse calls needed here.
    LV_UNUSED(backend);
}

void AmsPanel::update_step_progress(AmsAction action) {
    if (!step_progress_container_) {
        return;
    }

    // NOTE: Operation type is now set by start_operation() before backend calls.
    // We only fall back to heuristic detection for operations started externally
    // (e.g., gcode T commands, Mainsail/Fluidd UI).
    bool is_external = (target_load_slot_ < 0);
    bool filament_loaded = false;
    if (is_external) {
        AmsBackend* backend = AmsState::instance().get_backend();
        if (backend) {
            AmsSystemInfo info = backend->get_system_info();
            filament_loaded = (info.current_slot >= 0);
        }
    }

    auto detection = detect_step_operation(action, prev_ams_action_, current_operation_type_,
                                           is_external, filament_loaded);
    if (detection.should_recreate) {
        if (detection.op_type == StepOperationType::LOAD_SWAP &&
            current_operation_type_ == StepOperationType::UNLOAD) {
            spdlog::debug("[{}] Upgrading UNLOAD → LOAD_SWAP (loading detected after unload)",
                          get_name());
        }
        recreate_step_progress_for_operation(detection.op_type);
        if (detection.jump_to_step >= 0 && step_progress_) {
            ui_step_progress_set_current(step_progress_, detection.jump_to_step);
        }
    }

    if (!step_progress_) {
        return;
    }

    // Show/hide container based on action
    bool show_progress = (action == AmsAction::HEATING || action == AmsAction::LOADING ||
                          action == AmsAction::PURGING || action == AmsAction::CUTTING ||
                          action == AmsAction::FORMING_TIP || action == AmsAction::UNLOADING);

    if (show_progress) {
        lv_obj_remove_flag(step_progress_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        // IDLE, ERROR, SELECTING, RESETTING, PAUSED - hide progress immediately
        lv_obj_add_flag(step_progress_container_, LV_OBJ_FLAG_HIDDEN);

        // Stop pulse animation and reset target when operation completes
        // NOTE: Pulse animation stops automatically via subject-driven logic
        // in ui_ams_slot.cpp when action transitions to IDLE.
        target_load_slot_ = -1;
        return;
    }

    // Get step index for current action and operation type
    int step_index = get_step_index_for_action(action, current_operation_type_);
    if (step_index >= 0) {
        ui_step_progress_set_current(step_progress_, step_index);
    }
}

// ============================================================================
// Public API
// ============================================================================

void AmsPanel::refresh_slots() {
    if (!panel_ || !subjects_initialized_) {
        return;
    }

    update_slot_colors();

    // Update current slot highlight
    int current_slot = lv_subject_get_int(AmsState::instance().get_current_slot_subject());
    update_current_slot_highlight(current_slot);

    // Update endless spool arrows (config may have changed)
    update_endless_arrows_from_backend();
}

// ============================================================================
// UI Update Handlers
// ============================================================================

void AmsPanel::update_slot_colors() {
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    int backend_idx = AmsState::instance().active_backend_index();
    AmsBackend* backend = AmsState::instance().get_backend(backend_idx);

    for (int i = 0; i < MAX_VISIBLE_SLOTS; ++i) {
        if (!slot_widgets_[i]) {
            continue;
        }

        if (i >= slot_count) {
            // Hide slots beyond configured count
            lv_obj_add_flag(slot_widgets_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(slot_widgets_[i], LV_OBJ_FLAG_HIDDEN);

        // Get slot color from AmsState subject (using active backend)
        lv_subject_t* color_subject = AmsState::instance().get_slot_color_subject(backend_idx, i);
        if (color_subject) {
            uint32_t rgb = static_cast<uint32_t>(lv_subject_get_int(color_subject));
            lv_color_t color = lv_color_hex(rgb);

            // Find color swatch within slot
            lv_obj_t* swatch = lv_obj_find_by_name(slot_widgets_[i], "color_swatch");
            if (swatch) {
                lv_obj_set_style_bg_color(swatch, color, 0);
            }
        }

        // Update material label and fill level from backend slot info
        if (backend) {
            SlotInfo slot_info = backend->get_slot_info(i);

            // Update slot-internal material label
            // Truncate long material names when many slots to prevent overlap
            lv_obj_t* material_label = lv_obj_find_by_name(slot_widgets_[i], "material_label");
            if (material_label) {
                if (!slot_info.material.empty()) {
                    std::string material = slot_info.material;
                    // Truncate to 4 chars when overlapping (5+ slots)
                    if (slot_count > 4 && material.length() > 4) {
                        material = material.substr(0, 4);
                    }
                    lv_label_set_text(material_label, material.c_str());
                } else {
                    lv_label_set_text(material_label, "---");
                }
            }

            // Set fill level from Spoolman weight data
            if (slot_info.total_weight_g > 0.0f) {
                float fill_level = slot_info.remaining_weight_g / slot_info.total_weight_g;
                ui_ams_slot_set_fill_level(slot_widgets_[i], fill_level);
            }

            // Refresh slot to update tool badge and other dynamic state
            ui_ams_slot_refresh(slot_widgets_[i]);
        }

        // Update status indicator
        update_slot_status(i);
    }
}

void AmsPanel::update_slot_status(int slot_index) {
    if (slot_index < 0 || slot_index >= MAX_VISIBLE_SLOTS || !slot_widgets_[slot_index]) {
        return;
    }

    int backend_idx = AmsState::instance().active_backend_index();
    lv_subject_t* status_subject =
        AmsState::instance().get_slot_status_subject(backend_idx, slot_index);
    if (!status_subject) {
        return;
    }

    auto status = static_cast<SlotStatus>(lv_subject_get_int(status_subject));

    // Find status indicator icon within slot
    lv_obj_t* status_icon = lv_obj_find_by_name(slot_widgets_[slot_index], "status_icon");
    if (!status_icon) {
        return;
    }

    // Update icon based on status
    switch (status) {
    case SlotStatus::EMPTY:
        // Show empty indicator
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_30, 0);
        break;

    case SlotStatus::AVAILABLE:
    case SlotStatus::FROM_BUFFER:
        // Show filament available
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case SlotStatus::LOADED:
        // Show loaded (highlighted)
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case SlotStatus::BLOCKED:
        // Show error state
        lv_obj_remove_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(status_icon, LV_OPA_100, 0);
        break;

    case SlotStatus::UNKNOWN:
    default:
        lv_obj_add_flag(status_icon, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void AmsPanel::update_action_display(AmsAction action) {
    // Action display is handled via bind_text to ams_action_detail
    // This method handles visual feedback (progress indicators, canvas state)

    // Update nozzle heat glow on path canvas
    if (path_canvas_) {
        bool heating = (action == AmsAction::HEATING);
        ui_filament_path_canvas_set_heat_active(path_canvas_, heating);
    }

    // Update step progress stepper
    update_step_progress(action);

    // NOTE: Slot border pulsing is now fully subject-driven in ui_ams_slot.cpp.
    // Each slot auto-pulses based on ams_action + current_slot subjects.

    // Handle error state - show error modal (only if not already visible)
    if (action == AmsAction::ERROR) {
        if (!error_modal_ || !error_modal_->is_visible()) {
            show_loading_error_modal();
        }
    }
}

void AmsPanel::update_current_slot_highlight(int slot_index) {
    // NOTE: Visual highlight (border + glow) on spool_container is fully handled
    // by slot-level observers in ui_ams_slot.cpp (apply_current_slot_highlight).
    // This method just updates the "Currently Loaded" info card in the right column.
    update_current_loaded_display(slot_index);
}

void AmsPanel::update_current_loaded_display(int slot_index) {
    if (!panel_) {
        return;
    }

    // Sync subjects for reactive UI binding
    // This updates ams_current_material_text, ams_current_slot_text, ams_current_weight_text,
    // ams_current_has_weight, and ams_current_color subjects which are bound to XML elements
    AmsState::instance().sync_current_loaded_from_backend();

    // Find the swatch element - color binding is not supported in XML, so we set it via C++
    lv_obj_t* loaded_swatch = lv_obj_find_by_name(panel_, "loaded_swatch");
    if (loaded_swatch) {
        // Get color from subject (set by sync_current_loaded_from_backend)
        uint32_t color_rgb = static_cast<uint32_t>(
            lv_subject_get_int(AmsState::instance().get_current_color_subject()));
        lv_color_t color = lv_color_hex(color_rgb);
        lv_obj_set_style_bg_color(loaded_swatch, color, 0);
        lv_obj_set_style_border_color(loaded_swatch, color, 0);
    }

    // Update bypass-related state for path canvas visualization
    AmsBackend* backend = AmsState::instance().get_backend();
    bool bypass_active = (slot_index == -2 && backend && backend->is_bypass_active());

    // Update path canvas bypass visualization
    if (path_canvas_) {
        ui_filament_path_canvas_set_bypass_active(path_canvas_, bypass_active);
    }
}

// ============================================================================
// Event Callbacks
// ============================================================================

void AmsPanel::on_bypass_spool_clicked(void* user_data) {
    auto* self = static_cast<AmsPanel*>(user_data);
    if (self) {
        self->handle_bypass_spool_click();
    }
}

void AmsPanel::handle_bypass_spool_click() {
    if (!parent_screen_ || !path_canvas_) {
        return;
    }

    // Capture click point from input device for menu positioning
    lv_point_t click_pt = {0, 0};
    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &click_pt);
    }

    // Create context menu on first use
    if (!context_menu_) {
        context_menu_ = std::make_unique<helix::ui::AmsContextMenu>();
    }

    // Set callback to handle menu actions for external spool
    context_menu_->set_action_callback(
        [this](helix::ui::AmsContextMenu::MenuAction action, int /*slot*/) {
            switch (action) {
            case helix::ui::AmsContextMenu::MenuAction::EDIT:
                show_edit_modal(-2);
                break;

            case helix::ui::AmsContextMenu::MenuAction::SPOOLMAN:
                show_edit_modal(-2);
                break;

            case helix::ui::AmsContextMenu::MenuAction::CLEAR_SPOOL:
                AmsState::instance().clear_external_spool_info();
                // bypass display update handled reactively by external_spool_observer_
                NOTIFY_INFO("External spool cleared");
                break;

            case helix::ui::AmsContextMenu::MenuAction::CANCELLED:
            default:
                break;
            }
        });

    // Position menu at click point, show for external spool
    context_menu_->set_click_point(click_pt);
    context_menu_->show_for_external_spool(parent_screen_, path_canvas_);
}

void AmsPanel::on_path_slot_clicked(int slot_index, void* user_data) {
    auto* self = static_cast<AmsPanel*>(user_data);
    if (!self) {
        return;
    }

    spdlog::info("[AmsPanel] Path slot {} clicked - triggering load", slot_index);

    // Trigger filament load for the clicked slot
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Check if backend is busy (lockout during in-flight tool changes)
    AmsSystemInfo info = backend->get_system_info();
    if (info.action != AmsAction::IDLE && info.action != AmsAction::ERROR) {
        spdlog::debug("[AmsPanel] Ignoring path click - backend busy: {}",
                      ams_action_to_string(info.action));
        return;
    }

    // Ignore click on the already-active tool
    if (info.current_slot >= 0 && info.current_slot == slot_index) {
        spdlog::debug("[AmsPanel] Ignoring path click - tool {} already active", slot_index);
        return;
    }

    AmsError error;

    // If filament is already loaded from a DIFFERENT slot, use tool change (unload-then-load)
    if (info.current_slot >= 0 && info.current_slot != slot_index) {
        // Get the tool number for the target slot
        const SlotInfo* slot_info = info.get_slot_global(slot_index);
        if (slot_info && slot_info->mapped_tool >= 0) {
            spdlog::info(
                "[AmsPanel] Slot {} already loaded, swapping to slot {} via tool change T{}",
                info.current_slot, slot_index, slot_info->mapped_tool);
            // Set up step progress BEFORE backend call
            self->start_operation(StepOperationType::LOAD_SWAP, slot_index);
            error = backend->change_tool(slot_info->mapped_tool);
        } else {
            // Fallback: unload first, then load
            spdlog::info("[AmsPanel] Slot {} already loaded, unloading first then loading {}",
                         info.current_slot, slot_index);
            self->start_operation(StepOperationType::UNLOAD, info.current_slot);
            error = backend->unload_filament();
            if (error.result == AmsResult::SUCCESS) {
                // Note: The actual load will be triggered after unload completes
                // For now, we'll rely on the user clicking again or the backend auto-loading
                NOTIFY_INFO("Unloading... click again to load slot {}", slot_index + 1);
            }
        }
    } else {
        // Fresh load - no tool currently loaded
        self->start_operation(StepOperationType::LOAD_FRESH, slot_index);
        error = backend->load_filament(slot_index);
    }

    if (error.result != AmsResult::SUCCESS) {
        NOTIFY_ERROR("Load failed: {}", error.user_msg);
    }
}

void AmsPanel::on_slot_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[AmsPanel] on_slot_clicked");
    auto* self = static_cast<AmsPanel*>(lv_event_get_user_data(e));
    if (self) {
        // Capture click point from the input device while event is still active
        lv_point_t click_pt = {0, 0};
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_indev_get_point(indev, &click_pt);
        }

        // Use current_target (widget callback was registered on) not target (originally clicked
        // child)
        lv_obj_t* slot = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto slot_index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(slot)));
        self->handle_slot_tap(slot_index, click_pt);
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

void AmsPanel::on_slots_version_changed(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    spdlog::trace("[AmsPanel] Gates version changed - refreshing slots");
    self->refresh_slots();
}

// on_action_changed migrated to lambda in init_subjects()

void AmsPanel::on_current_slot_changed(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<AmsPanel*>(lv_observer_get_user_data(observer));
    if (!self) {
        return;
    }
    if (!self->subjects_initialized_ || !self->panel_) {
        return; // Not yet ready
    }
    int slot = lv_subject_get_int(subject);
    spdlog::debug("[AmsPanel] Current slot changed: {}", slot);
    self->update_current_slot_highlight(slot);

    // Also update path canvas when current slot changes
    self->update_path_canvas_from_backend();

    // Auto-set active Spoolman spool when slot becomes active
    if (slot >= 0 && self->api_) {
        auto* backend = AmsState::instance().get_backend();
        if (backend) {
            SlotInfo slot_info = backend->get_slot_info(slot);
            if (slot_info.spoolman_id > 0) {
                spdlog::info("[AmsPanel] Slot {} has Spoolman ID {}, setting as active spool", slot,
                             slot_info.spoolman_id);
                self->api_->set_active_spool(
                    slot_info.spoolman_id,
                    []() { spdlog::debug("[AmsPanel] Active spool set successfully"); },
                    [](const MoonrakerError& err) {
                        spdlog::warn("[AmsPanel] Failed to set active spool: {}", err.message);
                    });
            }
        }
    }
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

void AmsPanel::handle_slot_tap(int slot_index, lv_point_t click_pt) {
    spdlog::info("[{}] Slot {} tapped", get_name(), slot_index);

    // Validate slot index against configured slot count
    int slot_count = lv_subject_get_int(AmsState::instance().get_slot_count_subject());
    if (slot_index < 0 || slot_index >= slot_count) {
        spdlog::warn("[{}] Invalid slot index {} (slot_count={})", get_name(), slot_index,
                     slot_count);
        return;
    }

    // Show context menu near the tapped slot
    if (slot_index >= 0 && slot_index < MAX_VISIBLE_SLOTS && slot_widgets_[slot_index]) {
        show_context_menu(slot_index, slot_widgets_[slot_index], click_pt);
    }
}

void AmsPanel::handle_unload() {
    spdlog::info("[{}] Unload requested", get_name());

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Get current slot for pulse animation target
    AmsSystemInfo info = backend->get_system_info();
    if (info.current_slot >= 0) {
        start_operation(StepOperationType::UNLOAD, info.current_slot);
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
    // Switch state updates automatically via bypass_active subject binding
}

// ============================================================================
// Context Menu Management (delegates to helix::ui::AmsContextMenu)
// ============================================================================

void AmsPanel::show_context_menu(int slot_index, lv_obj_t* near_widget, lv_point_t click_pt) {
    if (!parent_screen_ || !near_widget) {
        return;
    }

    // Create context menu on first use
    if (!context_menu_) {
        context_menu_ = std::make_unique<helix::ui::AmsContextMenu>();
    }

    // Set callback to handle menu actions
    context_menu_->set_action_callback(
        [this](helix::ui::AmsContextMenu::MenuAction action, int slot) {
            AmsBackend* backend = AmsState::instance().get_backend();

            switch (action) {
            case helix::ui::AmsContextMenu::MenuAction::LOAD:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                // Check if backend is busy
                {
                    AmsSystemInfo info = backend->get_system_info();
                    if (info.action != AmsAction::IDLE && info.action != AmsAction::ERROR) {
                        NOTIFY_WARNING("AMS is busy: {}", ams_action_to_string(info.action));
                        return;
                    }
                }
                // Use preheat-aware load instead of direct load
                this->handle_load_with_preheat(slot);
                break;

            case helix::ui::AmsContextMenu::MenuAction::UNLOAD:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                {
                    AmsError error = backend->unload_filament();
                    if (error.result != AmsResult::SUCCESS) {
                        NOTIFY_ERROR("Unload failed: {}", error.user_msg);
                    }
                }
                break;

            case helix::ui::AmsContextMenu::MenuAction::EJECT:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                {
                    AmsError error = backend->eject_lane(slot);
                    if (error.result != AmsResult::SUCCESS) {
                        NOTIFY_ERROR("Eject failed: {}", error.user_msg);
                    }
                }
                break;

            case helix::ui::AmsContextMenu::MenuAction::RESET_LANE:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                {
                    AmsError error = backend->reset_lane(slot);
                    if (error.result != AmsResult::SUCCESS) {
                        NOTIFY_ERROR("Reset failed: {}", error.user_msg);
                    }
                }
                break;

            case helix::ui::AmsContextMenu::MenuAction::EDIT:
                show_edit_modal(slot);
                break;

            case helix::ui::AmsContextMenu::MenuAction::CLEAR_SPOOL:
                if (!backend) {
                    NOTIFY_WARNING("AMS not available");
                    return;
                }
                {
                    // Clear spool assignment: reset material/color/spool data, keep slot status
                    SlotInfo cleared = backend->get_slot_info(slot);
                    cleared.material.clear();
                    cleared.color_rgb = AMS_DEFAULT_SLOT_COLOR;
                    cleared.color_name.clear();
                    cleared.multi_color_hexes.clear();
                    cleared.brand.clear();
                    cleared.spool_name.clear();
                    cleared.spoolman_id = 0;
                    cleared.remaining_weight_g = -1;
                    cleared.total_weight_g = -1;
                    auto error = backend->set_slot_info(slot, cleared);
                    if (error.success()) {
                        AmsState::instance().sync_from_backend();
                        refresh_slots();
                        NOTIFY_INFO("Slot {} spool cleared", slot + 1);
                    } else {
                        NOTIFY_ERROR("Clear failed: {}", error.user_msg);
                    }
                }
                break;

            case helix::ui::AmsContextMenu::MenuAction::CANCELLED:
            default:
                break;
            }
        });

    // Determine if the slot is loaded (filament in extruder)
    bool is_loaded = false;
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        SlotInfo slot_info = backend->get_slot_info(slot_index);
        is_loaded = (slot_info.status == SlotStatus::LOADED);
    }

    // Position menu near the click point, then show
    context_menu_->set_click_point(click_pt);
    context_menu_->show_near_widget(parent_screen_, slot_index, near_widget, is_loaded, backend);
}

// ============================================================================
// Edit Modal (delegated to helix::ui::AmsEditModal)
// ============================================================================

void AmsPanel::show_edit_modal(int slot_index) {
    if (!parent_screen_) {
        spdlog::warn("[{}] Cannot show edit modal - no parent screen", get_name());
        return;
    }

    // Create modal on first use (lazy initialization)
    if (!edit_modal_) {
        edit_modal_ = std::make_unique<helix::ui::AmsEditModal>();
    }

    // External spool (bypass/direct) — not managed by backend
    if (slot_index == -2) {
        auto ext = AmsState::instance().get_external_spool_info();
        SlotInfo initial_info = ext.value_or(SlotInfo{});
        initial_info.slot_index = -2;
        initial_info.global_index = -2;

        edit_modal_->set_completion_callback([](const helix::ui::AmsEditModal::EditResult& result) {
            if (result.saved) {
                AmsState::instance().set_external_spool_info(result.slot_info);
                // bypass display update handled reactively by external_spool_observer_
                NOTIFY_INFO("External spool updated");
            }
        });
        edit_modal_->show_for_slot(parent_screen_, -2, initial_info, api_);
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING("AMS not available");
        return;
    }

    // Get current slot info
    SlotInfo initial_info = backend->get_slot_info(slot_index);

    // Set completion callback to handle save result
    edit_modal_->set_completion_callback([this](const helix::ui::AmsEditModal::EditResult& result) {
        if (result.saved && result.slot_index >= 0) {
            // Apply the edited slot info to the backend
            AmsBackend* backend = AmsState::instance().get_backend();
            if (backend) {
                backend->set_slot_info(result.slot_index, result.slot_info);

                // Update the slot display
                AmsState::instance().sync_from_backend();
                refresh_slots();

                NOTIFY_INFO("Slot {} updated", result.slot_index + 1);
            }
        }
    });

    // Show the modal
    edit_modal_->show_for_slot(parent_screen_, slot_index, initial_info, api_);
}

void AmsPanel::show_loading_error_modal() {
    if (!parent_screen_) {
        spdlog::warn("[{}] Cannot show error modal - no parent screen", get_name());
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    // Create modal on first use (lazy initialization)
    if (!error_modal_) {
        error_modal_ = std::make_unique<helix::ui::AmsLoadingErrorModal>();
    }

    // Get error details from backend
    AmsSystemInfo info = backend->get_system_info();
    std::string error_message = info.operation_detail;
    if (error_message.empty()) {
        error_message = "An error occurred during filament loading.";
    }

    // Store slot for retry
    int retry_slot = info.current_slot;

    // Show the error modal with retry callback
    error_modal_->show(parent_screen_, error_message, [this, retry_slot]() {
        // Retry load operation for the same slot
        if (retry_slot >= 0) {
            AmsBackend* backend = AmsState::instance().get_backend();
            if (backend) {
                spdlog::info("[AmsPanel] Retrying load for slot {}", retry_slot);
                // Reset the AMS first, then load
                backend->reset();
                handle_load_with_preheat(retry_slot);
            }
        }
    });
}

// ============================================================================
// Preheat Logic for Filament Loading
// ============================================================================

int AmsPanel::get_load_temp_for_slot(int slot_index) {
    // External spool (bypass/direct) — get info from AmsState, not backend
    if (slot_index == -2) {
        auto info = AmsState::instance().get_external_spool_info();
        if (info.has_value()) {
            if (info->nozzle_temp_min > 0)
                return info->nozzle_temp_min;
            if (!info->material.empty()) {
                auto mat = filament::find_material(info->material);
                if (mat.has_value())
                    return mat->nozzle_min;
            }
        }
        return AppConstants::Ams::DEFAULT_LOAD_PREHEAT_TEMP;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return AppConstants::Ams::DEFAULT_LOAD_PREHEAT_TEMP;
    }

    SlotInfo info = backend->get_slot_info(slot_index);

    // Priority 1: Slot's nozzle_temp_min (from Spoolman or manual entry)
    if (info.nozzle_temp_min > 0) {
        return info.nozzle_temp_min;
    }

    // Priority 2: Lookup material in filament database
    if (!info.material.empty()) {
        auto mat = filament::find_material(info.material);
        if (mat.has_value()) {
            return mat->nozzle_min;
        }
    }

    // Priority 3: Configurable fallback
    return AppConstants::Ams::DEFAULT_LOAD_PREHEAT_TEMP;
}

void AmsPanel::handle_load_with_preheat(int slot_index) {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }

    // Tool changers: just send T{n} — Klipper's toolchange macro handles
    // heating, parking, docking, etc. No step progress or preheat needed.
    if (backend->get_type() == AmsType::TOOL_CHANGER) {
        AmsSystemInfo info = backend->get_system_info();
        if (info.current_slot >= 0 && info.current_slot == slot_index) {
            spdlog::debug("[AmsPanel] Tool {} already active, ignoring load", slot_index);
            return;
        }
        backend->load_filament(slot_index);
        return;
    }

    // Determine operation type BEFORE calling backend
    AmsSystemInfo info = backend->get_system_info();
    if (info.current_slot >= 0 && info.current_slot != slot_index) {
        start_operation(StepOperationType::LOAD_SWAP, slot_index);
    } else {
        start_operation(StepOperationType::LOAD_FRESH, slot_index);
    }

    // Helper: initiate load or tool change depending on current state
    auto do_load_or_swap = [&]() {
        if (info.current_slot >= 0 && info.current_slot != slot_index) {
            const SlotInfo* slot_info = info.get_slot_global(slot_index);
            if (slot_info && slot_info->mapped_tool >= 0) {
                spdlog::info("[AmsPanel] Preheat path: swapping via tool change T{}",
                             slot_info->mapped_tool);
                backend->change_tool(slot_info->mapped_tool);
            } else {
                spdlog::info("[AmsPanel] Preheat path: unload first, then load {}", slot_index);
                backend->unload_filament();
            }
        } else {
            backend->load_filament(slot_index);
        }
    };

    // If backend handles heating automatically, just call load directly
    // Backend will also handle cooling after load completes
    if (backend->supports_auto_heat_on_load()) {
        ui_initiated_heat_ = false; // Backend manages temp
        do_load_or_swap();
        return;
    }

    // Otherwise, UI handles preheat
    int target = get_load_temp_for_slot(slot_index);

    // Get current temp in centidegrees, convert to degrees
    int current_centi = lv_subject_get_int(printer_state_.get_active_extruder_temp_subject());
    int current = current_centi / 10;

    // Check if within threshold (5 degrees C)
    constexpr int TEMP_THRESHOLD = 5;
    if (current >= (target - TEMP_THRESHOLD)) {
        // Already hot enough - load immediately, no UI-initiated heat
        ui_initiated_heat_ = false;
        do_load_or_swap();
        return;
    }

    // Start preheating, then load when ready
    pending_load_slot_ = slot_index;
    pending_load_target_temp_ = target;
    ui_initiated_heat_ = true; // Flag so we can cool down after load if needed

    // Send preheat command via API
    if (api_) {
        api_->set_temperature(
            printer_state_.active_extruder_name(), target, []() {},
            [](const MoonrakerError& /*err*/) {});
    }

    // Show immediate visual feedback
    show_preheat_feedback(slot_index, target);

    spdlog::info("[AmsPanel] Starting preheat to {}C for slot {} load", target, slot_index);
}

void AmsPanel::check_pending_load() {
    if (pending_load_slot_ < 0) {
        return;
    }

    // Get current temp in centidegrees, convert to degrees
    int current_centi = lv_subject_get_int(printer_state_.get_active_extruder_temp_subject());
    int current = current_centi / 10;

    // Update display with current temperature while waiting
    char temp_buf[32];
    helix::ui::temperature::format_temperature_pair(current, pending_load_target_temp_, temp_buf,
                                                    sizeof(temp_buf));
    AmsState::instance().set_action_detail(temp_buf);

    constexpr int TEMP_THRESHOLD = 5;

    if (current >= (pending_load_target_temp_ - TEMP_THRESHOLD)) {
        int slot = pending_load_slot_;
        pending_load_slot_ = -1;
        pending_load_target_temp_ = 0;

        AmsBackend* backend = AmsState::instance().get_backend();
        if (backend) {
            AmsSystemInfo preheat_info = backend->get_system_info();
            if (preheat_info.current_slot >= 0 && preheat_info.current_slot != slot) {
                const SlotInfo* slot_info = preheat_info.get_slot_global(slot);
                if (slot_info && slot_info->mapped_tool >= 0) {
                    spdlog::info("[AmsPanel] Preheat complete, swapping via tool change T{}",
                                 slot_info->mapped_tool);
                    backend->change_tool(slot_info->mapped_tool);
                } else {
                    spdlog::info("[AmsPanel] Preheat complete, unloading first then load {}", slot);
                    backend->unload_filament();
                }
            } else {
                spdlog::info("[AmsPanel] Preheat complete, loading slot {}", slot);
                backend->load_filament(slot);
            }
        }
    }
}

void AmsPanel::handle_load_complete() {
    // Only turn off heater if we (the UI) initiated the heating
    // If backend auto-heated or user was already printing, don't touch the heater
    if (ui_initiated_heat_) {
        if (api_) {
            api_->set_temperature(
                printer_state_.active_extruder_name(), 0, []() {},
                [](const MoonrakerError& /*err*/) {});
        }
        spdlog::info("[AmsPanel] Load complete, turning off heater (UI-initiated heat)");
        ui_initiated_heat_ = false;
    }
}

void AmsPanel::show_preheat_feedback(int slot_index, int target_temp) {
    LV_UNUSED(slot_index);

    // Get current temperature for display (convert centidegrees to degrees)
    int current_centi = lv_subject_get_int(printer_state_.get_active_extruder_temp_subject());
    int current_temp = current_centi / 10;

    // Update status text via AmsState subject to show heating progress
    // Format: "185 / 230°C" (context already shown in progress stepper)
    char temp_buf[32];
    helix::ui::temperature::format_temperature_pair(current_temp, target_temp, temp_buf,
                                                    sizeof(temp_buf));
    AmsState::instance().set_action_detail(temp_buf);

    // Show step progress at step 0 (Heat nozzle)
    if (step_progress_container_) {
        lv_obj_remove_flag(step_progress_container_, LV_OBJ_FLAG_HIDDEN);
    }
    if (step_progress_) {
        ui_step_progress_set_current(step_progress_, 0);
    }

    spdlog::debug("[AmsPanel] Showing preheat feedback: {}", temp_buf);
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

        helix::ui::safe_delete(s_ams_panel_obj);

        // Note: Widget registrations remain (LVGL doesn't support unregistration)
        // Note: g_ams_panel C++ object stays for state preservation
    }
}

AmsPanel& get_global_ams_panel() {
    if (!g_ams_panel) {
        g_ams_panel = std::make_unique<AmsPanel>(get_printer_state(), get_moonraker_api());
        StaticPanelRegistry::instance().register_destroy("AmsPanel", []() { g_ams_panel.reset(); });
    }

    // Lazy create the panel UI if not yet created
    if (!s_ams_panel_obj && g_ams_panel) {
        // Ensure widgets and XML are registered
        ensure_ams_widgets_registered();

        // Initialize AmsState subjects BEFORE XML creation so bindings work
        AmsState::instance().init_subjects(true);

        // Create the panel on the active screen
        lv_obj_t* screen = lv_scr_act();
        s_ams_panel_obj = static_cast<lv_obj_t*>(lv_xml_create(screen, "ams_panel", nullptr));

        if (s_ams_panel_obj) {
            // Initialize panel observers (AmsState already initialized above)
            if (!g_ams_panel->are_subjects_initialized()) {
                g_ams_panel->init_subjects();
            }

            // Setup the panel
            g_ams_panel->setup(s_ams_panel_obj, screen);
            lv_obj_add_flag(s_ams_panel_obj, LV_OBJ_FLAG_HIDDEN); // Hidden by default

            // Register overlay instance for lifecycle management
            NavigationManager::instance().register_overlay_instance(s_ams_panel_obj,
                                                                    g_ams_panel.get());

            // Register close callback to clear scope when overlay is closed
            // Panel stays alive for instant re-open (no lazy-load penalty)
            NavigationManager::instance().register_overlay_close_callback(s_ams_panel_obj, []() {
                if (g_ams_panel) {
                    g_ams_panel->clear_unit_scope();
                }
            });

            spdlog::info("[AMS Panel] Lazy-created panel UI with close callback");
        } else {
            spdlog::error("[AMS Panel] Failed to create panel from XML");
        }
    }

    return *g_ams_panel;
}
