// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_input_shaper.h"

#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_toast.h"

#include "moonraker_api.h"
#include "moonraker_client.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstdio>

// ============================================================================
// GLOBAL INSTANCE AND ROW CLICK HANDLER
// ============================================================================

static std::unique_ptr<InputShaperPanel> g_input_shaper_panel;

// State subject (0=IDLE, 1=MEASURING, 2=RESULTS, 3=ERROR)
static lv_subject_t s_input_shaper_state;

// Forward declarations
static void on_input_shaper_row_clicked(lv_event_t* e);
MoonrakerClient* get_moonraker_client();
MoonrakerAPI* get_moonraker_api();

InputShaperPanel& get_global_input_shaper_panel() {
    if (!g_input_shaper_panel) {
        g_input_shaper_panel = std::make_unique<InputShaperPanel>();
        StaticPanelRegistry::instance().register_destroy("InputShaperPanel",
                                                         []() { g_input_shaper_panel.reset(); });
    }
    return *g_input_shaper_panel;
}

InputShaperPanel::~InputShaperPanel() {
    // Applying [L011]: No mutex in destructors
    // Signal to async callbacks that this panel is being destroyed [L012]
    alive_->store(false);

    // Deinitialize subjects to disconnect observers before we're destroyed
    // This prevents use-after-free when lv_deinit() later deletes widgets
    if (subjects_initialized_) {
        lv_subject_deinit(&s_input_shaper_state);
        for (size_t i = 0; i < MAX_SHAPERS; i++) {
            lv_subject_deinit(&shaper_visible_subjects_[i]);
            lv_subject_deinit(&shaper_type_subjects_[i]);
            lv_subject_deinit(&shaper_freq_subjects_[i]);
            lv_subject_deinit(&shaper_vib_subjects_[i]);
        }
        subjects_initialized_ = false;
    }

    // Clear widget pointers (owned by LVGL)
    overlay_root_ = nullptr;
    parent_screen_ = nullptr;
    status_label_ = nullptr;
    error_message_ = nullptr;
    recommendation_label_ = nullptr;

    // Guard against static destruction order fiasco (spdlog may be gone)
    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::debug("[InputShaper] Destroyed");
    }
}

void init_input_shaper_row_handler() {
    lv_xml_register_event_cb(nullptr, "on_input_shaper_row_clicked", on_input_shaper_row_clicked);
    spdlog::debug("[InputShaper] Row click callback registered");
}

/**
 * @brief Row click handler for opening input shaper from Advanced panel
 */
static void on_input_shaper_row_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[InputShaper] Input Shaping row clicked");

    auto& panel = get_global_input_shaper_panel();

    // Lazy-create the input shaper panel
    if (!panel.get_root()) {
        spdlog::debug("[InputShaper] Creating input shaper panel...");

        // Set API references before create
        MoonrakerClient* client = get_moonraker_client();
        MoonrakerAPI* api = get_moonraker_api();
        panel.set_api(client, api);

        lv_obj_t* screen = lv_display_get_screen_active(NULL);
        if (!panel.create(screen)) {
            spdlog::error("[InputShaper] Failed to create input_shaper_panel");
            return;
        }
        spdlog::info("[InputShaper] Panel created");
    }

    // Show the overlay (registers with NavigationManager and pushes)
    panel.show();
}

// ============================================================================
// XML EVENT CALLBACK REGISTRATION
// ============================================================================

void ui_panel_input_shaper_register_callbacks() {
    // Register event callbacks for XML
    lv_xml_register_event_cb(nullptr, "input_shaper_calibrate_x_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_calibrate_x_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_calibrate_y_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_calibrate_y_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_measure_noise_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_measure_noise_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_cancel_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_cancel_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_apply_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_apply_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_close_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_close_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_retry_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_retry_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_save_config_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_save_config_clicked();
    });

    lv_xml_register_event_cb(nullptr, "input_shaper_help_cb", [](lv_event_t* /*e*/) {
        get_global_input_shaper_panel().handle_help_clicked();
    });

    // Initialize subjects BEFORE XML creation
    auto& panel = get_global_input_shaper_panel();
    panel.init_subjects();

    spdlog::debug("[InputShaper] Registered XML event callbacks");
}

// ============================================================================
// SUBJECT INITIALIZATION
// ============================================================================

