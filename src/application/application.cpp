// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file application.cpp
 * @brief Application lifecycle orchestrator - startup, main loop, and shutdown coordination
 *
 * @pattern Singleton orchestrator with ordered dependency initialization/teardown
 * @threading Main thread only; shutdown guards against double-call
 * @gotchas m_shutdown_complete prevents destructor re-entry
 *
 * @see display_manager.cpp, moonraker_manager.cpp
 */

#include "application.h"

#include "ui_update_queue.h"

#include "asset_manager.h"
#include "config.h"
#include "display_manager.h"
#include "hardware_validator.h"
#include "moonraker_manager.h"
#include "panel_factory.h"
#include "print_history_manager.h"
#include "screenshot.h"
#include "static_panel_registry.h"
#include "static_subject_registry.h"
#include "streaming_policy.h"
#include "subject_initializer.h"
#include "temperature_history_manager.h"

// UI headers
#include "ui_bed_mesh.h"
#include "ui_card.h"
#include "ui_component_header_bar.h"
#include "ui_dialog.h"
#include "ui_emergency_stop.h"
#include "ui_error_reporting.h"
#include "ui_fan_control_overlay.h"
#include "ui_gcode_viewer.h"
#include "ui_gradient_canvas.h"
#include "ui_icon.h"
#include "ui_icon_loader.h"
#include "ui_keyboard.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_panel_ams.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_panel_extrusion.h"
#include "ui_panel_gcode_test.h"
#include "ui_panel_glyphs.h"
#include "ui_panel_history_dashboard.h"
#include "ui_panel_home.h"
#include "ui_panel_input_shaper.h"
#include "ui_panel_memory_stats.h"
#include "ui_panel_motion.h"
#include "ui_panel_print_select.h"
#include "ui_panel_print_status.h"
#include "ui_panel_screws_tilt.h"
#include "ui_panel_settings.h"
#include "ui_panel_spoolman.h"
#include "ui_panel_step_test.h"
#include "ui_panel_temp_control.h"
#include "ui_panel_test.h"
#include "ui_severity_card.h"
#include "ui_status_bar.h"
#include "ui_switch.h"
#include "ui_temp_display.h"
#include "ui_theme.h"
#include "ui_toast.h"
#include "ui_utils.h"
#include "ui_wizard.h"
#include "ui_wizard_ams_identify.h"
#include "ui_wizard_wifi.h"

#include "settings_manager.h"
#include "wifi_manager.h"

// Backend headers
#include "abort_manager.h"
#include "action_prompt_manager.h"
#include "action_prompt_modal.h"
#include "app_globals.h"
#include "async_helpers.h"
#include "filament_sensor_manager.h"
#include "gcode_file_modifier.h"
#include "hv/hlog.h" // libhv logging - sync level with spdlog
#include "logging_init.h"
#include "lvgl/src/xml/lv_xml.h"
#include "lvgl_log_handler.h"
#include "memory_monitor.h"
#include "memory_profiling.h"
#include "memory_utils.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "plugin_manager.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "splash_screen.h"
#include "standard_macros.h"
#include "tips_manager.h"
#include "xml_registration.h"

#include <spdlog/spdlog.h>

#ifdef HELIX_DISPLAY_SDL
#include <SDL.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <signal.h>
#include <tuple>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// External globals for logging (defined in cli_args.cpp, populated by parse_cli_args)
extern std::string g_log_dest_cli;
extern std::string g_log_file_cli;

namespace {

/**
 * @brief Recursively invalidate all widgets in the tree
 *
 * With LV_DISPLAY_RENDER_MODE_PARTIAL, lv_obj_invalidate() on a parent may not
 * propagate to all descendants. This ensures every widget's area is explicitly
 * marked dirty for the initial framebuffer paint.
 */
void invalidate_all_recursive(lv_obj_t* obj) {
    if (!obj)
        return;
    lv_obj_invalidate(obj);
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        invalidate_all_recursive(lv_obj_get_child(obj, i));
    }
}

} // namespace

Application::Application() = default;

Application::~Application() {
    shutdown();
}

int Application::run(int argc, char** argv) {
    // Initialize minimal logging first so early log calls don't crash
    helix::logging::init_early();

    // Set libhv log level to WARN immediately - before ANY libhv usage
    // libhv's DEFAULT_LOG_LEVEL is INFO, which causes unwanted output on first start
    hlog_set_level(LOG_LEVEL_WARN);

    spdlog::info("[Application] Starting HelixScreen...");

    // Store argv early for restart capability
    app_store_argv(argc, argv);

    // Ensure we're running from the project root
    ensure_project_root_cwd();

    // Phase 1: Parse command line args
    if (!parse_args(argc, argv)) {
        return 0; // Help shown or parse error
    }

    // Phase 2: Initialize config system
    if (!init_config()) {
        return 1;
    }

    // Phase 3: Initialize logging
    if (!init_logging()) {
        return 1;
    }

    spdlog::info("[Application] ========================");
    spdlog::debug("[Application] Target: {}x{}", m_screen_width, m_screen_height);
    spdlog::debug("[Application] DPI: {}{}", (m_args.dpi > 0 ? m_args.dpi : LV_DPI_DEF),
                  (m_args.dpi > 0 ? " (custom)" : " (default)"));
    spdlog::debug("[Application] Initial Panel: {}", m_args.initial_panel);

    // Cleanup stale temp files from G-code modifications
    size_t cleaned = helix::gcode::GCodeFileModifier::cleanup_temp_files();
    if (cleaned > 0) {
        spdlog::info("[Application] Cleaned up {} stale G-code temp file(s)", cleaned);
    }

    // Phase 4: Initialize display
    if (!init_display()) {
        return 1;
    }

    // Phase 5: Register fonts and images (fonts needed for globals.xml parsing)
    if (!init_assets()) {
        shutdown();
        return 1;
    }

    // Phase 6: Initialize theme
    if (!init_theme()) {
        shutdown();
        return 1;
    }

    // Phase 7: Register widgets
    if (!register_widgets()) {
        shutdown();
        return 1;
    }

    // Phase 8: Register XML components
    if (!register_xml_components()) {
        shutdown();
        return 1;
    }

    // Phase 9a: Initialize core subjects and state (PrinterState, AmsState)
    // Must happen before Moonraker init because API creation needs PrinterState
    if (!init_core_subjects()) {
        shutdown();
        return 1;
    }

    // Phase 9b: Initialize Moonraker (creates client + API)
    // Now works because PrinterState exists from phase 9a
    if (!init_moonraker()) {
        shutdown();
        return 1;
    }

    // Phase 9c: Initialize panel subjects with API injection
    // Panels receive API at construction - no deferred set_api() needed
    if (!init_panel_subjects()) {
        shutdown();
        return 1;
    }

    // Phase 10: Create UI and wire panels
    if (!init_ui()) {
        shutdown();
        return 1;
    }

    // Phase 12: Run wizard if needed
    if (run_wizard()) {
        // Wizard is active - it handles its own flow
        m_wizard_active = true;
        set_wizard_active(true);
    }

    // Phase 13: Create overlay panels (if not in wizard)
    if (!m_wizard_active) {
        create_overlays();
    }

    // Phase 14: Initialize and load plugins
    // Must be after UI panels exist (injection points are registered by panels)
    if (!init_plugins()) {
        spdlog::warn("[Application] Plugin initialization had errors (non-fatal)");
    }

    // Phase 14b: Check WiFi availability if expected
    check_wifi_availability();

    // Phase 15: Connect to printer
    if (!connect_moonraker()) {
        // Non-fatal - app can still run without connection
        spdlog::warn("[Application] Running without printer connection");
    }

    // Phase 16: Start memory monitoring (logs at TRACE level, -vvv)
    helix::MemoryMonitor::instance().start(5000);

    // Phase 16b: Force full screen refresh
    // On framebuffer displays with PARTIAL render mode, some widgets may not paint
    // on the first frame. Schedule a deferred refresh after the first few frames
    // to ensure all widgets are fully rendered.
    lv_obj_update_layout(m_screen);
    invalidate_all_recursive(m_screen);
    lv_refr_now(nullptr);

    // Deferred refresh: Some widgets (nav icons, printer image) may not have their
    // content fully set until after the first frame. Schedule a second refresh.
    static auto deferred_refresh_cb = [](lv_timer_t* timer) {
        lv_obj_t* screen = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
        if (screen) {
            lv_obj_update_layout(screen);
            invalidate_all_recursive(screen);
            lv_refr_now(nullptr);
        }
        lv_timer_delete(timer);
    };
    lv_timer_create(deferred_refresh_cb, 100, m_screen); // 100ms delay

    // Phase 17: Main loop
    helix::MemoryMonitor::log_now("before_main_loop");
    int result = main_loop();

    // Phase 18: Shutdown
    shutdown();

    return result;
}

