// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_print_status.h"

#include "ui_ams_current_tool.h"
#include "ui_component_header_bar.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_gcode_viewer.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_panel_temp_control.h"
#include "ui_subject_registry.h"
#include "ui_temperature_utils.h"
#include "ui_toast.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "abort_manager.h"
#include "app_globals.h"
#include "config.h"
#include "filament_sensor_manager.h"
#include "injection_point_manager.h"
#include "memory_utils.h"
#include "moonraker_api.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "standard_macros.h"
#include "static_panel_registry.h"
#include "thumbnail_cache.h"
#include "thumbnail_processor.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

// Global instance for legacy API and resize callback
static std::unique_ptr<PrintStatusPanel> g_print_status_panel;

using helix::ui::temperature::centi_to_degrees;

// ============================================================================
// Modal Subclass Implementations
// ============================================================================

void ExcludeObjectModal::on_show() {
    wire_ok_button("btn_ok");
    wire_cancel_button("btn_cancel");
}

void RunoutGuidanceModal::on_show() {
    // RunoutGuidanceModal has 6 buttons for runout handling:
    // - btn_load_filament → on_ok() (primary action)
    // - btn_unload_filament → on_quaternary() (unload before loading new)
    // - btn_purge → on_quinary() (purge after loading)
    // - btn_resume → on_cancel() (resume paused print)
    // - btn_cancel_print → on_tertiary() (cancel print)
    // - btn_ok → on_senary() (dismiss when idle)
    wire_ok_button("btn_load_filament");
    wire_quaternary_button("btn_unload_filament");
    wire_quinary_button("btn_purge");
    wire_cancel_button("btn_resume");
    wire_tertiary_button("btn_cancel_print");
    wire_senary_button("btn_ok");
}

// Forward declarations for XML event callbacks (registered in init_subjects)
static void on_tune_speed_changed_cb(lv_event_t* e);
static void on_tune_flow_changed_cb(lv_event_t* e);
static void on_tune_reset_clicked_cb(lv_event_t* e);

// Z-offset tune callbacks (single handler extracts delta from button name)
static void on_tune_z_offset_cb(lv_event_t* e);
static void on_tune_save_z_offset_cb(lv_event_t* e);

// Helper to get or create the global instance
PrintStatusPanel& get_global_print_status_panel() {
    if (!g_print_status_panel) {
        g_print_status_panel = std::make_unique<PrintStatusPanel>(get_printer_state(), nullptr);
        StaticPanelRegistry::instance().register_destroy("PrintStatusPanel",
                                                         []() { g_print_status_panel.reset(); });
    }
    return *g_print_status_panel;
}

PrintStatusPanel::PrintStatusPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : printer_state_(printer_state), api_(api) {
    // Subscribe to PrinterState temperature subjects (ObserverGuard handles cleanup)
    extruder_temp_observer_ =
        ObserverGuard(printer_state_.get_extruder_temp_subject(), extruder_temp_observer_cb, this);
    extruder_target_observer_ = ObserverGuard(printer_state_.get_extruder_target_subject(),
                                              extruder_target_observer_cb, this);
    bed_temp_observer_ =
        ObserverGuard(printer_state_.get_bed_temp_subject(), bed_temp_observer_cb, this);
    bed_target_observer_ =
        ObserverGuard(printer_state_.get_bed_target_subject(), bed_target_observer_cb, this);

    // Subscribe to print progress and state
    print_progress_observer_ = ObserverGuard(printer_state_.get_print_progress_subject(),
                                             print_progress_observer_cb, this);
    print_state_observer_ =
        ObserverGuard(printer_state_.get_print_state_enum_subject(), print_state_observer_cb, this);
    print_filename_observer_ = ObserverGuard(printer_state_.get_print_filename_subject(),
                                             print_filename_observer_cb, this);

    // Subscribe to speed/flow factors
    speed_factor_observer_ =
        ObserverGuard(printer_state_.get_speed_factor_subject(), speed_factor_observer_cb, this);
    flow_factor_observer_ =
        ObserverGuard(printer_state_.get_flow_factor_subject(), flow_factor_observer_cb, this);
    gcode_z_offset_observer_ = ObserverGuard(printer_state_.get_gcode_z_offset_subject(),
                                             gcode_z_offset_observer_cb, this);

    // Subscribe to layer tracking for G-code viewer ghost layer updates
    print_layer_observer_ = ObserverGuard(printer_state_.get_print_layer_current_subject(),
                                          print_layer_observer_cb, this);

    // Subscribe to excluded objects changes (for syncing from Klipper)
    excluded_objects_observer_ = ObserverGuard(
        printer_state_.get_excluded_objects_version_subject(), excluded_objects_observer_cb, this);

    // Subscribe to print time tracking
    print_duration_observer_ = ObserverGuard(printer_state_.get_print_duration_subject(),
                                             print_duration_observer_cb, this);
    print_time_left_observer_ = ObserverGuard(printer_state_.get_print_time_left_subject(),
                                              print_time_left_observer_cb, this);

    // Subscribe to print start preparation phase subjects
    print_start_phase_observer_ = ObserverGuard(printer_state_.get_print_start_phase_subject(),
                                                print_start_phase_observer_cb, this);
    print_start_message_observer_ = ObserverGuard(printer_state_.get_print_start_message_subject(),
                                                  print_start_message_observer_cb, this);
    print_start_progress_observer_ = ObserverGuard(
        printer_state_.get_print_start_progress_subject(), print_start_progress_observer_cb, this);

    spdlog::debug("[{}] Subscribed to PrinterState subjects", get_name());

    // Load configured LED from wizard settings
    Config* config = Config::get_instance();
    if (config) {
        configured_led_ = config->get<std::string>(helix::wizard::LED_STRIP, "");
        if (!configured_led_.empty()) {
            led_state_observer_ =
                ObserverGuard(printer_state_.get_led_state_subject(), led_state_observer_cb, this);
            spdlog::debug("[{}] Configured LED: {} (observing state)", get_name(), configured_led_);
        }
    }
}

PrintStatusPanel::~PrintStatusPanel() {
    deinit_subjects();

    // Signal async callbacks to abort - must be first! [L012]
    m_alive->store(false);

    // ObserverGuard handles observer cleanup automatically
    resize_registered_ = false;

    // Clean up temp G-code file if any
    if (!temp_gcode_path_.empty()) {
        std::remove(temp_gcode_path_.c_str());
        temp_gcode_path_.clear();
    }

    // CRITICAL: Check if LVGL is still initialized before calling LVGL functions.
    // During static destruction, LVGL may already be torn down.
    if (lv_is_initialized()) {
        // Clean up exclude object resources
        if (exclude_undo_timer_) {
            lv_timer_delete(exclude_undo_timer_);
            exclude_undo_timer_ = nullptr;
        }
        // Modal subclasses (exclude_modal_, runout_modal_) use RAII cleanup
        // Their destructors will call hide() automatically
    }
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void PrintStatusPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[{}] init_subjects() called twice - ignoring", get_name());
        return;
    }

    // Initialize all subjects with default values
    // Note: Display filename is now handled by ActivePrintMediaManager via print_display_filename
    UI_SUBJECT_INIT_AND_REGISTER_STRING(progress_text_subject_, progress_text_buf_, "0%",
                                        "print_progress_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(layer_text_subject_, layer_text_buf_, "Layer 0 / 0",
                                        "print_layer_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(elapsed_subject_, elapsed_buf_, "0h 00m", "print_elapsed");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(remaining_subject_, remaining_buf_, "0h 00m",
                                        "print_remaining");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(nozzle_temp_subject_, nozzle_temp_buf_, "0 / 0°C",
                                        "nozzle_temp_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(bed_temp_subject_, bed_temp_buf_, "0 / 0°C",
                                        "bed_temp_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(speed_subject_, speed_buf_, "100%", "print_speed_text");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(flow_subject_, flow_buf_, "100%", "print_flow_text");
    // Pause button icon - MDI icons (pause=F03E4, play=F040A)
    // UTF-8: pause=F3 B0 8F A4, play=F3 B0 90 8A
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pause_button_subject_, pause_button_buf_,
                                        "\xF3\xB0\x8F\xA4", "pause_button_icon");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(pause_label_subject_, pause_label_buf_, "Pause",
                                        "pause_button_label");

    // Timelapse button icon (F0567=video, F0568=video-off)
    // MDI icons in Plane 15 (U+F0xxx) use 4-byte UTF-8 encoding
    // Default to video-off (timelapse disabled): U+F0568 = \xF3\xB0\x95\xA8
    UI_SUBJECT_INIT_AND_REGISTER_STRING(timelapse_button_subject_, timelapse_button_buf_,
                                        "\xF3\xB0\x95\xA8", "timelapse_button_icon");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(timelapse_label_subject_, timelapse_label_buf_, "Off",
                                        "timelapse_button_label");

    // Light button icon (F0336=lightbulb-outline OFF, F06E8=lightbulb-on ON)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(light_button_subject_, light_button_buf_,
                                        "\xF3\xB0\x8C\xB6", "light_button_icon");

    // Preparing state subjects
    UI_SUBJECT_INIT_AND_REGISTER_INT(preparing_visible_subject_, 0, "preparing_visible");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(preparing_operation_subject_, preparing_operation_buf_,
                                        "Preparing...", "preparing_operation");
    UI_SUBJECT_INIT_AND_REGISTER_INT(preparing_progress_subject_, 0, "preparing_progress");

    // Progress bar subject (integer 0-100 for XML bind_value)

    // Viewer mode subject (0=thumbnail, 1=3D gcode viewer, 2=2D gcode viewer)
    UI_SUBJECT_INIT_AND_REGISTER_INT(gcode_viewer_mode_subject_, 0, "gcode_viewer_mode");

    // Tuning panel subjects (for tune panel sliders)
    UI_SUBJECT_INIT_AND_REGISTER_STRING(tune_speed_subject_, tune_speed_buf_, "100%",
                                        "tune_speed_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(tune_flow_subject_, tune_flow_buf_, "100%",
                                        "tune_flow_display");
    UI_SUBJECT_INIT_AND_REGISTER_STRING(tune_z_offset_subject_, tune_z_offset_buf_, "0.000mm",
                                        "tune_z_offset_display");

    // Register XML event callbacks for tune panel
    lv_xml_register_event_cb(nullptr, "on_tune_speed_changed", on_tune_speed_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_flow_changed", on_tune_flow_changed_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_reset_clicked", on_tune_reset_clicked_cb);

    // Z-offset tune callbacks (single handler parses button name for delta)
    lv_xml_register_event_cb(nullptr, "on_tune_z_offset", on_tune_z_offset_cb);
    lv_xml_register_event_cb(nullptr, "on_tune_save_z_offset", on_tune_save_z_offset_cb);

    // Register XML event callbacks for print status panel buttons
    lv_xml_register_event_cb(nullptr, "on_print_status_light", on_light_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_timelapse", on_timelapse_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_pause", on_pause_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_tune", on_tune_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_cancel", on_cancel_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_reprint", on_reprint_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_nozzle_clicked", on_nozzle_card_clicked);
    lv_xml_register_event_cb(nullptr, "on_print_status_bed_clicked", on_bed_card_clicked);

    subjects_initialized_ = true;

    // Sync initial state from PrinterState (in case app opens while print is in progress)
    // This is necessary because observers only fire on VALUE CHANGE, not on subscribe.
    int initial_progress = lv_subject_get_int(printer_state_.get_print_progress_subject());
    int initial_layer = lv_subject_get_int(printer_state_.get_print_layer_current_subject());
    int initial_total_layers = lv_subject_get_int(printer_state_.get_print_layer_total_subject());
    if (initial_progress > 0 || initial_layer > 0 || initial_total_layers > 0) {
        current_progress_ = initial_progress;
        current_layer_ = initial_layer;
        total_layers_ = initial_total_layers;
        update_all_displays();
        spdlog::debug("[{}] Synced initial print state: progress={}%, layer={}/{}", get_name(),
                      initial_progress, initial_layer, initial_total_layers);
    }

    // Sync initial preparation state from PrinterState (in case panel opens mid-preparation)
    int initial_phase = lv_subject_get_int(printer_state_.get_print_start_phase_subject());
    if (initial_phase != 0) {
        on_print_start_phase_changed(initial_phase);
        auto* msg = lv_subject_get_string(printer_state_.get_print_start_message_subject());
        on_print_start_message_changed(msg);
        int prog = lv_subject_get_int(printer_state_.get_print_start_progress_subject());
        on_print_start_progress_changed(prog);
        spdlog::debug("[{}] Synced initial preparation state: phase={}, progress={}%", get_name(),
                      initial_phase, prog);
    }

    spdlog::debug("[{}] Subjects initialized (17 subjects)", get_name());
}

void PrintStatusPanel::deinit_subjects() {
    if (!subjects_initialized_)
        return;

    lv_subject_deinit(&progress_text_subject_);
    lv_subject_deinit(&layer_text_subject_);
    lv_subject_deinit(&elapsed_subject_);
    lv_subject_deinit(&remaining_subject_);
    lv_subject_deinit(&nozzle_temp_subject_);
    lv_subject_deinit(&bed_temp_subject_);
    lv_subject_deinit(&speed_subject_);
    lv_subject_deinit(&flow_subject_);
    lv_subject_deinit(&pause_button_subject_);
    lv_subject_deinit(&pause_label_subject_);
    lv_subject_deinit(&timelapse_button_subject_);
    lv_subject_deinit(&timelapse_label_subject_);
    lv_subject_deinit(&light_button_subject_);
    lv_subject_deinit(&preparing_visible_subject_);
    lv_subject_deinit(&preparing_operation_subject_);
    lv_subject_deinit(&preparing_progress_subject_);
    lv_subject_deinit(&gcode_viewer_mode_subject_);
    lv_subject_deinit(&tune_speed_subject_);
    lv_subject_deinit(&tune_flow_subject_);
    lv_subject_deinit(&tune_z_offset_subject_);

    subjects_initialized_ = false;
    spdlog::debug("[PrintStatusPanel] Subjects deinitialized");
}

