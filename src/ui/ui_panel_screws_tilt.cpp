// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_screws_tilt.h"

#include "ui_fonts.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_theme.h"

#include "moonraker_api.h"
#include "moonraker_client.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdio>
#include <memory>

// ============================================================================
// GLOBAL INSTANCE AND ROW CLICK HANDLER
// ============================================================================

static std::unique_ptr<ScrewsTiltPanel> s_screws_tilt_panel;

// State subject (0=IDLE, 1=PROBING, 2=RESULTS, 3=LEVELED, 4=ERROR)
static lv_subject_t s_screws_tilt_state;

// Forward declarations
static void on_screws_tilt_row_clicked(lv_event_t* e);
MoonrakerClient* get_moonraker_client();
MoonrakerAPI* get_moonraker_api();

ScrewsTiltPanel& get_global_screws_tilt_panel() {
    if (!s_screws_tilt_panel) {
        s_screws_tilt_panel = std::make_unique<ScrewsTiltPanel>();
        StaticPanelRegistry::instance().register_destroy("ScrewsTiltPanel",
                                                         []() { s_screws_tilt_panel.reset(); });
    }
    return *s_screws_tilt_panel;
}

void destroy_screws_tilt_panel() {
    if (s_screws_tilt_panel) {
        s_screws_tilt_panel->cleanup();
        s_screws_tilt_panel.reset();
    }
}

void init_screws_tilt_row_handler() {
    lv_xml_register_event_cb(nullptr, "on_screws_tilt_row_clicked", on_screws_tilt_row_clicked);
    spdlog::debug("[ScrewsTilt] Row click callback registered");
}

/**
 * @brief Row click handler for opening screws tilt from Advanced panel
 *
 * Registered via init_screws_tilt_row_handler().
 * Lazy-creates the screws tilt panel on first click.
 */
static void on_screws_tilt_row_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[ScrewsTilt] Bed leveling row clicked");

    auto& panel = get_global_screws_tilt_panel();

    // Lazy-create the screws tilt panel
    if (!panel.get_root()) {
        spdlog::debug("[ScrewsTilt] Creating screws tilt panel...");

        // Initialize subjects (must be before XML creation)
        if (!panel.are_subjects_initialized()) {
            panel.init_subjects();
        }

        // Set client and API before creating UI
        MoonrakerClient* client = get_moonraker_client();
        MoonrakerAPI* api = get_moonraker_api();
        panel.set_client(client, api);

        // Create the overlay UI
        lv_obj_t* overlay = panel.create(lv_display_get_screen_active(NULL));

        if (!overlay) {
            spdlog::error("[ScrewsTilt] Failed to create screws_tilt_panel");
            return;
        }

        spdlog::info("[ScrewsTilt] Panel created and setup complete");
    }

    // Show the overlay (registers and pushes)
    panel.show();
}

// ============================================================================
// XML EVENT CALLBACK REGISTRATION
// ============================================================================

void ui_panel_screws_tilt_register_callbacks() {
    // Register event callbacks
    lv_xml_register_event_cb(nullptr, "screws_tilt_start_cb", [](lv_event_t* /*e*/) {
        get_global_screws_tilt_panel().handle_start_clicked();
    });

    lv_xml_register_event_cb(nullptr, "screws_tilt_cancel_cb", [](lv_event_t* /*e*/) {
        get_global_screws_tilt_panel().handle_cancel_clicked();
    });

    lv_xml_register_event_cb(nullptr, "screws_tilt_done_cb", [](lv_event_t* /*e*/) {
        get_global_screws_tilt_panel().handle_done_clicked();
    });

    lv_xml_register_event_cb(nullptr, "screws_tilt_reprobe_cb", [](lv_event_t* /*e*/) {
        get_global_screws_tilt_panel().handle_reprobe_clicked();
    });

    lv_xml_register_event_cb(nullptr, "screws_tilt_retry_cb", [](lv_event_t* /*e*/) {
        get_global_screws_tilt_panel().handle_retry_clicked();
    });

    // Initialize subjects BEFORE XML creation (bindings resolve at parse time)
    auto& panel = get_global_screws_tilt_panel();
    panel.init_subjects();

    spdlog::debug("[ScrewsTilt] Registered XML event callbacks");
}

