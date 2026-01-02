// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 HelixScreen

#pragma once

#include "ui_observer_guard.h"

#include "cli_args.h"
#include "lvgl/lvgl.h"

#include <memory>

// Forward declarations
class Config;
namespace helix::plugin {
class PluginManager;
}
class DisplayManager;
class SubjectInitializer;
class MoonrakerManager;
class PanelFactory;
class PrintHistoryManager;
class TemperatureHistoryManager;

/**
 * @brief Main application orchestrator
 *
 * Application coordinates all subsystems in the correct order:
 * 1. Parse CLI args and configure runtime settings
 * 2. Initialize display (LVGL, backend, input devices)
 * 3. Register fonts and images
 * 4. Initialize reactive subjects
 * 5. Create UI from XML and wire panels
 * 6. Initialize Moonraker client/API
 * 7. Connect to printer and run main loop
 * 8. Shutdown in reverse order
 *
 * Usage:
 *   Application app;
 *   return app.run(argc, argv);
 */
class Application {
  public:
    Application();
    ~Application();

    // Non-copyable, non-movable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /**
     * @brief Run the application
     * @param argc Command line argument count
     * @param argv Command line argument array
     * @return Exit code (0 = success)
     */
    int run(int argc, char** argv);

  private:
    // Initialization phases
    bool parse_args(int argc, char** argv);
    bool init_config();
    bool init_logging();
    bool init_display();
    bool init_theme();
    bool init_assets();
    bool register_widgets();
    bool register_xml_components();
    bool init_subjects();
    bool init_ui();
    bool init_moonraker();
    bool connect_moonraker();
    void create_overlays();
    bool run_wizard();
    bool init_plugins();

    // Main loop
    int main_loop();
    void handle_keyboard_shortcuts();
    void process_notifications();
    void check_timeouts();

    // Shutdown
    void shutdown();

    // Helper functions
    void ensure_project_root_cwd();
    void auto_configure_mock_state();
    void signal_splash_exit();
    lv_obj_t* create_overlay_panel(lv_obj_t* screen, const char* component_name,
                                   const char* display_name);

    // Owned managers (in initialization order)
    std::unique_ptr<DisplayManager> m_display;
    std::unique_ptr<SubjectInitializer> m_subjects;
    std::unique_ptr<MoonrakerManager> m_moonraker;
    std::unique_ptr<PrintHistoryManager> m_history_manager;
    std::unique_ptr<TemperatureHistoryManager> m_temp_history_manager;
    std::unique_ptr<PanelFactory> m_panels;
    std::unique_ptr<helix::plugin::PluginManager> m_plugin_manager;

    // Configuration
    Config* m_config = nullptr; // Singleton, not owned
    helix::CliArgs m_args;

    // Screen dimensions
    int m_screen_width = 800;
    int m_screen_height = 480;

    // UI objects (not owned, managed by LVGL)
    lv_obj_t* m_screen = nullptr;
    lv_obj_t* m_app_layout = nullptr;

    // Overlay panels (for lifecycle management)
    struct OverlayPanels {
        lv_obj_t* motion = nullptr;
        lv_obj_t* nozzle_temp = nullptr;
        lv_obj_t* bed_temp = nullptr;
        lv_obj_t* extrusion = nullptr;
        lv_obj_t* print_status = nullptr;
        lv_obj_t* ams = nullptr;
        lv_obj_t* bed_mesh = nullptr;
    } m_overlay_panels;

    // NOTE: Print start collector and observers are kept in main.cpp
    // until the observer pattern is refactored to support capturing lambdas.

    // Timing for auto-screenshot and timeout
    uint32_t m_screenshot_time = 0;
    bool m_screenshot_taken = false;
    uint32_t m_start_time = 0;
    uint32_t m_last_timeout_check = 0;
    uint32_t m_timeout_check_interval = 2000;

    // Benchmark mode
    bool m_benchmark_mode = false;
    uint32_t m_benchmark_frame_count = 0;
    uint32_t m_benchmark_start_time = 0;
    uint32_t m_benchmark_last_report = 0;

    // State
    bool m_running = false;
    bool m_wizard_active = false;
    bool m_shutdown_complete = false;
    bool m_splash_signaled = false;
};