lv_obj_t* PrintStatusPanel::create(lv_obj_t* parent) {
    parent_screen_ = parent;

    // Create overlay root from XML
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, get_xml_component_name(), nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    spdlog::info("[{}] Setting up panel...", get_name());

    // Panel width is set via XML using #overlay_panel_width_large (same as print_file_detail)
    // Use standard overlay panel setup for header/content/back button
    ui_overlay_panel_setup_standard(overlay_root_, parent_screen_, "overlay_header",
                                    "overlay_content");

    // Store header reference for e-stop visibility control
    overlay_header_ = lv_obj_find_by_name(overlay_root_, "overlay_header");

    lv_obj_t* overlay_content = lv_obj_find_by_name(overlay_root_, "overlay_content");
    if (!overlay_content) {
        spdlog::error("[{}] overlay_content not found!", get_name());
        return nullptr;
    }

    // Find thumbnail section for nested widgets
    lv_obj_t* thumbnail_section = lv_obj_find_by_name(overlay_content, "thumbnail_section");
    if (!thumbnail_section) {
        spdlog::error("[{}] thumbnail_section not found!", get_name());
        return nullptr;
    }

    // Find G-code viewer, thumbnail, and gradient background widgets
    gcode_viewer_ = lv_obj_find_by_name(thumbnail_section, "print_gcode_viewer");
    print_thumbnail_ = lv_obj_find_by_name(thumbnail_section, "print_thumbnail");
    gradient_background_ = lv_obj_find_by_name(thumbnail_section, "gradient_background");

    if (gcode_viewer_) {
        spdlog::debug("[{}]   ✓ G-code viewer widget found", get_name());

        // Apply render mode - priority: cmdline > env var > settings
        // Note: HELIX_GCODE_MODE env var is handled at widget creation, so we only
        // override if there's an explicit command-line option or if no env var was set
        const auto* config = get_runtime_config();
        const char* env_mode = std::getenv("HELIX_GCODE_MODE");

        if (config->gcode_render_mode >= 0) {
            // Command line takes highest priority
            auto render_mode = static_cast<gcode_viewer_render_mode_t>(config->gcode_render_mode);
            ui_gcode_viewer_set_render_mode(gcode_viewer_, render_mode);
            spdlog::info("[{}]   ✓ Set G-code render mode: {} (cmdline)", get_name(),
                         config->gcode_render_mode);
        } else if (env_mode) {
            // Env var already applied at widget creation - just log
            spdlog::info("[{}]   ✓ G-code render mode: {} (env var)", get_name(),
                         ui_gcode_viewer_is_using_2d_mode(gcode_viewer_) ? "2D" : "3D");
        } else {
            // No cmdline or env var - apply saved settings
            int render_mode_val = SettingsManager::instance().get_gcode_render_mode();
            auto render_mode = static_cast<gcode_viewer_render_mode_t>(render_mode_val);
            ui_gcode_viewer_set_render_mode(gcode_viewer_, render_mode);
            spdlog::info("[{}]   ✓ Set G-code render mode: {} (settings)", get_name(),
                         render_mode_val);
        }

        // Register long-press callback for exclude object feature
        ui_gcode_viewer_set_object_long_press_callback(gcode_viewer_, on_object_long_pressed, this);
        spdlog::debug("[{}]   ✓ Registered long-press callback for exclude object", get_name());

        // Vertical offset to match thumbnail positioning (tuned empirically)
        ui_gcode_viewer_set_content_offset_y(gcode_viewer_, -0.10f);
    } else {
        spdlog::error("[{}]   ✗ G-code viewer widget NOT FOUND", get_name());
    }
    if (print_thumbnail_) {
        spdlog::debug("[{}]   ✓ Print thumbnail widget found", get_name());
    }
    if (gradient_background_) {
        spdlog::debug("[{}]   ✓ Gradient background widget found", get_name());
    }

    // Force layout calculation
    lv_obj_update_layout(overlay_root_);

    // Register resize callback
    ui_resize_handler_register(on_resize_static);
    resize_registered_ = true;

    // Store button references for potential state queries (not event wiring - that's in XML)
    btn_timelapse_ = lv_obj_find_by_name(overlay_content, "btn_timelapse");
    btn_pause_ = lv_obj_find_by_name(overlay_content, "btn_pause");
    btn_tune_ = lv_obj_find_by_name(overlay_content, "btn_tune");
    btn_cancel_ = lv_obj_find_by_name(overlay_content, "btn_cancel");
    btn_reprint_ = lv_obj_find_by_name(overlay_content, "btn_reprint");

    // Print complete celebration badge (for animation)
    success_badge_ = lv_obj_find_by_name(overlay_content, "success_badge");
    if (success_badge_) {
        spdlog::debug("[{}]   ✓ Success badge", get_name());
    }

    // Print cancelled badge (for animation)
    cancel_badge_ = lv_obj_find_by_name(overlay_content, "cancel_badge");
    if (cancel_badge_) {
        spdlog::debug("[{}]   ✓ Cancel badge", get_name());
    }

    // Progress bar widget
    progress_bar_ = lv_obj_find_by_name(overlay_content, "print_progress");
    if (progress_bar_) {
        lv_bar_set_range(progress_bar_, 0, 100);
        // WORKAROUND: LVGL bar has a bug where setting value=0 when cur_value=0
        // causes early return without proper layout update, showing full bar.
        // Force update by setting to 1 first, then 0.
        lv_bar_set_value(progress_bar_, 1, LV_ANIM_OFF);
        lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
        spdlog::debug("[{}]   ✓ Progress bar", get_name());
    } else {
        spdlog::error("[{}]   ✗ Progress bar NOT FOUND", get_name());
    }

    // Preparing progress bar (shown during pre-print operations)
    preparing_progress_bar_ = lv_obj_find_by_name(overlay_content, "preparing_progress_bar");
    if (preparing_progress_bar_) {
        lv_bar_set_range(preparing_progress_bar_, 0, 100);
        lv_bar_set_value(preparing_progress_bar_, 0, LV_ANIM_OFF);
        spdlog::debug("[{}]   ✓ Preparing progress bar", get_name());
    }

    // AMS current tool indicator (auto-hides when no AMS or no tool active)
    lv_obj_t* ams_indicator = lv_obj_find_by_name(overlay_content, "ams_current_tool_indicator");
    if (ams_indicator) {
        ui_ams_current_tool_setup(ams_indicator);
        spdlog::debug("[{}]   ✓ AMS current tool indicator", get_name());
    }

    // Check if --gcode-file was specified on command line for this panel
    const auto* config = get_runtime_config();
    if (config->gcode_test_file && gcode_viewer_) {
        // Check file size and memory safety before loading
        // Use 2D streaming check since that's the mode used on memory-constrained devices
        std::ifstream file(config->gcode_test_file, std::ios::binary | std::ios::ate);
        if (file) {
            size_t file_size = static_cast<size_t>(file.tellg());
            if (helix::is_gcode_2d_streaming_safe(file_size)) {
                spdlog::info("[{}] Loading G-code file from command line: {}", get_name(),
                             config->gcode_test_file);
                load_gcode_file(config->gcode_test_file);
            } else {
                spdlog::warn("[{}] G-code file too large for 2D streaming: {} ({} bytes) - using "
                             "thumbnail only",
                             get_name(), config->gcode_test_file, file_size);
            }
        }
    }

    // Restore cached thumbnail if a print was already in progress before panel was displayed
    // This handles the case where a print was started from Mainsail while on the Home panel
    if (print_thumbnail_ && !cached_thumbnail_path_.empty()) {
        lv_image_set_src(print_thumbnail_, cached_thumbnail_path_.c_str());
        spdlog::info("[{}] Restored cached thumbnail: {}", get_name(), cached_thumbnail_path_);
    }

    // Register plugin injection point for print status widgets
    lv_obj_t* extras_container = lv_obj_find_by_name(overlay_root_, "print_status_extras");
    if (extras_container) {
        helix::plugin::InjectionPointManager::instance().register_point("print_status_extras",
                                                                        extras_container);
        spdlog::debug("[{}] Registered injection point: print_status_extras", get_name());
    }

    // Hide initially - NavigationManager will show when pushed
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    spdlog::info("[{}] Setup complete!", get_name());
    return overlay_root_;
}

void PrintStatusPanel::on_activate() {
    OverlayBase::on_activate(); // Sets visible_ = true
    is_active_ = true;

    int state_enum = lv_subject_get_int(printer_state_.get_print_state_enum_subject());
    spdlog::debug("[{}] on_activate() print_state_enum={}", get_name(), state_enum);

    // Load deferred G-code if pending (lazy loading optimization)
    // This avoids downloading large files unless user navigates here
    if (!pending_gcode_filename_.empty()) {
        spdlog::info("[{}] Loading deferred G-code: {}", get_name(), pending_gcode_filename_);
        load_gcode_for_viewing(pending_gcode_filename_);
        pending_gcode_filename_.clear();
    }

    // Restore G-code viewer state based on current print conditions
    // This ensures the viewer is properly restored when returning from overlays like Tune panel
    bool want_viewer =
        (current_state_ == PrintState::Preparing || current_state_ == PrintState::Printing ||
         current_state_ == PrintState::Paused);
    show_gcode_viewer(want_viewer && gcode_loaded_);
}

void PrintStatusPanel::on_deactivate() {
    OverlayBase::on_deactivate(); // Sets visible_ = false
    is_active_ = false;
    spdlog::debug("[{}] on_deactivate()", get_name());

    // Pause G-code viewer rendering when panel is hidden (CPU optimization)
    if (gcode_viewer_) {
        ui_gcode_viewer_set_paused(gcode_viewer_, true);
    }

    // Hide runout guidance modal if panel is deactivated (e.g., navbar navigation)
    hide_runout_guidance_modal();
}

