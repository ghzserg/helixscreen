// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_printer_manager_overlay.h"

#include "ui_fan_control_overlay.h"
#include "ui_nav_manager.h"
#include "ui_overlay_retraction_settings.h"
#include "ui_overlay_timelapse_settings.h"
#include "ui_panel_ams.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_input_shaper.h"
#include "ui_panel_screws_tilt.h"
#include "ui_panel_spoolman.h"
#include "ui_settings_sound.h"
#include "ui_toast.h"

#include "app_globals.h"
#include "config.h"
#include "helix_version.h"
#include "printer_detector.h"
#include "printer_images.h"
#include "static_panel_registry.h"
#include "subject_debug_registry.h"
#include "ui/ui_lazy_panel_helper.h"
#include "wizard_config_paths.h"

#include <spdlog/spdlog.h>

#include <cstring>

// =============================================================================
// Global Instance
// =============================================================================

static std::unique_ptr<PrinterManagerOverlay> g_printer_manager_overlay;

PrinterManagerOverlay& get_printer_manager_overlay() {
    if (!g_printer_manager_overlay) {
        g_printer_manager_overlay = std::make_unique<PrinterManagerOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "PrinterManagerOverlay", []() { g_printer_manager_overlay.reset(); });
    }
    return *g_printer_manager_overlay;
}

void destroy_printer_manager_overlay() {
    g_printer_manager_overlay.reset();
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

PrinterManagerOverlay::PrinterManagerOverlay() {
    std::memset(name_buf_, 0, sizeof(name_buf_));
    std::memset(model_buf_, 0, sizeof(model_buf_));
    std::memset(version_buf_, 0, sizeof(version_buf_));
}

PrinterManagerOverlay::~PrinterManagerOverlay() {
    if (lv_is_initialized()) {
        deinit_subjects_base(subjects_);
    }
}

// =============================================================================
// Subject Initialization
// =============================================================================

void PrinterManagerOverlay::init_subjects() {
    init_subjects_guarded([this]() {
        UI_MANAGED_SUBJECT_STRING(printer_manager_name_, name_buf_, "Unknown",
                                  "printer_manager_name", subjects_);
        UI_MANAGED_SUBJECT_STRING(printer_manager_model_, model_buf_, "", "printer_manager_model",
                                  subjects_);
        UI_MANAGED_SUBJECT_STRING(helix_version_, version_buf_, "0.0.0", "helix_version",
                                  subjects_);
    });
}

// =============================================================================
// Create
// =============================================================================

lv_obj_t* PrinterManagerOverlay::create(lv_obj_t* parent) {
    if (!create_overlay_from_xml(parent, "printer_manager_overlay")) {
        return nullptr;
    }

    // Find the printer image widget for programmatic image source setting
    printer_image_obj_ = lv_obj_find_by_name(overlay_root_, "pm_printer_image");

    return overlay_root_;
}

// =============================================================================
// Callbacks
// =============================================================================

void PrinterManagerOverlay::register_callbacks() {
    // Chip navigation callbacks
    lv_xml_register_event_cb(nullptr, "pm_chip_bed_mesh_clicked", on_chip_bed_mesh_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_leds_clicked", on_chip_leds_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_adxl_clicked", on_chip_adxl_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_retraction_clicked", on_chip_retraction_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_spoolman_clicked", on_chip_spoolman_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_timelapse_clicked", on_chip_timelapse_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_screws_tilt_clicked", on_chip_screws_tilt_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_ams_clicked", on_chip_ams_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_fans_clicked", on_chip_fans_clicked);
    lv_xml_register_event_cb(nullptr, "pm_chip_speaker_clicked", on_chip_speaker_clicked);
}

// =============================================================================
// Chip Navigation Callbacks
// =============================================================================

void PrinterManagerOverlay::on_chip_bed_mesh_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Bed Mesh chip clicked");
    auto& pm = get_printer_manager_overlay();
    helix::ui::lazy_create_and_push_overlay<BedMeshPanel>(
        get_global_bed_mesh_panel, pm.bed_mesh_panel_, lv_display_get_screen_active(nullptr),
        "Bed Mesh", "Printer Manager");
}

void PrinterManagerOverlay::on_chip_leds_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] LEDs chip clicked");
    // TODO: Navigate to LED settings when available
    ui_toast_show(ToastSeverity::INFO, "LED settings coming soon", 2000);
}

void PrinterManagerOverlay::on_chip_adxl_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] ADXL chip clicked");
    auto& pm = get_printer_manager_overlay();
    helix::ui::lazy_create_and_push_overlay<InputShaperPanel>(
        get_global_input_shaper_panel, pm.input_shaper_panel_,
        lv_display_get_screen_active(nullptr), "Input Shaper", "Printer Manager");
}