void InputShaperPanel::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize state subject for state machine visibility
    lv_subject_init_int(&s_input_shaper_state, 0);
    lv_xml_register_subject(nullptr, "input_shaper_state", &s_input_shaper_state);

    // Initialize subjects for reactive results rows (5 shaper types max)
    for (size_t i = 0; i < MAX_SHAPERS; i++) {
        // Initialize char buffers to empty
        shaper_type_bufs_[i][0] = '\0';
        shaper_freq_bufs_[i][0] = '\0';
        shaper_vib_bufs_[i][0] = '\0';

        // Init subjects - visible defaults to 0 (hidden)
        lv_subject_init_int(&shaper_visible_subjects_[i], 0);
        lv_subject_init_string(&shaper_type_subjects_[i], shaper_type_bufs_[i], nullptr,
                               SHAPER_TYPE_BUF_SIZE, "");
        lv_subject_init_string(&shaper_freq_subjects_[i], shaper_freq_bufs_[i], nullptr,
                               SHAPER_VALUE_BUF_SIZE, "");
        lv_subject_init_string(&shaper_vib_subjects_[i], shaper_vib_bufs_[i], nullptr,
                               SHAPER_VALUE_BUF_SIZE, "");

        // Register subjects with XML system
        char reg_name[48];
        snprintf(reg_name, sizeof(reg_name), "shaper_%zu_visible", i);
        lv_xml_register_subject(nullptr, reg_name, &shaper_visible_subjects_[i]);

        snprintf(reg_name, sizeof(reg_name), "shaper_%zu_type", i);
        lv_xml_register_subject(nullptr, reg_name, &shaper_type_subjects_[i]);

        snprintf(reg_name, sizeof(reg_name), "shaper_%zu_freq", i);
        lv_xml_register_subject(nullptr, reg_name, &shaper_freq_subjects_[i]);

        snprintf(reg_name, sizeof(reg_name), "shaper_%zu_vib", i);
        lv_xml_register_subject(nullptr, reg_name, &shaper_vib_subjects_[i]);
    }

    subjects_initialized_ = true;
    spdlog::debug("[InputShaper] Subjects initialized and registered");
}

// ============================================================================
// CREATE
// ============================================================================

lv_obj_t* InputShaperPanel::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::debug("[InputShaper] Panel already created");
        return overlay_root_;
    }

    parent_screen_ = parent;

    spdlog::debug("[InputShaper] Creating overlay from XML");
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "input_shaper_panel", nullptr));

    if (!overlay_root_) {
        spdlog::error("[InputShaper] Failed to create overlay from XML");
        return nullptr;
    }

    // Start hidden (ui_nav_push_overlay will show it)
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    setup_widgets();

    spdlog::info("[InputShaper] Overlay created successfully");
    return overlay_root_;
}

// ============================================================================
// SETUP WIDGETS (private helper)
// ============================================================================

void InputShaperPanel::setup_widgets() {
    if (!overlay_root_) {
        spdlog::error("[InputShaper] NULL overlay_root_");
        return;
    }

    // State visibility is handled via XML subject bindings

    // Find display elements
    status_label_ = lv_obj_find_by_name(overlay_root_, "status_label");
    error_message_ = lv_obj_find_by_name(overlay_root_, "error_message");
    recommendation_label_ = lv_obj_find_by_name(overlay_root_, "recommendation_label");

    // Set initial state
    set_state(State::IDLE);

    spdlog::debug("[InputShaper] Widget setup complete");
}

// ============================================================================
// SHOW
// ============================================================================

