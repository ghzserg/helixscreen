// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subject_initializer.h"

#include "ui_component_keypad.h"
#include "ui_emergency_stop.h"
#include "ui_error_reporting.h"
#include "ui_fan_control_overlay.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_notification.h"
#include "ui_notification_manager.h"
#include "ui_overlay_retraction_settings.h"
#include "ui_overlay_timelapse_install.h"
#include "ui_overlay_timelapse_settings.h"
#include "ui_panel_advanced.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_panel_console.h"
#include "ui_panel_controls.h"
#include "ui_panel_filament.h"
#include "ui_panel_history_dashboard.h"
#include "ui_panel_history_list.h"
#include "ui_panel_home.h"
#include "ui_panel_input_shaper.h"
#include "ui_panel_motion.h"
#include "ui_panel_print_select.h"
#include "ui_panel_print_status.h"
#include "ui_panel_screws_tilt.h"
#include "ui_panel_settings.h"
#include "ui_panel_spoolman.h"
#include "ui_panel_temp_control.h"
#include "ui_printer_status_icon.h"
#include "ui_wizard.h"

#include "abort_manager.h"
#include "accel_sensor_manager.h"
#include "active_print_media_manager.h"
#include "ams_state.h"
#include "app_globals.h"
#include "color_sensor_manager.h"
#include "filament_sensor_manager.h"
#include "humidity_sensor_manager.h"
#include "lvgl/lvgl.h"
#include "print_completion.h"
#include "print_start_navigation.h"
#include "printer_state.h"
#include "probe_sensor_manager.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "static_subject_registry.h"
#include "system/telemetry_manager.h"
#include "temperature_sensor_manager.h"
#include "usb_manager.h"
#include "width_sensor_manager.h"
#include "xml_registration.h"

#include <spdlog/spdlog.h>

#include <chrono>

SubjectInitializer::SubjectInitializer() = default;
SubjectInitializer::~SubjectInitializer() = default;

bool SubjectInitializer::init_all(const RuntimeConfig& runtime_config) {
    if (m_initialized) {
        spdlog::warn("[SubjectInitializer] Already initialized");
        return false;
    }

    // Legacy path: init with nullptr API (panels will need inject_api later)
    spdlog::debug("[SubjectInitializer] Initializing reactive subjects (legacy path)...");

    init_core_and_state();
    init_panels(nullptr, runtime_config);
    init_post(runtime_config);

    return true;
}

void SubjectInitializer::init_core_and_state() {
    spdlog::debug("[SubjectInitializer] Initializing core and state subjects...");

    // Phase 1: Core subjects (must be first)
    init_core_subjects();

    // Phase 2: PrinterState subjects (panels depend on these)
    init_printer_state_subjects();

    // Phase 3: AMS and filament sensor subjects
    init_ams_subjects();

    spdlog::debug("[SubjectInitializer] Core and state subjects initialized");
}

void SubjectInitializer::init_panels(MoonrakerAPI* api, const RuntimeConfig& /* runtime_config */) {
    spdlog::debug("[SubjectInitializer] Initializing panel subjects (api={})...",
                  api ? "valid" : "nullptr");

    // Phase 4: Panel subjects
    init_panel_subjects(api);
}

void SubjectInitializer::init_post(const RuntimeConfig& runtime_config) {
    spdlog::debug("[SubjectInitializer] Initializing post-panel subjects...");

    // Phase 5: Observers (depend on subjects being ready)
    init_observers();

    // Phase 6: Utility subjects
    init_utility_subjects();

    // Phase 7: USB manager (needs notification system)
    init_usb_manager(runtime_config);

    m_initialized = true;
    spdlog::debug("[SubjectInitializer] Initialized {} observer guards", m_observers.size());
}

void SubjectInitializer::init_core_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing core subjects");
    app_globals_init_subjects();            // Global subjects (notification subject, etc.)
    ui_nav_init();                          // Navigation system (icon colors, active panel)
    ui_printer_status_icon_init_subjects(); // Printer icon state
    ui_status_bar_init_subjects();          // Notification badge subjects
}

void SubjectInitializer::init_printer_state_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing PrinterState subjects");
    // PrinterState must be initialized BEFORE panels that observe its subjects
    // (e.g., HomePanel observes led_state_, extruder_temp_, connection_state_)
    get_printer_state().init_subjects();

    // Register PrinterState cleanup - MUST happen before lv_deinit() to disconnect observers
    // Calls lv_subject_deinit() on all 60+ subjects across all sub-components
    StaticSubjectRegistry::instance().register_deinit(
        "PrinterState", []() { get_printer_state().deinit_subjects(); });

    // ActivePrintMediaManager observes print_filename_ and updates print_display_filename_
    // and print_thumbnail_path_. Must be initialized after PrinterState, before panels.
    helix::init_active_print_media_manager();
}

void SubjectInitializer::init_ams_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing AMS/FilamentSensor subjects");

    // Helper macro: initializes a sensor manager's subjects and registers cleanup.
    // Takes the fully-qualified class name (e.g., helix::sensors::HumiditySensorManager).