void PrintStatusPanel::cleanup() {
    OverlayBase::cleanup(); // Sets cleanup_called_ = true
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

void PrintStatusPanel::format_time(int seconds, char* buf, size_t buf_size) {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    std::snprintf(buf, buf_size, "%dh %02dm", hours, minutes);
}

void PrintStatusPanel::cleanup_temp_gcode() {
    if (!temp_gcode_path_.empty()) {
        if (std::remove(temp_gcode_path_.c_str()) == 0) {
            spdlog::debug("[{}] Cleaned up temp G-code file: {}", get_name(), temp_gcode_path_);
        } else {
            spdlog::trace("[{}] Temp G-code file already removed: {}", get_name(),
                          temp_gcode_path_);
        }
        temp_gcode_path_.clear();
    }
}

void PrintStatusPanel::show_gcode_viewer(bool show) {
    // Update viewer mode subject - XML bindings handle visibility reactively
    // Mode 0 = thumbnail (gradient + thumbnail visible, gcode viewer hidden)
    // Mode 1 = 3D gcode viewer (gcode visible, gradient + thumbnail hidden, rotate icon shown)
    // Mode 2 = 2D gcode viewer (gcode visible, gradient shown, thumbnail + rotate icon hidden)
    int mode = 0; // Default: thumbnail
    if (show) {
        // Check if the viewer is using 2D mode
        bool is_2d = gcode_viewer_ && ui_gcode_viewer_is_using_2d_mode(gcode_viewer_);
        mode = is_2d ? 2 : 1;
    }
    lv_subject_set_int(&gcode_viewer_mode_subject_, mode);

    // Pause/resume rendering based on visibility mode (CPU optimization)
    if (gcode_viewer_) {
        ui_gcode_viewer_set_paused(gcode_viewer_, !show);
    }

    spdlog::debug("[{}] G-code viewer mode: {} ({})", get_name(), mode,
                  mode == 0 ? "thumbnail" : (mode == 1 ? "3D" : "2D"));

    // Diagnostic: log visibility state of all viewer components
    if (print_thumbnail_) {
        bool thumb_hidden = lv_obj_has_flag(print_thumbnail_, LV_OBJ_FLAG_HIDDEN);
        const void* img_src = lv_image_get_src(print_thumbnail_);
        spdlog::debug("[{}]   -> thumbnail: hidden={}, has_src={}", get_name(), thumb_hidden,
                      img_src != nullptr);
    }
    if (gcode_viewer_) {
        bool viewer_hidden = lv_obj_has_flag(gcode_viewer_, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[{}]   -> gcode_viewer: hidden={}", get_name(), viewer_hidden);
    }
    if (gradient_background_) {
        bool grad_hidden = lv_obj_has_flag(gradient_background_, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[{}]   -> gradient: hidden={}", get_name(), grad_hidden);
    }
}

void PrintStatusPanel::load_gcode_file(const char* file_path) {
    if (!gcode_viewer_ || !file_path) {
        spdlog::warn("[{}] Cannot load G-code: viewer={}, path={}", get_name(),
                     gcode_viewer_ != nullptr, file_path != nullptr);
        return;
    }

    spdlog::info("[{}] Loading G-code file: {}", get_name(), file_path);

    // Register callback to be notified when loading completes
    ui_gcode_viewer_set_load_callback(
        gcode_viewer_,
        [](lv_obj_t* viewer, void* user_data, bool success) {
            auto* self = static_cast<PrintStatusPanel*>(user_data);
            if (!success) {
                spdlog::error("[{}] G-code load failed", self->get_name());
                self->gcode_loaded_ = false;
                return;
            }

            // Get layer count from loaded geometry
            int max_layer = ui_gcode_viewer_get_max_layer(viewer);
            spdlog::info("[{}] G-code loaded: {} layers", self->get_name(), max_layer);

            // Mark G-code as successfully loaded (enables viewer mode on state changes)
            self->gcode_loaded_ = true;

            // Only show viewer if print is still active (avoid race with completion)
            bool want_viewer = (self->current_state_ == PrintState::Preparing ||
                                self->current_state_ == PrintState::Printing ||
                                self->current_state_ == PrintState::Paused);
            if (want_viewer) {
                self->show_gcode_viewer(true);
            }

            // Force layout recalculation now that viewer is visible
            lv_obj_update_layout(viewer);
            // Reset camera to fit model to new viewport dimensions
            ui_gcode_viewer_reset_camera(viewer);

            // Set print progress to current layer (not 0!) when joining a print in progress.
            // Read directly from PrinterState subjects to get the latest values.
            int viewer_max_layer = ui_gcode_viewer_get_max_layer(viewer);
            int current_layer =
                lv_subject_get_int(self->printer_state_.get_print_layer_current_subject());
            int total_layers =
                lv_subject_get_int(self->printer_state_.get_print_layer_total_subject());

            // Update local state while we're at it
            self->current_layer_ = current_layer;
            self->total_layers_ = total_layers;

            // Map from Moonraker layer count to viewer layer count
            // Note: viewer_max_layer may be -1 if 2D renderer not yet initialized (lazy init)
            int viewer_layer = 0;
            if (viewer_max_layer > 0 && total_layers > 0) {
                viewer_layer = (current_layer * viewer_max_layer) / total_layers;
            } else if (viewer_max_layer <= 0 && current_layer > 0) {
                // 2D renderer not ready yet - use raw current layer, will be corrected later
                // The 2D renderer will use this value when it initializes on first render
                viewer_layer = current_layer;
            }

            // CRITICAL: Defer to avoid lv_obj_invalidate() during render phase
            // This callback runs during lv_timer_handler() which may be mid-render
            struct ViewerProgressCtx {
                lv_obj_t* viewer;
                int layer;
            };
            auto* ctx = new ViewerProgressCtx{viewer, viewer_layer};
            ui_async_call(
                [](void* user_data) {
                    auto* c = static_cast<ViewerProgressCtx*>(user_data);
                    if (c->viewer && lv_obj_is_valid(c->viewer)) {
                        ui_gcode_viewer_set_print_progress(c->viewer, c->layer);
                    }
                    delete c;
                },
                ctx);

            spdlog::debug("[{}] G-code loaded: initial layer progress set to {} "
                          "(current={}/{}, viewer_max={})",
                          self->get_name(), viewer_layer, current_layer, total_layers,
                          viewer_max_layer);

            // NOTE: PrintStatusPanel does NOT start prints - it only VIEWS them.
            // Prints are started from PrintSelectPanel via the Print button.
            // This callback is for loading G-code into the viewer for visualization only.
            spdlog::debug("[{}] G-code loaded for viewing: {}", self->get_name(),
                          ui_gcode_viewer_get_filename(viewer));
        },
        this);

    // Start loading the file
    ui_gcode_viewer_load_file(gcode_viewer_, file_path);
}

void PrintStatusPanel::update_all_displays() {
    // Guard: don't update if subjects aren't initialized yet
    if (!subjects_initialized_) {
        return;
    }

    // Progress text
    std::snprintf(progress_text_buf_, sizeof(progress_text_buf_), "%d%%", current_progress_);
    lv_subject_copy_string(&progress_text_subject_, progress_text_buf_);

    // Layer text
    std::snprintf(layer_text_buf_, sizeof(layer_text_buf_), "Layer %d / %d", current_layer_,
                  total_layers_);
    lv_subject_copy_string(&layer_text_subject_, layer_text_buf_);

    // Time displays
    format_time(elapsed_seconds_, elapsed_buf_, sizeof(elapsed_buf_));
    lv_subject_copy_string(&elapsed_subject_, elapsed_buf_);

    format_time(remaining_seconds_, remaining_buf_, sizeof(remaining_buf_));
    lv_subject_copy_string(&remaining_subject_, remaining_buf_);

    // Show "--" for target when heater is off (target=0) for better UX
    if (nozzle_target_ > 0) {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%d / %d°C",
                      centi_to_degrees(nozzle_current_), centi_to_degrees(nozzle_target_));
    } else {
        std::snprintf(nozzle_temp_buf_, sizeof(nozzle_temp_buf_), "%d / --",
                      centi_to_degrees(nozzle_current_));
    }
    lv_subject_copy_string(&nozzle_temp_subject_, nozzle_temp_buf_);

    if (bed_target_ > 0) {
        std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%d / %d°C",
                      centi_to_degrees(bed_current_), centi_to_degrees(bed_target_));
    } else {
        std::snprintf(bed_temp_buf_, sizeof(bed_temp_buf_), "%d / --",
                      centi_to_degrees(bed_current_));
    }
    lv_subject_copy_string(&bed_temp_subject_, bed_temp_buf_);

    // Speeds
    std::snprintf(speed_buf_, sizeof(speed_buf_), "%d%%", speed_percent_);
    lv_subject_copy_string(&speed_subject_, speed_buf_);

    std::snprintf(flow_buf_, sizeof(flow_buf_), "%d%%", flow_percent_);
    lv_subject_copy_string(&flow_subject_, flow_buf_);

    // Update pause button icon and label based on state
    // MDI icons: play=F040A, pause=F03E4 (UTF-8: play=F3 B0 90 8A, pause=F3 B0 8F A4)
    if (current_state_ == PrintState::Paused) {
        std::snprintf(pause_button_buf_, sizeof(pause_button_buf_), "\xF3\xB0\x90\x8A"); // play
        std::snprintf(pause_label_buf_, sizeof(pause_label_buf_), "Resume");
    } else {
        std::snprintf(pause_button_buf_, sizeof(pause_button_buf_), "\xF3\xB0\x8F\xA4"); // pause
        std::snprintf(pause_label_buf_, sizeof(pause_label_buf_), "Pause");
    }
    lv_subject_copy_string(&pause_button_subject_, pause_button_buf_);
    lv_subject_copy_string(&pause_label_subject_, pause_label_buf_);
}

// ============================================================================
// INSTANCE HANDLERS
// ============================================================================

void PrintStatusPanel::handle_nozzle_card_click() {
    spdlog::info("[{}] Nozzle temp card clicked - opening nozzle temp panel", get_name());

    if (!temp_control_panel_) {
        spdlog::error("[{}] TempControlPanel not initialized", get_name());
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create nozzle temp panel on first access (lazy initialization)
    if (!nozzle_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating nozzle temperature panel...", get_name());

        nozzle_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "nozzle_temp_panel", nullptr));
        if (nozzle_temp_panel_) {
            temp_control_panel_->setup_nozzle_panel(nozzle_temp_panel_, parent_screen_);
            lv_obj_add_flag(nozzle_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Nozzle temp panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create nozzle temp panel from XML", get_name());
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (nozzle_temp_panel_) {
        ui_nav_push_overlay(nozzle_temp_panel_);
    }
}

void PrintStatusPanel::handle_bed_card_click() {
    spdlog::info("[{}] Bed temp card clicked - opening bed temp panel", get_name());

    if (!temp_control_panel_) {
        spdlog::error("[{}] TempControlPanel not initialized", get_name());
        NOTIFY_ERROR("Temperature panel not available");
        return;
    }

    // Create bed temp panel on first access (lazy initialization)
    if (!bed_temp_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating bed temperature panel...", get_name());

        bed_temp_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "bed_temp_panel", nullptr));
        if (bed_temp_panel_) {
            temp_control_panel_->setup_bed_panel(bed_temp_panel_, parent_screen_);
            lv_obj_add_flag(bed_temp_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Bed temp panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create bed temp panel from XML", get_name());
            NOTIFY_ERROR("Failed to load temperature panel");
            return;
        }
    }

    if (bed_temp_panel_) {
        ui_nav_push_overlay(bed_temp_panel_);
    }
}

void PrintStatusPanel::handle_light_button() {
    spdlog::info("[{}] Light button clicked", get_name());

    // Check if LED is configured
    if (configured_led_.empty()) {
        spdlog::warn("[{}] Light toggle called but no LED configured", get_name());
        return;
    }

    // Toggle to opposite of current state
    bool new_state = !led_on_;

    // Send command to Moonraker
    if (api_) {
        if (new_state) {
            api_->set_led_on(
                configured_led_,
                [this]() {
                    spdlog::info("[{}] LED turned ON - waiting for state update", get_name());
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to turn LED on: {}", err.message);
                    NOTIFY_ERROR("Failed to turn light on: {}", err.user_message());
                });
        } else {
            api_->set_led_off(
                configured_led_,
                [this]() {
                    spdlog::info("[{}] LED turned OFF - waiting for state update", get_name());
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to turn LED off: {}", err.message);
                    NOTIFY_ERROR("Failed to turn light off: {}", err.user_message());
                });
        }
    } else {
        spdlog::warn("[{}] API not available - cannot control LED", get_name());
        NOTIFY_ERROR("Cannot control light: printer not connected");
    }
}

void PrintStatusPanel::handle_timelapse_button() {
    spdlog::info("[{}] Timelapse button clicked (current state: {})", get_name(),
                 timelapse_enabled_ ? "enabled" : "disabled");

    // Toggle to opposite of current state
    bool new_state = !timelapse_enabled_;

    if (api_) {
        api_->set_timelapse_enabled(
            new_state,
            [this, new_state]() {
                spdlog::info("[{}] Timelapse {} successfully", get_name(),
                             new_state ? "enabled" : "disabled");

                // Update local state
                timelapse_enabled_ = new_state;

                // Update icon and label: U+F0567 = video (enabled), U+F0568 = video-off (disabled)
                // MDI Plane 15 icons use 4-byte UTF-8 encoding
                if (timelapse_enabled_) {
                    std::snprintf(timelapse_button_buf_, sizeof(timelapse_button_buf_),
                                  "\xF3\xB0\x95\xA7"); // video
                    std::snprintf(timelapse_label_buf_, sizeof(timelapse_label_buf_), "On");
                } else {
                    std::snprintf(timelapse_button_buf_, sizeof(timelapse_button_buf_),
                                  "\xF3\xB0\x95\xA8"); // video-off
                    std::snprintf(timelapse_label_buf_, sizeof(timelapse_label_buf_), "Off");
                }
                lv_subject_copy_string(&timelapse_button_subject_, timelapse_button_buf_);
                lv_subject_copy_string(&timelapse_label_subject_, timelapse_label_buf_);
            },
            [this](const MoonrakerError& err) {
                spdlog::error("[{}] Failed to toggle timelapse: {}", get_name(), err.message);
                NOTIFY_ERROR("Failed to toggle timelapse: {}", err.user_message());
            });
    } else {
        spdlog::warn("[{}] API not available - cannot control timelapse", get_name());
        NOTIFY_ERROR("Cannot control timelapse: printer not connected");
    }
}

void PrintStatusPanel::handle_pause_button() {
    if (current_state_ == PrintState::Printing) {
        spdlog::info("[{}] Pausing print...", get_name());

        // Check if pause slot is available
        const auto& pause_info = StandardMacros::instance().get(StandardMacroSlot::Pause);
        if (pause_info.is_empty()) {
            spdlog::warn("[{}] Pause macro slot is empty", get_name());
            NOTIFY_WARNING("Pause macro not configured");
            return;
        }

        if (api_) {
            spdlog::info("[{}] Using StandardMacros pause: {}", get_name(), pause_info.get_macro());
            // Stateless callbacks to avoid use-after-free if panel destroyed [L012]
            StandardMacros::instance().execute(
                StandardMacroSlot::Pause, api_,
                []() {
                    spdlog::info("[Print Status] Pause command sent successfully");
                    // State will update via PrinterState observer when Moonraker confirms
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to pause print: {}", err.message);
                    NOTIFY_ERROR("Failed to pause print: {}", err.user_message());
                });
        } else {
            // Fall back to local state change for mock mode
            spdlog::warn("[{}] API not available - using local state change", get_name());
            set_state(PrintState::Paused);
        }
    } else if (current_state_ == PrintState::Paused) {
        spdlog::info("[{}] Resuming print...", get_name());

        // Check if resume slot is available
        const auto& resume_info = StandardMacros::instance().get(StandardMacroSlot::Resume);
        if (resume_info.is_empty()) {
            spdlog::warn("[{}] Resume macro slot is empty", get_name());
            NOTIFY_WARNING("Resume macro not configured");
            return;
        }

        if (api_) {
            spdlog::info("[{}] Using StandardMacros resume: {}", get_name(),
                         resume_info.get_macro());
            // Stateless callbacks to avoid use-after-free if panel destroyed [L012]
            StandardMacros::instance().execute(
                StandardMacroSlot::Resume, api_,
                []() {
                    spdlog::info("[Print Status] Resume command sent successfully");
                    // State will update via PrinterState observer when Moonraker confirms
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[Print Status] Failed to resume print: {}", err.message);
                    NOTIFY_ERROR("Failed to resume print: {}", err.user_message());
                });
        } else {
            // Fall back to local state change for mock mode
            spdlog::warn("[{}] API not available - using local state change", get_name());
            set_state(PrintState::Printing);
        }
    }
}

void PrintStatusPanel::handle_tune_button() {
    spdlog::info("[{}] Tune button clicked - opening tuning panel", get_name());

    // Create tune panel on first access (lazy initialization)
    if (!tune_panel_ && parent_screen_) {
        spdlog::debug("[{}] Creating tuning panel...", get_name());

        tune_panel_ =
            static_cast<lv_obj_t*>(lv_xml_create(parent_screen_, "print_tune_panel", nullptr));
        if (tune_panel_) {
            setup_tune_panel(tune_panel_);
            lv_obj_add_flag(tune_panel_, LV_OBJ_FLAG_HIDDEN);
            spdlog::info("[{}] Tuning panel created and initialized", get_name());
        } else {
            spdlog::error("[{}] Failed to create tuning panel from XML", get_name());
            NOTIFY_ERROR("Failed to load tuning panel");
            return;
        }
    }

    // Update displays with current values before showing
    update_tune_display();

    // Set slider values to current PrinterState values
    if (tune_panel_) {
        lv_obj_t* overlay_content = lv_obj_find_by_name(tune_panel_, "overlay_content");
        if (overlay_content) {
            lv_obj_t* speed_slider = lv_obj_find_by_name(overlay_content, "speed_slider");
            lv_obj_t* flow_slider = lv_obj_find_by_name(overlay_content, "flow_slider");

            if (speed_slider) {
                lv_slider_set_value(speed_slider, speed_percent_, LV_ANIM_OFF);
            }
            if (flow_slider) {
                lv_slider_set_value(flow_slider, flow_percent_, LV_ANIM_OFF);
            }
        }
        ui_nav_push_overlay(tune_panel_);
    }
}

void PrintStatusPanel::handle_cancel_button() {
    spdlog::info("[{}] Cancel button clicked - showing confirmation dialog", get_name());

    // Check if AbortManager is idle (not already aborting)
    if (helix::AbortManager::instance().is_aborting()) {
        spdlog::warn("[{}] Abort already in progress", get_name());
        NOTIFY_WARNING("Abort already in progress");
        return;
    }

    // Set up the confirm callback to start the abort process
    cancel_modal_.set_on_confirm([]() {
        spdlog::info("[PrintStatusPanel] Cancel confirmed - starting AbortManager");

        // AbortManager handles its own UI state (progress modal, button states)
        helix::AbortManager::instance().start_abort();
    });

    // Show the modal (RAII handles cleanup)
    cancel_modal_.show(lv_screen_active());
}

void PrintStatusPanel::handle_reprint_button() {
    spdlog::info("[{}] Reprint button clicked - reprinting: {}", get_name(),
                 current_print_filename_);

    if (current_print_filename_.empty()) {
        spdlog::warn("[{}] No filename to reprint", get_name());
        NOTIFY_WARNING("No file to reprint");
        return;
    }

    if (!api_) {
        spdlog::error("[{}] Cannot reprint: API not available", get_name());
        NOTIFY_ERROR("Cannot reprint: not connected to printer");
        return;
    }

    // Disable button immediately to prevent double-press
    if (btn_cancel_) {
        lv_obj_add_state(btn_cancel_, LV_STATE_DISABLED);
        lv_obj_set_style_opa(btn_cancel_, LV_OPA_50, LV_PART_MAIN);
    }

    // Capture variables for async callback [L012]
    auto alive = m_alive;
    std::string filename = current_print_filename_;

    api_->start_print(
        filename,
        [this, alive, filename]() {
            spdlog::info("[{}] Reprint started: {}", get_name(), filename);
            // State will update via PrinterState observer when Moonraker confirms
            // Button will transform back to Cancel mode when state changes to Printing
        },
        [this, alive](const MoonrakerError& err) {
            spdlog::error("[{}] Failed to reprint: {}", get_name(), err.message);
            NOTIFY_ERROR("Failed to reprint: {}", err.user_message());
            // Re-enable button on failure (with lifetime guard)
            if (!alive->load())
                return;
            if (btn_cancel_) {
                lv_obj_remove_state(btn_cancel_, LV_STATE_DISABLED);
                lv_obj_set_style_opa(btn_cancel_, LV_OPA_COVER, LV_PART_MAIN);
            }
        });
}

void PrintStatusPanel::handle_resize() {
    spdlog::debug("[{}] Handling resize event", get_name());

    // Reset gcode viewer camera to fit new dimensions
    if (gcode_viewer_ && !lv_obj_has_flag(gcode_viewer_, LV_OBJ_FLAG_HIDDEN)) {
        // Force layout recalculation so viewer gets correct dimensions
        lv_obj_update_layout(gcode_viewer_);
        ui_gcode_viewer_reset_camera(gcode_viewer_);
        spdlog::debug("[{}] Reset gcode viewer camera after resize", get_name());
    }
}

// ============================================================================
// STATIC TRAMPOLINES
// ============================================================================

void PrintStatusPanel::on_nozzle_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_nozzle_card_clicked");
    (void)e;
    get_global_print_status_panel().handle_nozzle_card_click();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_bed_card_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_bed_card_clicked");
    (void)e;
    get_global_print_status_panel().handle_bed_card_click();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_light_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_light_clicked");
    (void)e;
    get_global_print_status_panel().handle_light_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_timelapse_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_timelapse_clicked");
    (void)e;
    get_global_print_status_panel().handle_timelapse_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_pause_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_pause_clicked");
    (void)e;
    get_global_print_status_panel().handle_pause_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_tune_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_tune_clicked");
    (void)e;
    get_global_print_status_panel().handle_tune_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_cancel_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_cancel_clicked");
    (void)e;
    get_global_print_status_panel().handle_cancel_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_reprint_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrintStatusPanel] on_reprint_clicked");
    (void)e;
    get_global_print_status_panel().handle_reprint_button();
    LVGL_SAFE_EVENT_CB_END();
}