// ============================================================================
// SUBJECT INITIALIZATION (must run BEFORE XML creation)
// ============================================================================

void ScrewsTiltPanel::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Initialize state subject for state machine visibility
    lv_subject_init_int(&s_screws_tilt_state, 0);
    lv_xml_register_subject(nullptr, "screws_tilt_state", &s_screws_tilt_state);

    // Initialize subjects for reactive list rows (4 slots max)
    for (size_t i = 0; i < MAX_SCREWS; i++) {
        // Initialize char buffers to empty
        screw_name_bufs_[i][0] = '\0';
        screw_adj_bufs_[i][0] = '\0';

        // Init subjects - visible defaults to 0 (hidden)
        lv_subject_init_int(&screw_visible_subjects_[i], 0);
        lv_subject_init_string(&screw_name_subjects_[i], screw_name_bufs_[i], nullptr,
                               SCREW_NAME_BUF_SIZE, "");
        lv_subject_init_string(&screw_adjustment_subjects_[i], screw_adj_bufs_[i], nullptr,
                               SCREW_ADJ_BUF_SIZE, "");

        // Register subjects with XML system
        char reg_name[32];
        snprintf(reg_name, sizeof(reg_name), "screw_%zu_visible", i);
        lv_xml_register_subject(nullptr, reg_name, &screw_visible_subjects_[i]);

        snprintf(reg_name, sizeof(reg_name), "screw_%zu_name", i);
        lv_xml_register_subject(nullptr, reg_name, &screw_name_subjects_[i]);

        snprintf(reg_name, sizeof(reg_name), "screw_%zu_adjustment", i);
        lv_xml_register_subject(nullptr, reg_name, &screw_adjustment_subjects_[i]);
    }

    // Initialize status label subjects
    probe_count_buf_[0] = '\0';
    error_message_buf_[0] = '\0';
    lv_subject_init_string(&probe_count_subject_, probe_count_buf_, nullptr, PROBE_COUNT_BUF_SIZE,
                           "");
    lv_subject_init_string(&error_message_subject_, error_message_buf_, nullptr, ERROR_MSG_BUF_SIZE,
                           "");
    lv_xml_register_subject(nullptr, "probe_count_text", &probe_count_subject_);
    lv_xml_register_subject(nullptr, "error_message_text", &error_message_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[ScrewsTilt] Subjects initialized and registered");
}

void ScrewsTiltPanel::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }

    // Deinit file-static state machine subject
    lv_subject_deinit(&s_screws_tilt_state);

    // Deinit screw array subjects
    for (size_t i = 0; i < MAX_SCREWS; i++) {
        lv_subject_deinit(&screw_visible_subjects_[i]);
        lv_subject_deinit(&screw_name_subjects_[i]);
        lv_subject_deinit(&screw_adjustment_subjects_[i]);
    }

    // Deinit status label subjects
    lv_subject_deinit(&probe_count_subject_);
    lv_subject_deinit(&error_message_subject_);

    subjects_initialized_ = false;
    spdlog::debug("[ScrewsTiltPanel] Subjects deinitialized");
}

// ============================================================================
// DESTRUCTOR
// ============================================================================

ScrewsTiltPanel::~ScrewsTiltPanel() {
    // Applying [L011]: No mutex in destructors - may deadlock

    // Signal pending callbacks to stop (safe even if already done in cleanup())
    if (alive_) {
        alive_->store(false);
    }

    // Deinitialize subjects to disconnect observers before we're destroyed
    deinit_subjects();

    // Guard against static destruction order fiasco (spdlog may be gone)
    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::debug("[ScrewsTilt] Destroyed");
    }
}

// ============================================================================
// OVERLAYBASE INTERFACE
// ============================================================================

lv_obj_t* ScrewsTiltPanel::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::debug("[ScrewsTilt] Overlay already created, reusing");
        return overlay_root_;
    }

    parent_screen_ = parent;

    // Create UI from XML
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "screws_tilt_panel", nullptr));

    if (!overlay_root_) {
        spdlog::error("[ScrewsTilt] Failed to create screws_tilt_panel XML");
        return nullptr;
    }

    // Initially hidden
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    // Setup widget references
    setup_widgets();

    spdlog::info("[ScrewsTilt] Overlay created");
    return overlay_root_;
}