#define REGISTER_SENSOR_MANAGER(ManagerClass)                                                      \
    ManagerClass::instance().init_subjects();                                                      \
    StaticSubjectRegistry::instance().register_deinit(                                             \
        #ManagerClass, []() { ManagerClass::instance().deinit_subjects(); })

    // Initialize AmsState subjects BEFORE panels so XML bindings can find ams_gate_count
    // Note: In mock mode, init_subjects() also creates the mock backend internally
    AmsState::instance().init_subjects(true);

    // Register AmsState cleanup (StaticSubjectRegistry - core state singleton)
    StaticSubjectRegistry::instance().register_deinit(
        "AmsState", []() { AmsState::instance().deinit_subjects(); });

    // Initialize sensor manager subjects BEFORE panels so XML bindings can work
    REGISTER_SENSOR_MANAGER(helix::FilamentSensorManager);
    REGISTER_SENSOR_MANAGER(helix::sensors::HumiditySensorManager);
    REGISTER_SENSOR_MANAGER(helix::sensors::WidthSensorManager);
    REGISTER_SENSOR_MANAGER(helix::sensors::ProbeSensorManager);
    REGISTER_SENSOR_MANAGER(helix::sensors::AccelSensorManager);
    REGISTER_SENSOR_MANAGER(helix::sensors::ColorSensorManager);
    REGISTER_SENSOR_MANAGER(helix::sensors::TemperatureSensorManager);

#undef REGISTER_SENSOR_MANAGER
}

void SubjectInitializer::init_panel_subjects(MoonrakerAPI* api) {
    spdlog::trace("[SubjectInitializer] Initializing panel subjects");

    // Basic panels - these use PanelBase which stores API
    get_global_home_panel().init_subjects();
    if (api)
        get_global_home_panel().set_api(api);
    StaticPanelRegistry::instance().register_destroy(
        "HomePanelSubjects", []() { get_global_home_panel().deinit_subjects(); });

    // Controls, Filament, Settings panels: deinit handled by destructor
    // (registered with StaticPanelRegistry in their get_global_* functions)
    get_global_controls_panel().init_subjects();
    if (api)
        get_global_controls_panel().set_api(api);
    get_global_filament_panel().init_subjects();
    if (api)
        get_global_filament_panel().set_api(api);
    get_global_settings_panel().init_subjects();
    if (api)
        get_global_settings_panel().set_api(api);

    // SettingsManager subjects are initialized by settings_panel.init_subjects() above
    // Register cleanup here (StaticSubjectRegistry - core state singleton)
    StaticSubjectRegistry::instance().register_deinit(
        "SettingsManager", []() { SettingsManager::instance().deinit_subjects(); });

    // Advanced panel family
    init_global_advanced_panel(get_printer_state(), api);
    get_global_advanced_panel().init_subjects();

    // SpoolmanPanel uses lazy initialization via get_global_spoolman_panel()
    // and is initialized on first access in AdvancedPanel::handle_spoolman_clicked()

    // HistoryDashboardPanel is now lazy-initialized (OverlayBase pattern)
    // HistoryListPanel is now lazy-initialized by HistoryDashboardPanel (OverlayBase pattern)

    // Settings overlays
    init_global_timelapse_settings(api);
    get_global_timelapse_settings().init_subjects();

    init_global_timelapse_install(api);
    get_global_timelapse_install().init_subjects();

    init_global_retraction_settings(api);
    get_global_retraction_settings().init_subjects();

    // Fan control overlay (opened from Controls panel secondary fans list)
    init_fan_control_overlay(get_printer_state());
    get_fan_control_overlay().init_subjects();

    // ConsolePanel is now lazy-initialized by AdvancedPanel (OverlayBase pattern)

    // Row handlers for advanced features
    init_screws_tilt_row_handler();
    init_input_shaper_row_handler();
    init_zoffset_row_handler();
    init_zoffset_event_callbacks();

    // Wizard and keypad - register cleanup with StaticPanelRegistry
    ui_wizard_init_subjects();
    StaticPanelRegistry::instance().register_destroy("WizardSubjects", ui_wizard_deinit_subjects);

    ui_keypad_init_subjects();
    StaticPanelRegistry::instance().register_destroy("KeypadSubjects", ui_keypad_deinit_subjects);

    // Core state subjects cleanup (StaticSubjectRegistry - not panels)
    StaticSubjectRegistry::instance().register_deinit("AppGlobals", app_globals_deinit_subjects);
    StaticSubjectRegistry::instance().register_deinit("XmlSubjects", helix::deinit_xml_subjects);

    // UI component subjects cleanup (StaticPanelRegistry - UI components)
    StaticPanelRegistry::instance().register_destroy("PrinterStatusIconSubjects",
                                                     ui_printer_status_icon_deinit_subjects);
    StaticPanelRegistry::instance().register_destroy("StatusBarSubjects",
                                                     ui_status_bar_deinit_subjects);

    // Panels with API injection at construction
    // Note: PrintSelectPanel registers its own deinit+destroy callback in get_print_select_panel()
    m_print_select_panel = get_print_select_panel(get_printer_state(), api);
    m_print_select_panel->init_subjects();

    m_print_status_panel = &get_global_print_status_panel();
    if (api)
        m_print_status_panel->set_api(api);
    m_print_status_panel->init_subjects();
    StaticPanelRegistry::instance().register_destroy(
        "PrintStatusPanelSubjects", []() { get_global_print_status_panel().deinit_subjects(); });

    // Motion panel: deinit handled by destructor
    // (registered with StaticPanelRegistry in their get_global_* functions)
    m_motion_panel = &get_global_motion_panel();
    m_motion_panel->init_subjects();

    m_bed_mesh_panel = &get_global_bed_mesh_panel();
    m_bed_mesh_panel->init_subjects();
    StaticPanelRegistry::instance().register_destroy(
        "BedMeshPanelSubjects", []() { get_global_bed_mesh_panel().deinit_subjects(); });

    // Panel initialization via global instances
    // PIDCalibrationPanel: deinit handled by destructor (registered with StaticPanelRegistry)
    get_global_pid_cal_panel().init_subjects();

    get_global_zoffset_cal_panel().init_subjects();

    // TempControlPanel (owned by SubjectInitializer - destructor handles deinit_subjects)
    m_temp_control_panel = std::make_unique<TempControlPanel>(get_printer_state(), api);
    m_temp_control_panel->init_subjects();

    // Inject TempControlPanel into dependent panels
    get_global_controls_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_home_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_print_status_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_filament_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_pid_cal_panel().set_temp_control_panel(m_temp_control_panel.get());

    // E-Stop overlay
    EmergencyStopOverlay::instance().init_subjects();
    StaticPanelRegistry::instance().register_destroy(
        "EmergencyStopSubjects", []() { EmergencyStopOverlay::instance().deinit_subjects(); });

    // AbortManager subjects (for smart print cancellation)
    helix::AbortManager::instance().init_subjects();
    StaticPanelRegistry::instance().register_destroy(
        "AbortManagerSubjects", []() { helix::AbortManager::instance().deinit_subjects(); });

    // Navigation manager subjects (StaticSubjectRegistry - state manager, not a visual panel)
    StaticSubjectRegistry::instance().register_deinit(
        "NavigationManager", []() { NavigationManager::instance().deinit_subjects(); });

    // ActivePrintMediaManager needs API for thumbnail loading
    if (api) {
        helix::get_active_print_media_manager().set_api(api);
    }
}