void PrintStatusPanel::on_resize_static() {
    // Use global instance for resize callback (registered without user_data)
    if (g_print_status_panel) {
        g_print_status_panel->handle_resize();
    }
}

// ============================================================================
// PRINTERSTATE OBSERVER CALLBACKS
// ============================================================================

void PrintStatusPanel::extruder_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::extruder_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::bed_temp_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::bed_target_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    (void)subject;
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_temperature_changed();
    }
}

void PrintStatusPanel::print_progress_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_progress_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        // Read enum from integer subject (type-safe, no string parsing)
        auto state = static_cast<PrintJobState>(lv_subject_get_int(subject));
        self->on_print_state_changed(state);
    }
}

void PrintStatusPanel::print_filename_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_filename_changed(lv_subject_get_string(subject));
    }
}

void PrintStatusPanel::speed_factor_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_speed_factor_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::flow_factor_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_flow_factor_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::gcode_z_offset_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_gcode_z_offset_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::led_state_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_led_state_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_layer_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_layer_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::excluded_objects_observer_cb(lv_observer_t* observer,
                                                    lv_subject_t* subject) {
    (void)subject; // Version number not needed, just signals a change
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_excluded_objects_changed();
    }
}

void PrintStatusPanel::print_duration_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_duration_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_time_left_observer_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_time_left_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_start_phase_observer_cb(lv_observer_t* observer,
                                                     lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_start_phase_changed(lv_subject_get_int(subject));
    }
}

void PrintStatusPanel::print_start_message_observer_cb(lv_observer_t* observer,
                                                       lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        auto* msg = lv_subject_get_string(subject);
        self->on_print_start_message_changed(msg);
    }
}

void PrintStatusPanel::print_start_progress_observer_cb(lv_observer_t* observer,
                                                        lv_subject_t* subject) {
    auto* self = static_cast<PrintStatusPanel*>(lv_observer_get_user_data(observer));
    if (self) {
        self->on_print_start_progress_changed(lv_subject_get_int(subject));
    }
}

// ============================================================================
// OBSERVER INSTANCE METHODS
// ============================================================================

void PrintStatusPanel::on_temperature_changed() {
    // Read all temperature values from PrinterState subjects
    nozzle_current_ = lv_subject_get_int(printer_state_.get_extruder_temp_subject());
    nozzle_target_ = lv_subject_get_int(printer_state_.get_extruder_target_subject());
    bed_current_ = lv_subject_get_int(printer_state_.get_bed_temp_subject());
    bed_target_ = lv_subject_get_int(printer_state_.get_bed_target_subject());

    update_all_displays();

    spdlog::trace("[{}] Temperatures updated: nozzle {}/{}°C, bed {}/{}°C", get_name(),
                  nozzle_current_, nozzle_target_, bed_current_, bed_target_);
}

void PrintStatusPanel::on_print_progress_changed(int progress) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send progress=0 when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring progress update ({}) in Complete state", get_name(), progress);
        return;
    }

    // Update progress display without calling update_all_displays()
    // to avoid redundant updates when multiple subjects change
    current_progress_ = progress;
    if (current_progress_ < 0)
        current_progress_ = 0;
    if (current_progress_ > 100)
        current_progress_ = 100;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    // Update progress text
    std::snprintf(progress_text_buf_, sizeof(progress_text_buf_), "%d%%", current_progress_);
    lv_subject_copy_string(&progress_text_subject_, progress_text_buf_);

    // Update progress bar with smooth animation (300ms ease-out) if animations enabled
    // This complements the subject binding with animated transitions
    if (progress_bar_) {
        lv_anim_enable_t anim_enable =
            SettingsManager::instance().get_animations_enabled() ? LV_ANIM_ON : LV_ANIM_OFF;
        lv_bar_set_value(progress_bar_, current_progress_, anim_enable);
    }

    spdlog::trace("[{}] Progress updated: {}%", get_name(), current_progress_);
}

void PrintStatusPanel::on_print_state_changed(PrintJobState job_state) {
    spdlog::debug("[{}] on_print_state_changed() job_state={} current_state_={}", get_name(),
                  static_cast<int>(job_state), static_cast<int>(current_state_));

    // Map PrintJobState (from PrinterState) to PrintState (UI-specific)
    // Note: PrintState has a Preparing state that doesn't exist in PrintJobState -
    // that's managed locally via end_preparing()
    PrintState new_state = PrintState::Idle;

    switch (job_state) {
    case PrintJobState::STANDBY:
        new_state = PrintState::Idle;
        break;
    case PrintJobState::PRINTING:
        new_state = PrintState::Printing;
        break;
    case PrintJobState::PAUSED:
        new_state = PrintState::Paused;
        break;
    case PrintJobState::COMPLETE:
        new_state = PrintState::Complete;
        break;
    case PrintJobState::CANCELLED:
        new_state = PrintState::Cancelled;
        break;
    case PrintJobState::ERROR:
        new_state = PrintState::Error;
        break;
    }

    // Note: Badge/Reprint button visibility is now handled via the print_outcome subject,
    // which persists the terminal state (Complete/Cancelled/Error) until a new print starts.
    // The print_state_enum subject now always reflects the true Moonraker state.

    // Only update if state actually changed
    if (new_state != current_state_) {
        PrintState old_state = current_state_;

        // Clear thumbnail and G-code tracking when print ends (Complete/Cancelled/Error/Idle)
        // This ensures they're available during the entire print but cleared for the next one
        bool print_ended =
            (new_state == PrintState::Complete || new_state == PrintState::Cancelled ||
             new_state == PrintState::Error || new_state == PrintState::Idle);
        if (print_ended) {
            if (!thumbnail_source_filename_.empty() || !loaded_thumbnail_filename_.empty() ||
                gcode_loaded_ || !temp_gcode_path_.empty() || !pending_gcode_filename_.empty()) {
                spdlog::debug("[{}] Clearing thumbnail/gcode tracking (print ended)", get_name());
                thumbnail_source_filename_.clear();
                loaded_thumbnail_filename_.clear();
                cached_thumbnail_path_.clear();
                pending_gcode_filename_.clear();
                gcode_loaded_ = false;
                cleanup_temp_gcode();

                // Note: Shared subjects (print_thumbnail_path, print_display_filename)
                // are cleared by ActivePrintMediaManager when print_filename_ becomes empty
            }
        }

        set_state(new_state);
        spdlog::info("[{}] Print state changed: {} -> {}", get_name(),
                     print_job_state_to_string(job_state), static_cast<int>(new_state));

        // Toggle G-code viewer visibility based on print state
        // Show 3D/2D viewer during preparing/printing/paused ONLY if G-code was successfully
        // loaded. If memory check failed (gcode_loaded_ = false), stay in thumbnail mode. On
        // completion, always show thumbnail.
        bool want_viewer = (new_state == PrintState::Preparing ||
                            new_state == PrintState::Printing || new_state == PrintState::Paused);
        bool show_viewer = want_viewer && gcode_loaded_;
        show_gcode_viewer(show_viewer);

        // Check for runout condition when entering Paused state
        if (new_state == PrintState::Paused) {
            check_and_show_runout_guidance();
        }

        // Reset runout modal flag when resuming print
        if (new_state == PrintState::Printing) {
            runout_modal_shown_for_pause_ = false;
            hide_runout_guidance_modal();

            // Reset progress bar on new print start (not resume from pause).
            // Without this, the bar animates from its old position to the new value,
            // showing only a partial segment (e.g., 50%→75% instead of 0%→75%).
            if (old_state != PrintState::Paused && progress_bar_) {
                lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
                spdlog::debug("[{}] Reset progress bar for new print", get_name());
            }
        }

        // Show print complete overlay when entering Complete state
        if (new_state == PrintState::Complete) {
            // Ensure progress shows 100% on completion
            if (current_progress_ < 100) {
                current_progress_ = 100;
                std::snprintf(progress_text_buf_, sizeof(progress_text_buf_), "100%%");
                lv_subject_copy_string(&progress_text_subject_, progress_text_buf_);
            }

            // Trigger celebratory animation on the success badge
            animate_print_complete();

            spdlog::info("[{}] Print complete! Final progress: {}%, elapsed: {}s", get_name(),
                         current_progress_, elapsed_seconds_);
        }

        // Show print cancelled overlay when entering Cancelled state
        if (new_state == PrintState::Cancelled) {
            animate_print_cancelled();
            spdlog::info("[{}] Print cancelled at progress: {}%", get_name(), current_progress_);
        }

        // Update e-stop button visibility: show only during active print
        // (Preparing/Printing/Paused), hide when idle or finished
        if (overlay_header_) {
            bool show_estop =
                (new_state == PrintState::Preparing || new_state == PrintState::Printing ||
                 new_state == PrintState::Paused);
            if (show_estop) {
                ui_header_bar_show_action_button(overlay_header_);
            } else {
                ui_header_bar_hide_action_button(overlay_header_);
            }
            spdlog::debug("[{}] E-stop button {} (state={})", get_name(),
                          show_estop ? "shown" : "hidden", static_cast<int>(new_state));
        }
    }
}