void ScrewsTiltPanel::setup_widgets() {
    if (!overlay_root_) {
        return;
    }

    // Find display elements
    bed_diagram_container_ = lv_obj_find_by_name(overlay_root_, "bed_diagram_container");
    results_instruction_ = lv_obj_find_by_name(overlay_root_, "results_instruction");

    // Find screw dot widgets for color updates
    screw_dots_[0] = lv_obj_find_by_name(overlay_root_, "screw_dot_0");
    screw_dots_[1] = lv_obj_find_by_name(overlay_root_, "screw_dot_1");
    screw_dots_[2] = lv_obj_find_by_name(overlay_root_, "screw_dot_2");
    screw_dots_[3] = lv_obj_find_by_name(overlay_root_, "screw_dot_3");
}

void ScrewsTiltPanel::show() {
    if (!overlay_root_) {
        spdlog::error("[ScrewsTilt] Cannot show - overlay not created");
        return;
    }

    spdlog::debug("[ScrewsTilt] Showing overlay");

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    ui_nav_push_overlay(overlay_root_);
}

void ScrewsTiltPanel::on_activate() {
    OverlayBase::on_activate();

    // Reset for fresh session
    probe_count_ = 0;
    set_state(State::IDLE);
    clear_results();

    spdlog::info("[ScrewsTilt] Activated (probe count reset)");

    // Auto-start probing for testing (env var)
    if (std::getenv("SCREWS_AUTO_START")) {
        spdlog::info("[ScrewsTilt] Auto-starting probe (SCREWS_AUTO_START set)");
        start_probing();
    }
}

void ScrewsTiltPanel::on_deactivate() {
    if (state_ == State::PROBING) {
        // Cancel ongoing probe via Moonraker
        if (api_) {
            spdlog::info("[ScrewsTilt] Aborting probe on deactivate");
            api_->execute_gcode("ABORT", nullptr, nullptr);
        }
    }

    // Clean up dynamic indicators
    clear_results();

    OverlayBase::on_deactivate();
    spdlog::debug("[ScrewsTilt] Deactivated");
}

void ScrewsTiltPanel::cleanup() {
    spdlog::debug("[ScrewsTilt] Cleanup called");

    // Signal async callbacks to stop [L012]
    if (alive_) {
        alive_->store(false);
    }

    // Unregister from NavigationManager
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }

    OverlayBase::cleanup();
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void ScrewsTiltPanel::set_state(State new_state) {
    spdlog::debug("[ScrewsTilt] State change: {} -> {}", static_cast<int>(state_),
                  static_cast<int>(new_state));
    state_ = new_state;

    // Update subject - XML bindings handle visibility automatically
    // State mapping: 0=IDLE, 1=PROBING, 2=RESULTS, 3=LEVELED, 4=ERROR
    lv_subject_set_int(&s_screws_tilt_state, static_cast<int>(new_state));
}

// ============================================================================
// COMMAND HELPERS
// ============================================================================

void ScrewsTiltPanel::start_probing() {
    if (!api_) {
        spdlog::error("[ScrewsTilt] No API - cannot probe");
        on_screws_tilt_error("Internal error: API not available");
        return;
    }

    probe_count_++;
    set_state(State::PROBING);

    spdlog::info("[ScrewsTilt] Starting probe #{}", probe_count_);

    // Capture alive_ for async safety [L012]
    auto alive = alive_;
    api_->calculate_screws_tilt(
        [this, alive](const std::vector<ScrewTiltResult>& results) {
            // Check if panel was destroyed or cleanup was called
            if (!alive->load()) {
                spdlog::debug("[ScrewsTilt] Ignoring results - panel destroyed");
                return;
            }
            if (cleanup_called()) {
                spdlog::debug("[ScrewsTilt] Ignoring results - cleanup called");
                return;
            }
            on_screws_tilt_results(results);
        },
        [this, alive](const MoonrakerError& err) {
            // Check if panel was destroyed or cleanup was called
            if (!alive->load()) {
                spdlog::debug("[ScrewsTilt] Ignoring error - panel destroyed");
                return;
            }
            if (cleanup_called()) {
                spdlog::debug("[ScrewsTilt] Ignoring error - cleanup called");
                return;
            }
            on_screws_tilt_error(err.message);
        });
}

