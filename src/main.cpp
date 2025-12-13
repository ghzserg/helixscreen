// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_bed_mesh.h"
#include "ui_card.h"
#include "ui_component_header_bar.h"
#include "ui_component_keypad.h"
#include "ui_dialog.h"
#include "ui_emergency_stop.h"
#include "ui_error_reporting.h"
#include "ui_fatal_error.h"
#include "ui_fonts.h"
#include "ui_gcode_viewer.h"
#include "ui_gradient_canvas.h"
#include "ui_icon.h"
#include "ui_icon_loader.h"
#include "ui_keyboard.h"
#include "ui_modal.h"
#include "ui_nav.h"
#include "ui_notification.h"
#include "ui_observer_guard.h"
#include "ui_panel_advanced.h"
#include "ui_panel_ams.h"
#include "ui_panel_bed_mesh.h"
#include "ui_panel_calibration_pid.h"
#include "ui_panel_calibration_zoffset.h"
#include "ui_panel_console.h"
#include "ui_panel_controls.h"
#include "ui_panel_extrusion.h"
#include "ui_panel_fan.h"
#include "ui_panel_filament.h"
#include "ui_panel_gcode_test.h"
#include "ui_panel_glyphs.h"
#include "ui_panel_history_dashboard.h"
#include "ui_panel_history_list.h"
#include "ui_panel_home.h"
#include "ui_panel_input_shaper.h"
#include "ui_panel_memory_stats.h"
#include "ui_panel_motion.h"
#include "ui_panel_notification_history.h"
#include "ui_panel_print_select.h"
#include "ui_panel_print_status.h"
#include "ui_panel_screws_tilt.h"
#include "ui_panel_settings.h"
#include "ui_panel_step_test.h"
#include "ui_panel_temp_control.h"
#include "ui_panel_test.h"
#include "ui_severity_card.h"
#include "ui_spinner.h"
#include "ui_status_bar.h"
#include "ui_switch.h"
#include "ui_temp_display.h"
#include "ui_text.h"
#include "ui_text_input.h"
#include "ui_theme.h"
#include "ui_timelapse_settings.h"
#include "ui_toast.h"
#include "ui_utils.h"
#include "ui_wizard.h"
#include "ui_wizard_wifi.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "app_globals.h"
#include "cli_args.h"
#include "config.h"
#include "display_backend.h"
#include "gcode_file_modifier.h"
#include "logging_init.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/libs/svg/lv_svg_decoder.h"
#include "lvgl/src/xml/lv_xml.h"
#include "memory_profiling.h"
#include "moonraker_api.h"
#include "moonraker_api_mock.h"
#include "moonraker_client.h"
#include "moonraker_client_mock.h"
#include "print_completion.h"
#include "print_history_data.h"
#include "print_start_collector.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "settings_manager.h"
#include "sound_manager.h"
#include "tips_manager.h"
#include "usb_backend_mock.h"
#include "usb_manager.h"
#include "xml_registration.h"

#include <spdlog/spdlog.h>

#ifdef HELIX_DISPLAY_SDL
#include <SDL.h>
#endif
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <queue>
#include <signal.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Portable timing functions (SDL-independent for embedded builds)
#ifdef HELIX_DISPLAY_SDL
// Use SDL timing when available (more precise on desktop)
inline uint32_t helix_get_ticks() {
    return SDL_GetTicks();
}
inline void helix_delay(uint32_t ms) {
    SDL_Delay(ms);
}
#else
// POSIX fallback for embedded Linux
#include <time.h>
inline uint32_t helix_get_ticks() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
inline void helix_delay(uint32_t ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}
#endif

// Forward declarations for panel global accessor functions
class HomePanel;
class ControlsPanel;
class MotionPanel;
class SettingsPanel;
class FilamentPanel;
class AdvancedPanel;
class PrintSelectPanel;
class PrintStatusPanel;
class ExtrusionPanel;
class BedMeshPanel;
class StepTestPanel;
class TestPanel;
class GlyphsPanel;
class GcodeTestPanel;

HomePanel& get_global_home_panel();
ControlsPanel& get_global_controls_panel();
MotionPanel& get_global_motion_panel();
SettingsPanel& get_global_settings_panel();
FilamentPanel& get_global_filament_panel();
AdvancedPanel& get_global_advanced_panel();
void init_global_advanced_panel(PrinterState& printer_state, MoonrakerAPI* api);
PrintSelectPanel* get_print_select_panel(PrinterState& printer_state, MoonrakerAPI* api);
PrintStatusPanel& get_global_print_status_panel();
AmsPanel& get_global_ams_panel();
ExtrusionPanel& get_global_extrusion_panel();
BedMeshPanel& get_global_bed_mesh_panel();
StepTestPanel& get_global_step_test_panel();
TestPanel& get_global_test_panel();
GlyphsPanel& get_global_glyphs_panel();
GcodeTestPanel* get_gcode_test_panel(PrinterState& printer_state, MoonrakerAPI* api);

// ============================================================================
// Overlay Creation Helper
// ============================================================================

/**
 * @brief Create an overlay panel from XML and optionally push to nav stack
 *
 * Handles the common pattern of:
 * 1. Debug log announcing the overlay creation
 * 2. Create the XML component
 * 3. Error log if creation fails
 *
 * @param screen Parent screen for the overlay
 * @param component_name XML component name (e.g., "bed_mesh_panel")
 * @param display_name Human-readable name for logging (e.g., "bed mesh")
 * @return Created object, or nullptr if creation failed
 */
static lv_obj_t* create_overlay_panel(lv_obj_t* screen, const char* component_name,
                                      const char* display_name) {
    spdlog::debug("[Overlay] Opening {} overlay as requested by command-line flag", display_name);
    lv_obj_t* panel = (lv_obj_t*)lv_xml_create(screen, component_name, nullptr);
    if (!panel) {
        spdlog::error("[Overlay] Failed to create {} overlay from XML component '{}'", display_name,
                      component_name);
    }
    return panel;
}

// ============================================================================

// Ensure we're running from the project root directory.
// If the executable is in build/bin/, change to the project root so relative paths work.
static void ensure_project_root_cwd() {
    char exe_path[PATH_MAX];

#ifdef __APPLE__
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return; // Failed to get path, assume CWD is correct
    }
    // Resolve symlinks
    char resolved[PATH_MAX];
    if (realpath(exe_path, resolved)) {
        strncpy(exe_path, resolved, PATH_MAX - 1);
        exe_path[PATH_MAX - 1] = '\0';
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return; // Failed to get path, assume CWD is correct
    }
    exe_path[len] = '\0';
#else
    return; // Unsupported platform, assume CWD is correct
#endif

    // Get directory containing executable
    char* last_slash = strrchr(exe_path, '/');
    if (!last_slash)
        return;
    *last_slash = '\0';

    // Check if we're in build/bin/ and go up two levels
    size_t dir_len = strlen(exe_path);
    const char* suffix = "/build/bin";
    size_t suffix_len = strlen(suffix);

    if (dir_len >= suffix_len && strcmp(exe_path + dir_len - suffix_len, suffix) == 0) {
        // Strip /build/bin to get project root
        exe_path[dir_len - suffix_len] = '\0';

        if (chdir(exe_path) == 0) {
            spdlog::debug("[Init] Changed working directory to: {}", exe_path);
        }
    }
}

// Display backend and LVGL display/input
static std::unique_ptr<DisplayBackend> g_display_backend;
static lv_display_t* display = nullptr;
static lv_indev_t* indev_mouse = nullptr;

// Screen dimensions (configurable via command line, default to small = 800x480)
static int SCREEN_WIDTH = UI_SCREEN_SMALL_W;
static int SCREEN_HEIGHT = UI_SCREEN_SMALL_H;

// Local instances (registered with app_globals via setters)
// Note: PrinterState is now a singleton accessed via get_printer_state()
// Using unique_ptr for RAII - raw pointers are passed to app_globals for access
static std::unique_ptr<MoonrakerClient> moonraker_client;
static std::unique_ptr<MoonrakerAPI> moonraker_api;
static std::unique_ptr<TempControlPanel> temp_control_panel;
static std::unique_ptr<UsbManager> usb_manager;

// Panels that need MoonrakerAPI - stored as pointers for deferred set_api() call
static PrintSelectPanel* print_select_panel = nullptr;
static PrintStatusPanel* print_status_panel = nullptr;
static MotionPanel* motion_panel = nullptr;
static ExtrusionPanel* extrusion_panel = nullptr;
static BedMeshPanel* bed_mesh_panel = nullptr;

// Runtime configuration
static RuntimeConfig g_runtime_config;

// Logging configuration (parsed before Config system is available)
// Note: These are referenced via extern in cli_args.cpp
std::string g_log_dest_cli; // CLI override for log destination
std::string g_log_file_cli; // CLI override for log file path

// Thread-safe queue for Moonraker notifications (cross-thread communication)
static std::queue<json> notification_queue;
static std::mutex notification_mutex;

// Overlay panel tracking for proper lifecycle management
struct OverlayPanels {
    lv_obj_t* motion = nullptr;
    lv_obj_t* nozzle_temp = nullptr;
    lv_obj_t* bed_temp = nullptr;
    lv_obj_t* extrusion = nullptr;
    lv_obj_t* print_status = nullptr;
    lv_obj_t* ams = nullptr;
} static overlay_panels;