void PrintStatusPanel::on_print_filename_changed(const char* filename) {
    // Check if this is a non-empty filename (new print starting)
    bool has_filename = filename && filename[0] != '\0';

    // Guard: preserve final values when in Complete state and filename is empty
    // Moonraker sends empty filename when transitioning to Standby, but we want
    // to keep showing the completed print's filename. However, if a NEW print
    // starts (non-empty filename), we should accept it even if current_state_
    // hasn't been updated yet (race condition between state and filename observers)
    if (current_state_ == PrintState::Complete && !has_filename) {
        spdlog::trace("[{}] Ignoring empty filename update in Complete state", get_name());
        return;
    }

    if (has_filename) {
        std::string raw_filename = filename;

        // Auto-resolve temp file patterns to original filename.
        // This handles the race condition where Moonraker reports the temp path
        // (e.g., .helix_temp/modified_*) before set_thumbnail_source() is called.
        // Common when Helix plugin is not installed or during direct Moonraker prints.
        std::string resolved = resolve_gcode_filename(raw_filename);
        if (resolved != raw_filename && thumbnail_source_filename_.empty()) {
            spdlog::debug("[{}] Auto-resolved temp filename: {} -> {}", get_name(), raw_filename,
                          resolved);
            set_thumbnail_source(resolved);
        }

        // Call set_filename() which is idempotent (won't reload if effective filename unchanged)
        // Only log when filename actually changes to avoid log spam
        if (raw_filename != current_print_filename_) {
            spdlog::debug("[{}] Filename changed: {}", get_name(), raw_filename);
        }
        set_filename(filename);
    }
}

void PrintStatusPanel::on_speed_factor_changed(int speed) {
    speed_percent_ = speed;
    if (subjects_initialized_) {
        std::snprintf(speed_buf_, sizeof(speed_buf_), "%d%%", speed_percent_);
        lv_subject_copy_string(&speed_subject_, speed_buf_);
    }
    spdlog::trace("[{}] Speed factor updated: {}%", get_name(), speed);
}

void PrintStatusPanel::on_flow_factor_changed(int flow) {
    flow_percent_ = flow;
    if (subjects_initialized_) {
        std::snprintf(flow_buf_, sizeof(flow_buf_), "%d%%", flow_percent_);
        lv_subject_copy_string(&flow_subject_, flow_buf_);
    }
    spdlog::trace("[{}] Flow factor updated: {}%", get_name(), flow);
}

void PrintStatusPanel::on_gcode_z_offset_changed(int microns) {
    // Update display from PrinterState (microns -> mm)
    current_z_offset_ = microns / 1000.0;
    if (subjects_initialized_) {
        std::snprintf(tune_z_offset_buf_, sizeof(tune_z_offset_buf_), "%.3fmm", current_z_offset_);
        lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);
    }
    spdlog::trace("[{}] G-code Z-offset updated: {}µm ({}mm)", get_name(), microns,
                  current_z_offset_);
}

void PrintStatusPanel::on_led_state_changed(int state) {
    led_on_ = (state != 0);

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    // Update light button icon: lightbulb_on (F06E8) or lightbulb_outline (F0336)
    if (led_on_) {
        std::snprintf(light_button_buf_, sizeof(light_button_buf_), "\xF3\xB0\x9B\xA8");
    } else {
        std::snprintf(light_button_buf_, sizeof(light_button_buf_), "\xF3\xB0\x8C\xB6");
    }
    lv_subject_copy_string(&light_button_subject_, light_button_buf_);

    spdlog::debug("[{}] LED state changed: {} (from PrinterState)", get_name(),
                  led_on_ ? "ON" : "OFF");
}

void PrintStatusPanel::on_print_layer_changed(int current_layer) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send layer=0 when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring layer update ({}) in Complete state", get_name(),
                      current_layer);
        return;
    }

    // Update internal layer state
    current_layer_ = current_layer;
    int total_layers = lv_subject_get_int(printer_state_.get_print_layer_total_subject());
    total_layers_ = total_layers;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    // Update the layer text display
    std::snprintf(layer_text_buf_, sizeof(layer_text_buf_), "Layer %d / %d", current_layer_,
                  total_layers_);
    lv_subject_copy_string(&layer_text_subject_, layer_text_buf_);

    // Update G-code viewer ghost layer if viewer is active and visible
    if (gcode_viewer_ && !lv_obj_has_flag(gcode_viewer_, LV_OBJ_FLAG_HIDDEN)) {
        // Map from Moonraker layer count (e.g., 240) to viewer layer count (e.g., 2912)
        // The slicer metadata and parsed G-code often have different layer counts
        int viewer_max_layer = ui_gcode_viewer_get_max_layer(gcode_viewer_);
        int viewer_layer = current_layer;
        if (total_layers_ > 0 && viewer_max_layer > 0) {
            viewer_layer = (current_layer * viewer_max_layer) / total_layers_;
        }

        // CRITICAL: Defer to avoid lv_obj_invalidate() during render phase
        // Observer callbacks can fire during lv_timer_handler() which may be mid-render
        struct ViewerProgressCtx {
            lv_obj_t* viewer;
            int layer;
        };
        auto* ctx = new ViewerProgressCtx{gcode_viewer_, viewer_layer};
        ui_async_call(
            [](void* user_data) {
                auto* c = static_cast<ViewerProgressCtx*>(user_data);
                if (c->viewer && lv_obj_is_valid(c->viewer)) {
                    ui_gcode_viewer_set_print_progress(c->viewer, c->layer);
                }
                delete c;
            },
            ctx);

        spdlog::trace("[{}] G-code viewer ghost layer updated to {} (Moonraker: {}/{})", get_name(),
                      viewer_layer, current_layer, total_layers_);
    }
}

void PrintStatusPanel::on_excluded_objects_changed() {
    // Sync excluded objects from PrinterState (Klipper/Moonraker)
    const auto& klipper_excluded = printer_state_.get_excluded_objects();

    // Merge Klipper's excluded set with our local set
    // This ensures objects excluded via Klipper (e.g., from another client) are shown
    for (const auto& obj : klipper_excluded) {
        if (excluded_objects_.count(obj) == 0) {
            excluded_objects_.insert(obj);
            spdlog::info("[{}] Synced excluded object from Klipper: '{}'", get_name(), obj);
        }
    }

    // Update the G-code viewer visual state
    if (gcode_viewer_) {
        // Combine confirmed excluded with any pending exclusion for visual display
        std::unordered_set<std::string> visual_excluded = excluded_objects_;
        if (!pending_exclude_object_.empty()) {
            visual_excluded.insert(pending_exclude_object_);
        }
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, visual_excluded);
        spdlog::debug("[{}] Updated viewer with {} excluded objects", get_name(),
                      visual_excluded.size());
    }
}

void PrintStatusPanel::on_print_duration_changed(int seconds) {
    // Guard: preserve final values when in Complete state
    // Moonraker may send duration=0 when transitioning to Standby
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring duration update ({}) in Complete state", get_name(), seconds);
        return;
    }

    elapsed_seconds_ = seconds;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    format_time(elapsed_seconds_, elapsed_buf_, sizeof(elapsed_buf_));
    lv_subject_copy_string(&elapsed_subject_, elapsed_buf_);
    spdlog::trace("[{}] Print duration updated: {}s", get_name(), seconds);
}

void PrintStatusPanel::on_print_time_left_changed(int seconds) {
    // Guard: preserve final values when in Complete state
    if (current_state_ == PrintState::Complete) {
        spdlog::trace("[{}] Ignoring time_left update ({}) in Complete state", get_name(), seconds);
        return;
    }

    remaining_seconds_ = seconds;

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    format_time(remaining_seconds_, remaining_buf_, sizeof(remaining_buf_));
    lv_subject_copy_string(&remaining_subject_, remaining_buf_);
    spdlog::trace("[{}] Time remaining updated: {}s", get_name(), seconds);
}

void PrintStatusPanel::on_print_start_phase_changed(int phase) {
    // Phase 0 = IDLE (not preparing), non-zero = preparing
    bool preparing = (phase != 0);

    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    lv_subject_set_int(&preparing_visible_subject_, preparing ? 1 : 0);

    if (preparing) {
        current_state_ = PrintState::Preparing;
    }
    spdlog::debug("[{}] Print start phase changed: {} (visible={})", get_name(), phase, preparing);
}

void PrintStatusPanel::on_print_start_message_changed(const char* message) {
    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    if (message) {
        strncpy(preparing_operation_buf_, message, sizeof(preparing_operation_buf_) - 1);
        preparing_operation_buf_[sizeof(preparing_operation_buf_) - 1] = '\0';
        lv_subject_copy_string(&preparing_operation_subject_, preparing_operation_buf_);
        spdlog::trace("[{}] Print start message: {}", get_name(), message);
    }
}

void PrintStatusPanel::on_print_start_progress_changed(int progress) {
    // Guard: subjects may not be initialized if called from constructor's observer setup
    if (!subjects_initialized_) {
        return;
    }

    lv_subject_set_int(&preparing_progress_subject_, progress);

    // Animate bar for smooth visual feedback
    if (preparing_progress_bar_) {
        lv_anim_enable_t anim_enable =
            SettingsManager::instance().get_animations_enabled() ? LV_ANIM_ON : LV_ANIM_OFF;
        lv_bar_set_value(preparing_progress_bar_, progress, anim_enable);
    }
    spdlog::trace("[{}] Print start progress: {}%", get_name(), progress);
}

// ============================================================================
// TUNE PANEL HELPERS
// ============================================================================

void PrintStatusPanel::setup_tune_panel(lv_obj_t* panel) {
    // Use standard overlay panel setup for back button handling
    ui_overlay_panel_setup_standard(panel, parent_screen_, "overlay_header", "overlay_content");

    // Event handlers are registered via XML event_cb declarations
    // (on_tune_speed_changed, on_tune_flow_changed, on_tune_reset_clicked, on_tune_z_offset)
    // Callbacks registered in init_subjects() via lv_xml_register_event_cb()

    // Update Z-offset icons based on printer kinematics
    update_z_offset_icons(panel);

    spdlog::debug("[{}] Tune panel setup complete (events wired via XML)", get_name());
}

void PrintStatusPanel::update_z_offset_icons(lv_obj_t* panel) {
    // Get kinematics type from PrinterState
    // 0 = unknown, 1 = bed moves Z (CoreXY), 2 = head moves Z (Cartesian/Delta)
    int kin = lv_subject_get_int(printer_state_.get_printer_bed_moves_subject());
    bool bed_moves_z = (kin == 1);

    // Select icon codepoints based on kinematics
    // CoreXY (bed moves): expand icons show bed motion
    // Cartesian/Delta (head moves): arrow icons show head motion
    const char* closer_icon =
        bed_moves_z ? "\xF3\xB0\x9E\x93" : "\xF3\xB0\x81\x85"; // arrow-expand-down : arrow-down
    const char* farther_icon =
        bed_moves_z ? "\xF3\xB0\x9E\x96" : "\xF3\xB0\x81\x9D"; // arrow-expand-up : arrow-up

    // Find and update all closer icons (3 buttons)
    const char* closer_names[] = {"icon_z_closer_01", "icon_z_closer_005", "icon_z_closer_001"};
    for (const char* name : closer_names) {
        lv_obj_t* icon = lv_obj_find_by_name(panel, name);
        if (icon) {
            lv_label_set_text(icon, closer_icon);
        }
    }

    // Find and update all farther icons (3 buttons)
    const char* farther_names[] = {"icon_z_farther_001", "icon_z_farther_005", "icon_z_farther_01"};
    for (const char* name : farther_names) {
        lv_obj_t* icon = lv_obj_find_by_name(panel, name);
        if (icon) {
            lv_label_set_text(icon, farther_icon);
        }
    }

    spdlog::debug("[{}] Z-offset icons set for {} kinematics", get_name(),
                  bed_moves_z ? "bed-moves-Z" : "head-moves-Z");
}