void ScrewsTiltPanel::cancel_probing() {
    spdlog::info("[ScrewsTilt] Probing cancelled by user");
    set_state(State::IDLE);
}

// ============================================================================
// RESULT CALLBACKS
// ============================================================================

void ScrewsTiltPanel::on_screws_tilt_results(const std::vector<ScrewTiltResult>& results) {
    spdlog::info("[ScrewsTilt] Received {} screw results", results.size());

    screw_results_ = results;
    populate_results(results);

    // Check if all screws are within tolerance
    if (check_all_level()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Completed in %d probe%s", probe_count_,
                 probe_count_ == 1 ? "" : "s");
        lv_subject_copy_string(&probe_count_subject_, buf);
        set_state(State::LEVELED);
    } else {
        set_state(State::RESULTS);
    }
}

void ScrewsTiltPanel::on_screws_tilt_error(const std::string& message) {
    spdlog::error("[ScrewsTilt] Error: {}", message);

    lv_subject_copy_string(&error_message_subject_, message.c_str());
    set_state(State::ERROR);
}

// ============================================================================
// UI UPDATE HELPERS
// ============================================================================

void ScrewsTiltPanel::populate_results(const std::vector<ScrewTiltResult>& results) {
    clear_results();

    // Store results first so find_worst_screw_index can access them
    screw_results_ = results;

    // Find the screw needing the most adjustment (to highlight it)
    size_t worst_index = find_worst_screw_index();

    // Update subjects for reactive list rows (XML handles the UI)
    for (size_t i = 0; i < MAX_SCREWS; i++) {
        if (i < results.size()) {
            const auto& screw = results[i];
            bool is_worst = (i == worst_index && !screw.is_reference && screw.needs_adjustment());

            // Copy strings into fixed buffers (LVGL string subjects require stable storage)
            snprintf(screw_name_bufs_[i], SCREW_NAME_BUF_SIZE, "%s", screw.display_name().c_str());
            // Use friendly adjustment text (e.g., "Tighten 1/4 turn" instead of "CW 00:18")
            snprintf(screw_adj_bufs_[i], SCREW_ADJ_BUF_SIZE, "%s",
                     screw.friendly_adjustment().c_str());

            // Update subjects - this triggers XML binding updates
            lv_subject_set_int(&screw_visible_subjects_[i], 1); // Show row
            lv_subject_copy_string(&screw_name_subjects_[i], screw_name_bufs_[i]);
            lv_subject_copy_string(&screw_adjustment_subjects_[i], screw_adj_bufs_[i]);

            // Update dot color (not bindable via subject, so do directly)
            if (screw_dots_[i]) {
                lv_obj_set_style_bg_color(screw_dots_[i], get_adjustment_color(screw, is_worst), 0);
            }

            // Create bed diagram indicator (position varies, so still dynamic)
            create_screw_indicator(i, screw, is_worst);
        } else {
            // Hide unused rows
            lv_subject_set_int(&screw_visible_subjects_[i], 0);
        }
    }

    update_screw_diagram();
}

void ScrewsTiltPanel::clear_results() {
    // Clear bed diagram indicators (dynamically positioned)
    for (auto* indicator : screw_indicators_) {
        if (indicator) {
            lv_obj_delete(indicator);
        }
    }
    screw_indicators_.clear();

    // Hide all list rows via subjects (reactive pattern)
    for (size_t i = 0; i < MAX_SCREWS; i++) {
        lv_subject_set_int(&screw_visible_subjects_[i], 0);
    }
}

/**
 * @brief Create a screw indicator widget for the bed diagram
 *
 * Uses LVGL alignment to position indicators at corners rather than
 * complex coordinate math. This is more robust and works regardless
 * of container size.
 */
// Animation callback for rotating icons
static void rotation_anim_cb(void* var, int32_t value) {
    lv_obj_set_style_transform_rotation(static_cast<lv_obj_t*>(var), value, 0);
}

