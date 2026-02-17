// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_probe_overlay.h"

#include "ui_nav_manager.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "probe_sensor_manager.h"
#include "probe_sensor_types.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

using namespace helix;
using helix::sensors::probe_type_to_display_string;
using helix::sensors::ProbeSensorManager;
using helix::sensors::ProbeSensorType;

// ============================================================================
// GLOBAL INSTANCE AND ROW CLICK HANDLER
// ============================================================================

static std::unique_ptr<ProbeOverlay> g_probe_overlay;

// Forward declarations
static void on_probe_row_clicked(lv_event_t* e);
MoonrakerAPI* get_moonraker_api();
MoonrakerClient* get_moonraker_client();

ProbeOverlay& get_global_probe_overlay() {
    if (!g_probe_overlay) {
        g_probe_overlay = std::make_unique<ProbeOverlay>();
        StaticPanelRegistry::instance().register_destroy("ProbeOverlay",
                                                         []() { g_probe_overlay.reset(); });
    }
    return *g_probe_overlay;
}

ProbeOverlay::~ProbeOverlay() {
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }

    overlay_root_ = nullptr;
    parent_screen_ = nullptr;

    if (!StaticPanelRegistry::is_destroyed()) {
        spdlog::trace("[Probe] Destroyed");
    }
}

void init_probe_row_handler() {
    lv_xml_register_event_cb(nullptr, "on_probe_row_clicked", on_probe_row_clicked);
    spdlog::trace("[Probe] Row click callback registered");
}

static void on_probe_row_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Probe] Probe row clicked");

    auto& overlay = get_global_probe_overlay();

    // Lazy-create the probe overlay
    if (!overlay.get_root()) {
        spdlog::debug("[Probe] Creating probe overlay...");

        MoonrakerAPI* api = get_moonraker_api();
        overlay.set_api(api);

        lv_obj_t* screen = lv_display_get_screen_active(nullptr);
        if (!overlay.create(screen)) {
            spdlog::error("[Probe] Failed to create probe_overlay");
            return;
        }
        spdlog::info("[Probe] Overlay created");
    }

    overlay.show();
}

// ============================================================================
// XML EVENT CALLBACK REGISTRATION
// ============================================================================

// Helper to send a GCode command via MoonrakerClient
static void send_probe_gcode(const char* gcode, const char* label) {
    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        spdlog::error("[Probe] No client for {} command", label);
        return;
    }
    spdlog::debug("[Probe] Sending {}: {}", label, gcode);
    client->gcode_script(gcode);
}

void ui_probe_overlay_register_callbacks() {
    // Universal probe actions
    lv_xml_register_event_cb(nullptr, "on_probe_accuracy", [](lv_event_t* /*e*/) {
        get_global_probe_overlay().handle_probe_accuracy();
    });
    lv_xml_register_event_cb(nullptr, "on_zoffset_cal", [](lv_event_t* /*e*/) {
        get_global_probe_overlay().handle_zoffset_cal();
    });
    lv_xml_register_event_cb(nullptr, "on_bed_mesh", [](lv_event_t* /*e*/) {
        get_global_probe_overlay().handle_bed_mesh();
    });

    // BLTouch controls
    lv_xml_register_event_cb(nullptr, "on_bltouch_deploy", [](lv_event_t* /*e*/) {
        send_probe_gcode("BLTOUCH_DEBUG COMMAND=pin_down", "BLTouch Deploy");
    });
    lv_xml_register_event_cb(nullptr, "on_bltouch_stow", [](lv_event_t* /*e*/) {
        send_probe_gcode("BLTOUCH_DEBUG COMMAND=pin_up", "BLTouch Stow");
    });
    lv_xml_register_event_cb(nullptr, "on_bltouch_reset", [](lv_event_t* /*e*/) {
        send_probe_gcode("BLTOUCH_DEBUG COMMAND=reset", "BLTouch Reset");
    });
    lv_xml_register_event_cb(nullptr, "on_bltouch_selftest", [](lv_event_t* /*e*/) {
        send_probe_gcode("BLTOUCH_DEBUG COMMAND=self_test", "BLTouch Self-Test");
    });
    lv_xml_register_event_cb(nullptr, "on_bltouch_output_5v", [](lv_event_t* /*e*/) {
        send_probe_gcode("SET_BLTOUCH OUTPUT_MODE=5V", "BLTouch Output 5V");
    });
    lv_xml_register_event_cb(nullptr, "on_bltouch_output_od", [](lv_event_t* /*e*/) {
        send_probe_gcode("SET_BLTOUCH OUTPUT_MODE=OD", "BLTouch Output OD");
    });

    spdlog::trace("[Probe] Event callbacks registered");
}

