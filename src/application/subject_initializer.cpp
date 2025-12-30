// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#include "subject_initializer.h"

#include "ui_component_keypad.h"
#include "ui_emergency_stop.h"
#include "ui_error_reporting.h"
#include "ui_nav.h"
#include "ui_notification.h"
#include "ui_panel_advanced.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_panel_console.h"
#include "ui_panel_controls.h"
#include "ui_panel_extrusion.h"
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
#include "ui_retraction_settings.h"
#include "ui_status_bar.h"
#include "ui_timelapse_settings.h"
#include "ui_wizard.h"

#include "active_print_media_manager.h"
#include "ams_state.h"
#include "app_globals.h"
#include "filament_sensor_manager.h"
#include "lvgl/lvgl.h"
#include "print_completion.h"
#include "print_start_navigation.h"
#include "printer_state.h"
#include "usb_backend_mock.h"
#include "usb_manager.h"

#include <spdlog/spdlog.h>

#include <chrono>

SubjectInitializer::SubjectInitializer() = default;
SubjectInitializer::~SubjectInitializer() = default;

bool SubjectInitializer::init_all(const RuntimeConfig& runtime_config) {
    if (m_initialized) {
        spdlog::warn("[SubjectInitializer] Already initialized");
        return false;
    }

    spdlog::debug("[SubjectInitializer] Initializing reactive subjects...");

    // Phase 1: Core subjects (must be first)
    init_core_subjects();

    // Phase 2: PrinterState subjects (panels depend on these)
    init_printer_state_subjects();

    // Phase 3: AMS and filament sensor subjects
    init_ams_subjects();

    // Phase 4: Panel subjects
    init_panel_subjects();

    // Phase 5: Observers (depend on subjects being ready)
    init_observers();

    // Phase 6: Utility subjects
    init_utility_subjects();

    // Phase 7: USB manager (needs notification system)
    init_usb_manager(runtime_config);

    m_initialized = true;
    spdlog::info("[SubjectInitializer] Initialized {} observer guards", m_observers.size());

    return true;
}

void SubjectInitializer::inject_api(MoonrakerAPI* api) {
    if (!api) {
        spdlog::warn("[SubjectInitializer] inject_api called with nullptr");
        return;
    }

    spdlog::debug("[SubjectInitializer] Injecting MoonrakerAPI into panels");

    // Panels with member pointer references
    if (m_print_select_panel) {
        m_print_select_panel->set_api(api);
    }
    if (m_print_status_panel) {
        m_print_status_panel->set_api(api);
    }
    // MotionPanel now uses get_moonraker_api() global - no set_api() needed
    if (m_extrusion_panel) {
        m_extrusion_panel->set_api(api);
    }
    if (m_bed_mesh_panel) {
        m_bed_mesh_panel->set_api(api);
    }
    if (m_temp_control_panel) {
        m_temp_control_panel->set_api(api);
    }

    // Panels using global accessor pattern
    get_global_home_panel().set_api(api);
    get_global_controls_panel().set_api(api);
    get_global_filament_panel().set_api(api);
    get_global_advanced_panel().set_api(api);
    // SpoolmanPanel uses get_moonraker_api() global directly
    get_global_history_dashboard_panel().set_api(api);
    // HistoryListPanel uses get_moonraker_api() global directly (OverlayBase pattern)
    get_global_timelapse_settings().set_api(api);

    // ActivePrintMediaManager needs API for thumbnail loading
    helix::get_active_print_media_manager().set_api(api);
}

void SubjectInitializer::init_core_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing core subjects");
    app_globals_init_subjects();   // Global subjects (notification subject, etc.)
    ui_nav_init();                 // Navigation system (icon colors, active panel)
    ui_status_bar_init_subjects(); // Status bar subjects (printer/network icon states)
}

void SubjectInitializer::init_printer_state_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing PrinterState subjects");
    // PrinterState must be initialized BEFORE panels that observe its subjects
    // (e.g., HomePanel observes led_state_, extruder_temp_, connection_state_)
    get_printer_state().init_subjects();

    // ActivePrintMediaManager observes print_filename_ and updates print_display_filename_
    // and print_thumbnail_path_. Must be initialized after PrinterState, before panels.
    helix::init_active_print_media_manager();
}

void SubjectInitializer::init_ams_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing AMS/FilamentSensor subjects");
    // Initialize AmsState subjects BEFORE panels so XML bindings can find ams_gate_count
    // Note: In mock mode, init_subjects() also creates the mock backend internally
    AmsState::instance().init_subjects(true);

    // Initialize FilamentSensorManager subjects BEFORE panels so XML bindings can work
    helix::FilamentSensorManager::instance().init_subjects();
}