void ScrewsTiltPanel::create_screw_indicator(size_t index, const ScrewTiltResult& screw,
                                             bool is_worst) {
    if (!bed_diagram_container_) {
        return;
    }

    // Circular screw indicators - size based on icon
    constexpr int INDICATOR_SIZE = 40; // Square for circle

    // Create circular indicator
    lv_obj_t* indicator = lv_obj_create(bed_diagram_container_);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_size(indicator, INDICATOR_SIZE, INDICATOR_SIZE);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0); // Fully round
    lv_obj_set_style_border_width(indicator, is_worst ? 3 : 2, 0);
    lv_obj_set_style_border_color(indicator, lv_color_white(), 0);

    // Color based on adjustment severity (worst screw gets highlighted)
    lv_color_t bg_color = get_adjustment_color(screw, is_worst);
    lv_obj_set_style_bg_color(indicator, bg_color, 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0); // Must be AFTER bg_color

    spdlog::debug("[ScrewsTilt] Indicator {} ({}): color=0x{:06X}, is_worst={}", index,
                  screw.screw_name, (bg_color.red << 16) | (bg_color.green << 8) | bg_color.blue,
                  is_worst);

    // Create centered icon/text label
    lv_obj_t* label = lv_label_create(indicator);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    if (screw.is_reference) {
        // Reference screw - show checkmark icon (no animation)
        lv_obj_set_style_text_font(label, &mdi_icons_24, 0);
        // check icon (F012C)
        lv_label_set_text(label, "\xF3\xB0\x84\xAC");
    } else {
        // Adjustment needed - show animated rotation icon
        bool is_clockwise = screw.adjustment.find("CW") == 0 && screw.adjustment.find("CCW") != 0;

        lv_obj_set_style_text_font(label, &mdi_icons_24, 0);
        // rotate-right (F0467) = clockwise/tighten, rotate-left (F0465) = CCW/loosen
        const char* dir_icon = is_clockwise ? "\xF3\xB0\x91\xA7" : "\xF3\xB0\x91\xA5";
        lv_label_set_text(label, dir_icon);

        // Set transform pivot to center for rotation
        lv_obj_set_style_transform_pivot_x(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(label, LV_PCT(50), 0);

        // Animate rotation continuously
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, label);
        lv_anim_set_exec_cb(&anim, rotation_anim_cb);
        lv_anim_set_duration(&anim, 2000); // 2 seconds per rotation
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);

        if (is_clockwise) {
            lv_anim_set_values(&anim, 0, 3600); // 0 to 360 degrees (LVGL uses 0.1 degree units)
        } else {
            lv_anim_set_values(&anim, 3600, 0); // 360 to 0 degrees (reverse)
        }

        lv_anim_start(&anim);
    }

    screw_indicators_.push_back(indicator);

    (void)index;
}

/**
 * @brief Position screw indicators using LVGL alignment
 *
 * Maps screw positions to corner alignments based on their relative positions
 * on the bed. This is simpler and more reliable than coordinate math.
 */
void ScrewsTiltPanel::update_screw_diagram() {
    if (!bed_diagram_container_ || screw_results_.empty()) {
        return;
    }

    // Force layout calculation
    lv_obj_update_layout(bed_diagram_container_);

    // Find bed bounds
    float min_x = screw_results_[0].x_pos;
    float max_x = screw_results_[0].x_pos;
    float min_y = screw_results_[0].y_pos;
    float max_y = screw_results_[0].y_pos;

    for (const auto& screw : screw_results_) {
        min_x = std::min(min_x, screw.x_pos);
        max_x = std::max(max_x, screw.x_pos);
        min_y = std::min(min_y, screw.y_pos);
        max_y = std::max(max_y, screw.y_pos);
    }

    float center_x = (min_x + max_x) / 2.0f;
    float center_y = (min_y + max_y) / 2.0f;

    // Position indicators using alignment based on quadrant
    for (size_t i = 0; i < screw_results_.size() && i < screw_indicators_.size(); i++) {
        const auto& screw = screw_results_[i];
        lv_obj_t* indicator = screw_indicators_[i];

        // Determine quadrant and set alignment
        bool is_left = screw.x_pos < center_x;
        bool is_front = screw.y_pos < center_y; // Front = lower Y in bed coords

        lv_align_t align;
        if (is_left && is_front) {
            align = LV_ALIGN_BOTTOM_LEFT;
        } else if (!is_left && is_front) {
            align = LV_ALIGN_BOTTOM_RIGHT;
        } else if (is_left && !is_front) {
            align = LV_ALIGN_TOP_LEFT;
        } else {
            align = LV_ALIGN_TOP_RIGHT;
        }

        // Apply alignment with small offset from edges
        lv_obj_align(indicator, align, 0, 0);

        spdlog::debug("[ScrewsTilt] {} -> {} (x:{:.0f}, y:{:.0f})", screw.screw_name,
                      is_left ? (is_front ? "bottom_left" : "top_left")
                              : (is_front ? "bottom_right" : "top_right"),
                      screw.x_pos, screw.y_pos);
    }
}