void Application::ensure_project_root_cwd() {
    char exe_path[PATH_MAX];

#ifdef __APPLE__
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return;
    }
    char resolved[PATH_MAX];
    if (realpath(exe_path, resolved)) {
        strncpy(exe_path, resolved, PATH_MAX - 1);
        exe_path[PATH_MAX - 1] = '\0';
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return;
    }
    exe_path[len] = '\0';
#else
    return;
#endif

    char* last_slash = strrchr(exe_path, '/');
    if (!last_slash)
        return;
    *last_slash = '\0';

    size_t dir_len = strlen(exe_path);
    const char* suffix = "/build/bin";
    size_t suffix_len = strlen(suffix);

    if (dir_len >= suffix_len && strcmp(exe_path + dir_len - suffix_len, suffix) == 0) {
        exe_path[dir_len - suffix_len] = '\0';
        if (chdir(exe_path) == 0) {
            spdlog::debug("[Application] Changed working directory to: {}", exe_path);
        }
    }
}

void Application::signal_splash_exit() {
    if (m_splash_signaled) {
        return; // Already signaled
    }
    m_splash_signaled = true;

    RuntimeConfig* runtime_config = get_runtime_config();
    if (runtime_config->splash_pid <= 0) {
        return; // No splash to signal
    }

    spdlog::info("[Application] UI ready, signaling splash process (PID {}) to exit...",
                 runtime_config->splash_pid);

    if (kill(runtime_config->splash_pid, SIGUSR1) == 0) {
        // Wait for splash to exit. Note: we check /proc/<pid>/status for zombie
        // state because kill(pid, 0) returns 0 for zombies. The splash is reaped
        // by watchdog, so we just need to know it's done (zombie = exited).
        int wait_attempts = 50;
        char proc_path[64];
        snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", runtime_config->splash_pid);

        while (wait_attempts-- > 0) {
            // First check if process exists at all
            if (kill(runtime_config->splash_pid, 0) != 0) {
                break; // Process gone
            }

            // Check if it's a zombie (exited but not reaped)
            FILE* f = fopen(proc_path, "r");
            if (f) {
                char line[256];
                bool is_zombie = false;
                while (fgets(line, sizeof(line), f)) {
                    if (strncmp(line, "State:", 6) == 0) {
                        is_zombie = (strchr(line, 'Z') != nullptr);
                        break;
                    }
                }
                fclose(f);
                if (is_zombie) {
                    spdlog::debug("[Application] Splash process exited (zombie, waiting for reap)");
                    break;
                }
            }

            usleep(20000);
        }
        if (wait_attempts <= 0) {
            spdlog::warn("[Application] Splash process did not exit in time");
        } else {
            spdlog::info("[Application] Splash process exited");
        }
    }
    runtime_config->splash_pid = 0;
}

bool Application::parse_args(int argc, char** argv) {
    // Parse CLI args first
    if (!helix::parse_cli_args(argc, argv, m_args, m_screen_width, m_screen_height)) {
        return false;
    }

    // Auto-configure mock state based on requested panel (after parsing args)
    auto_configure_mock_state();

    // Check HELIX_AUTO_QUIT_MS environment variable
    if (m_args.timeout_sec == 0) {
        const char* auto_quit_env = std::getenv("HELIX_AUTO_QUIT_MS");
        if (auto_quit_env != nullptr) {
            char* endptr;
            long val = strtol(auto_quit_env, &endptr, 10);
            if (*endptr == '\0' && val >= 100 && val <= 3600000) {
                m_args.timeout_sec = static_cast<int>((val + 999) / 1000);
            }
        }
    }

    // Check HELIX_AUTO_SCREENSHOT
    const char* auto_screenshot_env = std::getenv("HELIX_AUTO_SCREENSHOT");
    if (auto_screenshot_env != nullptr && strcmp(auto_screenshot_env, "1") == 0) {
        m_args.screenshot_enabled = true;
    }

    // Check HELIX_AMS_GATES
    const char* ams_gates_env = std::getenv("HELIX_AMS_GATES");
    if (ams_gates_env != nullptr) {
        char* endptr;
        long val = strtol(ams_gates_env, &endptr, 10);
        if (*endptr == '\0' && val >= 1 && val <= 16) {
            get_runtime_config()->mock_ams_gate_count = static_cast<int>(val);
        }
    }

    // Check benchmark mode
    m_benchmark_mode = (std::getenv("HELIX_BENCHMARK") != nullptr);
    if (m_benchmark_mode) {
        spdlog::info("[Application] Benchmark mode enabled");
    }

    return true;
}