void SubjectInitializer::init_panel_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing panel subjects");

    // Basic panels
    get_global_home_panel().init_subjects();
    get_global_controls_panel().init_subjects();
    get_global_filament_panel().init_subjects();
    get_global_settings_panel().init_subjects();

    // Advanced panel family
    init_global_advanced_panel(get_printer_state(), nullptr);
    get_global_advanced_panel().init_subjects();

    // SpoolmanPanel uses lazy initialization via get_global_spoolman_panel()
    // and is initialized on first access in AdvancedPanel::handle_spoolman_clicked()

    init_global_history_dashboard_panel(get_printer_state(), nullptr, get_print_history_manager());
    get_global_history_dashboard_panel().init_subjects();

    // HistoryListPanel is now lazy-initialized by HistoryDashboardPanel (OverlayBase pattern)

    // Settings overlays
    init_global_timelapse_settings(get_printer_state(), nullptr);
    get_global_timelapse_settings().init_subjects();

    init_global_retraction_settings(get_printer_state(), nullptr);
    get_global_retraction_settings().init_subjects();

    // ConsolePanel is now lazy-initialized by AdvancedPanel (OverlayBase pattern)

    // Row handlers for advanced features
    init_screws_tilt_row_handler();
    init_input_shaper_row_handler();
    init_zoffset_row_handler();
    init_zoffset_event_callbacks();

    // Wizard and keypad
    ui_wizard_init_subjects();
    ui_keypad_init_subjects();

    // Panels that need deferred API injection
    m_print_select_panel = get_print_select_panel(get_printer_state(), nullptr);
    m_print_select_panel->init_subjects();

    m_print_status_panel = &get_global_print_status_panel();
    m_print_status_panel->init_subjects();

    m_motion_panel = &get_global_motion_panel();
    m_motion_panel->init_subjects();

    m_extrusion_panel = &get_global_extrusion_panel();
    m_extrusion_panel->init_subjects();

    m_bed_mesh_panel = &get_global_bed_mesh_panel();
    m_bed_mesh_panel->init_subjects();

    // Panel initialization via global instances
    get_global_pid_cal_panel().init_subjects();
    get_global_zoffset_cal_panel().init_subjects();

    // TempControlPanel (owned by SubjectInitializer)
    m_temp_control_panel = std::make_unique<TempControlPanel>(get_printer_state(), nullptr);
    m_temp_control_panel->init_subjects();

    // Inject TempControlPanel into dependent panels
    get_global_controls_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_home_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_print_status_panel().set_temp_control_panel(m_temp_control_panel.get());
    get_global_filament_panel().set_temp_control_panel(m_temp_control_panel.get());

    // E-Stop overlay
    EmergencyStopOverlay::instance().init_subjects();
}

void SubjectInitializer::init_observers() {
    spdlog::trace("[SubjectInitializer] Initializing observers");

    // Print completion notification observer
    m_observers.push_back(helix::init_print_completion_observer());

    // Print start navigation observer (auto-navigate to print status)
    m_observers.push_back(helix::init_print_start_navigation_observer());
}

void SubjectInitializer::init_utility_subjects() {
    spdlog::trace("[SubjectInitializer] Initializing utility subjects");
    ui_notification_init();
}

void SubjectInitializer::init_usb_manager(const RuntimeConfig& runtime_config) {
    spdlog::trace("[SubjectInitializer] Initializing USB manager");

    m_usb_manager = std::make_unique<UsbManager>(runtime_config.should_mock_usb());
    if (m_usb_manager->start()) {
        spdlog::info("[SubjectInitializer] USB Manager started (mock={})",
                     runtime_config.should_mock_usb());
        if (m_print_select_panel) {
            m_print_select_panel->set_usb_manager(m_usb_manager.get());
        }
    } else {
        spdlog::warn("[SubjectInitializer] USB Manager failed to start");
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

        // In test mode, schedule demo drive insertion after UI is ready
        if (runtime_config.should_mock_usb()) {
            UsbManager* usb_mgr = m_usb_manager.get();
            lv_timer_create(
                [](lv_timer_t* timer) {
                    auto* usb_mgr = static_cast<UsbManager*>(lv_timer_get_user_data(timer));
                    if (auto* mock = dynamic_cast<UsbBackendMock*>(usb_mgr->get_backend())) {
                        mock->add_demo_drives();
                        spdlog::debug("[SubjectInitializer] Added demo USB drives for test mode");
                    }
                    lv_timer_delete(timer);
                },
                1500, // 1.5s delay - within grace period so no toast
                usb_mgr);
        }
    }
}