// Print completion notification observer (implementation in print_completion.cpp)
static ObserverGuard print_completion_observer;

// PRINT_START progress collector (monitors G-code responses during print initialization)
static std::shared_ptr<PrintStartCollector> print_start_collector;
static ObserverGuard print_start_observer;
static ObserverGuard print_start_phase_observer;

const RuntimeConfig& get_runtime_config() {
    return g_runtime_config;
}

RuntimeConfig* get_mutable_runtime_config() {
    return &g_runtime_config;
}

// Forward declarations
static void save_screenshot();
static void initialize_moonraker_client(Config* config);

// NOTE: CLI argument parsing moved to cli_args.cpp - see helix::parse_cli_args()

/**
 * Register fonts and images for XML component system
 *
 * IMPORTANT: Fonts require TWO steps to work:
 *   1. Enable in lv_conf.h: #define LV_FONT_MONTSERRAT_XX 1
 *   2. Register here with lv_xml_register_font()
 *
 * If either step is missing, LVGL will silently fall back to a different font,
 * causing visual bugs. The semantic text components (text_heading, text_body,
 * text_small) in ui_text.cpp will crash with a clear error if a font is not
 * properly registered - this is intentional to catch configuration errors early.
 *
 * See docs/LVGL9_XML_GUIDE.md "Typography - Semantic Text Components" for details.
 */
static void register_fonts_and_images() {
    spdlog::debug("[Init] Registering fonts and images...");

    // Material Design Icons (various sizes for different UI elements)
    // Source: https://pictogrammers.com/library/mdi/
    lv_xml_register_font(NULL, "mdi_icons_64", &mdi_icons_64);
    lv_xml_register_font(NULL, "mdi_icons_48", &mdi_icons_48);
    lv_xml_register_font(NULL, "mdi_icons_32", &mdi_icons_32);
    lv_xml_register_font(NULL, "mdi_icons_24", &mdi_icons_24);
    lv_xml_register_font(NULL, "mdi_icons_16", &mdi_icons_16);

    // Montserrat text fonts - used by semantic text components:
    // - text_heading uses font_heading (20/26/28 for small/medium/large breakpoints)
    // - text_body uses font_body (14/18/20 for small/medium/large breakpoints)
    // - text_small uses font_small (12/16/18 for small/medium/large breakpoints)
    // ALL sizes used by the responsive typography system MUST be registered here!
    // NOTE: Registering as "montserrat_*" for XML compatibility but using noto_sans_* fonts
    lv_xml_register_font(NULL, "montserrat_10", &noto_sans_10);
    lv_xml_register_font(NULL, "montserrat_12", &noto_sans_12); // text_small (small)
    lv_xml_register_font(NULL, "montserrat_14", &noto_sans_14); // text_body (small)
    lv_xml_register_font(NULL, "montserrat_16", &noto_sans_16); // text_small (medium)
    lv_xml_register_font(NULL, "montserrat_18",
                         &noto_sans_18); // text_body (medium), text_small (large)
    lv_xml_register_font(NULL, "montserrat_20",
                         &noto_sans_20); // text_heading (small), text_body (large)
    lv_xml_register_font(NULL, "montserrat_24", &noto_sans_24);
    lv_xml_register_font(NULL, "montserrat_26", &noto_sans_26); // text_heading (medium)
    lv_xml_register_font(NULL, "montserrat_28",
                         &noto_sans_28); // text_heading (large), numeric displays

    // Noto Sans fonts - same sizes as Montserrat, with extended Unicode support
    // (includes ©®™€£¥°±•… and other symbols)
    lv_xml_register_font(NULL, "noto_sans_10", &noto_sans_10);
    lv_xml_register_font(NULL, "noto_sans_12", &noto_sans_12);
    lv_xml_register_font(NULL, "noto_sans_14", &noto_sans_14);
    lv_xml_register_font(NULL, "noto_sans_16", &noto_sans_16);
    lv_xml_register_font(NULL, "noto_sans_18", &noto_sans_18);
    lv_xml_register_font(NULL, "noto_sans_20", &noto_sans_20);
    lv_xml_register_font(NULL, "noto_sans_24", &noto_sans_24);
    lv_xml_register_font(NULL, "noto_sans_26", &noto_sans_26);
    lv_xml_register_font(NULL, "noto_sans_28", &noto_sans_28);

    // Noto Sans Bold fonts (for future use)
    lv_xml_register_font(NULL, "noto_sans_bold_14", &noto_sans_bold_14);
    lv_xml_register_font(NULL, "noto_sans_bold_16", &noto_sans_bold_16);
    lv_xml_register_font(NULL, "noto_sans_bold_18", &noto_sans_bold_18);
    lv_xml_register_font(NULL, "noto_sans_bold_20", &noto_sans_bold_20);
    lv_xml_register_font(NULL, "noto_sans_bold_24", &noto_sans_bold_24);
    lv_xml_register_font(NULL, "noto_sans_bold_28", &noto_sans_bold_28);

    lv_xml_register_image(NULL, "A:assets/images/printer_400.png",
                          "A:assets/images/printer_400.png");
    lv_xml_register_image(NULL, "filament_spool", "A:assets/images/filament_spool.png");
    lv_xml_register_image(NULL, "A:assets/images/placeholder_thumb_centered.png",
                          "A:assets/images/placeholder_thumb_centered.png");
    lv_xml_register_image(NULL, "A:assets/images/thumbnail-gradient-bg.png",
                          "A:assets/images/thumbnail-gradient-bg.png");
    lv_xml_register_image(NULL, "A:assets/images/thumbnail-placeholder.png",
                          "A:assets/images/thumbnail-placeholder.png");
    lv_xml_register_image(NULL, "A:assets/images/large-extruder-icon.svg",
                          "A:assets/images/large-extruder-icon.svg");
    lv_xml_register_image(NULL, "A:assets/images/benchy_thumbnail_white.png",
                          "A:assets/images/benchy_thumbnail_white.png");
}

// Initialize all reactive subjects for data binding
static void initialize_subjects() {
    spdlog::debug("[Init] Initializing reactive subjects...");
    app_globals_init_subjects();   // Global subjects (notification subject, etc.)
    ui_nav_init();                 // Navigation system (icon colors, active panel)
    ui_status_bar_init_subjects(); // Status bar subjects (printer/network icon states)

    // PrinterState must be initialized BEFORE panels that observe its subjects
    // (e.g., HomePanel observes led_state_, extruder_temp_, connection_state_)
    get_printer_state()
        .init_subjects(); // Printer state subjects (CRITICAL: must be before panel creation)

    // Initialize AmsState subjects BEFORE panels so XML bindings can find ams_gate_count
    // This is critical for the home panel's AMS indicator visibility binding
    // Note: In mock mode, init_subjects() also creates the mock backend internally
    AmsState::instance().init_subjects(true);

    // NOTE: AMS panel is lazily initialized when first accessed via get_global_ams_panel()

    // Register print completion notification observer (watches print_state_enum for terminal
    // states) - implementation in print_completion.cpp handles panel detection
    print_completion_observer = helix::init_print_completion_observer();

    get_global_home_panel().init_subjects();                  // Home panel data bindings
    get_global_controls_panel().init_subjects();              // Controls panel launcher
    get_global_filament_panel().init_subjects();              // Filament panel
    get_global_settings_panel().init_subjects();              // Settings panel launcher
    init_global_advanced_panel(get_printer_state(), nullptr); // Initialize advanced panel instance
    get_global_advanced_panel().init_subjects();              // Advanced panel capability subjects
    init_global_history_dashboard_panel(get_printer_state(),
                                        nullptr);         // Initialize history dashboard
    get_global_history_dashboard_panel().init_subjects(); // History dashboard subjects
    init_global_history_list_panel(get_printer_state(),
                                   nullptr);         // Initialize history list panel
    get_global_history_list_panel().init_subjects(); // History list panel subjects
    init_global_timelapse_settings(get_printer_state(),
                                   nullptr);         // Initialize timelapse settings overlay
    get_global_timelapse_settings().init_subjects(); // Timelapse settings callbacks
    init_global_console_panel(get_printer_state(),
                              nullptr);         // Initialize console panel
    get_global_console_panel().init_subjects(); // Console panel callbacks
    init_screws_tilt_row_handler();             // Screws tilt row callback
    init_input_shaper_row_handler();            // Input shaper row callback
    init_zoffset_row_handler();                 // Z-Offset row callback
    ui_wizard_init_subjects();                  // Wizard subjects (for first-run config)
    ui_keypad_init_subjects();                  // Keypad display subject (for reactive binding)

    // Panels that need MoonrakerAPI - store pointers for deferred set_api()
    print_select_panel = get_print_select_panel(get_printer_state(), nullptr);
    print_select_panel->init_subjects();

    // Initialize UsbManager with mock backend in test mode
    usb_manager = std::make_unique<UsbManager>(g_runtime_config.should_mock_usb());
    if (usb_manager->start()) {
        spdlog::info("[USB Manager] Started (mock={})", g_runtime_config.should_mock_usb());
        print_select_panel->set_usb_manager(usb_manager.get());
    } else {
        spdlog::warn("[USB Manager] Failed to start");
    }
    print_status_panel = &get_global_print_status_panel();
    print_status_panel->init_subjects();
    motion_panel = &get_global_motion_panel();
    motion_panel->init_subjects();
    extrusion_panel = &get_global_extrusion_panel();
    extrusion_panel->init_subjects();
    bed_mesh_panel = &get_global_bed_mesh_panel();
    bed_mesh_panel->init_subjects();

    // Initialize PID calibration panel subjects
    PIDCalibrationPanel::init_subjects();

    // Initialize Z-Offset calibration panel subjects
    ZOffsetCalibrationPanel::init_subjects();

    // Initialize TempControlPanel (needs PrinterState ready)
    temp_control_panel = std::make_unique<TempControlPanel>(get_printer_state(), nullptr);
    temp_control_panel->init_subjects();

    // Inject TempControlPanel into ControlsPanel for temperature sub-screens
    get_global_controls_panel().set_temp_control_panel(temp_control_panel.get());

    // Inject TempControlPanel into HomePanel for temperature icon click
    get_global_home_panel().set_temp_control_panel(temp_control_panel.get());

    // Inject TempControlPanel into PrintStatusPanel for temp card clicks
    get_global_print_status_panel().set_temp_control_panel(temp_control_panel.get());

    // Initialize notification system (after subjects are ready)
    ui_notification_init();

    // Initialize E-Stop overlay subjects (must be before XML creation)
    EmergencyStopOverlay::instance().init_subjects();

    // Set up USB drive event notifications (after notification system is ready)
    if (usb_manager) {
        usb_manager->set_drive_callback([](UsbEvent event, const UsbDrive& drive) {
            (void)drive; // Currently not using drive info in messages
            if (event == UsbEvent::DRIVE_INSERTED) {
                ui_notification_success("USB drive connected");

                // Show USB tab in PrintSelectPanel
                if (print_select_panel) {
                    print_select_panel->on_usb_drive_inserted();
                }
            } else if (event == UsbEvent::DRIVE_REMOVED) {
                ui_notification_info("USB drive removed");

                // Hide USB tab and switch to Printer source if viewing USB
                if (print_select_panel) {
                    print_select_panel->on_usb_drive_removed();
                }
            }
        });

        // In test mode, schedule demo drive insertion after UI is fully ready
        // This ensures the toast notification is visible to the user
        if (g_runtime_config.should_mock_usb()) {
            // Use LVGL timer to delay insertion - this runs on the main thread after UI init
            lv_timer_create(
                [](lv_timer_t* timer) {
                    if (auto* mock = dynamic_cast<UsbBackendMock*>(usb_manager->get_backend())) {
                        mock->add_demo_drives();
                        spdlog::debug(
                            "[USB Manager] Added demo USB drives for test mode (delayed)");
                    }
                    lv_timer_delete(timer);
                },
                3000, // 3 second delay for UI to fully initialize
                nullptr);
        }
    }
}