// ============================================================================
// OVERLAY LIFECYCLE
// ============================================================================

void ProbeOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    // Display subjects
    UI_MANAGED_SUBJECT_STRING(probe_display_name_, probe_display_name_buf_, "",
                              "probe_display_name", subjects_);
    UI_MANAGED_SUBJECT_STRING(probe_type_label_, probe_type_label_buf_, "", "probe_type_label",
                              subjects_);
    UI_MANAGED_SUBJECT_STRING(probe_z_offset_display_, probe_z_offset_display_buf_, "--",
                              "probe_z_offset_display", subjects_);

    // Overlay state (0=normal)
    UI_MANAGED_SUBJECT_INT(probe_overlay_state_, 0, "probe_overlay_state", subjects_);

    // Accuracy test results
    UI_MANAGED_SUBJECT_STRING(probe_accuracy_result_, probe_accuracy_result_buf_, "",
                              "probe_accuracy_result", subjects_);
    UI_MANAGED_SUBJECT_INT(probe_accuracy_visible_, 0, "probe_accuracy_visible", subjects_);

    subjects_initialized_ = true;
    spdlog::trace("[Probe] Subjects initialized");
}

lv_obj_t* ProbeOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::debug("[Probe] Overlay already created");
        return overlay_root_;
    }

    parent_screen_ = parent;

    // Ensure subjects are initialized before XML creation
    if (!subjects_initialized_) {
        init_subjects();
    }

    spdlog::debug("[Probe] Creating overlay from XML");
    overlay_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "probe_overlay", nullptr));

    if (!overlay_root_) {
        spdlog::error("[Probe] Failed to create overlay from XML");
        return nullptr;
    }

    // Start hidden (push_overlay will show it)
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);

    // Cache type panel container for later swapping
    type_panel_container_ = lv_obj_find_by_name(overlay_root_, "probe_type_panel");

    spdlog::info("[Probe] Overlay created successfully");
    return overlay_root_;
}

void ProbeOverlay::show() {
    if (!overlay_root_) {
        spdlog::error("[Probe] Cannot show: overlay not created");
        return;
    }

    spdlog::debug("[Probe] Showing overlay");

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack - on_activate() will be called by NavigationManager
    NavigationManager::instance().push_overlay(overlay_root_);

    spdlog::info("[Probe] Overlay shown");
}

void ProbeOverlay::on_activate() {
    spdlog::debug("[Probe] Activated");

    // Update display subjects from current probe state
    update_display_subjects();

    // Load type-specific panel
    load_type_panel();
}

void ProbeOverlay::on_deactivate() {
    spdlog::debug("[Probe] Deactivated");
}

void ProbeOverlay::cleanup() {
    spdlog::trace("[Probe] Cleanup");
}

void ProbeOverlay::set_api(MoonrakerAPI* api) {
    api_ = api;
}

// ============================================================================
// DISPLAY SUBJECTS
// ============================================================================

void ProbeOverlay::update_display_subjects() {
    auto& mgr = ProbeSensorManager::instance();
    auto sensors = mgr.get_sensors();

    if (sensors.empty()) {
        snprintf(probe_display_name_buf_, sizeof(probe_display_name_buf_), "No Probe Detected");
        lv_subject_copy_string(&probe_display_name_, probe_display_name_buf_);
        lv_subject_copy_string(&probe_type_label_, "");
        snprintf(probe_z_offset_display_buf_, sizeof(probe_z_offset_display_buf_), "--");
        lv_subject_copy_string(&probe_z_offset_display_, probe_z_offset_display_buf_);
        return;
    }

    // Use first sensor (primary probe)
    const auto& sensor = sensors[0];
    std::string display_name = probe_type_to_display_string(sensor.type);
    snprintf(probe_display_name_buf_, sizeof(probe_display_name_buf_), "%s", display_name.c_str());
    lv_subject_copy_string(&probe_display_name_, probe_display_name_buf_);

    // Type description label
    const char* type_label = "Standard Probe";
    switch (sensor.type) {
    case ProbeSensorType::CARTOGRAPHER:
        type_label = "Eddy Current Scanning Probe";
        break;
    case ProbeSensorType::BEACON:
        type_label = "Eddy Current Probe";
        break;
    case ProbeSensorType::BLTOUCH:
        type_label = "Servo-Actuated Touch Probe";
        break;
    case ProbeSensorType::TAP:
        type_label = "Nozzle Contact Probe";
        break;
    case ProbeSensorType::KLICKY:
        type_label = "Magnetic Dock Probe";
        break;
    case ProbeSensorType::EDDY_CURRENT:
        type_label = "Eddy Current Probe";
        break;
    case ProbeSensorType::SMART_EFFECTOR:
        type_label = "Piezo Contact Probe";
        break;
    case ProbeSensorType::STANDARD:
    default:
        break;
    }
    snprintf(probe_type_label_buf_, sizeof(probe_type_label_buf_), "%s", type_label);
    lv_subject_copy_string(&probe_type_label_, probe_type_label_buf_);

    // Z offset display
    float z_offset = mgr.get_z_offset();
    snprintf(probe_z_offset_display_buf_, sizeof(probe_z_offset_display_buf_), "%.3fmm", z_offset);
    lv_subject_copy_string(&probe_z_offset_display_, probe_z_offset_display_buf_);
}