lv_color_t ScrewsTiltPanel::get_adjustment_color(const ScrewTiltResult& screw,
                                                 bool is_worst_screw) const {
    // Helper to get color from globals.xml constant
    auto get_theme_color = [](const char* const_name) -> lv_color_t {
        const char* hex = lv_xml_get_const(nullptr, const_name);
        if (hex) {
            return ui_theme_parse_hex_color(hex);
        }
        return ui_theme_get_color(const_name); // Fallback to direct token lookup
    };

    if (screw.is_reference) {
        return get_theme_color("success_color");
    }

    if (!screw.needs_adjustment()) {
        return get_theme_color("success_color");
    }

    // Parse adjustment severity
    int turns = 0;
    int minutes = 0;
    if (sscanf(screw.adjustment.c_str(), "%*s %d:%d", &turns, &minutes) == 2) {
        int total_minutes = turns * 60 + minutes;

        if (total_minutes <= 5) {
            return get_theme_color("success_color");
        } else if (is_worst_screw) {
            // Highlight the worst screw with primary color (bright, attention-grabbing)
            return get_theme_color("primary_color");
        } else if (total_minutes <= 30) {
            return get_theme_color("warning_color");
        }
    }

    return get_theme_color("error_color");
}

bool ScrewsTiltPanel::check_all_level(int tolerance_minutes) const {
    for (const auto& screw : screw_results_) {
        if (screw.is_reference) {
            continue;
        }

        int turns = 0;
        int minutes = 0;
        if (sscanf(screw.adjustment.c_str(), "%*s %d:%d", &turns, &minutes) == 2) {
            int total_minutes = turns * 60 + minutes;
            if (total_minutes > tolerance_minutes) {
                return false;
            }
        }
    }
    return true;
}

size_t ScrewsTiltPanel::find_worst_screw_index() const {
    size_t worst_index = 0;
    int worst_minutes = 0;

    for (size_t i = 0; i < screw_results_.size(); i++) {
        const auto& screw = screw_results_[i];
        if (screw.is_reference) {
            continue;
        }

        int turns = 0;
        int minutes = 0;
        if (sscanf(screw.adjustment.c_str(), "%*s %d:%d", &turns, &minutes) == 2) {
            int total_minutes = turns * 60 + minutes;
            if (total_minutes > worst_minutes) {
                worst_minutes = total_minutes;
                worst_index = i;
            }
        }
    }

    return worst_index;
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void ScrewsTiltPanel::handle_start_clicked() {
    spdlog::debug("[ScrewsTilt] Start clicked");
    start_probing();
}

void ScrewsTiltPanel::handle_cancel_clicked() {
    spdlog::debug("[ScrewsTilt] Cancel clicked");
    cancel_probing();
}

void ScrewsTiltPanel::handle_reprobe_clicked() {
    spdlog::debug("[ScrewsTilt] Re-probe clicked");
    start_probing();
}

void ScrewsTiltPanel::handle_done_clicked() {
    spdlog::debug("[ScrewsTilt] Done clicked");
    probe_count_ = 0;
    clear_results();
    set_state(State::IDLE);
    ui_nav_go_back();
}

void ScrewsTiltPanel::handle_retry_clicked() {
    spdlog::debug("[ScrewsTilt] Retry clicked");
    start_probing();
}