// Initialize LVGL with auto-detected display backend
static bool init_lvgl() {
    lv_init();

    // Create display backend (auto-detects: DRM → framebuffer → SDL)
    g_display_backend = DisplayBackend::create_auto();
    if (!g_display_backend) {
        spdlog::error("[Display] No display backend available");
        lv_deinit();
        return false;
    }

    spdlog::info("[Display] Using backend: {}", g_display_backend->name());

    // Create display
    display = g_display_backend->create_display(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!display) {
        spdlog::error("[Display] Failed to create display");
        g_display_backend.reset();
        lv_deinit();
        return false;
    }

    // Create pointer input device (mouse/touch)
    indev_mouse = g_display_backend->create_input_pointer();
    if (!indev_mouse) {
#if defined(HELIX_DISPLAY_DRM) || defined(HELIX_DISPLAY_FBDEV)
        // On embedded platforms (DRM/fbdev), no input device is fatal - show error screen
        spdlog::error("[Display] No input device found - cannot operate touchscreen UI");

        static const char* suggestions[] = {
            "Check /dev/input/event* devices exist",
            "Ensure user is in 'input' group: sudo usermod -aG input $USER",
            "Check touchscreen driver is loaded: dmesg | grep -i touch",
            "Set HELIX_TOUCH_DEVICE=/dev/input/eventX to override",
            "Add \"touch_device\": \"/dev/input/event1\" to helixconfig.json",
            nullptr};

        ui_show_fatal_error("No Input Device",
                            "Could not find or open a touch/pointer input device.\n"
                            "The UI requires an input device to function.",
                            suggestions,
                            30000 // Show for 30 seconds then exit
        );

        return false;
#else
        // On desktop (SDL), continue without pointer - mouse is optional
        spdlog::warn("[Display] No pointer input device created - touch/mouse disabled");
#endif
    }

    // Configure scroll behavior from config (improves touchpad/touchscreen scrolling feel)
    // scroll_throw: momentum decay rate (1-99), higher = faster decay, default LVGL is 10
    // scroll_limit: pixels before scrolling starts, lower = more responsive, default LVGL is 10
    if (indev_mouse) {
        Config* cfg = Config::get_instance();
        int scroll_throw = cfg->get<int>("/input/scroll_throw", 25);
        int scroll_limit = cfg->get<int>("/input/scroll_limit", 5);
        lv_indev_set_scroll_throw(indev_mouse, static_cast<uint8_t>(scroll_throw));
        lv_indev_set_scroll_limit(indev_mouse, static_cast<uint8_t>(scroll_limit));
        spdlog::debug("[Display] Scroll config: throw={}, limit={}", scroll_throw, scroll_limit);
    }

    // Create keyboard input device (optional - enables physical keyboard input)
    lv_indev_t* indev_keyboard = g_display_backend->create_input_keyboard();
    if (indev_keyboard) {
        spdlog::debug("[Display] Physical keyboard input enabled");

        // Create input group for keyboard navigation and text input
        lv_group_t* input_group = lv_group_create();
        lv_group_set_default(input_group);
        lv_indev_set_group(indev_keyboard, input_group);
        spdlog::debug("[Display] Created default input group for keyboard");
    }

    spdlog::debug("[Display] LVGL initialized: {}x{}", SCREEN_WIDTH, SCREEN_HEIGHT);

    // Initialize SVG decoder for loading .svg files
    lv_svg_decoder_init();

    return true;
}

// Show splash screen with HelixScreen logo
static void show_splash_screen() {
    spdlog::debug("[Splash] Showing splash screen");

    // Get the active screen
    lv_obj_t* screen = lv_screen_active();

    // Apply theme background color (app_bg_color runtime constant set by ui_theme_init)
    ui_theme_apply_bg_color(screen, "app_bg_color", LV_PART_MAIN);

    // Disable scrollbars on screen
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Create centered container for logo (disable scrolling)
    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);         // Disable scrollbars
    lv_obj_set_style_opa(container, LV_OPA_TRANSP, LV_PART_MAIN); // Start invisible for fade-in
    lv_obj_center(container);

    // Create image widget for logo
    lv_obj_t* logo = lv_image_create(container);
    const char* logo_path = "A:assets/images/helixscreen-logo.png";
    lv_image_set_src(logo, logo_path);

    // Get actual image dimensions
    lv_image_header_t header;
    lv_result_t res = lv_image_decoder_get_info(logo_path, &header);

    if (res == LV_RESULT_OK) {
        // Scale logo to fill more of the screen (60% of screen width)
        lv_coord_t target_size = (SCREEN_WIDTH * 3) / 5; // 60% of screen width
        if (SCREEN_HEIGHT < 500) {                       // Tiny screen
            target_size = SCREEN_WIDTH / 2;              // 50% on tiny screens
        }

        // Calculate scale: (target_size * 256) / actual_width
        // LVGL uses 1/256 scale units (256 = 100%, 128 = 50%, etc.)
        uint32_t width = header.w;  // Copy bit-field to local var for logging
        uint32_t height = header.h; // Copy bit-field to local var for logging
        uint32_t scale = (static_cast<uint32_t>(target_size) * 256U) / width;
        lv_image_set_scale(logo, scale);

        spdlog::debug("[Splash] Logo: {}x{} scaled to {} (scale factor: {})", width, height,
                      target_size, scale);
    } else {
        spdlog::warn("[Splash] Could not get logo dimensions, using default scale");
        lv_image_set_scale(logo, 128); // 50% scale as fallback
    }

    // Create fade-in animation (0.5 seconds)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, 500); // 500ms = 0.5 seconds
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa((lv_obj_t*)obj, static_cast<lv_opa_t>(value), LV_PART_MAIN);
    });
    lv_anim_start(&anim);

    // Run LVGL timer to process fade-in animation and keep splash visible
    // Total display time: 2 seconds (including 0.5s fade-in)
    uint32_t splash_start = helix_get_ticks();
    uint32_t splash_duration = 2000; // 2 seconds total

    while (helix_get_ticks() - splash_start < splash_duration) {
        lv_timer_handler(); // Process animations and rendering
        helix_delay(5);
    }

    // Clean up splash screen
    lv_obj_delete(container);

    spdlog::debug("[Splash] Splash screen complete");
}