void Application::auto_configure_mock_state() {
    RuntimeConfig* config = get_runtime_config();

    if (config->test_mode && !config->use_real_moonraker) {
        if (m_args.overlays.print_status) {
            config->mock_auto_start_print = true;
            config->gcode_test_file = RuntimeConfig::get_default_test_file_path();
            spdlog::info("[Auto] Mock will simulate active print for print-status panel");
        }

        if (m_args.initial_panel == UI_PANEL_PRINT_SELECT && !config->select_file) {
            config->select_file = RuntimeConfig::DEFAULT_TEST_FILE;
            spdlog::info("[Auto] Auto-selecting '{}' for print-select panel",
                         RuntimeConfig::DEFAULT_TEST_FILE);
        }

        if (m_args.overlays.history_dashboard) {
            config->mock_auto_history = true;
            spdlog::info("[Auto] Mock will generate history data for history panel");
        }
    }
}

bool Application::init_config() {
    m_config = Config::get_instance();

    // Use separate config file for test mode to avoid conflicts with real printer settings
    const char* config_path = get_runtime_config()->test_mode ? RuntimeConfig::TEST_CONFIG_PATH
                                                              : RuntimeConfig::PROD_CONFIG_PATH;
    spdlog::info("[Application] Using config: {}", config_path);
    m_config->init(config_path);

    // Initialize streaming policy from config (auto-detects thresholds from RAM)
    helix::StreamingPolicy::instance().load_from_config();

    return true;
}

bool Application::init_logging() {
    helix::logging::LogConfig log_config;

    // Determine log level: CLI verbosity takes precedence, then config file
    if (m_args.verbosity > 0) {
        // CLI -v flags override config
        switch (m_args.verbosity) {
        case 1:
            log_config.level = spdlog::level::info;
            break;
        case 2:
            log_config.level = spdlog::level::debug;
            break;
        default:
            log_config.level = spdlog::level::trace;
            break;
        }
    } else {
        // No CLI verbosity - check config file
        std::string level_str = m_config->get<std::string>("/log_level", "warn");
        if (level_str == "trace") {
            log_config.level = spdlog::level::trace;
        } else if (level_str == "debug") {
            log_config.level = spdlog::level::debug;
        } else if (level_str == "info") {
            log_config.level = spdlog::level::info;
        } else {
            log_config.level = spdlog::level::warn; // default
        }
    }

    std::string log_dest_str = g_log_dest_cli;
    if (log_dest_str.empty()) {
        log_dest_str = m_config->get<std::string>("/log_dest", "auto");
    }
    log_config.target = helix::logging::parse_log_target(log_dest_str);

    log_config.file_path = g_log_file_cli;
    if (log_config.file_path.empty()) {
        log_config.file_path = m_config->get<std::string>("/log_path", "");
    }

    helix::logging::init(log_config);

    // Set libhv log level from config file ONLY (CLI -v flags don't affect libhv)
    // libhv levels: VERBOSE(0) < DEBUG < INFO < WARN < ERROR < FATAL < SILENT
    std::string hv_level_str = m_config->get<std::string>("/log_level", "warn");
    int hv_level = LOG_LEVEL_WARN;
    if (hv_level_str == "trace" || hv_level_str == "debug") {
        hv_level = LOG_LEVEL_DEBUG; // Cap at DEBUG; libhv VERBOSE is too noisy
    } else if (hv_level_str == "info") {
        hv_level = LOG_LEVEL_INFO;
    }
    hlog_set_level(hv_level);

    return true;
}

bool Application::init_display() {
#ifdef HELIX_DISPLAY_SDL
    // Set window position environment variables
    if (m_args.display_num >= 0) {
        char display_str[32];
        snprintf(display_str, sizeof(display_str), "%d", m_args.display_num);
        setenv("HELIX_SDL_DISPLAY", display_str, 1);
    }
    if (m_args.x_pos >= 0 && m_args.y_pos >= 0) {
        char x_str[32], y_str[32];
        snprintf(x_str, sizeof(x_str), "%d", m_args.x_pos);
        snprintf(y_str, sizeof(y_str), "%d", m_args.y_pos);
        setenv("HELIX_SDL_XPOS", x_str, 1);
        setenv("HELIX_SDL_YPOS", y_str, 1);
    }
#endif

    m_display = std::make_unique<DisplayManager>();
    DisplayManager::Config config;
    config.width = m_screen_width;
    config.height = m_screen_height;

    // Get scroll config from helixconfig.json
    config.scroll_throw = m_config->get<int>("/input/scroll_throw", 25);
    config.scroll_limit = m_config->get<int>("/input/scroll_limit", 5);

    if (!m_display->init(config)) {
        spdlog::error("[Application] Display initialization failed");
        return false;
    }

    // Register LVGL log handler AFTER lv_init() (called inside display->init())
    // Must be after lv_init() because it resets global state and clears callbacks
    helix::logging::register_lvgl_log_handler();

    // Apply custom DPI if specified
    if (m_args.dpi > 0) {
        lv_display_set_dpi(m_display->display(), m_args.dpi);
    }

    // Get active screen
    m_screen = lv_screen_active();

    // Set window icon
    ui_set_window_icon(m_display->display());

    // Initialize resize handler
    ui_resize_handler_init(m_screen);

    // Initialize tips manager
    TipsManager* tips_mgr = TipsManager::get_instance();
    if (!tips_mgr->init("config/printing_tips.json")) {
        spdlog::warn("[Application] Failed to initialize tips manager");
    }

    spdlog::debug("[Application] Display initialized");
    helix::MemoryMonitor::log_now("after_display_init");
    return true;
}

bool Application::init_theme() {
    // Determine theme mode
    bool dark_mode;
    if (m_args.dark_mode_cli >= 0) {
        dark_mode = (m_args.dark_mode_cli == 1);
    } else {
        dark_mode = m_config->get<bool>("/dark_mode", true);
    }

    // Register globals.xml first (required for theme constants)
    // Note: fonts must be registered before this (done in init_assets phase)
    lv_xml_register_component_from_file("A:ui_xml/globals.xml");

    // Initialize theme
    ui_theme_init(m_display->display(), dark_mode);

    // Apply background color to screen
    ui_theme_apply_bg_color(m_screen, "app_bg_color", LV_PART_MAIN);

    // Show splash screen if not skipped
    if (!get_runtime_config()->should_skip_splash()) {
        helix::show_splash_screen(m_screen_width, m_screen_height);
    }

    spdlog::debug("[Application] Theme initialized (dark={})", dark_mode);
    return true;
}

bool Application::init_assets() {
    AssetManager::register_all();
    spdlog::debug("[Application] Assets registered");
    helix::MemoryMonitor::log_now("after_fonts_loaded");
    return true;
}