void SubjectInitializer::init_observers() {
    spdlog::trace("[SubjectInitializer] Initializing observers");

    // Print completion notification observer
    m_observers.push_back(helix::init_print_completion_observer());

    // Print start navigation observer (auto-navigate to print status)
    m_observers.push_back(helix::init_print_start_navigation_observer());

    // Print outcome telemetry observer (records anonymous print stats when telemetry enabled)
    m_observers.push_back(TelemetryManager::instance().init_print_outcome_observer());
}

void SubjectInitializer::init_utility_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing utility subjects");
    ui_notification_init();
}

void SubjectInitializer::init_usb_manager(const RuntimeConfig& runtime_config) {
    spdlog::trace("[SubjectInitializer] Initializing USB manager");

    m_usb_manager = std::make_unique<UsbManager>(runtime_config.should_mock_usb());
    if (m_usb_manager->start()) {
        spdlog::debug("[SubjectInitializer] USB Manager started (mock={})",
                      runtime_config.should_mock_usb());
        if (m_print_select_panel) {
            m_print_select_panel->set_usb_manager(m_usb_manager.get());
        }
    } else {
        spdlog::info(
            "[SubjectInitializer] USB Manager not started (not available on this platform)");
    }

    // Set up USB drive event notifications
    if (m_usb_manager) {
        // Track when USB callbacks were set up - suppress toasts for drives at startup
        static auto usb_setup_time = std::chrono::steady_clock::now();

        // Capture print_select_panel for the callback
        PrintSelectPanel* panel = m_print_select_panel;

        m_usb_manager->set_drive_callback([panel](UsbEvent event, const UsbDrive& drive) {
            (void)drive;

            // Suppress toast for drives detected within 3 seconds of startup
            constexpr auto GRACE_PERIOD = std::chrono::seconds(3);
            auto now = std::chrono::steady_clock::now();
            bool within_grace_period = (now - usb_setup_time) < GRACE_PERIOD;

            if (event == UsbEvent::DRIVE_INSERTED) {
                if (!within_grace_period) {
                    NOTIFY_SUCCESS("USB drive connected");
                } else {
                    spdlog::debug("[USB] Suppressing toast for drive present at startup");
                }
                if (panel) {
                    panel->on_usb_drive_inserted();
                }
            } else if (event == UsbEvent::DRIVE_REMOVED) {
                NOTIFY_INFO("USB drive removed");
                if (panel) {
                    panel->on_usb_drive_removed();
                }
            }
        });
        // Note: Demo drives are now auto-added by UsbBackendMock::start() after 1.5s delay
    }
}