// Save screenshot using SDL renderer
// Simple BMP file writer for ARGB8888 format
static bool write_bmp(const char* filename, const uint8_t* data, int width, int height) {
    // RAII for file handle - automatically closes on all return paths
    std::unique_ptr<FILE, decltype(&fclose)> f(fopen(filename, "wb"), fclose);
    if (!f)
        return false;

    // BMP header (54 bytes total)
    uint32_t file_size = 54U + (static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4U);
    uint32_t pixel_offset = 54;
    uint32_t dib_size = 40;
    uint16_t planes = 1;
    uint16_t bpp = 32;
    uint32_t reserved = 0;
    uint32_t compression = 0;
    uint32_t ppm = 2835; // pixels per meter
    uint32_t colors = 0;

    // BMP file header (14 bytes)
    fputc('B', f.get());
    fputc('M', f.get());                  // Signature
    fwrite(&file_size, 4, 1, f.get());    // File size
    fwrite(&reserved, 4, 1, f.get());     // Reserved
    fwrite(&pixel_offset, 4, 1, f.get()); // Pixel data offset

    // DIB header (40 bytes)
    fwrite(&dib_size, 4, 1, f.get());    // DIB header size
    fwrite(&width, 4, 1, f.get());       // Width
    fwrite(&height, 4, 1, f.get());      // Height
    fwrite(&planes, 2, 1, f.get());      // Planes
    fwrite(&bpp, 2, 1, f.get());         // Bits per pixel
    fwrite(&compression, 4, 1, f.get()); // Compression (none)
    uint32_t image_size = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4U;
    fwrite(&image_size, 4, 1, f.get()); // Image size
    fwrite(&ppm, 4, 1, f.get());        // X pixels per meter
    fwrite(&ppm, 4, 1, f.get());        // Y pixels per meter
    fwrite(&colors, 4, 1, f.get());     // Colors in palette
    fwrite(&colors, 4, 1, f.get());     // Important colors

    // Write pixel data (BMP is bottom-up, so flip rows)
    for (int y = height - 1; y >= 0; y--) {
        fwrite(data + (static_cast<size_t>(y) * static_cast<size_t>(width) * 4), 4,
               static_cast<size_t>(width), f.get());
    }

    // File automatically closed by unique_ptr destructor
    return true;
}

static void save_screenshot() {
    // Generate unique filename with timestamp
    char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/ui-screenshot-%lu.bmp", (unsigned long)time(NULL));

    // Take snapshot using LVGL's native API (platform-independent)
    lv_obj_t* screen = lv_screen_active();
    lv_draw_buf_t* snapshot = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);

    if (!snapshot) {
        spdlog::error("[Screenshot] Failed to take screenshot");
        return;
    }

    // Write BMP file
    if (write_bmp(filename, snapshot->data, snapshot->header.w, snapshot->header.h)) {
        spdlog::info("[Screenshot] Saved: {}", filename);
    } else {
        NOTIFY_ERROR("Failed to save screenshot");
        LOG_ERROR_INTERNAL("Failed to save screenshot to {}", filename);
    }

    // Free snapshot buffer
    lv_draw_buf_destroy(snapshot);
}

// Initialize Moonraker client and API instances
static void initialize_moonraker_client(Config* config) {
    spdlog::debug("[Moonraker Client] Initializing...");

    // Create client instance (mock or real based on test mode)
    if (get_runtime_config().should_mock_moonraker()) {
        double speedup = get_runtime_config().sim_speedup;
        spdlog::debug("[Test Mode] Creating MOCK Moonraker client (Voron 2.4 profile, {}x speed)",
                      speedup);
        auto mock = std::make_unique<MoonrakerClientMock>(
            MoonrakerClientMock::PrinterType::VORON_24, speedup);
        moonraker_client = std::move(mock);
    } else {
        spdlog::debug("[Moonraker Client] Creating REAL Moonraker client");
        moonraker_client = std::make_unique<MoonrakerClient>();
    }

    // Register with app_globals (raw pointer for access, main.cpp owns lifetime)
    set_moonraker_client(moonraker_client.get());

    // Initialize SoundManager with Moonraker client for M300 audio feedback
    SoundManager::instance().set_moonraker_client(moonraker_client.get());

    // Configure timeouts from config file
    uint32_t connection_timeout = static_cast<uint32_t>(
        config->get<int>(config->df() + "moonraker_connection_timeout_ms", 10000));
    uint32_t request_timeout = static_cast<uint32_t>(
        config->get<int>(config->df() + "moonraker_request_timeout_ms", 30000));
    uint32_t keepalive_interval = static_cast<uint32_t>(
        config->get<int>(config->df() + "moonraker_keepalive_interval_ms", 10000));
    uint32_t reconnect_min_delay = static_cast<uint32_t>(
        config->get<int>(config->df() + "moonraker_reconnect_min_delay_ms", 200));
    uint32_t reconnect_max_delay = static_cast<uint32_t>(
        config->get<int>(config->df() + "moonraker_reconnect_max_delay_ms", 2000));

    moonraker_client->configure_timeouts(connection_timeout, request_timeout, keepalive_interval,
                                         reconnect_min_delay, reconnect_max_delay);

    spdlog::debug(
        "[Moonraker Client] Timeouts configured: connection={}ms, request={}ms, keepalive={}ms",
        connection_timeout, request_timeout, keepalive_interval);

    // Register event handler to translate transport events to UI notifications
    // This decouples the transport layer (MoonrakerClient) from the UI layer
    moonraker_client->register_event_handler([](const MoonrakerEvent& evt) {
        const char* title = nullptr;
        if (evt.type == MoonrakerEventType::CONNECTION_FAILED) {
            title = "Connection Failed";
        } else if (evt.type == MoonrakerEventType::KLIPPY_DISCONNECTED) {
            title = "Printer Firmware Disconnected";
        }

        if (evt.is_error) {
            ui_notification_error(title, evt.message.c_str(),
                                  evt.type == MoonrakerEventType::CONNECTION_FAILED ||
                                      evt.type == MoonrakerEventType::KLIPPY_DISCONNECTED);
        } else {
            ui_notification_warning(evt.message.c_str());
        }
    });

    // Set up state change callback to queue updates for main thread
    // CRITICAL: This callback runs on the Moonraker event loop thread, NOT the main thread.
    // LVGL is NOT thread-safe, so we must NOT call any LVGL functions here.
    // Instead, queue the state change and process it on the main thread.
    moonraker_client->set_state_change_callback([](ConnectionState old_state,
                                                   ConnectionState new_state) {
        spdlog::trace("[main] State change callback invoked: {} -> {} (queueing for main thread)",
                      static_cast<int>(old_state), static_cast<int>(new_state));

        // Queue state change for main thread processing (same mutex as notifications)
        // Use a special JSON object with "_connection_state" marker
        std::lock_guard<std::mutex> lock(notification_mutex);
        json state_change;
        state_change["_connection_state"] = true;
        state_change["old_state"] = static_cast<int>(old_state);
        state_change["new_state"] = static_cast<int>(new_state);
        notification_queue.push(state_change);
    });

    // Register notification callback to queue updates for main thread
    // CRITICAL: Moonraker callbacks run on background thread, but LVGL is NOT thread-safe
    // Queue notifications here, process on main thread in event loop
    moonraker_client->register_notify_update([](json notification) {
        std::lock_guard<std::mutex> lock(notification_mutex);
        notification_queue.push(notification);
    });

    // Create MoonrakerAPI instance (mock or real based on test mode)
    spdlog::debug("[Moonraker API] Creating instance...");
    if (get_runtime_config().should_use_test_files()) {
        spdlog::debug("[Test Mode] Creating MOCK MoonrakerAPI (local file transfers)");
        moonraker_api = std::make_unique<MoonrakerAPIMock>(*moonraker_client, get_printer_state());
    } else {
        moonraker_api = std::make_unique<MoonrakerAPI>(*moonraker_client, get_printer_state());
    }

    // Register with app_globals (raw pointer for access, main.cpp owns lifetime)
    set_moonraker_api(moonraker_api.get());

    // Update all panels with API reference (pass raw pointer, main.cpp owns lifetime)
    // Note: These panels are initialized in initialize_subjects() before this function
    get_global_home_panel().set_api(moonraker_api.get());
    if (temp_control_panel) {
        temp_control_panel->set_api(moonraker_api.get());
    }
    if (print_select_panel) {
        print_select_panel->set_api(moonraker_api.get());
    }
    if (print_status_panel) {
        print_status_panel->set_api(moonraker_api.get());
    }
    if (motion_panel) {
        motion_panel->set_api(moonraker_api.get());
    }
    if (extrusion_panel) {
        extrusion_panel->set_api(moonraker_api.get());
    }
    if (bed_mesh_panel) {
        bed_mesh_panel->set_api(moonraker_api.get());
    }
    get_global_history_dashboard_panel().set_api(moonraker_api.get());
    get_global_history_list_panel().set_api(moonraker_api.get());

    // Initialize E-Stop overlay with dependencies (creates the floating button)
    EmergencyStopOverlay::instance().init(get_printer_state(), moonraker_api.get());
    EmergencyStopOverlay::instance().create();
    // Apply persisted E-Stop confirmation setting
    EmergencyStopOverlay::instance().set_require_confirmation(
        SettingsManager::instance().get_estop_require_confirmation());
    // Set initial panel for visibility tracking (home_panel is default)
    EmergencyStopOverlay::instance().on_panel_changed("home_panel");

    spdlog::info("[main] Moonraker client initialized (not connected yet)");

    // Create PRINT_START progress collector
    // Monitors notify_gcode_response during print initialization to show phase progress
    print_start_collector =
        std::make_shared<PrintStartCollector>(*moonraker_client, get_printer_state());

    // Set up observer to start/stop collector based on print state
    // When print state becomes PRINTING, start the collector
    // When print completes, cancels, or errors, stop the collector
    print_start_observer = ObserverGuard(
        get_printer_state().get_print_state_enum_subject(),
        [](lv_observer_t* /*observer*/, lv_subject_t* subject) {
            auto state = static_cast<PrintJobState>(lv_subject_get_int(subject));

            if (state == PrintJobState::PRINTING) {
                // Check if not already active (print just started)
                if (!print_start_collector->is_active()) {
                    print_start_collector->reset();
                    print_start_collector->start();
                    spdlog::info("[main] PRINT_START collector started");
                }
            } else {
                // Any non-printing state stops the collector
                if (print_start_collector->is_active()) {
                    print_start_collector->stop();
                    spdlog::info("[main] PRINT_START collector stopped (state={})",
                                 static_cast<int>(state));
                }
            }
        },
        nullptr);

    // Watch print_start_phase for COMPLETE to stop collector when layer 1 detected
    print_start_phase_observer = ObserverGuard(
        get_printer_state().get_print_start_phase_subject(),
        [](lv_observer_t* /*observer*/, lv_subject_t* subject) {
            auto phase = static_cast<PrintStartPhase>(lv_subject_get_int(subject));

            if (phase == PrintStartPhase::COMPLETE) {
                // Layer 1 detected - collector has done its job
                if (print_start_collector && print_start_collector->is_active()) {
                    print_start_collector->stop();
                    spdlog::info("[main] PRINT_START collector stopped (phase=COMPLETE)");
                }
            }
        },
        nullptr);

    // Test print history API if requested (for Stage 1 validation)
    if (get_runtime_config().test_history_api) {
        spdlog::info("[History Test] Testing print history API...");

        // Test get_history_list
        moonraker_api->get_history_list(
            10, 0, 0.0, 0.0,
            [](const std::vector<PrintHistoryJob>& jobs, uint64_t total_count) {
                spdlog::info("[History Test] get_history_list SUCCESS: {} jobs (total: {})",
                             jobs.size(), total_count);
                for (size_t i = 0; i < std::min(jobs.size(), size_t(3)); ++i) {
                    const auto& job = jobs[i];
                    spdlog::info("[History Test]   Job {}: {} - {} ({})", i + 1, job.filename,
                                 job.duration_str, job.date_str);
                }
            },
            [](const MoonrakerError& err) {
                spdlog::error("[History Test] get_history_list FAILED: {}", err.message);
            });

        // Test get_history_totals
        moonraker_api->get_history_totals(
            [](const PrintHistoryTotals& totals) {
                spdlog::info("[History Test] get_history_totals SUCCESS:");
                spdlog::info("[History Test]   Total jobs: {}", totals.total_jobs);
                spdlog::info("[History Test]   Completed: {}, Cancelled: {}, Failed: {}",
                             totals.total_completed, totals.total_cancelled, totals.total_failed);
                spdlog::info("[History Test]   Total time: {}s, Filament: {:.1f}mm",
                             totals.total_time, totals.total_filament_used);
            },
            [](const MoonrakerError& err) {
                spdlog::error("[History Test] get_history_totals FAILED: {}", err.message);
            });
    }
}