bool Application::register_widgets() {
    ui_icon_register_widget();
    ui_switch_register();
    ui_card_register();
    ui_temp_display_init();
    ui_severity_card_register();
    ui_dialog_register();
    ui_bed_mesh_register();
    ui_gcode_viewer_register();
    ui_gradient_canvas_register();

    // Initialize component systems
    ui_component_header_bar_init();

    // Small delay to stabilize display
    DisplayManager::delay(100);

    // Initialize memory profiling
    helix::MemoryProfiler::init(m_args.memory_report);

    // Log system memory info
    auto mem = helix::get_system_memory_info();
    spdlog::info("[Application] System memory: total={}MB, available={}MB", mem.total_kb / 1024,
                 mem.available_mb());

    spdlog::debug("[Application] Widgets registered");
    return true;
}

bool Application::register_xml_components() {
    helix::register_xml_components();
    spdlog::debug("[Application] XML components registered");
    return true;
}

bool Application::init_core_subjects() {
    m_subjects = std::make_unique<SubjectInitializer>();

    // Phase 1-3: Core subjects, PrinterState, AmsState
    // These must exist before MoonrakerManager::init() can create the API
    m_subjects->init_core_and_state();

    spdlog::debug("[Application] Core subjects initialized");
    helix::MemoryMonitor::log_now("after_core_subjects_init");
    return true;
}

bool Application::init_panel_subjects() {
    // Phase 4: Panel subjects with API injection
    // API is now available from MoonrakerManager
    m_subjects->init_panels(m_moonraker->api(), *get_runtime_config());

    // Phase 5-7: Observers and utility subjects
    m_subjects->init_post(*get_runtime_config());

    // Initialize EmergencyStopOverlay (moved from MoonrakerManager)
    // Must happen after both API and EmergencyStopOverlay::init_subjects()
    EmergencyStopOverlay::instance().init(get_printer_state(), m_moonraker->api());
    EmergencyStopOverlay::instance().create();
    EmergencyStopOverlay::instance().set_require_confirmation(
        SettingsManager::instance().get_estop_require_confirmation());

    // Initialize AbortManager for smart print cancellation
    // Must happen after both API and AbortManager::init_subjects()
    helix::AbortManager::instance().init(m_moonraker->api(), &get_printer_state());

    // Register status bar callbacks
    ui_status_bar_register_callbacks();
    ui_panel_screws_tilt_register_callbacks();
    ui_panel_input_shaper_register_callbacks();

    // Create temperature history manager (collects temp samples from PrinterState subjects)
    m_temp_history_manager = std::make_unique<TemperatureHistoryManager>(get_printer_state());
    set_temperature_history_manager(m_temp_history_manager.get());
    spdlog::debug("[Application] TemperatureHistoryManager created");

    spdlog::debug("[Application] Panel subjects initialized");
    helix::MemoryMonitor::log_now("after_panel_subjects_init");
    return true;
}

bool Application::init_ui() {
    // Create entire UI from XML
    m_app_layout = static_cast<lv_obj_t*>(lv_xml_create(m_screen, "app_layout", NULL));
    if (!m_app_layout) {
        spdlog::error("[Application] Failed to create app_layout from XML");
        return false;
    }

    // Disable scrollbars on screen
    lv_obj_clear_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(m_screen, LV_SCROLLBAR_MODE_OFF);

    // Force layout calculation
    lv_obj_update_layout(m_screen);

    // Register app_layout with navigation
    ui_nav_set_app_layout(m_app_layout);

    // Initialize status bar
    ui_status_bar_init();

    // Initialize toast system
    ui_toast_init();

    // Initialize overlay backdrop
    ui_nav_init_overlay_backdrop(m_screen);

    // Find navbar and content area
    lv_obj_t* navbar = lv_obj_find_by_name(m_app_layout, "navbar");
    lv_obj_t* content_area = lv_obj_find_by_name(m_app_layout, "content_area");

    if (!navbar || !content_area) {
        spdlog::error("[Application] Failed to find navbar/content_area");
        return false;
    }

    // Wire navigation
    ui_nav_wire_events(navbar);

    // Find panel container
    lv_obj_t* panel_container = lv_obj_find_by_name(content_area, "panel_container");
    if (!panel_container) {
        spdlog::error("[Application] Failed to find panel_container");
        return false;
    }

    // Initialize panels
    m_panels = std::make_unique<PanelFactory>();
    if (!m_panels->find_panels(panel_container)) {
        return false;
    }
    m_panels->setup_panels(m_screen);

    // Create print status overlay
    if (!m_panels->create_print_status_overlay(m_screen)) {
        spdlog::error("[Application] Failed to create print status overlay");
        return false;
    }
    m_overlay_panels.print_status = m_panels->print_status_panel();

    // Initialize keypad
    m_panels->init_keypad(m_screen);

    spdlog::info("[Application] UI created successfully");
    helix::MemoryMonitor::log_now("after_ui_created");
    return true;
}

bool Application::init_moonraker() {
    m_moonraker = std::make_unique<MoonrakerManager>();
    if (!m_moonraker->init(*get_runtime_config(), m_config)) {
        spdlog::error("[Application] Moonraker initialization failed");
        return false;
    }

    // API is now injected at panel construction in init_panel_subjects()
    // No need for deferred inject_api() call

    // Register MoonrakerManager globally (for Advanced panel access to MacroModificationManager)
    set_moonraker_manager(m_moonraker.get());

    // Create print history manager (shared cache for history panels and file status indicators)
    m_history_manager =
        std::make_unique<PrintHistoryManager>(m_moonraker->api(), get_moonraker_client());
    set_print_history_manager(m_history_manager.get());
    spdlog::debug("[Application] PrintHistoryManager created");

    // Initialize macro modification manager (for PRINT_START wizard)
    m_moonraker->init_macro_analysis(m_config);

    // Initialize global keyboard
    ui_keyboard_init(m_screen);

    // Initialize memory stats overlay
    MemoryStatsOverlay::instance().init(m_screen, m_args.show_memory);

    spdlog::debug("[Application] Moonraker initialized");
    helix::MemoryMonitor::log_now("after_moonraker_init");
    return true;
}