void PrintStatusPanel::update_tune_display() {
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "%d%%", speed_percent_);
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "%d%%", flow_percent_);
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);
}

void PrintStatusPanel::update_button_states() {
    // Buttons should only be enabled during Printing or Paused states
    // When Complete, Cancelled, Error, or Idle - disable print control buttons
    bool buttons_enabled =
        (current_state_ == PrintState::Printing || current_state_ == PrintState::Paused);

    // Helper lambda for enable/disable with visual feedback
    auto set_button_enabled = [](lv_obj_t* btn, bool enabled) {
        if (!btn)
            return;
        if (enabled) {
            lv_obj_remove_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        } else {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_opa(btn, LV_OPA_50, LV_PART_MAIN);
        }
    };

    // Timelapse and tune buttons don't depend on StandardMacros
    set_button_enabled(btn_timelapse_, buttons_enabled);
    set_button_enabled(btn_tune_, buttons_enabled);

    // Pause/Resume button: check slot availability based on current state
    // In Printing state: need Pause slot; in Paused state: need Resume slot
    bool pause_button_enabled = buttons_enabled;
    if (buttons_enabled) {
        if (current_state_ == PrintState::Printing) {
            const auto& pause_info = StandardMacros::instance().get(StandardMacroSlot::Pause);
            pause_button_enabled = !pause_info.is_empty();
        } else if (current_state_ == PrintState::Paused) {
            const auto& resume_info = StandardMacros::instance().get(StandardMacroSlot::Resume);
            pause_button_enabled = !resume_info.is_empty();
        }
    }
    set_button_enabled(btn_pause_, pause_button_enabled);

    // Cancel button: check if Cancel slot is available
    bool cancel_button_enabled = buttons_enabled;
    if (buttons_enabled) {
        const auto& cancel_info = StandardMacros::instance().get(StandardMacroSlot::Cancel);
        cancel_button_enabled = !cancel_info.is_empty();
    }
    set_button_enabled(btn_cancel_, cancel_button_enabled);

    spdlog::debug("[{}] Button states updated: base={}, pause={}, cancel={} (state={})", get_name(),
                  buttons_enabled ? "enabled" : "disabled",
                  pause_button_enabled ? "enabled" : "disabled",
                  cancel_button_enabled ? "enabled" : "disabled", static_cast<int>(current_state_));
}

void PrintStatusPanel::animate_print_complete() {
    if (!success_badge_) {
        return;
    }

    // Skip animation if disabled - show badge in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        constexpr int32_t SCALE_FINAL = 256; // 100% scale
        lv_obj_set_style_transform_scale(success_badge_, SCALE_FINAL, LV_PART_MAIN);
        lv_obj_set_style_opa(success_badge_, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[{}] Animations disabled - showing success badge instantly", get_name());
        return;
    }

    // Animation constants for celebration effect
    // Stage 1: Quick scale-up with overshoot (300ms)
    // Stage 2: Settle to final size (150ms)
    constexpr int32_t CELEBRATION_DURATION_MS = 300;
    constexpr int32_t SETTLE_DURATION_MS = 150;
    constexpr int32_t SCALE_START = 128;     // 50% scale (128/256)
    constexpr int32_t SCALE_OVERSHOOT = 282; // ~110% scale (slight overshoot)
    constexpr int32_t SCALE_FINAL = 256;     // 100% scale (256/256)

    // Start badge small and transparent
    lv_obj_set_style_transform_scale(success_badge_, SCALE_START, LV_PART_MAIN);
    lv_obj_set_style_opa(success_badge_, LV_OPA_TRANSP, LV_PART_MAIN);

    // Stage 1: Scale up with overshoot + fade in
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, success_badge_);
    lv_anim_set_values(&scale_anim, SCALE_START, SCALE_OVERSHOOT);
    lv_anim_set_duration(&scale_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    // Fade in animation (parallel with scale)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, success_badge_);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    // Stage 2: Settle from overshoot to final size (delayed start)
    lv_anim_t settle_anim;
    lv_anim_init(&settle_anim);
    lv_anim_set_var(&settle_anim, success_badge_);
    lv_anim_set_values(&settle_anim, SCALE_OVERSHOOT, SCALE_FINAL);
    lv_anim_set_duration(&settle_anim, SETTLE_DURATION_MS);
    lv_anim_set_delay(&settle_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&settle_anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&settle_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&settle_anim);

    spdlog::debug("[{}] Print complete celebration animation started", get_name());
}

void PrintStatusPanel::animate_print_cancelled() {
    if (!cancel_badge_) {
        return;
    }

    // Skip animation if disabled - show badge in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        constexpr int32_t SCALE_FINAL = 256; // 100% scale
        lv_obj_set_style_transform_scale(cancel_badge_, SCALE_FINAL, LV_PART_MAIN);
        lv_obj_set_style_opa(cancel_badge_, LV_OPA_COVER, LV_PART_MAIN);
        spdlog::debug("[{}] Animations disabled - showing cancel badge instantly", get_name());
        return;
    }

    // Animation constants - same pop-in effect as completion
    constexpr int32_t CELEBRATION_DURATION_MS = 300;
    constexpr int32_t SETTLE_DURATION_MS = 150;
    constexpr int32_t SCALE_START = 128;     // 50% scale (128/256)
    constexpr int32_t SCALE_OVERSHOOT = 282; // ~110% scale (slight overshoot)
    constexpr int32_t SCALE_FINAL = 256;     // 100% scale (256/256)

    // Start badge small and transparent
    lv_obj_set_style_transform_scale(cancel_badge_, SCALE_START, LV_PART_MAIN);
    lv_obj_set_style_opa(cancel_badge_, LV_OPA_TRANSP, LV_PART_MAIN);

    // Stage 1: Scale up with overshoot + fade in
    lv_anim_t scale_anim;
    lv_anim_init(&scale_anim);
    lv_anim_set_var(&scale_anim, cancel_badge_);
    lv_anim_set_values(&scale_anim, SCALE_START, SCALE_OVERSHOOT);
    lv_anim_set_duration(&scale_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
    lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&scale_anim);

    // Fade in animation (parallel with scale)
    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, cancel_badge_);
    lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&fade_anim);

    // Stage 2: Settle from overshoot to final size (delayed start)
    lv_anim_t settle_anim;
    lv_anim_init(&settle_anim);
    lv_anim_set_var(&settle_anim, cancel_badge_);
    lv_anim_set_values(&settle_anim, SCALE_OVERSHOOT, SCALE_FINAL);
    lv_anim_set_duration(&settle_anim, SETTLE_DURATION_MS);
    lv_anim_set_delay(&settle_anim, CELEBRATION_DURATION_MS);
    lv_anim_set_path_cb(&settle_anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&settle_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
    });
    lv_anim_start(&settle_anim);

    spdlog::debug("[{}] Print cancelled animation started", get_name());
}