// ============================================================================
// TYPE-SPECIFIC PANEL LOADING
// ============================================================================

void ProbeOverlay::load_type_panel() {
    if (!type_panel_container_) {
        spdlog::warn("[Probe] Type panel container not found");
        return;
    }

    // Clear existing type panel children
    lv_obj_clean(type_panel_container_);

    auto& mgr = ProbeSensorManager::instance();
    auto sensors = mgr.get_sensors();

    if (sensors.empty()) {
        spdlog::debug("[Probe] No sensors, skipping type panel load");
        return;
    }

    const auto& sensor = sensors[0];
    const char* component = nullptr;

    switch (sensor.type) {
    case ProbeSensorType::BLTOUCH:
        component = "probe_bltouch_panel";
        break;
    case ProbeSensorType::CARTOGRAPHER:
        component = "probe_cartographer_panel";
        break;
    case ProbeSensorType::BEACON:
        component = "probe_beacon_panel";
        break;
    default:
        component = "probe_generic_panel";
        break;
    }

    spdlog::debug("[Probe] Loading type panel: {}", component);
    auto* panel = static_cast<lv_obj_t*>(lv_xml_create(type_panel_container_, component, nullptr));
    if (!panel) {
        spdlog::warn("[Probe] Failed to create type panel: {}", component);
    }
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void ProbeOverlay::handle_probe_accuracy() {
    spdlog::debug("[Probe] Probe accuracy test requested");

    MoonrakerClient* client = get_moonraker_client();
    if (!client) {
        spdlog::error("[Probe] No client available for accuracy test");
        return;
    }

    // Show that test is in progress
    snprintf(probe_accuracy_result_buf_, sizeof(probe_accuracy_result_buf_), "Running...");
    lv_subject_copy_string(&probe_accuracy_result_, probe_accuracy_result_buf_);
    lv_subject_set_int(&probe_accuracy_visible_, 1);

    // Send PROBE_ACCURACY command (async via gcode_script)
    int result = client->gcode_script("PROBE_ACCURACY");
    if (result != 0) {
        spdlog::error("[Probe] PROBE_ACCURACY command failed: {}", result);
        snprintf(probe_accuracy_result_buf_, sizeof(probe_accuracy_result_buf_),
                 "Test failed (error %d)", result);
        lv_subject_copy_string(&probe_accuracy_result_, probe_accuracy_result_buf_);
    } else {
        snprintf(probe_accuracy_result_buf_, sizeof(probe_accuracy_result_buf_),
                 "Test started - results in console");
        lv_subject_copy_string(&probe_accuracy_result_, probe_accuracy_result_buf_);
    }
}

void ProbeOverlay::handle_zoffset_cal() {
    spdlog::debug("[Probe] Z-Offset calibration requested");

    auto& overlay = get_global_zoffset_cal_panel();

    // Lazy-create z-offset overlay
    if (!overlay.get_root()) {
        overlay.init_subjects();
        overlay.set_api(get_moonraker_api());
        overlay.create(lv_display_get_screen_active(nullptr));
    }

    overlay.show();
}

void ProbeOverlay::handle_bed_mesh() {
    spdlog::debug("[Probe] Bed mesh requested");

    auto& panel = get_global_bed_mesh_panel();

    // Lazy-create bed mesh overlay
    if (!panel.get_root()) {
        if (!panel.are_subjects_initialized()) {
            panel.init_subjects();
        }
        panel.register_callbacks();
        auto* root = panel.create(lv_display_get_screen_active(nullptr));
        if (root) {
            NavigationManager::instance().register_overlay_instance(root, &panel);
        }
    }

    if (panel.get_root()) {
        NavigationManager::instance().push_overlay(panel.get_root());
    }
}