bool Application::init_plugins() {
    spdlog::info("[Application] Initializing plugin system");

    m_plugin_manager = std::make_unique<helix::plugin::PluginManager>();

    // Set core services - API and client may be nullptr if mock mode
    m_plugin_manager->set_core_services(m_moonraker->api(), m_moonraker->client(),
                                        get_printer_state(), m_config);

    // Read enabled plugins from config
    auto enabled_plugins =
        m_config->get<std::vector<std::string>>("/plugins/enabled", std::vector<std::string>{});
    m_plugin_manager->set_enabled_plugins(enabled_plugins);
    spdlog::info("[Application] Enabled plugins from config: {}", enabled_plugins.size());

    // Discover plugins in the plugins directory
    if (!m_plugin_manager->discover_plugins("plugins")) {
        spdlog::error("[Application] Plugin discovery failed");
        return false;
    }

    // Load all enabled plugins
    bool all_loaded = m_plugin_manager->load_all();

    // Log any errors and show toast notification with action buttons
    auto errors = m_plugin_manager->get_load_errors();
    if (!errors.empty()) {
        spdlog::warn("[Application] {} plugin(s) failed to load", errors.size());
        for (const auto& err : errors) {
            spdlog::warn("[Application]   - {}: {}", err.plugin_id, err.message);
        }

        if (errors.size() == 1) {
            // Single failure: Show [Disable] button for quick action
            // Context struct to pass plugin_id and manager pointer to callback
            struct PluginDisableContext {
                helix::plugin::PluginManager* manager;
                std::string plugin_id;
            };
            auto* ctx = new PluginDisableContext{m_plugin_manager.get(), errors[0].plugin_id};

            char toast_msg[96];
            snprintf(toast_msg, sizeof(toast_msg), "\"%s\" failed to load",
                     errors[0].plugin_id.c_str());

            ToastManager::instance().show_with_action(
                ToastSeverity::WARNING, toast_msg, "Disable",
                [](void* user_data) {
                    auto* ctx = static_cast<PluginDisableContext*>(user_data);
                    if (ctx->manager && ctx->manager->disable_plugin(ctx->plugin_id)) {
                        ui_toast_show(ToastSeverity::SUCCESS, "Plugin disabled", 3000);
                    }
                    delete ctx;
                },
                ctx, 8000);
        } else {
            // Multiple failures: Show [Manage] button to open Settings > Plugins
            char toast_msg[64];
            snprintf(toast_msg, sizeof(toast_msg), "%zu plugins failed to load", errors.size());

            ToastManager::instance().show_with_action(
                ToastSeverity::WARNING, toast_msg, "Manage",
                [](void* /*user_data*/) {
                    ui_nav_set_active(UI_PANEL_SETTINGS);
                    get_global_settings_panel().handle_plugins_clicked();
                },
                nullptr, 8000);
        }
    }

    auto loaded = m_plugin_manager->get_loaded_plugins();
    spdlog::info("[Application] {} plugin(s) loaded successfully", loaded.size());

    helix::MemoryMonitor::log_now("after_plugins_loaded");
    return all_loaded;
}

bool Application::run_wizard() {
    bool wizard_required = (m_args.force_wizard || m_config->is_wizard_required()) &&
                           !m_args.overlays.step_test && !m_args.overlays.test_panel &&
                           !m_args.overlays.keypad && !m_args.overlays.keyboard &&
                           !m_args.overlays.gcode_test && !m_args.overlays.wizard_ams_identify &&
                           !m_args.panel_requested;

    if (!wizard_required) {
        return false;
    }

    spdlog::info("[Application] Starting first-run wizard");

    ui_wizard_register_event_callbacks();
    ui_wizard_container_register_responsive_constants();

    lv_obj_t* wizard = ui_wizard_create(m_screen);
    if (!wizard) {
        spdlog::error("[Application] Failed to create wizard");
        return false;
    }

    int initial_step = (m_args.wizard_step >= 1) ? m_args.wizard_step : 1;
    ui_wizard_navigate_to_step(initial_step);

    // Move keyboard above wizard
    lv_obj_t* keyboard = ui_keyboard_get_instance();
    if (keyboard) {
        lv_obj_move_foreground(keyboard);
    }

    return true;
}