void PrintStatusPanel::handle_tune_speed_changed(int value) {
    // Update display immediately for responsive feel
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "%d%%", value);
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);

    // Send G-code command
    if (api_) {
        std::string gcode = "M220 S" + std::to_string(value);
        api_->execute_gcode(
            gcode, [value]() { spdlog::debug("[PrintStatusPanel] Speed set to {}%", value); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Failed to set speed: {}", err.message);
                NOTIFY_ERROR("Failed to set print speed: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_flow_changed(int value) {
    // Update display immediately for responsive feel
    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "%d%%", value);
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);

    // Send G-code command
    if (api_) {
        std::string gcode = "M221 S" + std::to_string(value);
        api_->execute_gcode(
            gcode, [value]() { spdlog::debug("[PrintStatusPanel] Flow set to {}%", value); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Failed to set flow: {}", err.message);
                NOTIFY_ERROR("Failed to set flow rate: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_reset() {
    if (!tune_panel_) {
        return;
    }

    lv_obj_t* overlay_content = lv_obj_find_by_name(tune_panel_, "overlay_content");
    if (!overlay_content) {
        return;
    }

    // Reset sliders to 100%
    lv_obj_t* speed_slider = lv_obj_find_by_name(overlay_content, "speed_slider");
    lv_obj_t* flow_slider = lv_obj_find_by_name(overlay_content, "flow_slider");

    if (speed_slider) {
        lv_slider_set_value(speed_slider, 100, LV_ANIM_ON);
    }
    if (flow_slider) {
        lv_slider_set_value(flow_slider, 100, LV_ANIM_ON);
    }

    // Update displays
    std::snprintf(tune_speed_buf_, sizeof(tune_speed_buf_), "100%%");
    lv_subject_copy_string(&tune_speed_subject_, tune_speed_buf_);
    std::snprintf(tune_flow_buf_, sizeof(tune_flow_buf_), "100%%");
    lv_subject_copy_string(&tune_flow_subject_, tune_flow_buf_);

    // Send G-code commands
    if (api_) {
        api_->execute_gcode(
            "M220 S100", []() { spdlog::debug("[PrintStatusPanel] Speed reset to 100%"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Failed to reset speed: {}", err.user_message());
            });
        api_->execute_gcode(
            "M221 S100", []() { spdlog::debug("[PrintStatusPanel] Flow reset to 100%"); },
            [](const MoonrakerError& err) {
                NOTIFY_ERROR("Failed to reset flow: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_z_offset_changed(double delta) {
    // Update local display immediately for responsive feel
    current_z_offset_ += delta;
    std::snprintf(tune_z_offset_buf_, sizeof(tune_z_offset_buf_), "%.3fmm", current_z_offset_);
    lv_subject_copy_string(&tune_z_offset_subject_, tune_z_offset_buf_);

    // Track pending delta for "unsaved adjustment" notification in Controls panel
    int delta_microns = static_cast<int>(delta * 1000.0);
    get_printer_state().add_pending_z_offset_delta(delta_microns);

    spdlog::debug("[{}] Z-offset adjust: {:+.3f}mm (total: {:.3f}mm)", get_name(), delta,
                  current_z_offset_);

    // Send SET_GCODE_OFFSET Z_ADJUST command to Klipper
    if (api_) {
        char gcode[64];
        std::snprintf(gcode, sizeof(gcode), "SET_GCODE_OFFSET Z_ADJUST=%.3f", delta);
        api_->execute_gcode(
            gcode, [delta]() { spdlog::debug("[PrintStatusPanel] Z adjusted {:+.3f}mm", delta); },
            [](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Z-offset adjust failed: {}", err.message);
                NOTIFY_ERROR("Z-offset failed: {}", err.user_message());
            });
    }
}

void PrintStatusPanel::handle_tune_save_z_offset() {
    // Show warning modal - SAVE_CONFIG restarts Klipper and cancels active prints!
    save_z_offset_modal_.set_on_confirm([this]() {
        if (api_) {
            api_->execute_gcode(
                "SAVE_CONFIG",
                []() {
                    spdlog::info("[PrintStatusPanel] Z-offset saved - Klipper restarting");
                    ui_toast_show(ToastSeverity::WARNING, "Z-offset saved - Klipper restarting...",
                                  5000);
                },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintStatusPanel] SAVE_CONFIG failed: {}", err.message);
                    NOTIFY_ERROR("Save failed: {}", err.user_message());
                });
        }
    });
    save_z_offset_modal_.show(lv_screen_active());
}

// ============================================================================
// XML EVENT CALLBACKS (free functions using global accessor)
// ============================================================================

static void on_tune_speed_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_global_print_status_panel().handle_tune_speed_changed(value);
    }
}

static void on_tune_flow_changed_cb(lv_event_t* e) {
    lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (slider) {
        int value = lv_slider_get_value(slider);
        get_global_print_status_panel().handle_tune_flow_changed(value);
    }
}

static void on_tune_reset_clicked_cb(lv_event_t* /*e*/) {
    get_global_print_status_panel().handle_tune_reset();
}

/**
 * @brief Single callback for all Z-offset buttons
 *
 * Parses button name to determine direction and magnitude:
 *   - btn_z_closer_01  -> -0.1mm (closer = negative = more squish)
 *   - btn_z_closer_005 -> -0.05mm
 *   - btn_z_closer_001 -> -0.01mm
 *   - btn_z_farther_001 -> +0.01mm (farther = positive = less squish)
 *   - btn_z_farther_005 -> +0.05mm
 *   - btn_z_farther_01 -> +0.1mm
 */
static void on_tune_z_offset_cb(lv_event_t* e) {
    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!btn) {
        return;
    }

    // Get button name to determine delta
    const char* name = lv_obj_get_name(btn);
    if (!name) {
        spdlog::warn("[on_tune_z_offset_cb] Button has no name");
        return;
    }

    // Parse direction: "closer" = negative, "farther" = positive
    double delta = 0.0;
    bool is_closer = (strstr(name, "closer") != nullptr);
    bool is_farther = (strstr(name, "farther") != nullptr);

    if (!is_closer && !is_farther) {
        spdlog::warn("[on_tune_z_offset_cb] Unknown button name: {}", name);
        return;
    }

    // Parse magnitude from suffix: "_01" = 0.1, "_005" = 0.05, "_001" = 0.01
    if (strstr(name, "_01") && !strstr(name, "_001")) {
        delta = 0.1;
    } else if (strstr(name, "_005")) {
        delta = 0.05;
    } else if (strstr(name, "_001")) {
        delta = 0.01;
    } else {
        spdlog::warn("[on_tune_z_offset_cb] Unknown delta in button name: {}", name);
        return;
    }

    // Apply direction: closer = more squish = negative Z adjust
    if (is_closer) {
        delta = -delta;
    }

    spdlog::trace("[on_tune_z_offset_cb] Button '{}' -> delta {:+.3f}mm", name, delta);
    get_global_print_status_panel().handle_tune_z_offset_changed(delta);
}

static void on_tune_save_z_offset_cb(lv_event_t* /*e*/) {
    get_global_print_status_panel().handle_tune_save_z_offset();
}

// ============================================================================
// THUMBNAIL LOADING
// ============================================================================

void PrintStatusPanel::load_thumbnail_for_file(const std::string& filename) {
    // Increment generation to invalidate any in-flight async operations
    ++thumbnail_load_generation_;
    uint32_t current_gen = thumbnail_load_generation_;

    // Skip if no API available (e.g., in mock mode)
    if (!api_) {
        spdlog::debug("[{}] No API available - skipping thumbnail load", get_name());
        return;
    }

    // Note: We intentionally do NOT skip if print_thumbnail_ is null.
    // The thumbnail must still be fetched and cached so that:
    // 1. The shared print_thumbnail_path is set for HomePanel to use
    // 2. The thumbnail is ready when PrintStatusPanel is later displayed
    // The lv_image_set_src() call is guarded separately below.

    // Resolve to original filename if this is a modified temp file
    // (Moonraker only has metadata for original files, not modified copies)
    std::string metadata_filename = resolve_gcode_filename(filename);

    // Capture alive flag for shutdown safety [L012]
    auto alive = m_alive;

    // First, get file metadata to find thumbnail path
    api_->get_file_metadata(
        metadata_filename,
        [this, alive, current_gen](const FileMetadata& metadata) {
            // Abort if panel was destroyed during async operation
            if (!alive->load()) {
                return;
            }
            // Check if this callback is still relevant
            if (current_gen != thumbnail_load_generation_) {
                spdlog::trace("[{}] Stale metadata callback (gen {} != {}), ignoring", get_name(),
                              current_gen, thumbnail_load_generation_);
                return;
            }

            // Note: Layer count from metadata is now set by ActivePrintMediaManager

            // Get the largest thumbnail available
            std::string thumbnail_rel_path = metadata.get_largest_thumbnail();
            if (thumbnail_rel_path.empty()) {
                spdlog::debug("[{}] No thumbnail available in metadata", get_name());
                return;
            }

            spdlog::debug("[{}] Found thumbnail: {}", get_name(), thumbnail_rel_path);

            // Note: We intentionally do NOT invalidate the cache here.
            // PrintSelectPanel already handles file modification detection and cache
            // invalidation when files are re-uploaded. Aggressive invalidation here
            // causes a race condition where Print Status deletes thumbnails that
            // Print Select just cached, resulting in placeholder thumbnails.

            // Use fetch_for_detail_view() for full-resolution PNG (not pre-scaled .bin)
            // The semantic API ensures we always get the right format for large views.
            // Create context with captured generation for validity checking.
            ThumbnailLoadContext ctx;
            ctx.alive = alive;
            ctx.generation = nullptr; // Using manual gen check below
            ctx.captured_gen = current_gen;

            get_thumbnail_cache().fetch_for_detail_view(
                api_, thumbnail_rel_path, ctx,
                [this, current_gen](const std::string& lvgl_path) {
                    // Note: alive check is done by fetch_for_detail_view's guard.
                    // We still need generation check since we passed nullptr for generation.
                    if (current_gen != thumbnail_load_generation_) {
                        spdlog::trace("[{}] Stale thumbnail callback (gen {} != {}), ignoring",
                                      get_name(), current_gen, thumbnail_load_generation_);
                        return;
                    }

                    // Store the cached path (without "A:" prefix for internal use)
                    cached_thumbnail_path_ = lvgl_path;

                    // Share the thumbnail path via PrinterState for other panels (e.g., HomePanel)
                    get_printer_state().set_print_thumbnail_path(lvgl_path);

                    if (print_thumbnail_) {
                        lv_image_set_src(print_thumbnail_, lvgl_path.c_str());
                        spdlog::info("[{}] Thumbnail loaded and displayed: {}", get_name(),
                                     lvgl_path);
                    } else {
                        spdlog::info("[{}] Thumbnail cached (panel not yet displayed): {}",
                                     get_name(), lvgl_path);
                    }
                },
                [this](const std::string& error) {
                    spdlog::warn("[{}] Failed to fetch thumbnail: {}", get_name(), error);
                });
        },
        [this, alive](const MoonrakerError& err) {
            if (!alive->load()) {
                return;
            }
            spdlog::debug("[{}] Failed to get file metadata: {}", get_name(), err.message);
        },
        true // silent - don't trigger RPC_ERROR event/toast
    );
}

// ============================================================================
// G-CODE VIEWER LOADING
// ============================================================================

void PrintStatusPanel::load_gcode_for_viewing(const std::string& filename) {
    spdlog::debug("[{}] Loading G-code for viewing: {}", get_name(), filename);

    // Skip if no viewer widget
    if (!gcode_viewer_) {
        spdlog::debug("[{}] No gcode_viewer_ widget - skipping G-code load", get_name());
        return;
    }

    // Skip if no API available
    if (!api_) {
        spdlog::debug("[{}] No API available - skipping G-code load", get_name());
        return;
    }

    // Check config option to disable 3D rendering entirely
    auto* cfg = Config::get_instance();
    bool gcode_3d_enabled = cfg->get<bool>("/display/gcode_3d_enabled", true);
    if (!gcode_3d_enabled) {
        spdlog::info("[{}] G-code 3D rendering disabled via config - using thumbnail only",
                     get_name());
        show_gcode_viewer(false); // Ensure thumbnail is shown, not empty viewer
        return;
    }

    // Generate temp file path - check if we already have a cached copy
    // Use persistent cache directory (not /tmp which may be RAM-backed on embedded)
    std::string cache_dir = get_helix_cache_dir("gcode_temp");
    if (cache_dir.empty()) {
        spdlog::warn("[{}] No writable cache directory - skipping G-code preview", get_name());
        show_gcode_viewer(false);
        return;
    }
    std::string temp_path =
        cache_dir + "/print_view_" + std::to_string(std::hash<std::string>{}(filename)) + ".gcode";

    // Check if file already exists and is non-empty (cached from previous session)
    std::ifstream cached_file(temp_path, std::ios::binary | std::ios::ate);
    if (cached_file && cached_file.tellg() > 0) {
        size_t cached_size = static_cast<size_t>(cached_file.tellg());
        cached_file.close();

        // Check if cached file is safe to render
        if (helix::is_gcode_2d_streaming_safe(cached_size)) {
            spdlog::info("[{}] Using cached G-code file ({} bytes): {}", get_name(), cached_size,
                         temp_path);
            temp_gcode_path_ = temp_path;
            load_gcode_file(temp_path.c_str());
            return;
        } else {
            spdlog::debug("[{}] Cached file too large for 2D streaming, removing", get_name());
            std::remove(temp_path.c_str());
        }
    }

    // Get file metadata to check size before downloading
    // This prevents OOM on memory-constrained devices like AD5M
    std::string metadata_filename = resolve_gcode_filename(filename);

    // Capture alive flag for shutdown safety [L012]
    auto alive = m_alive;

    api_->get_file_metadata(
        metadata_filename,
        [this, alive, filename, temp_path](const FileMetadata& metadata) {
            // Abort if panel was destroyed during async operation
            if (!alive->load()) {
                return;
            }
            // Check if 2D streaming rendering is safe for this file size + available RAM
            // 2D streaming has much lower memory requirements than 3D:
            // - Layer index: ~24 bytes per layer
            // - LRU cache: 1MB fixed
            // - Ghost buffer: display_width * display_height * 4 bytes
            // - File streams directly to disk (no memory spike during download)
            if (!helix::is_gcode_2d_streaming_safe(metadata.size)) {
                auto mem = helix::get_system_memory_info();
                spdlog::warn(
                    "[{}] G-code too large for 2D streaming: file={} bytes, available RAM={}MB "
                    "- using thumbnail only",
                    get_name(), metadata.size, mem.available_mb());
                // Revert to thumbnail mode since rendering is not safe
                show_gcode_viewer(false);
                return;
            }

            spdlog::debug("[{}] G-code size {} bytes - safe to render, streaming to disk...",
                          get_name(), metadata.size);

            // Clean up previous temp file if any
            if (!temp_gcode_path_.empty() && temp_gcode_path_ != temp_path) {
                std::remove(temp_gcode_path_.c_str());
                temp_gcode_path_.clear();
            }

            // Stream download directly to disk (no memory spike)
            // For mock mode, this copies from test_gcodes/ directory
            // For real mode, this streams from Moonraker using libhv's chunked download
            api_->download_file_to_path(
                "gcodes", filename, temp_path,
                [this, alive, temp_path](const std::string& path) {
                    // Abort if panel was destroyed during download [L012]
                    if (!alive->load()) {
                        return;
                    }
                    // Track the temp file for cleanup
                    temp_gcode_path_ = path;

                    spdlog::info("[{}] Streamed G-code to disk, loading into viewer: {}",
                                 get_name(), path);

                    // Load into the viewer widget
                    load_gcode_file(path.c_str());
                },
                [this, alive, filename](const MoonrakerError& err) {
                    // Abort if panel was destroyed during download [L012]
                    if (!alive->load()) {
                        return;
                    }
                    spdlog::warn("[{}] Failed to stream G-code for viewing '{}': {}", get_name(),
                                 filename, err.message);
                    // Revert to thumbnail mode on download failure
                    show_gcode_viewer(false);
                });
        },
        [this, alive, filename](const MoonrakerError& err) {
            // Abort if panel was destroyed during async operation [L012]
            if (!alive->load()) {
                return;
            }
            spdlog::debug("[{}] Failed to get G-code metadata for '{}': {} - skipping 3D render",
                          get_name(), filename, err.message);
            // Revert to thumbnail mode on metadata fetch failure
            show_gcode_viewer(false);
        },
        true // silent - don't trigger RPC_ERROR event/toast
    );
}

// ============================================================================
// PUBLIC API
// ============================================================================

void PrintStatusPanel::set_temp_control_panel(TempControlPanel* temp_panel) {
    temp_control_panel_ = temp_panel;
    spdlog::debug("[{}] TempControlPanel reference set", get_name());
}

void PrintStatusPanel::set_filename(const char* filename) {
    // Store the actual filename (may be a temp file path)
    current_print_filename_ = filename ? filename : "";

    // Use thumbnail_source_filename_ if set (for modified temp files)
    // This affects BOTH the display name AND the thumbnail lookup
    std::string effective_filename =
        thumbnail_source_filename_.empty() ? current_print_filename_ : thumbnail_source_filename_;

    // Note: Display filename is now handled by ActivePrintMediaManager
    // PrintStatusPanel only needs to load local resources (gcode viewer, local thumbnail)

    // Load thumbnail ONLY if effective filename changed (makes this function idempotent)
    // This prevents redundant loads when observer fires repeatedly with same filename
    if (!effective_filename.empty() && effective_filename != loaded_thumbnail_filename_) {
        spdlog::debug("[{}] Loading thumbnail for: {}", get_name(), effective_filename);
        load_thumbnail_for_file(effective_filename);

        // G-code loading: immediate if panel active, deferred otherwise
        if (is_active_) {
            // Panel is already visible - load immediately instead of deferring
            spdlog::info("[{}] Panel active, loading G-code immediately: {}", get_name(),
                         effective_filename);
            load_gcode_for_viewing(effective_filename);
            pending_gcode_filename_.clear();
        } else {
            // Panel not visible - defer to on_activate()
            pending_gcode_filename_ = effective_filename;
        }
        loaded_thumbnail_filename_ = effective_filename;
    }
}

void PrintStatusPanel::set_thumbnail_source(const std::string& filename) {
    thumbnail_source_filename_ = filename;
    spdlog::debug("[{}] Thumbnail source set to: {}", get_name(),
                  filename.empty() ? "(cleared)" : filename);

    // If we already have a print filename, refresh everything now.
    // This handles the race condition where Moonraker sends the filename
    // before PrintPreparationManager calls set_thumbnail_source().
    // set_filename() will re-compute the effective filename (now using the
    // thumbnail source) and reload: display name, thumbnail, and G-code viewer.
    if (!current_print_filename_.empty() && !filename.empty()) {
        spdlog::info("[{}] Refreshing display/thumbnail/gcode with source override: {} -> {}",
                     get_name(), current_print_filename_, filename);
        set_filename(current_print_filename_.c_str());
    } else if (!filename.empty()) {
        // WebSocket hasn't updated current_print_filename_ yet (race condition).
        // Clear loaded filename so when on_print_filename_changed() eventually
        // fires and calls set_filename(), the idempotency check will pass and
        // trigger the actual thumbnail/gcode load.
        loaded_thumbnail_filename_.clear();
        spdlog::debug(
            "[{}] Source set before WebSocket, cleared loaded filename for deferred reload",
            get_name());
    }
}

void PrintStatusPanel::set_progress(int percent) {
    current_progress_ = percent;
    if (current_progress_ < 0)
        current_progress_ = 0;
    if (current_progress_ > 100)
        current_progress_ = 100;
    update_all_displays();
}

void PrintStatusPanel::set_layer(int current, int total) {
    current_layer_ = current;
    total_layers_ = total;
    update_all_displays();
}

void PrintStatusPanel::set_times(int elapsed_secs, int remaining_secs) {
    elapsed_seconds_ = elapsed_secs;
    remaining_seconds_ = remaining_secs;
    update_all_displays();
}

void PrintStatusPanel::set_speeds(int speed_pct, int flow_pct) {
    speed_percent_ = speed_pct;
    flow_percent_ = flow_pct;
    update_all_displays();
}

void PrintStatusPanel::set_state(PrintState state) {
    current_state_ = state;
    update_all_displays();
    update_button_states();
    spdlog::debug("[{}] State changed to: {}", get_name(), static_cast<int>(state));
}

// ============================================================================
// PRE-PRINT PREPARATION STATE
// ============================================================================

void PrintStatusPanel::end_preparing(bool success) {
    // Hide preparing UI
    lv_subject_set_int(&preparing_visible_subject_, 0);
    lv_subject_set_int(&preparing_progress_subject_, 0);

    if (success) {
        // Transition to Printing state
        set_state(PrintState::Printing);
        spdlog::info("[{}] Preparation complete, starting print", get_name());
    } else {
        // Transition back to Idle
        set_state(PrintState::Idle);
        spdlog::warn("[{}] Preparation cancelled or failed", get_name());
    }
}

// ============================================================================
// EXCLUDE OBJECT FEATURE
// ============================================================================

constexpr uint32_t EXCLUDE_UNDO_WINDOW_MS = 5000; // 5 second undo window

void PrintStatusPanel::on_object_long_pressed(lv_obj_t* viewer, const char* object_name,
                                              void* user_data) {
    (void)viewer;
    auto* self = static_cast<PrintStatusPanel*>(user_data);
    if (self && object_name && object_name[0] != '\0') {
        self->handle_object_long_press(object_name);
    }
}

void PrintStatusPanel::handle_object_long_press(const char* object_name) {
    if (!object_name || object_name[0] == '\0') {
        spdlog::debug("[{}] Long-press on empty area (no object)", get_name());
        return;
    }

    // Check if already excluded
    if (excluded_objects_.count(object_name) > 0) {
        spdlog::info("[{}] Object '{}' already excluded - ignoring", get_name(), object_name);
        return;
    }

    // Check if there's already a pending exclusion
    if (!pending_exclude_object_.empty()) {
        spdlog::warn("[{}] Already have pending exclusion for '{}' - ignoring new request",
                     get_name(), pending_exclude_object_);
        return;
    }

    spdlog::info("[{}] Long-press on object: '{}' - showing confirmation", get_name(), object_name);

    // Store the object name for when confirmation happens
    pending_exclude_object_ = object_name;

    // Configure and show the modal
    exclude_modal_.set_object_name(object_name);
    exclude_modal_.set_on_confirm([this]() { handle_exclude_confirmed(); });
    exclude_modal_.set_on_cancel([this]() { handle_exclude_cancelled(); });

    std::string message = "Stop printing \"" + std::string(object_name) +
                          "\"?\n\nThis cannot be undone after 5 seconds.";
    const char* attrs[] = {"title", "Exclude Object?", "message", message.c_str(), nullptr};

    if (!exclude_modal_.show(lv_screen_active(), attrs)) {
        spdlog::error("[{}] Failed to show exclude confirmation modal", get_name());
        pending_exclude_object_.clear();
    }
}

void PrintStatusPanel::handle_exclude_confirmed() {
    spdlog::info("[{}] Exclusion confirmed for '{}'", get_name(), pending_exclude_object_);

    // Note: Modal is hidden by on_ok() calling hide() before this callback

    if (pending_exclude_object_.empty()) {
        spdlog::error("[{}] No pending object for exclusion", get_name());
        return;
    }

    // Immediately update visual state in G-code viewer (red/semi-transparent)
    if (gcode_viewer_) {
        // For immediate visual feedback, we add to a "visually excluded" set
        // but don't send to Klipper yet - that happens after undo timer
        std::unordered_set<std::string> visual_excluded = excluded_objects_;
        visual_excluded.insert(pending_exclude_object_);
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, visual_excluded);
        spdlog::debug("[{}] Updated viewer with visual exclusion", get_name());
    }

    // Start undo timer - when it fires, we send EXCLUDE_OBJECT to Klipper
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
    }
    exclude_undo_timer_ = lv_timer_create(exclude_undo_timer_cb, EXCLUDE_UNDO_WINDOW_MS, this);
    lv_timer_set_repeat_count(exclude_undo_timer_, 1);

    // Show toast with "Undo" action button
    std::string toast_msg = "Excluding \"" + pending_exclude_object_ + "\"...";
    ui_toast_show_with_action(
        ToastSeverity::WARNING, toast_msg.c_str(), "Undo",
        [](void* user_data) {
            auto* self = static_cast<PrintStatusPanel*>(user_data);
            if (self) {
                self->handle_exclude_undo();
            }
        },
        this, EXCLUDE_UNDO_WINDOW_MS);

    spdlog::info("[{}] Started {}ms undo window for '{}'", get_name(), EXCLUDE_UNDO_WINDOW_MS,
                 pending_exclude_object_);
}

void PrintStatusPanel::handle_exclude_cancelled() {
    spdlog::info("[{}] Exclusion cancelled for '{}'", get_name(), pending_exclude_object_);

    // Modal hides itself via on_cancel() - no manual cleanup needed

    // Clear pending state
    pending_exclude_object_.clear();

    // Clear selection in viewer
    if (gcode_viewer_) {
        std::unordered_set<std::string> empty_set;
        ui_gcode_viewer_set_highlighted_objects(gcode_viewer_, empty_set);
    }
}

void PrintStatusPanel::handle_exclude_undo() {
    if (pending_exclude_object_.empty()) {
        spdlog::warn("[{}] Undo called but no pending exclusion", get_name());
        return;
    }

    spdlog::info("[{}] Undo pressed - cancelling exclusion of '{}'", get_name(),
                 pending_exclude_object_);

    // Cancel the timer
    if (exclude_undo_timer_) {
        lv_timer_delete(exclude_undo_timer_);
        exclude_undo_timer_ = nullptr;
    }

    // Restore visual state - remove from visual exclusion
    if (gcode_viewer_) {
        ui_gcode_viewer_set_excluded_objects(gcode_viewer_, excluded_objects_);
    }

    // Clear pending
    pending_exclude_object_.clear();

    // Show confirmation that undo succeeded
    ui_toast_show(ToastSeverity::SUCCESS, "Exclusion cancelled", 2000);
}

void PrintStatusPanel::exclude_undo_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<PrintStatusPanel*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    self->exclude_undo_timer_ = nullptr; // Timer auto-deletes after single shot

    if (self->pending_exclude_object_.empty()) {
        spdlog::warn("[PrintStatusPanel] Undo timer fired but no pending object");
        return;
    }

    std::string object_name = self->pending_exclude_object_;
    self->pending_exclude_object_.clear();

    spdlog::info("[PrintStatusPanel] Undo window expired - sending EXCLUDE_OBJECT for '{}'",
                 object_name);

    // Actually send the command to Klipper via MoonrakerAPI
    if (self->api_) {
        self->api_->exclude_object(
            object_name,
            [self, object_name]() {
                spdlog::info("[PrintStatusPanel] EXCLUDE_OBJECT '{}' sent successfully",
                             object_name);
                // Move to confirmed excluded set
                self->excluded_objects_.insert(object_name);
            },
            [self, object_name](const MoonrakerError& err) {
                spdlog::error("[PrintStatusPanel] Failed to exclude '{}': {}", object_name,
                              err.message);
                NOTIFY_ERROR("Failed to exclude '{}': {}", object_name, err.user_message());

                // Revert visual state - refresh viewer with only confirmed exclusions
                if (self->gcode_viewer_) {
                    ui_gcode_viewer_set_excluded_objects(self->gcode_viewer_,
                                                         self->excluded_objects_);
                    spdlog::debug("[PrintStatusPanel] Reverted visual exclusion for '{}'",
                                  object_name);
                }
            });
    } else {
        spdlog::warn("[PrintStatusPanel] No API available - simulating exclusion");
        self->excluded_objects_.insert(object_name);
    }
}

// ============================================================================
// Runout Guidance Modal
// ============================================================================

void PrintStatusPanel::check_and_show_runout_guidance() {
    // Only show once per pause event
    if (runout_modal_shown_for_pause_) {
        return;
    }

    // Skip if AMS/MMU present and not forced (runout during swaps is normal)
    if (!get_runtime_config()->should_show_runout_modal()) {
        return;
    }

    auto& sensor_mgr = helix::FilamentSensorManager::instance();

    // Check if any runout sensor shows no filament
    if (sensor_mgr.has_any_runout()) {
        spdlog::info("[{}] Runout detected during pause - showing guidance modal", get_name());
        show_runout_guidance_modal();
        runout_modal_shown_for_pause_ = true;
    }
}

void PrintStatusPanel::show_runout_guidance_modal() {
    if (runout_modal_.is_visible()) {
        // Already showing
        return;
    }

    spdlog::info("[{}] Showing runout guidance modal", get_name());

    // Configure callbacks for the three options
    runout_modal_.set_on_load_filament([this]() {
        spdlog::info("[{}] User chose to load filament after runout", get_name());
        // Navigate to filament panel for loading
        ui_nav_set_active(UI_PANEL_FILAMENT);
    });

    runout_modal_.set_on_resume([this]() {
        // Check if filament is now present before allowing resume
        auto& sensor_mgr = helix::FilamentSensorManager::instance();
        if (sensor_mgr.has_any_runout()) {
            spdlog::warn("[{}] User attempted resume but filament still not detected", get_name());
            NOTIFY_WARNING("Insert filament before resuming");
            return; // Modal stays open - user needs to load filament first
        }

        // Check if resume slot is available
        const auto& resume_info = StandardMacros::instance().get(StandardMacroSlot::Resume);
        if (resume_info.is_empty()) {
            spdlog::warn("[{}] Resume macro slot is empty", get_name());
            NOTIFY_WARNING("Resume macro not configured");
            return;
        }

        spdlog::info("[{}] User chose to resume print after runout", get_name());

        // Resume the print via StandardMacros
        if (api_) {
            spdlog::info("[{}] Using StandardMacros resume: {}", get_name(),
                         resume_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::Resume, api_,
                []() { spdlog::info("[PrintStatusPanel] Print resumed after runout"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintStatusPanel] Failed to resume print: {}", err.message);
                    NOTIFY_ERROR("Failed to resume: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_cancel_print([this]() {
        spdlog::info("[{}] User chose to cancel print after runout", get_name());

        // Check if cancel slot is available
        const auto& cancel_info = StandardMacros::instance().get(StandardMacroSlot::Cancel);
        if (cancel_info.is_empty()) {
            spdlog::warn("[{}] Cancel macro slot is empty", get_name());
            NOTIFY_WARNING("Cancel macro not configured");
            return;
        }

        // Cancel the print via StandardMacros
        if (api_) {
            spdlog::info("[{}] Using StandardMacros cancel: {}", get_name(),
                         cancel_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::Cancel, api_,
                []() { spdlog::info("[PrintStatusPanel] Print cancelled after runout"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintStatusPanel] Failed to cancel print: {}", err.message);
                    NOTIFY_ERROR("Failed to cancel: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_unload_filament([this]() {
        spdlog::info("[{}] User chose to unload filament after runout", get_name());

        const auto& unload_info = StandardMacros::instance().get(StandardMacroSlot::UnloadFilament);
        if (unload_info.is_empty()) {
            spdlog::warn("[{}] Unload filament macro slot is empty", get_name());
            NOTIFY_WARNING("Unload macro not configured");
            return;
        }

        if (api_) {
            spdlog::info("[{}] Using StandardMacros unload: {}", get_name(),
                         unload_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::UnloadFilament, api_,
                []() { spdlog::info("[PrintStatusPanel] Unload filament started"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintStatusPanel] Failed to unload filament: {}", err.message);
                    NOTIFY_ERROR("Failed to unload: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_purge([this]() {
        spdlog::info("[{}] User chose to purge after runout", get_name());

        const auto& purge_info = StandardMacros::instance().get(StandardMacroSlot::Purge);
        if (purge_info.is_empty()) {
            spdlog::warn("[{}] Purge macro slot is empty", get_name());
            NOTIFY_WARNING("Purge macro not configured");
            return;
        }

        if (api_) {
            spdlog::info("[{}] Using StandardMacros purge: {}", get_name(), purge_info.get_macro());
            StandardMacros::instance().execute(
                StandardMacroSlot::Purge, api_,
                []() { spdlog::info("[PrintStatusPanel] Purge started"); },
                [](const MoonrakerError& err) {
                    spdlog::error("[PrintStatusPanel] Failed to purge: {}", err.message);
                    NOTIFY_ERROR("Failed to purge: {}", err.user_message());
                });
        }
    });

    runout_modal_.set_on_ok_dismiss([this]() {
        spdlog::info("[{}] User dismissed runout modal (idle mode)", get_name());
        // Just hide the modal - no action needed
    });

    if (!runout_modal_.show(lv_screen_active())) {
        spdlog::error("[{}] Failed to create runout guidance modal", get_name());
    }
}

void PrintStatusPanel::hide_runout_guidance_modal() {
    if (!runout_modal_.is_visible()) {
        return;
    }

    spdlog::debug("[{}] Hiding runout guidance modal", get_name());
    runout_modal_.hide();
}