// Main application
int main(int argc, char** argv) {
    // Store argv early for restart capability (before any modifications)
    app_store_argv(argc, argv);

    // Ensure we're running from the project root for relative path access
    ensure_project_root_cwd();

    // Parse command-line arguments using extracted CLI module
    helix::CliArgs args;
    if (!helix::parse_cli_args(argc, argv, args, SCREEN_WIDTH, SCREEN_HEIGHT)) {
        return 0; // Help shown or parse error
    }

    // Auto-configure mock state based on requested panel in test mode
    // This ensures panels have appropriate data without requiring extra command-line args
    if (g_runtime_config.test_mode && !g_runtime_config.use_real_moonraker) {
        // print-status: Auto-start a print simulation so the panel shows content
        if (args.overlays.print_status) {
            g_runtime_config.mock_auto_start_print = true;
            // Also set gcode_test_file so the G-code viewer loads the preview
            g_runtime_config.gcode_test_file = RuntimeConfig::get_default_test_file_path();
            printf("  [Auto] Mock will simulate active print for print-status panel\n");
        }

        // print-select: Auto-select a file to show the detail view
        if (args.initial_panel == UI_PANEL_PRINT_SELECT && !g_runtime_config.select_file) {
            g_runtime_config.select_file = RuntimeConfig::DEFAULT_TEST_FILE;
            printf("  [Auto] Auto-selecting '%s' for print-select panel\n",
                   RuntimeConfig::DEFAULT_TEST_FILE);
        }

        // history: Enable mock history data generation
        if (args.overlays.history_dashboard) {
            g_runtime_config.mock_auto_history = true;
            printf("  [Auto] Mock will generate history data for history panel\n");
        }
    }

    // Check HELIX_AUTO_QUIT_MS environment variable (only if --timeout not specified)
    if (args.timeout_sec == 0) {
        const char* auto_quit_env = std::getenv("HELIX_AUTO_QUIT_MS");
        if (auto_quit_env != nullptr) {
            char* endptr;
            long val = strtol(auto_quit_env, &endptr, 10);
            if (*endptr == '\0' && val >= 100 && val <= 3600000) {
                // Convert milliseconds to seconds (round up to ensure at least 1 second)
                args.timeout_sec = static_cast<int>((val + 999) / 1000);
            }
        }
    }

    // Check HELIX_AUTO_SCREENSHOT environment variable
    const char* auto_screenshot_env = std::getenv("HELIX_AUTO_SCREENSHOT");
    if (auto_screenshot_env != nullptr && strcmp(auto_screenshot_env, "1") == 0) {
        args.screenshot_enabled = true;
    }

    // Initialize config system early so we can read logging settings
    Config* config = Config::get_instance();
    config->init("helixconfig.json");

    // Initialize logging subsystem
    // Priority: CLI > config > auto-detect
    {
        helix::logging::LogConfig log_config;

        // Set log level from verbosity flags
        switch (args.verbosity) {
        case 0:
            log_config.level = spdlog::level::warn;
            break;
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

        // Determine log destination: CLI > config > auto
        std::string log_dest_str = g_log_dest_cli;
        if (log_dest_str.empty()) {
            log_dest_str = config->get<std::string>("/log_dest", "auto");
        }
        log_config.target = helix::logging::parse_log_target(log_dest_str);

        // Determine log file path: CLI > config > auto
        log_config.file_path = g_log_file_cli;
        if (log_config.file_path.empty()) {
            log_config.file_path = config->get<std::string>("/log_path", "");
        }

        helix::logging::init(log_config);
    }

    spdlog::info("[main] HelixScreen UI Prototype");
    spdlog::info("[main] ========================");
    spdlog::debug("[main] Target: {}x{}", SCREEN_WIDTH, SCREEN_HEIGHT);
    spdlog::debug("[main] DPI: {}{}", (args.dpi > 0 ? args.dpi : LV_DPI_DEF),
                  (args.dpi > 0 ? " (custom)" : " (default)"));
    spdlog::debug("[main] Nav Width: {} pixels", UI_NAV_WIDTH(SCREEN_WIDTH));
    spdlog::debug("[main] Initial Panel: {}", args.initial_panel);

    // Cleanup stale temp files from G-code modifications (older than 1 hour)
    size_t cleaned = helix::gcode::GCodeFileModifier::cleanup_temp_files();
    if (cleaned > 0) {
        spdlog::info("[main] Cleaned up {} stale G-code temp file(s)", cleaned);
    }

    // Determine theme: CLI overrides config, config overrides default (dark)
    bool dark_mode;
    if (args.dark_mode_cli >= 0) {
        // CLI explicitly set --dark or --light (temporary override, not saved)
        dark_mode = (args.dark_mode_cli == 1);
        spdlog::debug("[Theme] Using CLI theme override: {}", dark_mode ? "dark" : "light");
    } else {
        // Load from config (or default to dark)
        dark_mode = config->get<bool>("/dark_mode", true);
        spdlog::debug("[Theme] Loaded preference from config: {}", dark_mode ? "dark" : "light");
    }

#ifdef HELIX_DISPLAY_SDL
    // Set window position environment variables for LVGL SDL driver (desktop only)
    if (args.display_num >= 0) {
        char display_str[32];
        snprintf(display_str, sizeof(display_str), "%d", args.display_num);
        if (setenv("HELIX_SDL_DISPLAY", display_str, 1) != 0) {
            spdlog::error("[SDL] Failed to set HELIX_SDL_DISPLAY environment variable");
            return 1;
        }
        spdlog::debug("[SDL] Window will be centered on display {}", args.display_num);
    }
    if (args.x_pos >= 0 && args.y_pos >= 0) {
        char x_str[32], y_str[32];
        snprintf(x_str, sizeof(x_str), "%d", args.x_pos);
        snprintf(y_str, sizeof(y_str), "%d", args.y_pos);
        if (setenv("HELIX_SDL_XPOS", x_str, 1) != 0 || setenv("HELIX_SDL_YPOS", y_str, 1) != 0) {
            spdlog::error("[SDL] Failed to set window position environment variables");
            return 1;
        }
        spdlog::debug("[SDL] Window will be positioned at ({}, {})", args.x_pos, args.y_pos);
    } else if ((args.x_pos >= 0 && args.y_pos < 0) || (args.x_pos < 0 && args.y_pos >= 0)) {
        spdlog::warn("[SDL] Both -x and -y must be specified for exact positioning. Ignoring.");
    }
#endif

    // Signal external splash process to exit BEFORE creating our display
    // This is critical for DRM - only one process can hold the display at a time
    if (g_runtime_config.splash_pid > 0) {
        spdlog::info("[Init] Signaling splash process (PID {}) to exit...",
                     g_runtime_config.splash_pid);
        if (kill(g_runtime_config.splash_pid, SIGUSR1) == 0) {
            // Wait for splash to actually exit and release DRM resources
            // We can't use waitpid() since we're not the parent, so poll with kill(pid, 0)
            int wait_attempts = 50; // 50 * 20ms = 1 second max
            while (wait_attempts-- > 0 && kill(g_runtime_config.splash_pid, 0) == 0) {
                usleep(20000); // 20ms
            }
            if (wait_attempts <= 0) {
                spdlog::warn("[Init] Splash process did not exit in time, proceeding anyway");
            } else {
                spdlog::debug("[Init] Splash process exited, proceeding with display init");
            }
        } else {
            spdlog::debug("[Init] Splash process already exited (PID {})",
                          g_runtime_config.splash_pid);
        }
        // Clear the PID so we don't try to signal it again later
        g_runtime_config.splash_pid = 0;
    }

    // Initialize LVGL with display backend
    if (!init_lvgl()) {
        return 1;
    }

    // Apply custom DPI if specified (before theme init)
    if (args.dpi > 0) {
        lv_display_set_dpi(display, args.dpi);
        spdlog::debug("[Display] DPI set to: {}", args.dpi);
    } else {
        spdlog::debug("[Display] DPI: {} (from LV_DPI_DEF)", lv_display_get_dpi(display));
    }

    // Create main screen
    lv_obj_t* screen = lv_screen_active();

    // Set window icon (after screen is created)
    ui_set_window_icon(display);

    // Initialize app-level resize handler for responsive layouts
    ui_resize_handler_init(screen);

    // Initialize tips manager (uses standard C++ file I/O, not LVGL's "A:" filesystem)
    TipsManager* tips_mgr = TipsManager::get_instance();
    if (!tips_mgr->init("config/printing_tips.json")) {
        spdlog::warn("[Tips] Failed to initialize - tips will not be available");
    } else {
        spdlog::debug("[Tips] Loaded {} tips", tips_mgr->get_total_tips());
    }

    // Register fonts and images for XML (must be done BEFORE globals.xml for theme init)
    register_fonts_and_images();

    // Register XML components (globals first to make constants available)
    spdlog::debug("[XML] Registering components...");
    lv_xml_register_component_from_file("A:ui_xml/globals.xml");

    // Initialize LVGL theme from globals.xml constants (after fonts and globals are registered)
    ui_theme_init(display,
                  dark_mode); // dark_mode from command-line args (--dark/--light) or config

    // Theme preference is saved by the settings panel when user toggles dark mode

    // Apply theme background color to screen
    ui_theme_apply_bg_color(screen, "app_bg_color", LV_PART_MAIN);

    // Show splash screen AFTER theme init (skip if requested via --skip-splash or --test)
    // Theme must be initialized first so app_bg_color runtime constant is available
    if (!g_runtime_config.should_skip_splash()) {
        show_splash_screen();
    }

    // Register custom widgets (must be before XML component registration)
    // Note: Material Design icons are now font-based (mdi_icons_*.c)
    // Icon lookup happens via ui_icon_codepoints.h
    ui_icon_register_widget();
    ui_switch_register();
    ui_card_register();
    ui_temp_display_init();
    ui_severity_card_register();
    ui_dialog_register();
    ui_bed_mesh_register();
    ui_gcode_viewer_register();
    ui_gradient_canvas_register();

    // Initialize component systems (BEFORE XML registration)
    ui_component_header_bar_init();

    // WORKAROUND: Add small delay to stabilize display/LVGL initialization
    // Prevents race condition between display backend and LVGL 9 XML component registration
    helix_delay(100);

    // Initialize memory profiling (development feature)
    // SIGUSR1 handler allows on-demand snapshots: kill -USR1 $(pidof helix-screen)
    helix::MemoryProfiler::init(args.memory_report);

    // Register remaining XML components (globals already registered for theme init)
    helix::register_xml_components();

    // Initialize reactive subjects BEFORE creating XML
    initialize_subjects();

    // Register status bar event callbacks BEFORE creating XML (so LVGL can find them)
    ui_status_bar_register_callbacks();

    // Register screws tilt panel callbacks BEFORE creating XML
    ui_panel_screws_tilt_register_callbacks();

    // PID calibration callbacks are registered in PIDCalibrationPanel::init_subjects()

    // Register input shaper panel callbacks BEFORE creating XML
    ui_panel_input_shaper_register_callbacks();

    // Create entire UI from XML (single component contains everything)
    lv_obj_t* app_layout = (lv_obj_t*)lv_xml_create(screen, "app_layout", NULL);

    // Disable scrollbars on screen to prevent overflow issues with overlay panels
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    // Force layout calculation for all LV_SIZE_CONTENT widgets
    lv_obj_update_layout(screen);

    // Register app_layout with navigation system (to prevent hiding it)
    ui_nav_set_app_layout(app_layout);

    // Initialize status bar (must be after XML creation and layout update)
    ui_status_bar_init();

    // Initialize toast notification system (registers close button callback)
    ui_toast_init();

    // Initialize shared overlay backdrop
    ui_nav_init_overlay_backdrop(screen);

    // Find widgets by name (robust to XML structure changes)
    lv_obj_t* navbar = lv_obj_find_by_name(app_layout, "navbar");
    lv_obj_t* content_area = lv_obj_find_by_name(app_layout, "content_area");

    if (!navbar || !content_area) {
        spdlog::error("[Init] Failed to find navbar/content_area in app_layout");
        lv_deinit();
        return 1;
    }

    // Wire up navigation button click handlers and trigger initial color update
    ui_nav_wire_events(navbar);

    // NOTE: Status icons (printer, network, notification) are now in home_panel.xml
    // They use XML-defined event callbacks and don't need C++ wiring

    // Find panel container by name (robust to layout changes like removing status_bar)
    lv_obj_t* panel_container = lv_obj_find_by_name(content_area, "panel_container");
    if (!panel_container) {
        spdlog::error("[Init] Failed to find panel_container in content_area");
        lv_deinit();
        return 1;
    }

    // Find all panel widgets by name (robust to child order changes)
    static const char* panel_names[UI_PANEL_COUNT] = {"home_panel",     "print_select_panel",
                                                      "controls_panel", "filament_panel",
                                                      "settings_panel", "advanced_panel"};

    lv_obj_t* panels[UI_PANEL_COUNT];
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        panels[i] = lv_obj_find_by_name(panel_container, panel_names[i]);
        if (!panels[i]) {
            spdlog::error("[Init] Missing panel '{}' in panel_container", panel_names[i]);
            lv_deinit();
            return 1;
        }
    }

    // Register panels with navigation system for show/hide management
    ui_nav_set_panels(panels);

    // Setup home panel observers (panels[0] is home panel)
    get_global_home_panel().setup(panels[0], screen);

    // Setup controls panel (wire launcher card click handlers)
    get_global_controls_panel().setup(panels[UI_PANEL_CONTROLS], screen);

    // Setup print select panel (wires up events, creates overlays, NOTE: data populated later)
    get_print_select_panel(get_printer_state(), nullptr)
        ->setup(panels[UI_PANEL_PRINT_SELECT], screen);

    // Setup filament panel (wire preset/action button handlers)
    get_global_filament_panel().setup(panels[UI_PANEL_FILAMENT], screen);

    // Setup settings panel (wire launcher card click handlers)
    get_global_settings_panel().setup(panels[UI_PANEL_SETTINGS], screen);

    // Setup advanced panel (wire action row click handlers)
    get_global_advanced_panel().setup(panels[UI_PANEL_ADVANCED], screen);

    // Initialize numeric keypad modal component (creates reusable keypad widget)
    ui_keypad_init(screen);

    // Create print status panel (overlay for active prints)
    overlay_panels.print_status = (lv_obj_t*)lv_xml_create(screen, "print_status_panel", nullptr);
    if (overlay_panels.print_status) {
        get_global_print_status_panel().setup(overlay_panels.print_status, screen);
        lv_obj_add_flag(overlay_panels.print_status, LV_OBJ_FLAG_HIDDEN); // Hidden by default

        // Wire print status panel to print select (for launching prints)
        get_print_select_panel(get_printer_state(), nullptr)
            ->set_print_status_panel(overlay_panels.print_status);

        spdlog::debug("[Print Status] Panel created and wired to print select");
    } else {
        spdlog::error("[Print Status] Failed to create panel");
    }

    // NOTE: AMS panel is created lazily when first accessed via get_global_ams_panel()

    spdlog::info("[Init] XML UI created successfully with reactive navigation");

    // Test notifications - commented out, uncomment to debug notification history
    // if (get_runtime_config().test_mode) {
    //     NOTIFY_INFO("Info notification test");
    //     NOTIFY_SUCCESS("Success notification test");
    //     NOTIFY_WARNING("Warning notification test");
    //     NOTIFY_ERROR("Error notification test");
    // }

    // Initialize Moonraker client EARLY (before wizard, so it's available for connection test)
    // But don't connect yet - just create the instances
    initialize_moonraker_client(config);

    // Initialize global keyboard BEFORE wizard (required for textarea registration)
    // NOTE: Keyboard is created early but will appear on top due to being moved to top layer below
    ui_keyboard_init(screen);

    // Initialize memory stats overlay (development tool, toggle with M key)
    MemoryStatsOverlay::instance().init(screen, g_runtime_config.show_memory_overlay);

    // Check if first-run wizard is required (skip for special test panels and explicit panel
    // requests)
    bool wizard_active = false;
    if ((args.force_wizard || config->is_wizard_required()) && !args.overlays.step_test &&
        !args.overlays.test_panel && !args.overlays.keypad && !args.overlays.keyboard &&
        !args.overlays.gcode_test && !args.panel_requested) {
        spdlog::info("[Wizard] Starting first-run configuration wizard");

        // Register wizard event callbacks and responsive constants BEFORE creating
        ui_wizard_register_event_callbacks();
        ui_wizard_container_register_responsive_constants();

        lv_obj_t* wizard = ui_wizard_create(screen);

        if (wizard) {
            spdlog::debug("[Wizard] Created successfully");
            wizard_active = true;

            // Set initial step (screen loader sets appropriate title)
            int initial_step = (args.wizard_step >= 1) ? args.wizard_step : 1;
            ui_wizard_navigate_to_step(initial_step);

            // Move keyboard to top layer so it appears above the full-screen wizard overlay
            lv_obj_t* keyboard = ui_keyboard_get_instance();
            if (keyboard) {
                lv_obj_move_foreground(keyboard);
                spdlog::debug("[Keyboard] Moved to foreground (above wizard overlay)");
            }
        } else {
            spdlog::error("[Wizard] Failed to create wizard");
        }
    }

    // Navigate to initial panel (if not showing wizard and panel was requested)
    if (!wizard_active && args.initial_panel >= 0) {
        spdlog::debug("[Nav] Navigating to initial panel: {}", args.initial_panel);
        ui_nav_set_active(static_cast<ui_panel_id_t>(args.initial_panel));
    }

    // Show requested overlay panels (motion, temp controls, etc.)
    if (!wizard_active) {
        if (args.overlays.motion) {
            if (auto* p = create_overlay_panel(screen, "motion_panel", "motion")) {
                overlay_panels.motion = p;
                get_global_motion_panel().setup(p, screen);
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.nozzle_temp) {
            if (auto* p = create_overlay_panel(screen, "nozzle_temp_panel", "nozzle temp")) {
                overlay_panels.nozzle_temp = p;
                temp_control_panel->setup_nozzle_panel(p, screen);
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.bed_temp) {
            if (auto* p = create_overlay_panel(screen, "bed_temp_panel", "bed temp")) {
                overlay_panels.bed_temp = p;
                temp_control_panel->setup_bed_panel(p, screen);
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.extrusion) {
            if (auto* p = create_overlay_panel(screen, "extrusion_panel", "extrusion")) {
                overlay_panels.extrusion = p;
                get_global_extrusion_panel().setup(p, screen);
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.fan) {
            spdlog::debug(
                "[Overlay] Opening fan control overlay as requested by command-line flag");
            auto& fan_panel = get_global_fan_panel();
            if (!fan_panel.are_subjects_initialized()) {
                fan_panel.init_subjects();
            }
            lv_obj_t* fan_obj = (lv_obj_t*)lv_xml_create(screen, "fan_panel", nullptr);
            if (fan_obj) {
                fan_panel.setup(fan_obj, screen);
                ui_nav_push_overlay(fan_obj);
            }
        }
        if (args.overlays.print_status && overlay_panels.print_status) {
            spdlog::debug(
                "[Overlay] Opening print status overlay as requested by command-line flag");
            ui_nav_push_overlay(overlay_panels.print_status);
        }
        if (args.overlays.bed_mesh) {
            if (auto* p = create_overlay_panel(screen, "bed_mesh_panel", "bed mesh")) {
                get_global_bed_mesh_panel().setup(p, screen);
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.zoffset) {
            if (auto* p = create_overlay_panel(screen, "calibration_zoffset_panel", "Z-offset")) {
                get_global_zoffset_cal_panel().setup(p, screen, moonraker_client.get());
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.pid) {
            if (auto* p = create_overlay_panel(screen, "calibration_pid_panel", "PID tuning")) {
                get_global_pid_cal_panel().setup(p, screen, moonraker_client.get());
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.screws_tilt) {
            if (auto* p = create_overlay_panel(screen, "screws_tilt_panel", "screws tilt")) {
                get_global_screws_tilt_panel().setup(p, screen, moonraker_client.get(),
                                                     moonraker_api.get());
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.input_shaper) {
            if (auto* p = create_overlay_panel(screen, "input_shaper_panel", "input shaper")) {
                get_global_input_shaper_panel().setup(p, screen, moonraker_client.get(),
                                                      moonraker_api.get());
                ui_nav_push_overlay(p);
            }
        }
        if (args.overlays.history_dashboard) {
            if (auto* p = create_overlay_panel(screen, "history_dashboard_panel", "history")) {
                get_global_history_dashboard_panel().setup(p, screen);
                ui_nav_push_overlay(p);
                get_global_history_dashboard_panel().on_activate();
            }
        }
        if (args.overlays.keypad) {
            spdlog::debug("[Overlay] Opening keypad modal as requested by command-line flag");
            ui_keypad_config_t keypad_config = {.initial_value = 0.0f,
                                                .min_value = 0.0f,
                                                .max_value = 300.0f,
                                                .title_label = "Test Keypad",
                                                .unit_label = "°C",
                                                .allow_decimal = true,
                                                .allow_negative = false,
                                                .callback = nullptr,
                                                .user_data = nullptr};
            ui_keypad_show(&keypad_config);
        }
        if (args.overlays.keyboard) {
            spdlog::debug("[Overlay] Showing keyboard as requested by command-line flag");
            ui_keyboard_show(nullptr);
        }
        if (args.overlays.step_test) {
            if (auto* p = create_overlay_panel(screen, "step_progress_test", "step progress")) {
                get_global_step_test_panel().setup(p, screen);
            }
        }
        if (args.overlays.test_panel) {
            if (auto* p = create_overlay_panel(screen, "test_panel", "test")) {
                get_global_test_panel().setup(p, screen);
            }
        }
        if (args.overlays.file_detail) {
            spdlog::debug(
                "[Nav] File detail view requested - navigating to print select panel first");
            ui_nav_set_active(UI_PANEL_PRINT_SELECT);
        }

        // Handle --select-file flag: auto-select a file in the print select panel
        if (g_runtime_config.select_file != nullptr) {
            spdlog::info("--select-file flag: Will auto-select file '{}'",
                         g_runtime_config.select_file);
            ui_nav_set_active(UI_PANEL_PRINT_SELECT);
            // Set pending selection - will trigger when file list is loaded
            auto* print_panel = get_print_select_panel(get_printer_state(), moonraker_api.get());
            if (print_panel) {
                print_panel->set_pending_file_selection(g_runtime_config.select_file);
            }
        }
    }

    // Create G-code test panel if requested (independent of wizard state)
    if (args.overlays.gcode_test) {
        spdlog::debug("[GCode Test] Creating panel");
        lv_obj_t* gcode_test =
            ui_panel_gcode_test_create(screen); // Uses deprecated wrapper (creates + setups)
        if (gcode_test) {
            spdlog::debug("[GCode Test] Panel created successfully");
        } else {
            spdlog::error("[GCode Test] Failed to create panel");
        }
    }

    // Create glyphs panel if requested (independent of wizard state)
    if (args.overlays.glyphs) {
        spdlog::debug("[Glyphs] Creating panel");
        lv_obj_t* glyphs_panel = ui_panel_glyphs_create(screen);
        if (glyphs_panel) {
            spdlog::debug("[Glyphs] Panel created successfully");
        } else {
            spdlog::error("[Glyphs] Failed to create panel");
        }
    }

    // Create gradient test panel if requested (independent of wizard state)
    if (args.overlays.gradient_test) {
        create_overlay_panel(screen, "gradient_test_panel", "gradient test");
    }

    // Open AMS panel if requested via -p ams
    if (args.overlays.ams) {
        spdlog::debug("[Overlay] Opening AMS panel as requested by command-line flag");
        auto& ams_panel = get_global_ams_panel();
        if (!ams_panel.are_subjects_initialized()) {
            ams_panel.init_subjects();
        }
        lv_obj_t* panel_obj = ams_panel.get_panel();
        if (panel_obj) {
            ui_nav_push_overlay(panel_obj);
        }
    }

    // Connect to Moonraker (only if not in wizard and we have saved config OR CLI override)
    // Wizard will handle its own connection test
    std::string saved_host = config->get<std::string>(config->df() + "moonraker_host", "");
    bool has_cli_url = !args.moonraker_url.empty();
    if (!args.force_wizard &&
        (has_cli_url || (!config->is_wizard_required() && !saved_host.empty()))) {
        std::string moonraker_url;
        std::string http_base_url;

        if (has_cli_url) {
            // Use CLI-provided URL (already normalized to ws://host:port/websocket)
            moonraker_url = args.moonraker_url;
            // Extract host:port for HTTP base URL
            // ws://host:port/websocket -> http://host:port
            std::string host_port = moonraker_url.substr(5); // Skip "ws://"
            auto ws_pos = host_port.find("/websocket");
            if (ws_pos != std::string::npos) {
                host_port = host_port.substr(0, ws_pos);
            }
            http_base_url = "http://" + host_port;
            spdlog::info("[Moonraker Client] Using CLI-provided URL: {}", moonraker_url);
        } else {
            // Build WebSocket URL from config
            moonraker_url =
                "ws://" + config->get<std::string>(config->df() + "moonraker_host") + ":" +
                std::to_string(config->get<int>(config->df() + "moonraker_port")) + "/websocket";
            http_base_url = "http://" + config->get<std::string>(config->df() + "moonraker_host") +
                            ":" + std::to_string(config->get<int>(config->df() + "moonraker_port"));
        }

        // Set HTTP base URL for file transfers
        moonraker_api->set_http_base_url(http_base_url);

        // Register EARLY hardware discovery callback - fires right after printer.objects.list
        // This allows AMS/MMU backends to initialize BEFORE the subscription response arrives,
        // so they can receive initial state naturally without needing a separate query
        moonraker_client->set_on_hardware_discovered([](const PrinterCapabilities& caps) {
            // Initialize AMS/MMU backend if detected (AFC/Box Turtle, Happy Hare, etc.)
            // This must happen EARLY so the backend is ready to receive initial state
            // from the printer.objects.subscribe response
            AmsState::instance().init_backend_from_capabilities(caps, moonraker_api.get(),
                                                                moonraker_client.get());
        });

        // Register LATE discovery callback - fires after subscription response is processed
        // Use this for state updates that depend on initial subscription data
        moonraker_client->set_on_discovery_complete([](const PrinterCapabilities& caps) {
            // Update PrinterState with discovered capabilities for reactive UI bindings
            get_printer_state().set_printer_capabilities(caps);

            // Initialize multi-fan tracking from discovered fan objects
            get_printer_state().init_fans(moonraker_client->get_fans());

            // Update version info from client (for Settings About section)
            get_printer_state().set_klipper_version(moonraker_client->get_software_version());
            get_printer_state().set_moonraker_version(moonraker_client->get_moonraker_version());
        });

        // Connect to Moonraker
        spdlog::debug("[Moonraker Client] Connecting to {}", moonraker_url);
        int connect_result = moonraker_client->connect(
            moonraker_url.c_str(),
            []() {
                spdlog::info("[Moonraker Client] ✓ Connected to Moonraker");
                // State change callback will handle updating PrinterState

                // Start auto-discovery (must be called AFTER connection is established)
                moonraker_client->discover_printer(
                    []() { spdlog::info("[Moonraker Client] ✓ Printer auto-discovery complete"); });
            },
            []() {
                spdlog::warn("[Moonraker Client] ✗ Disconnected from Moonraker");
                // State change callback will handle updating PrinterState
            });

        if (connect_result != 0) {
            spdlog::error("[Moonraker Client] Failed to initiate connection (code {})",
                          connect_result);
            // State change callback will handle updating PrinterState
        }
    }

    // Auto-screenshot timer (configurable delay after UI creation)
    uint32_t screenshot_time =
        helix_get_ticks() + (static_cast<uint32_t>(args.screenshot_delay_sec) * 1000U);
    bool screenshot_taken = false;

    // Auto-quit timeout timer (if enabled)
    uint32_t start_time = helix_get_ticks();
    uint32_t timeout_ms = static_cast<uint32_t>(args.timeout_sec) * 1000U;

    // Request timeout check timer (check every 2 seconds)
    uint32_t last_timeout_check = helix_get_ticks();
    uint32_t timeout_check_interval = static_cast<uint32_t>(
        config->get<int>(config->df() + "moonraker_timeout_check_interval_ms", 2000));

    // Main event loop - LVGL handles display events internally via lv_timer_handler()
    // Loop continues while display exists and quit not requested
    while (lv_display_get_next(NULL) && !app_quit_requested()) {
#ifdef HELIX_DISPLAY_SDL
        // Desktop keyboard shortcuts (SDL only)
        // Check for Cmd+Q (macOS) or Win+Q (Windows) to quit
        SDL_Keymod modifiers = SDL_GetModState();
        const Uint8* keyboard_state = SDL_GetKeyboardState(NULL);
        if ((modifiers & KMOD_GUI) && keyboard_state[SDL_SCANCODE_Q]) {
            spdlog::info("[Input] Cmd+Q/Win+Q pressed - exiting...");
            break;
        }

        // M key toggle for memory stats overlay (with debounce)
        static bool m_key_was_pressed = false;
        bool m_key_pressed = keyboard_state[SDL_SCANCODE_M] != 0;
        if (m_key_pressed && !m_key_was_pressed) {
            MemoryStatsOverlay::instance().toggle();
        }
        m_key_was_pressed = m_key_pressed;

        // S key for screenshot (with debounce)
        static bool s_key_was_pressed = false;
        bool s_key_pressed = keyboard_state[SDL_SCANCODE_S] != 0;
        if (s_key_pressed && !s_key_was_pressed) {
            spdlog::info("[Input] S key pressed - taking screenshot...");
            save_screenshot();
        }
        s_key_was_pressed = s_key_pressed;
#endif

        // Auto-screenshot after configured delay (only if enabled)
        if (args.screenshot_enabled && !screenshot_taken && helix_get_ticks() >= screenshot_time) {
            save_screenshot();
            screenshot_taken = true;
        }

        // Auto-quit after timeout (if enabled)
        if (args.timeout_sec > 0 && (helix_get_ticks() - start_time) >= timeout_ms) {
            spdlog::info("[main] Timeout reached ({} seconds) - exiting...", args.timeout_sec);
            break;
        }

        // Check for request timeouts (using configured interval)
        uint32_t current_time = helix_get_ticks();
        if (current_time - last_timeout_check >= timeout_check_interval) {
            moonraker_client->process_timeouts();
            last_timeout_check = current_time;
        }

        // Process queued Moonraker notifications on main thread (LVGL thread-safety)
        {
            std::lock_guard<std::mutex> lock(notification_mutex);
            while (!notification_queue.empty()) {
                json notification = notification_queue.front();
                notification_queue.pop();

                // Check for connection state change (queued from state_change_callback)
                if (notification.contains("_connection_state")) {
                    int new_state = notification["new_state"].get<int>();
                    static const char* messages[] = {
                        "Disconnected",     // DISCONNECTED
                        "Connecting...",    // CONNECTING
                        "Connected",        // CONNECTED
                        "Reconnecting...",  // RECONNECTING
                        "Connection Failed" // FAILED
                    };
                    spdlog::trace("[main] Processing queued connection state change: {}",
                                  messages[new_state]);
                    get_printer_state().set_printer_connection_state(new_state,
                                                                     messages[new_state]);
                } else {
                    // Regular Moonraker notification
                    get_printer_state().update_from_notification(notification);
                }
            }
        }

        // Check display sleep (inactivity timeout)
        SettingsManager::instance().check_display_sleep();

        // Run LVGL tasks - handles display events and processes input
        lv_timer_handler();
        fflush(stdout);
        helix_delay(5); // Small delay to prevent 100% CPU usage
    }

    // Cleanup
    spdlog::info("[main] Shutting down...");

    // Clear app_globals references before destroying instances
    set_moonraker_api(nullptr);
    set_moonraker_client(nullptr);

    // Reset unique_ptrs explicitly in correct order (API before client)
    moonraker_api.reset();
    moonraker_client.reset();

    // Clean up USB manager explicitly BEFORE spdlog shutdown.
    // UsbBackendMock::stop() logs, and we need spdlog alive for that.
    usb_manager.reset();

    // Clean up wizard WiFi step explicitly BEFORE lv_deinit and spdlog shutdown.
    // This owns WiFiManager and EthernetManager which have background threads.
    // If destroyed during static destruction, those threads may access destroyed mutexes.
    destroy_wizard_wifi_step();

    lv_deinit(); // LVGL handles SDL cleanup internally

    // Shutdown spdlog BEFORE static destruction begins.
    // Many static unique_ptr<Panel> objects have destructors that may log.
    // If spdlog is destroyed first during static destruction, logging crashes.
    // By calling shutdown() here, we flush and drop all sinks, making any
    // subsequent log calls safe no-ops.
    spdlog::shutdown();

    return 0;
}