void Application::create_overlays() {
    // Navigate to initial panel
    if (m_args.initial_panel >= 0) {
        ui_nav_set_active(static_cast<ui_panel_id_t>(m_args.initial_panel));
    }

    // Create requested overlay panels
    if (m_args.overlays.motion) {
        auto& motion = get_global_motion_panel();

        // Initialize subjects and callbacks if not already done
        if (!motion.are_subjects_initialized()) {
            motion.init_subjects();
        }
        motion.register_callbacks();

        // Create overlay UI
        auto* p = motion.create(m_screen);
        if (p) {
            m_overlay_panels.motion = p;
            NavigationManager::instance().register_overlay_instance(p, &motion);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.nozzle_temp) {
        if (auto* p = create_overlay_panel(m_screen, "nozzle_temp_panel", "nozzle temp")) {
            m_overlay_panels.nozzle_temp = p;
            m_subjects->temp_control_panel()->setup_nozzle_panel(p, m_screen);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.bed_temp) {
        if (auto* p = create_overlay_panel(m_screen, "bed_temp_panel", "bed temp")) {
            m_overlay_panels.bed_temp = p;
            m_subjects->temp_control_panel()->setup_bed_panel(p, m_screen);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.extrusion) {
        auto& extrusion = get_global_extrusion_panel();

        // Initialize subjects and callbacks if not already done
        if (!extrusion.are_subjects_initialized()) {
            extrusion.init_subjects();
        }
        extrusion.register_callbacks();

        // Create overlay UI
        auto* p = extrusion.create(m_screen);
        if (p) {
            m_overlay_panels.extrusion = p;
            NavigationManager::instance().register_overlay_instance(p, &extrusion);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.fan) {
        auto& overlay = get_fan_control_overlay();

        // Initialize subjects and callbacks if not already done
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();

        // Pass API reference for fan commands
        overlay.set_api(get_moonraker_api());

        // Create overlay UI
        auto* p = overlay.create(m_screen);
        if (p) {
            NavigationManager::instance().register_overlay_instance(p, &overlay);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.print_status && m_overlay_panels.print_status) {
        ui_nav_push_overlay(m_overlay_panels.print_status);
    }

    if (m_args.overlays.bed_mesh) {
        auto& overlay = get_global_bed_mesh_panel();

        // Initialize subjects and callbacks if not already done
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();

        // Create overlay UI
        auto* p = overlay.create(m_screen);
        if (p) {
            m_overlay_panels.bed_mesh = p;
            NavigationManager::instance().register_overlay_instance(p, &overlay);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.zoffset) {
        auto& overlay = get_global_zoffset_cal_panel();
        // init_subjects already called by SubjectInitializer
        overlay.set_client(m_moonraker->client());
        if (overlay.create(m_screen)) {
            overlay.show();
        }
    }

    if (m_args.overlays.pid) {
        auto& overlay = get_global_pid_cal_panel();
        // init_subjects already called by SubjectInitializer
        overlay.set_client(m_moonraker->client());
        if (overlay.create(m_screen)) {
            overlay.show();
        }
    }

    if (m_args.overlays.screws_tilt) {
        auto& overlay = get_global_screws_tilt_panel();
        // init_subjects already called by SubjectInitializer
        overlay.set_client(m_moonraker->client(), m_moonraker->api());
        if (overlay.create(m_screen)) {
            overlay.show();
        }
    }

    if (m_args.overlays.input_shaper) {
        auto& panel = get_global_input_shaper_panel();
        panel.set_api(m_moonraker->client(), m_moonraker->api());
        if (panel.create(m_screen)) {
            panel.show();
        }
    }

    if (m_args.overlays.history_dashboard) {
        auto& overlay = get_global_history_dashboard_panel();
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();
        auto* p = overlay.create(m_screen);
        if (p) {
            NavigationManager::instance().register_overlay_instance(p, &overlay);
            ui_nav_push_overlay(p);
        }
    }

    if (m_args.overlays.step_test) {
        if (auto* p = create_overlay_panel(m_screen, "step_progress_test", "step progress")) {
            get_global_step_test_panel().setup(p, m_screen);
        }
    }

    if (m_args.overlays.test_panel) {
        if (auto* p = create_overlay_panel(m_screen, "test_panel", "test")) {
            get_global_test_panel().setup(p, m_screen);
        }
    }

    if (m_args.overlays.gcode_test) {
        ui_panel_gcode_test_create(m_screen);
    }

    if (m_args.overlays.glyphs) {
        ui_panel_glyphs_create(m_screen);
    }

    if (m_args.overlays.gradient_test) {
        create_overlay_panel(m_screen, "gradient_test_panel", "gradient test");
    }

    if (m_args.overlays.ams) {
        auto& ams_panel = get_global_ams_panel();
        if (!ams_panel.are_subjects_initialized()) {
            ams_panel.init_subjects();
        }
        lv_obj_t* panel_obj = ams_panel.get_panel();
        if (panel_obj) {
            ams_panel.on_activate();
            ui_nav_push_overlay(panel_obj);
        }
    }

    if (m_args.overlays.spoolman) {
        auto& spoolman = get_global_spoolman_panel();
        if (!spoolman.are_subjects_initialized()) {
            spoolman.init_subjects();
        }
        spoolman.register_callbacks();
        lv_obj_t* panel_obj = spoolman.create(m_screen);
        if (panel_obj) {
            NavigationManager::instance().register_overlay_instance(panel_obj, &spoolman);
            ui_nav_push_overlay(panel_obj);
        }
    }

    if (m_args.overlays.wizard_ams_identify) {
        auto* step = get_wizard_ams_identify_step();
        step->init_subjects();
        lv_obj_t* panel_obj = step->create(m_screen);
        if (panel_obj) {
            ui_nav_push_overlay(panel_obj);
        }
    }

    // Handle --select-file flag
    RuntimeConfig* runtime_config = get_runtime_config();
    if (runtime_config->select_file != nullptr) {
        ui_nav_set_active(UI_PANEL_PRINT_SELECT);
        auto* print_panel = get_print_select_panel(get_printer_state(), m_moonraker->api());
        if (print_panel) {
            print_panel->set_pending_file_selection(runtime_config->select_file);
        }
    }
}

bool Application::connect_moonraker() {
    // Determine if we should connect
    std::string saved_host = m_config->get<std::string>(m_config->df() + "moonraker_host", "");
    bool has_cli_url = !m_args.moonraker_url.empty();
    // In test mode, still respect wizard state - don't connect until wizard completes
    bool should_connect =
        has_cli_url || (get_runtime_config()->test_mode && !m_wizard_active) ||
        (!m_args.force_wizard && !m_config->is_wizard_required() && !saved_host.empty());

    if (!should_connect) {
        return true; // Not connecting is not an error
    }

    std::string moonraker_url;
    std::string http_base_url;

    if (has_cli_url) {
        moonraker_url = m_args.moonraker_url;
        std::string host_port = moonraker_url.substr(5);
        auto ws_pos = host_port.find("/websocket");
        if (ws_pos != std::string::npos) {
            host_port = host_port.substr(0, ws_pos);
        }
        http_base_url = "http://" + host_port;
    } else {
        moonraker_url =
            "ws://" + m_config->get<std::string>(m_config->df() + "moonraker_host") + ":" +
            std::to_string(m_config->get<int>(m_config->df() + "moonraker_port")) + "/websocket";
        http_base_url = "http://" + m_config->get<std::string>(m_config->df() + "moonraker_host") +
                        ":" + std::to_string(m_config->get<int>(m_config->df() + "moonraker_port"));
    }

    // Set up discovery callbacks
    MoonrakerClient* client = m_moonraker->client();
    MoonrakerAPI* api = m_moonraker->api();

    client->set_on_hardware_discovered([api, client](const PrinterCapabilities& caps) {
        ui_async_call(
            [](void* user_data) {
                auto* ctx = static_cast<
                    std::pair<PrinterCapabilities, std::pair<MoonrakerAPI*, MoonrakerClient*>>*>(
                    user_data);
                init_subsystems_from_capabilities(ctx->first, ctx->second.first,
                                                  ctx->second.second);
                delete ctx;
            },
            new std::pair<PrinterCapabilities, std::pair<MoonrakerAPI*, MoonrakerClient*>>(
                caps, {api, client}));
    });

    // Capture Application pointer for callback - used to check shutdown state and access plugin
    // manager
    Application* app = this;

    client->set_on_discovery_complete([api, client, app](const PrinterCapabilities& caps) {
        ui_async_call(
            [](void* user_data) {
                auto* ctx = static_cast<
                    std::tuple<PrinterCapabilities, std::pair<MoonrakerAPI*, MoonrakerClient*>,
                               Application*>*>(user_data);
                auto* api_ptr = std::get<1>(*ctx).first;
                auto* client_ptr = std::get<1>(*ctx).second;
                auto* app_ptr = std::get<2>(*ctx);

                // Safety check: if Application is shutting down, skip all processing
                // This prevents use-after-free if shutdown races with callback delivery
                if (app_ptr->m_shutdown_complete) {
                    delete ctx;
                    return;
                }

                get_printer_state().set_printer_capabilities(std::get<0>(*ctx));
                get_printer_state().init_fans(client_ptr->get_fans());
                get_printer_state().set_klipper_version(client_ptr->get_software_version());
                get_printer_state().set_moonraker_version(client_ptr->get_moonraker_version());

                // Hardware validation: check config expectations vs discovered hardware
                HardwareValidator validator;
                auto validation_result =
                    validator.validate(Config::get_instance(), client_ptr, std::get<0>(*ctx));
                get_printer_state().set_hardware_validation_result(validation_result);

                if (validation_result.has_issues()) {
                    validator.notify_user(validation_result);
                }

                // Save session snapshot for next comparison (even if no issues)
                validator.save_session_snapshot(Config::get_instance(), client_ptr,
                                                std::get<0>(*ctx));

                // Detect helix_print plugin during discovery (not UI-initiated)
                // This ensures plugin status is known early for UI gating
                api_ptr->check_helix_plugin(
                    [](bool available) {
                        get_printer_state().set_helix_plugin_installed(available);
                    },
                    [](const MoonrakerError&) {
                        // Silently treat errors as "plugin not installed"
                        get_printer_state().set_helix_plugin_installed(false);
                    });

                // Notify plugins that Moonraker is connected
                if (app_ptr->m_plugin_manager) {
                    app_ptr->m_plugin_manager->on_moonraker_connected();
                }

                delete ctx;
            },
            new std::tuple<PrinterCapabilities, std::pair<MoonrakerAPI*, MoonrakerClient*>,
                           Application*>(caps, {api, client}, app));
    });

    // Set HTTP base URL for API
    api->set_http_base_url(http_base_url);

    // Connect
    spdlog::debug("[Application] Connecting to {}", moonraker_url);
    int result = m_moonraker->connect(moonraker_url, http_base_url);

    if (result != 0) {
        spdlog::error("[Application] Failed to initiate connection (code {})", result);
        return false;
    }

    // Start auto-discovery (client handles this internally after connect)

    // Initialize print start collector (monitors PRINT_START macro progress)
    m_moonraker->init_print_start_collector();

    // Initialize action prompt system (Klipper action:prompt protocol)
    init_action_prompt();

    return true;
}

lv_obj_t* Application::create_overlay_panel(lv_obj_t* screen, const char* component_name,
                                            const char* display_name) {
    spdlog::debug("[Application] Opening {} overlay", display_name);
    lv_obj_t* panel = static_cast<lv_obj_t*>(lv_xml_create(screen, component_name, nullptr));
    if (!panel) {
        spdlog::error("[Application] Failed to create {} overlay from '{}'", display_name,
                      component_name);
    }
    return panel;
}

void Application::init_action_prompt() {
    MoonrakerClient* client = m_moonraker->client();
    MoonrakerAPI* api = m_moonraker->api();

    if (!client) {
        spdlog::warn("[Application] Cannot init action prompt - no client");
        return;
    }

    // Create ActionPromptManager
    m_action_prompt_manager = std::make_unique<helix::ActionPromptManager>();

    // Create ActionPromptModal
    m_action_prompt_modal = std::make_unique<helix::ui::ActionPromptModal>();

    // Set up gcode callback to send button commands via API
    if (api) {
        m_action_prompt_modal->set_gcode_callback([api](const std::string& gcode) {
            spdlog::info("[ActionPrompt] Sending gcode: {}", gcode);
            api->execute_gcode(
                gcode, []() { spdlog::debug("[ActionPrompt] Gcode executed successfully"); },
                [gcode](const MoonrakerError& err) {
                    spdlog::error("[ActionPrompt] Gcode execution failed: {}", err.message);
                });
        });
    }

    // Wire on_show callback to display modal (uses ui_async_call for thread safety)
    m_action_prompt_manager->set_on_show([this](const helix::PromptData& data) {
        spdlog::info("[ActionPrompt] Showing prompt: {}", data.title);
        // WebSocket callbacks run on background thread - must use helix::async::invoke
        helix::async::invoke([this, data]() {
            if (m_action_prompt_modal && m_screen) {
                m_action_prompt_modal->show_prompt(m_screen, data);
            }
        });
    });

    // Wire on_close callback to hide modal
    m_action_prompt_manager->set_on_close([this]() {
        spdlog::info("[ActionPrompt] Closing prompt");
        helix::async::invoke([this]() {
            if (m_action_prompt_modal) {
                m_action_prompt_modal->hide();
            }
        });
    });

    // Wire on_notify callback for standalone notifications (action:notify)
    m_action_prompt_manager->set_on_notify([](const std::string& message) {
        spdlog::info("[ActionPrompt] Notification: {}", message);
        helix::async::invoke([message]() {
            ToastManager::instance().show(ToastSeverity::INFO, message.c_str(), 5000);
        });
    });

    // Register for notify_gcode_response messages from Moonraker
    // All lines from G-code console output come through this notification
    client->register_method_callback(
        "notify_gcode_response", "action_prompt_manager", [this](const nlohmann::json& msg) {
            // notify_gcode_response has params: [["line1", "line2", ...]]
            if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
                return;
            }

            const auto& params = msg["params"];
            // params can be an array of strings, or an array containing an array of strings
            // Handle both formats
            if (params[0].is_array()) {
                for (const auto& line : params[0]) {
                    if (line.is_string()) {
                        m_action_prompt_manager->process_line(line.get<std::string>());
                    }
                }
            } else if (params[0].is_string()) {
                for (const auto& line : params) {
                    if (line.is_string()) {
                        m_action_prompt_manager->process_line(line.get<std::string>());
                    }
                }
            }
        });

    spdlog::info("[Application] Action prompt system initialized");
}

void Application::check_wifi_availability() {
    if (!m_config || !m_config->is_wifi_expected()) {
        return; // WiFi not expected, no need to check
    }

    auto wifi = get_wifi_manager();
    if (wifi && !wifi->has_hardware()) {
        NOTIFY_ERROR_MODAL("WiFi Unavailable", "WiFi was configured but hardware is not available. "
                                               "Check system configuration.");
    }
}

int Application::main_loop() {
    spdlog::info("[Application] Entering main loop");
    m_running = true;

    // Initialize timing
    m_start_time = DisplayManager::get_ticks();
    m_screenshot_time = m_start_time + (static_cast<uint32_t>(m_args.screenshot_delay_sec) * 1000U);
    m_last_timeout_check = m_start_time;
    m_timeout_check_interval = static_cast<uint32_t>(
        m_config->get<int>(m_config->df() + "moonraker_timeout_check_interval_ms", 2000));

    if (m_benchmark_mode) {
        m_benchmark_start_time = m_start_time;
        m_benchmark_last_report = m_start_time;
    }

    // Main event loop
    while (lv_display_get_next(NULL) && !app_quit_requested()) {
        handle_keyboard_shortcuts();

        // Auto-screenshot
        if (m_args.screenshot_enabled && !m_screenshot_taken &&
            DisplayManager::get_ticks() >= m_screenshot_time) {
            helix::save_screenshot();
            m_screenshot_taken = true;
        }

        // Auto-quit timeout
        if (m_args.timeout_sec > 0) {
            uint32_t elapsed = DisplayManager::get_ticks() - m_start_time;
            if (elapsed >= static_cast<uint32_t>(m_args.timeout_sec) * 1000U) {
                spdlog::info("[Application] Timeout reached ({} seconds)", m_args.timeout_sec);
                break;
            }
        }

        // Process timeouts
        check_timeouts();

        // Process Moonraker notifications
        process_notifications();

        // Check display sleep
        m_display->check_display_sleep();

        // Run LVGL tasks
        lv_timer_handler();
        fflush(stdout);

        // Signal splash to exit after first frame is rendered
        // This ensures our UI is visible before splash disappears
        signal_splash_exit();

        // Benchmark mode
        if (m_benchmark_mode) {
            lv_obj_invalidate(lv_screen_active());
            m_benchmark_frame_count++;
            uint32_t now = DisplayManager::get_ticks();
            if (now - m_benchmark_last_report >= 5000) {
                float elapsed_sec = (now - m_benchmark_last_report) / 1000.0f;
                float fps = m_benchmark_frame_count / elapsed_sec;
                spdlog::info("[Application] Benchmark FPS: {:.1f}", fps);
                m_benchmark_frame_count = 0;
                m_benchmark_last_report = now;
            }
        }

        DisplayManager::delay(5);
    }

    m_running = false;

    if (m_benchmark_mode) {
        uint32_t total_time = DisplayManager::get_ticks() - m_benchmark_start_time;
        spdlog::info("[Application] Benchmark total runtime: {:.1f}s", total_time / 1000.0f);
    }

    return 0;
}

void Application::handle_keyboard_shortcuts() {
#ifdef HELIX_DISPLAY_SDL
    SDL_Keymod modifiers = SDL_GetModState();
    const Uint8* keyboard_state = SDL_GetKeyboardState(NULL);

    // Cmd+Q / Win+Q to quit
    if ((modifiers & KMOD_GUI) && keyboard_state[SDL_SCANCODE_Q]) {
        spdlog::info("[Application] Cmd+Q/Win+Q pressed - exiting");
        app_request_quit();
    }

    // M key toggle for memory stats (with debounce)
    static bool m_key_was_pressed = false;
    bool m_key_pressed = keyboard_state[SDL_SCANCODE_M] != 0;
    if (m_key_pressed && !m_key_was_pressed) {
        MemoryStatsOverlay::instance().toggle();
    }
    m_key_was_pressed = m_key_pressed;

    // F key toggle for filament runout simulation (test mode only, with debounce)
    // Uses base class virtual method - no-op on real client, toggles on mock
    static bool f_key_was_pressed = false;
    bool f_key_pressed = keyboard_state[SDL_SCANCODE_F] != 0;
    if (f_key_pressed && !f_key_was_pressed) {
        if (m_moonraker && m_moonraker->client()) {
            spdlog::info("[Application] F key - toggling filament runout simulation");
            m_moonraker->client()->toggle_filament_runout_simulation();
        }
    }
    f_key_was_pressed = f_key_pressed;

    // P key to trigger test action prompt (test mode only, with debounce)
    static bool p_key_was_pressed = false;
    bool p_key_pressed = keyboard_state[SDL_SCANCODE_P] != 0;
    if (p_key_pressed && !p_key_was_pressed) {
        if (get_runtime_config()->is_test_mode() && m_action_prompt_manager) {
            spdlog::info("[Application] P key - triggering test action prompt");
            m_action_prompt_manager->trigger_test_prompt();
        }
    }
    p_key_was_pressed = p_key_pressed;

    // N key to trigger test action notification (test mode only, with debounce)
    static bool n_key_was_pressed = false;
    bool n_key_pressed = keyboard_state[SDL_SCANCODE_N] != 0;
    if (n_key_pressed && !n_key_was_pressed) {
        if (get_runtime_config()->is_test_mode() && m_action_prompt_manager) {
            spdlog::info("[Application] N key - triggering test action notification");
            m_action_prompt_manager->trigger_test_notify();
        }
    }
    n_key_was_pressed = n_key_pressed;
#endif
}

void Application::process_notifications() {
    if (m_moonraker) {
        m_moonraker->process_notifications();
    }
}

void Application::check_timeouts() {
    uint32_t current_time = DisplayManager::get_ticks();
    if (current_time - m_last_timeout_check >= m_timeout_check_interval) {
        if (m_moonraker) {
            m_moonraker->process_timeouts();
        }
        m_last_timeout_check = current_time;
    }
}

void Application::shutdown() {
    // Guard against multiple calls (destructor + explicit shutdown)
    if (m_shutdown_complete) {
        return;
    }
    m_shutdown_complete = true;

    // Stop memory monitor first
    helix::MemoryMonitor::instance().stop();

    spdlog::info("[Application] Shutting down...");

    // Clear app_globals references BEFORE destroying managers to prevent
    // destructors (e.g., PrintSelectPanel) from accessing destroyed objects
    set_moonraker_manager(nullptr);
    set_moonraker_api(nullptr);
    set_moonraker_client(nullptr);
    set_print_history_manager(nullptr);
    set_temperature_history_manager(nullptr);

    // Deactivate UI and clear navigation registries
    NavigationManager::instance().shutdown();

    // Unload plugins before destroying managers they depend on
    if (m_plugin_manager) {
        m_plugin_manager->unload_all();
        m_plugin_manager.reset();
    }

    // Reset managers in reverse order (MoonrakerManager handles print_start_collector cleanup)
    // History manager MUST be reset before moonraker (uses client for unregistration)
    m_history_manager.reset();
    m_temp_history_manager.reset();

    // Unregister action prompt callback before moonraker is destroyed
    if (m_moonraker && m_moonraker->client() && m_action_prompt_manager) {
        m_moonraker->client()->unregister_method_callback("notify_gcode_response",
                                                          "action_prompt_manager");
    }
    m_action_prompt_modal.reset();
    m_action_prompt_manager.reset();

    m_moonraker.reset();
    m_panels.reset();
    m_subjects.reset();

    // Destroy ALL static panel/overlay globals via self-registration pattern.
    // This runs BEFORE spdlog's registry is destroyed during static destruction,
    // ensuring panel destructors can safely log.
    StaticPanelRegistry::instance().destroy_all();

    // Deinitialize core singleton subjects (PrinterState, AmsState, SettingsManager, etc.)
    // This MUST happen AFTER panels are destroyed (panels have observers on these subjects)
    // but BEFORE lv_deinit() which will delete any remaining widgets
    StaticSubjectRegistry::instance().deinit_all();

    // Restore display backlight (guard for early exit paths like --help)
    if (m_display) {
        m_display->restore_display_on_shutdown();
    }

    // Shutdown display (calls lv_deinit)
    m_display.reset();

    spdlog::info("[Application] Shutdown complete");
}