void PrinterManagerOverlay::on_chip_retraction_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Retraction chip clicked");
    auto& pm = get_printer_manager_overlay();
    helix::ui::lazy_create_and_push_overlay<RetractionSettingsOverlay>(
        get_global_retraction_settings, pm.retraction_panel_, lv_display_get_screen_active(nullptr),
        "Retraction Settings", "Printer Manager");
}

void PrinterManagerOverlay::on_chip_spoolman_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Spoolman chip clicked");
    auto& pm = get_printer_manager_overlay();
    helix::ui::lazy_create_and_push_overlay<SpoolmanPanel>(
        get_global_spoolman_panel, pm.spoolman_panel_, lv_display_get_screen_active(nullptr),
        "Spoolman", "Printer Manager");
}

void PrinterManagerOverlay::on_chip_timelapse_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Timelapse chip clicked");
    auto& pm = get_printer_manager_overlay();
    helix::ui::lazy_create_and_push_overlay<TimelapseSettingsOverlay>(
        get_global_timelapse_settings, pm.timelapse_panel_, lv_display_get_screen_active(nullptr),
        "Timelapse Settings", "Printer Manager");
}

void PrinterManagerOverlay::on_chip_screws_tilt_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Screws Tilt chip clicked");
    auto& pm = get_printer_manager_overlay();
    helix::ui::lazy_create_and_push_overlay<ScrewsTiltPanel>(
        get_global_screws_tilt_panel, pm.screws_tilt_panel_, lv_display_get_screen_active(nullptr),
        "Bed Screws", "Printer Manager");
}

void PrinterManagerOverlay::on_chip_ams_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] AMS chip clicked");

    auto& ams_panel = get_global_ams_panel();
    if (!ams_panel.are_subjects_initialized()) {
        ams_panel.init_subjects();
    }
    lv_obj_t* panel_obj = ams_panel.get_panel();
    if (panel_obj) {
        ui_nav_push_overlay(panel_obj);
    }
}

void PrinterManagerOverlay::on_chip_fans_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Fans chip clicked");

    auto& pm = get_printer_manager_overlay();
    if (!pm.fan_control_panel_) {
        auto& overlay = get_fan_control_overlay();
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();
        overlay.set_api(get_moonraker_api());

        lv_obj_t* screen = lv_display_get_screen_active(nullptr);
        pm.fan_control_panel_ = overlay.create(screen);
        if (!pm.fan_control_panel_) {
            spdlog::warn("[Printer Manager] Failed to create fan control overlay");
            return;
        }
        NavigationManager::instance().register_overlay_instance(pm.fan_control_panel_, &overlay);
    }

    if (pm.fan_control_panel_) {
        get_fan_control_overlay().set_api(get_moonraker_api());
        ui_nav_push_overlay(pm.fan_control_panel_);
    }
}

void PrinterManagerOverlay::on_chip_speaker_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Printer Manager] Speaker chip clicked");
    auto& overlay = helix::settings::get_sound_settings_overlay();
    overlay.show(lv_display_get_screen_active(nullptr));
}

// =============================================================================
// Lifecycle
// =============================================================================

void PrinterManagerOverlay::on_activate() {
    OverlayBase::on_activate();
    refresh_printer_info();
}

// =============================================================================
// Refresh Printer Info
// =============================================================================

void PrinterManagerOverlay::refresh_printer_info() {
    Config* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[{}] Config not available", get_name());
        return;
    }

    // Printer name from config (user-given name, or fallback)
    std::string name = config->get<std::string>(helix::wizard::PRINTER_NAME, "");
    if (name.empty()) {
        name = "My Printer";
    }
    std::strncpy(name_buf_, name.c_str(), sizeof(name_buf_) - 1);
    name_buf_[sizeof(name_buf_) - 1] = '\0';
    lv_subject_copy_string(&printer_manager_name_, name_buf_);

    // Printer model/type from config
    std::string model = config->get<std::string>(helix::wizard::PRINTER_TYPE, "");
    std::strncpy(model_buf_, model.c_str(), sizeof(model_buf_) - 1);
    model_buf_[sizeof(model_buf_) - 1] = '\0';
    lv_subject_copy_string(&printer_manager_model_, model_buf_);

    // HelixScreen version
    const char* version = helix_version();
    std::strncpy(version_buf_, version, sizeof(version_buf_) - 1);
    version_buf_[sizeof(version_buf_) - 1] = '\0';
    lv_subject_copy_string(&helix_version_, version_buf_);

    spdlog::debug("[{}] Refreshed: name='{}', model='{}', version='{}'", get_name(), name_buf_,
                  model_buf_, version_buf_);

    // Update printer image programmatically (exception to declarative rule)
    // Store path in member to ensure string lifetime outlives lv_image_set_src
    if (printer_image_obj_ && !model.empty()) {
        current_image_path_ = PrinterImages::get_best_printer_image(model);
        lv_image_set_src(printer_image_obj_, current_image_path_.c_str());
        spdlog::debug("[{}] Printer image: '{}' for '{}'", get_name(), current_image_path_, model);
    }
}