void InputShaperPanel::show() {
    if (!overlay_root_) {
        spdlog::error("[InputShaper] Cannot show: overlay not created");
        return;
    }

    spdlog::debug("[InputShaper] Showing overlay");

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    ui_nav_push_overlay(overlay_root_);

    spdlog::info("[InputShaper] Overlay shown");
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void InputShaperPanel::on_activate() {
    // Call base class first
    OverlayBase::on_activate();

    spdlog::debug("[InputShaper] on_activate()");

    // Reset to idle state
    set_state(State::IDLE);
    clear_results();

    // Auto-start calibration for testing (env var)
    if (std::getenv("INPUT_SHAPER_AUTO_START")) {
        spdlog::info("[InputShaper] Auto-starting X calibration (INPUT_SHAPER_AUTO_START set)");
        start_calibration('X');
    }
}

void InputShaperPanel::on_deactivate() {
    spdlog::debug("[InputShaper] on_deactivate()");

    // If calibration is in progress, cancel it
    if (state_ == State::MEASURING) {
        spdlog::info("[InputShaper] Cancelling calibration on deactivate");
        cancel_calibration();
    }

    // Call base class
    OverlayBase::on_deactivate();
}

void InputShaperPanel::cleanup() {
    spdlog::debug("[InputShaper] Cleaning up");

    // Signal to async callbacks that this panel is being destroyed [L012]
    alive_->store(false);

    // Unregister from NavigationManager before cleaning up
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    // Call base class to set cleanup_called_ flag
    OverlayBase::cleanup();

    // Clear references
    parent_screen_ = nullptr;
    status_label_ = nullptr;
    error_message_ = nullptr;
    recommendation_label_ = nullptr;
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void InputShaperPanel::set_state(State new_state) {
    spdlog::debug("[InputShaper] State change: {} -> {}", static_cast<int>(state_),
                  static_cast<int>(new_state));
    state_ = new_state;

    // Update subject - XML bindings handle visibility automatically
    // State mapping: 0=IDLE, 1=MEASURING, 2=RESULTS, 3=ERROR
    lv_subject_set_int(&s_input_shaper_state, static_cast<int>(new_state));
}

// ============================================================================
// CALIBRATION COMMANDS (using MoonrakerAPI)
// ============================================================================

void InputShaperPanel::start_calibration(char axis) {
    if (!api_) {
        spdlog::error("[InputShaper] No API - cannot calibrate");
        on_calibration_error("Internal error: API not available");
        return;
    }

    current_axis_ = axis;
    shaper_results_.clear();
    recommended_type_.clear();
    recommended_freq_ = 0.0f;

    // Update status label
    char status[64];
    snprintf(status, sizeof(status), "Calibrating %c axis...", axis);
    update_status_label(status);

    set_state(State::MEASURING);
    spdlog::info("[InputShaper] Starting calibration for axis {}", axis);

    // Capture alive flag for destruction detection [L012]
    auto alive = alive_;

    // Use the API - collector handles all the G-code response parsing
    api_->start_resonance_test(
        axis,
        nullptr, // on_progress (not used currently)
        [this, alive](const InputShaperResult& result) {
            if (!alive->load())
                return;
            on_calibration_result(result);
        },
        [this, alive](const MoonrakerError& err) {
            if (!alive->load())
                return;
            on_calibration_error(err.message);
        });
}

void InputShaperPanel::measure_noise() {
    if (!api_) {
        spdlog::error("[InputShaper] No API - cannot measure noise");
        on_calibration_error("Internal error: API not available");
        return;
    }

    update_status_label("Measuring accelerometer noise...");
    set_state(State::MEASURING);
    spdlog::info("[InputShaper] Starting MEASURE_AXES_NOISE");

    // Capture alive flag for destruction detection [L012]
    auto alive = alive_;

    api_->execute_gcode(
        "MEASURE_AXES_NOISE",
        [this, alive]() {
            if (!alive->load())
                return;
            spdlog::debug("[InputShaper] MEASURE_AXES_NOISE command accepted");
            // Note: This fires when command is accepted, not when complete
            // Results appear in console output
            ui_toast_show(ToastSeverity::INFO, "Noise measurement started. Check console.", 3000);
            set_state(State::IDLE);
        },
        [this, alive](const MoonrakerError& err) {
            if (!alive->load())
                return;
            spdlog::error("[InputShaper] Failed to measure noise: {}", err.message);
            on_calibration_error(err.message);
        });
}

void InputShaperPanel::cancel_calibration() {
    spdlog::info("[InputShaper] Calibration cancelled by user");
    // Note: The API collector will continue running but we just ignore its results
    // by checking state in the callback
    set_state(State::IDLE);
}

void InputShaperPanel::apply_recommendation() {
    if (!api_ || recommended_type_.empty() || recommended_freq_ <= 0) {
        spdlog::error("[InputShaper] Cannot apply - no valid recommendation");
        return;
    }

    spdlog::info("[InputShaper] Applying {} axis shaper: {} @ {:.1f} Hz", current_axis_,
                 recommended_type_, recommended_freq_);

    api_->set_input_shaper(
        current_axis_, recommended_type_, recommended_freq_,
        []() {
            spdlog::info("[InputShaper] Settings applied successfully");
            ui_toast_show(ToastSeverity::SUCCESS, "Input shaper settings applied!", 2500);
        },
        [](const MoonrakerError& err) {
            spdlog::error("[InputShaper] Failed to apply settings: {}", err.message);
            ui_toast_show(ToastSeverity::ERROR, "Failed to apply settings", 3000);
        });
}

void InputShaperPanel::save_configuration() {
    if (!api_) {
        return;
    }

    spdlog::info("[InputShaper] Saving configuration (SAVE_CONFIG)");
    ui_toast_show(ToastSeverity::WARNING, "Saving config... Klipper will restart.", 3000);

    api_->save_config([]() { spdlog::info("[InputShaper] SAVE_CONFIG sent - Klipper restarting"); },
                      [](const MoonrakerError& err) {
                          spdlog::error("[InputShaper] SAVE_CONFIG failed: {}", err.message);
                          ui_toast_show(ToastSeverity::ERROR, "Failed to save configuration", 3000);
                      });
}

// ============================================================================
// RESULT CALLBACKS (from API)
// ============================================================================

void InputShaperPanel::on_calibration_result(const InputShaperResult& result) {
    // Ignore if we're not in measuring state (user may have cancelled)
    if (state_ != State::MEASURING) {
        spdlog::debug("[InputShaper] Ignoring result - not in measuring state");
        return;
    }

    spdlog::info("[InputShaper] Calibration complete: {} @ {:.1f} Hz (vib: {:.1f}%)",
                 result.shaper_type, result.shaper_freq, result.vibrations);

    // Store recommendation
    recommended_type_ = result.shaper_type;
    recommended_freq_ = result.shaper_freq;

    // Create a single result entry for display
    // Note: The collector currently only provides the recommended shaper
    // A future enhancement could collect all fitted shapers
    ShaperFit fit;
    fit.type = result.shaper_type;
    fit.frequency = result.shaper_freq;
    fit.vibrations = result.vibrations;
    fit.smoothing = result.smoothing;
    fit.is_recommended = true;
    shaper_results_.push_back(fit);

    populate_results();
    set_state(State::RESULTS);
}

void InputShaperPanel::on_calibration_error(const std::string& message) {
    // Ignore if we're not in measuring state
    if (state_ != State::MEASURING) {
        spdlog::debug("[InputShaper] Ignoring error - not in measuring state");
        return;
    }

    spdlog::error("[InputShaper] Calibration error: {}", message);

    if (error_message_) {
        lv_label_set_text(error_message_, message.c_str());
    }
    set_state(State::ERROR);
}

// ============================================================================
// UI UPDATE HELPERS
// ============================================================================

void InputShaperPanel::populate_results() {
    clear_results();

    // Update recommendation label
    if (recommendation_label_ && !recommended_type_.empty()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Recommended: %s @ %.1f Hz", recommended_type_.c_str(),
                 recommended_freq_);
        lv_label_set_text(recommendation_label_, buf);
    }

    // Update subjects for reactive rows
    for (size_t i = 0; i < MAX_SHAPERS; i++) {
        if (i < shaper_results_.size()) {
            const auto& fit = shaper_results_[i];

            // Copy to fixed buffers
            snprintf(shaper_type_bufs_[i], SHAPER_TYPE_BUF_SIZE, "%s%s", fit.type.c_str(),
                     fit.is_recommended ? " ★" : "");
            snprintf(shaper_freq_bufs_[i], SHAPER_VALUE_BUF_SIZE, "%.1f Hz", fit.frequency);
            snprintf(shaper_vib_bufs_[i], SHAPER_VALUE_BUF_SIZE, "%.1f%%", fit.vibrations);

            // Update subjects
            lv_subject_set_int(&shaper_visible_subjects_[i], 1);
            lv_subject_copy_string(&shaper_type_subjects_[i], shaper_type_bufs_[i]);
            lv_subject_copy_string(&shaper_freq_subjects_[i], shaper_freq_bufs_[i]);
            lv_subject_copy_string(&shaper_vib_subjects_[i], shaper_vib_bufs_[i]);
        } else {
            lv_subject_set_int(&shaper_visible_subjects_[i], 0);
        }
    }
}

void InputShaperPanel::clear_results() {
    for (size_t i = 0; i < MAX_SHAPERS; i++) {
        lv_subject_set_int(&shaper_visible_subjects_[i], 0);
    }
}

void InputShaperPanel::update_status_label(const std::string& text) {
    if (status_label_) {
        lv_label_set_text(status_label_, text.c_str());
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void InputShaperPanel::handle_calibrate_x_clicked() {
    if (state_ != State::IDLE) {
        return; // Prevent double-click during operation
    }
    spdlog::debug("[InputShaper] Calibrate X clicked");
    start_calibration('X');
}

void InputShaperPanel::handle_calibrate_y_clicked() {
    if (state_ != State::IDLE) {
        return;
    }
    spdlog::debug("[InputShaper] Calibrate Y clicked");
    start_calibration('Y');
}

void InputShaperPanel::handle_measure_noise_clicked() {
    if (state_ != State::IDLE) {
        return;
    }
    spdlog::debug("[InputShaper] Measure Noise clicked");
    measure_noise();
}

void InputShaperPanel::handle_cancel_clicked() {
    spdlog::debug("[InputShaper] Cancel clicked");
    cancel_calibration();
}

void InputShaperPanel::handle_apply_clicked() {
    spdlog::debug("[InputShaper] Apply clicked");
    apply_recommendation();
}

void InputShaperPanel::handle_close_clicked() {
    spdlog::debug("[InputShaper] Close clicked");
    clear_results();
    set_state(State::IDLE);
    ui_nav_go_back();
}

void InputShaperPanel::handle_retry_clicked() {
    spdlog::debug("[InputShaper] Retry clicked");
    start_calibration(current_axis_);
}

void InputShaperPanel::handle_save_config_clicked() {
    spdlog::debug("[InputShaper] Save Config clicked");
    save_configuration();
}

void InputShaperPanel::handle_help_clicked() {
    spdlog::debug("[InputShaper] Help clicked - showing help modal");

    // Detailed help text explaining Input Shaper calibration
    static const char* help_message =
        "Input Shaper reduces ringing and ghosting artifacts caused by "
        "printer vibrations during fast movements.\n\n"

        "REQUIREMENTS:\n"
        "• ADXL345 accelerometer connected to your toolhead\n"
        "• [resonance_tester] section configured in printer.cfg\n"
        "• [input_shaper] section in printer.cfg (can be empty initially)\n\n"

        "HOW TO USE:\n"
        "1. Tap 'Measure Noise' first to verify accelerometer is working\n"
        "2. Tap 'Calibrate X' to measure X-axis resonance (~1-2 min)\n"
        "3. Tap 'Calibrate Y' to measure Y-axis resonance (~1-2 min)\n"
        "4. Review results and tap 'Apply' to use recommended settings\n"
        "5. Optionally 'Save Config' to make permanent (restarts Klipper)\n\n"

        "SHAPER TYPES:\n"
        "• ZV - Lowest smoothing, good for low vibration printers\n"
        "• MZV - Balanced choice, recommended for most printers\n"
        "• EI - More aggressive, better vibration reduction\n"
        "• 2HUMP_EI / 3HUMP_EI - Maximum reduction, more smoothing\n\n"

        "Lower vibration % is better. Lower smoothing preserves detail.";

    const char* attrs[] = {"title", "Input Shaper Help", "message", help_message, nullptr};

    ui_modal_configure(ModalSeverity::Info, false, "Got It", nullptr);
    lv_obj_t* help_dialog = ui_modal_show("modal_dialog", attrs);

    if (!help_dialog) {
        spdlog::error("[InputShaper] Failed to show help modal");
        return;
    }

    // Wire up Ok button to close
    lv_obj_t* ok_btn = lv_obj_find_by_name(help_dialog, "btn_primary");
    if (ok_btn) {
        lv_obj_set_user_data(ok_btn, help_dialog);
        lv_obj_add_event_cb(
            ok_btn,
            [](lv_event_t* e) {
                auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* dialog = static_cast<lv_obj_t*>(lv_obj_get_user_data(btn));
                if (dialog) {
                    ui_modal_hide(dialog);
                }
            },
            LV_EVENT_CLICKED, nullptr);
    }
}

void InputShaperPanel::process_pending_responses() {
    // Not used - API collector handles response processing
}
