// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file helix_watchdog.cpp
 * @brief Ultra-stable watchdog supervisor for helix-screen crash recovery
 *
 * This is a lightweight supervisor process that monitors helix-screen for
 * crashes and displays a recovery dialog with user choices:
 * - Restart App: Fork a new helix-screen process
 * - Restart System: Reboot the system
 *
 * Design goals (same philosophy as helix-splash):
 * - Minimal dependencies (LVGL + display backend + spdlog)
 * - No networking (no libhv, no Moonraker)
 * - Direct LVGL API calls for crash dialog (no XML/theme system)
 * - Ultra-stable: must not crash when the main app crashes
 *
 * Only built and used on embedded Linux targets (DRM/fbdev).
 * Desktop developers use terminal output for crash debugging.
 */

#include "ui_fonts.h"

#include "backlight_backend.h"
#include "config.h"
#include "display_backend.h"
#ifdef HELIX_DISPLAY_DRM
#include "display_backend_drm.h"
#endif
#ifdef HELIX_DISPLAY_FBDEV
#include "display_backend_fbdev.h"
#endif
#include "logging_init.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <lvgl.h>
#include <regex>
#include <signal.h>
#include <string>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// =============================================================================
// Constants
// =============================================================================

static constexpr int DEFAULT_WIDTH = 800;
static constexpr int DEFAULT_HEIGHT = 480;
static constexpr int FRAME_DELAY_US = 16000; // ~60 FPS
static constexpr int DEFAULT_AUTO_RESTART_SEC = 30;

// UI Colors (dark theme, matches main app)
static constexpr uint32_t BG_COLOR_DARK = 0x121212;
static constexpr uint32_t CONTAINER_BG = 0x1E1E1E;
static constexpr uint32_t BORDER_ERROR = 0xF44336;
static constexpr uint32_t BUTTON_PRIMARY = 0x2196F3; // Blue - restart app
static constexpr uint32_t BUTTON_DANGER = 0xF44336;  // Red - restart system
static constexpr uint32_t TEXT_PRIMARY = 0xFFFFFF;
static constexpr uint32_t TEXT_SECONDARY = 0xAAAAAA;
static constexpr uint32_t TEXT_MUTED = 0x888888;

// =============================================================================
// Global State
// =============================================================================

// Signal handling
static volatile sig_atomic_t g_quit = 0;

// Dialog choice from button press
enum class DialogChoice { NONE, RESTART_APP, RESTART_SYSTEM };
static volatile DialogChoice g_dialog_choice = DialogChoice::NONE;

// Countdown state
static int g_countdown_seconds = 0;
static lv_obj_t* g_countdown_label = nullptr;

// Crash information
struct CrashInfo {
    int exit_code = 0;
    int signal_num = 0;
    bool was_signaled = false;
    std::string signal_name;
    time_t crash_time = 0;
};

// =============================================================================
// Signal Handling
// =============================================================================

static void signal_handler(int sig) {
    (void)sig;
    g_quit = 1;
}

static void setup_signal_handlers() {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    // Ignore SIGCHLD - we use waitpid explicitly
    signal(SIGCHLD, SIG_DFL);
}

// =============================================================================
// Configuration Reading
// =============================================================================

/**
 * @brief Read auto_restart_sec from helixconfig.json
 * @return Timeout in seconds (0 = disabled), or default on failure
 */
static int read_auto_restart_timeout() {
    const char* paths[] = {"config/helixconfig.json", "helixconfig.json",
                           "/opt/helixscreen/helixconfig.json"};

    for (const char* path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Look for "watchdog" section with "auto_restart_sec"
        // Simple regex parsing to avoid JSON library dependency
        std::regex timeout_regex(R"("auto_restart_sec"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(content, match, timeout_regex) && match.size() > 1) {
            int timeout = std::stoi(match[1].str());
            spdlog::debug("[Watchdog] Read auto_restart_sec={} from {}", timeout, path);
            return timeout;
        }
    }

    return DEFAULT_AUTO_RESTART_SEC;
}

/**
 * @brief Read brightness from helixconfig.json (same as splash)
 */
static int read_config_brightness(int default_value = 100) {
    const char* paths[] = {"config/helixconfig.json", "helixconfig.json",
                           "/opt/helixscreen/helixconfig.json"};

    for (const char* path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        std::regex brightness_regex(R"("brightness"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(content, match, brightness_regex) && match.size() > 1) {
            int brightness = std::stoi(match[1].str());
            if (brightness < 10)
                brightness = 10;
            if (brightness > 100)
                brightness = 100;
            return brightness;
        }
    }

    return default_value;
}

// =============================================================================
// Command Line Parsing
// =============================================================================

struct WatchdogArgs {
    int width = 0; // 0 = auto-detect from display hardware
    int height = 0;
    int rotation = 0;          // Display rotation in degrees (0, 90, 180, 270)
    std::string splash_binary; // Optional: --splash-bin=<path>
    pid_t splash_pid = 0;      // Optional: --splash-pid=N (externally started splash)
    std::string child_binary;
    std::vector<std::string> child_args;
};

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [-w width] [-h height] [--splash-bin=<path>] [--splash-pid=N] -- "
            "<helix-screen> [args...]\n",
            program);
    fprintf(stderr, "  -w <width>          Screen width (default: %d)\n", DEFAULT_WIDTH);
    fprintf(stderr, "  -h <height>         Screen height (default: %d)\n", DEFAULT_HEIGHT);
    fprintf(stderr,
            "  -r <degrees>        Display rotation: 0, 90, 180, 270 (default: from config)\n");
    fprintf(stderr, "  --splash-bin=<path> Path to splash screen binary (optional)\n");
    fprintf(stderr, "  --splash-pid=<pid>  PID of externally started splash process (optional)\n");
    fprintf(stderr, "  --                  Separator before child binary and args\n");
}

static bool parse_args(int argc, char** argv, WatchdogArgs& args) {
    bool after_separator = false;

    for (int i = 1; i < argc; i++) {
        if (after_separator) {
            if (args.child_binary.empty()) {
                args.child_binary = argv[i];
            } else {
                args.child_args.push_back(argv[i]);
            }
        } else if (strcmp(argv[i], "--") == 0) {
            after_separator = true;
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            args.width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            args.height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            args.rotation = atoi(argv[++i]);
        } else if (strncmp(argv[i], "--splash-bin=", 13) == 0) {
            args.splash_binary = argv[i] + 13;
        } else if (strncmp(argv[i], "--splash-pid=", 13) == 0) {
            args.splash_pid = static_cast<pid_t>(atoi(argv[i] + 13));
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }

    if (args.child_binary.empty()) {
        fprintf(stderr, "Error: No child binary specified after '--'\n");
        print_usage(argv[0]);
        return false;
    }

    return true;
}

// =============================================================================
// Splash Process Management
// =============================================================================

// Global splash PID for cleanup
static volatile pid_t g_splash_pid = 0;

/**
 * @brief Check if the display backend will use DRM
 *
 * The external splash process uses fbdev, which conflicts with DRM — both
 * backends fight over the same display hardware, causing mmap failures on
 * DRM dumb buffer allocation. When DRM will be used (forced or auto-detected),
 * skip the external splash and let the main app show its internal LVGL splash.
 */
static bool would_use_drm() {
    const char* env = std::getenv("HELIX_DISPLAY_BACKEND");
    if (env != nullptr) {
        return strcmp(env, "drm") == 0;
    }
    // Auto mode: DRM is tried first in create_auto(), check if it's available
#ifdef HELIX_DISPLAY_DRM
    DisplayBackendDRM drm;
    return drm.is_available();
#else
    return false;
#endif
}

/**
 * @brief Start splash screen process
 * @return PID of splash process, or 0 if not started
 */
static pid_t start_splash_process(const WatchdogArgs& args) {
    if (args.splash_binary.empty()) {
        return 0;
    }

    // Check if binary exists
    if (access(args.splash_binary.c_str(), X_OK) != 0) {
        spdlog::warn("[Watchdog] Splash binary not found or not executable: {}",
                     args.splash_binary);
        return 0;
    }

    pid_t pid = fork();

    if (pid < 0) {
        spdlog::error("[Watchdog] Failed to fork splash process: {}", strerror(errno));
        return 0;
    }

    if (pid == 0) {
        // Child process: exec splash with rotation if configured
        if (args.rotation != 0) {
            std::string rot_str = std::to_string(args.rotation);
            execl(args.splash_binary.c_str(), "helix-splash", "-r", rot_str.c_str(), nullptr);
        } else {
            execl(args.splash_binary.c_str(), "helix-splash", nullptr);
        }

        // If exec fails
        fprintf(stderr, "[Watchdog] Failed to exec splash: %s\n", strerror(errno));
        _exit(127);
    }

    // Parent: splash started successfully
    spdlog::info("[Watchdog] Started splash process (PID {})", pid);
    g_splash_pid = pid;
    return pid;
}

/**
 * @brief Clean up splash process if still running
 */
static void cleanup_splash(pid_t splash_pid) {
    if (splash_pid <= 0) {
        return;
    }

    // Check if still running
    if (kill(splash_pid, 0) == 0) {
        spdlog::debug("[Watchdog] Cleaning up splash process (PID {})", splash_pid);
        kill(splash_pid, SIGTERM);
        // Non-blocking wait - don't hang if splash is stuck
        int status;
        pid_t result = waitpid(splash_pid, &status, WNOHANG);
        if (result == 0) {
            // Still running after SIGTERM, give it a moment
            usleep(100000); // 100ms
            waitpid(splash_pid, &status, WNOHANG);
        }
    }

    if (g_splash_pid == splash_pid) {
        g_splash_pid = 0;
    }
}

// =============================================================================
// Process Management
// =============================================================================

/**
 * @brief Fork and exec helix-screen, wait for it to exit
 * @param args Watchdog arguments
 * @param splash_pid PID of splash process to pass to helix-screen (0 if none)
 * @return CrashInfo with exit status
 */
static CrashInfo run_child_process(const WatchdogArgs& args, pid_t splash_pid) {
    CrashInfo crash = {};

    // Build argv for execv - need to own the strings for splash_pid arg
    std::vector<std::string> arg_strings;
    arg_strings.push_back(args.child_binary);

    // Add splash PID argument if splash is running
    if (splash_pid > 0) {
        arg_strings.push_back("--splash-pid=" + std::to_string(splash_pid));
    }

    // Pass rotation to child if configured
    if (args.rotation != 0) {
        arg_strings.push_back("--rotate=" + std::to_string(args.rotation));
    }

    // Add remaining child args, but skip any --splash-pid from the original
    // launcher invocation — the watchdog manages splash PIDs itself and the
    // original PID is stale on restart.
    for (const auto& arg : args.child_args) {
        if (arg.rfind("--splash-pid=", 0) == 0) {
            continue;
        }
        arg_strings.push_back(arg);
    }

    // Build char* argv from owned strings
    std::vector<char*> child_argv;
    for (auto& arg : arg_strings) {
        child_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    child_argv.push_back(nullptr);

    spdlog::info("[Watchdog] Launching: {}", args.child_binary);
    if (splash_pid > 0) {
        spdlog::debug("[Watchdog] Passing splash PID {} to child", splash_pid);
    }

    pid_t child_pid = fork();

    if (child_pid < 0) {
        spdlog::error("[Watchdog] fork() failed: {}", strerror(errno));
        crash.was_signaled = false;
        crash.exit_code = 127;
        crash.crash_time = time(nullptr);
        return crash;
    }

    if (child_pid == 0) {
        // Child process: set supervisor env var so app knows not to fork on restart
        setenv("HELIX_SUPERVISED", "1", 1);

        // exec helix-screen
        execv(args.child_binary.c_str(), child_argv.data());
        // If we get here, exec failed
        fprintf(stderr, "[Watchdog] execv failed: %s\n", strerror(errno));
        _exit(127);
    }

    // Parent: wait for child with proper EINTR handling
    // Use waitpid(-1) to reap any child, including splash process
    int status;
    while (true) {
        pid_t result = waitpid(-1, &status, 0);

        if (result == child_pid) {
            // helix-screen exited
            break;
        }

        if (result == splash_pid) {
            // Splash exited (reaped) - this is expected, continue waiting for helix-screen
            spdlog::debug("[Watchdog] Splash process reaped (PID {})", splash_pid);
            continue;
        }

        if (result < 0) {
            if (errno == EINTR) {
                // Signal interrupted waitpid, check if we should quit
                if (g_quit) {
                    // Watchdog is shutting down, kill child
                    spdlog::info("[Watchdog] Shutting down, terminating child");
                    kill(child_pid, SIGTERM);
                    waitpid(child_pid, &status, 0);
                    crash.exit_code = 0;
                    crash.was_signaled = false;
                    return crash;
                }
                continue;
            }
            if (errno == ECHILD) {
                // No more children - shouldn't happen but handle gracefully
                spdlog::warn("[Watchdog] No children to wait for");
                crash.exit_code = 0;
                crash.was_signaled = false;
                return crash;
            }
            // Actual error
            spdlog::error("[Watchdog] waitpid error: {}", strerror(errno));
            crash.exit_code = 127;
            crash.was_signaled = false;
            crash.crash_time = time(nullptr);
            return crash;
        }
    }

    crash.crash_time = time(nullptr);

    if (WIFEXITED(status)) {
        crash.exit_code = WEXITSTATUS(status);
        crash.was_signaled = false;
        spdlog::info("[Watchdog] Child exited with code {}", crash.exit_code);
    } else if (WIFSIGNALED(status)) {
        crash.signal_num = WTERMSIG(status);
        crash.was_signaled = true;
        crash.signal_name = strsignal(crash.signal_num);
        spdlog::warn("[Watchdog] Child killed by signal {} ({})", crash.signal_num,
                     crash.signal_name);
    }

    return crash;
}

// =============================================================================
// System Restart
// =============================================================================

/**
 * @brief Try a reboot method via fork+exec, return true if exec succeeded
 *
 * Uses fork so that exec failure doesn't replace the watchdog process,
 * allowing fallback to the next method.
 */
static bool try_reboot_exec(const char* desc, const char* path,
                            const std::vector<const char*>& argv) {
    spdlog::info("[Watchdog] Trying {}", desc);

    pid_t pid = fork();
    if (pid < 0) {
        spdlog::warn("[Watchdog] fork() failed for {}: {}", desc, strerror(errno));
        return false;
    }

    if (pid == 0) {
        // Child - exec the reboot command
        execv(path, const_cast<char* const*>(argv.data()));
        _exit(127); // exec failed
    }

    // Parent - wait briefly for the child
    int status = 0;
    int attempts = 0;
    while (attempts < 10) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                spdlog::info("[Watchdog] {} succeeded, system rebooting...", desc);
                // Give the system a moment to process the reboot request
                sleep(10);
                return true; // Shouldn't reach here if reboot worked
            }
            spdlog::warn("[Watchdog] {} failed (exit code {})", desc,
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            return false;
        }
        if (ret < 0 && errno != EINTR) {
            break;
        }
        usleep(200000); // 200ms
        attempts++;
    }

    // Still running after 2s - assume it's working (reboot in progress)
    spdlog::info("[Watchdog] {} still running, assuming reboot in progress...", desc);
    sleep(10);
    // If we're still here, it didn't work
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    return false;
}

/**
 * @brief Perform system restart using appropriate method
 *
 * Tries multiple reboot strategies in order:
 * 1. systemctl reboot (needs privileges or polkit)
 * 2. gdbus call to logind Reboot (works without privilege escalation)
 * 3. /sbin/reboot
 * 4. reboot() syscall (needs CAP_SYS_BOOT)
 */
[[noreturn]] static void perform_system_restart() {
    spdlog::info("[Watchdog] Initiating system restart");

    // Flush filesystems
    sync();

    std::error_code ec;
    if (std::filesystem::exists("/run/systemd/system", ec)) {
        // Try systemctl reboot (works if polkit allows it or running as root)
        try_reboot_exec("systemctl reboot", "/usr/bin/systemctl",
                        {"/usr/bin/systemctl", "reboot", nullptr});

        // Try D-Bus call to logind - works for unprivileged users on systems
        // with active sessions (no polkit prompt needed for interactive=true)
        try_reboot_exec("logind D-Bus Reboot", "/usr/bin/gdbus",
                        {"/usr/bin/gdbus", "call", "--system", "--dest", "org.freedesktop.login1",
                         "--object-path", "/org/freedesktop/login1", "--method",
                         "org.freedesktop.login1.Manager.Reboot", "true", nullptr});
    }

    // Fallback to /sbin/reboot
    try_reboot_exec("/sbin/reboot", "/sbin/reboot", {"/sbin/reboot", nullptr});

    // Last resort: direct syscall (requires CAP_SYS_BOOT)
    spdlog::warn("[Watchdog] All exec methods failed, trying reboot syscall");
    reboot(RB_AUTOBOOT);

    // Should never reach here
    spdlog::error("[Watchdog] All reboot methods failed");
    _exit(1);
}

// =============================================================================
// Crash Dialog UI
// =============================================================================

// Portable timing (same as ui_fatal_error.cpp)
static uint32_t get_ticks_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Button callbacks
static void on_restart_app_clicked(lv_event_t* /*e*/) {
    g_dialog_choice = DialogChoice::RESTART_APP;
}

static void on_restart_system_clicked(lv_event_t* /*e*/) {
    g_dialog_choice = DialogChoice::RESTART_SYSTEM;
}

// Cancel countdown on any touch
static void on_screen_pressed(lv_event_t* /*e*/) {
    if (g_countdown_seconds > 0 && g_countdown_label) {
        g_countdown_seconds = 0;
        lv_obj_add_flag(g_countdown_label, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[Watchdog] Countdown cancelled by touch");
    }
}

/**
 * @brief Create a styled button
 */
static lv_obj_t* create_button(lv_obj_t* parent, const char* text, uint32_t color,
                               lv_event_cb_t callback) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 180, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &noto_sans_bold_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_center(label);

    return btn;
}

/**
 * @brief Create the crash dialog UI
 */
static void create_crash_dialog(lv_obj_t* screen, int /*width*/, int /*height*/,
                                const CrashInfo& crash, int auto_restart_sec) {
    // Dark background
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Touch anywhere cancels countdown
    lv_obj_add_event_cb(screen, on_screen_pressed, LV_EVENT_PRESSED, nullptr);

    // Main container
    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_PCT(85), LV_SIZE_CONTENT);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(CONTAINER_BG), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 2, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(BORDER_ERROR), 0);
    lv_obj_set_style_radius(container, 12, 0);
    lv_obj_set_style_pad_all(container, 24, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Warning icon
    lv_obj_t* icon = lv_label_create(container);
    lv_label_set_text(icon, ICON_TRIANGLE_EXCLAMATION);
    lv_obj_set_style_text_font(icon, &mdi_icons_64, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(BORDER_ERROR), 0);

    // Title
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "HelixScreen Crashed");
    lv_obj_set_style_text_font(title, &noto_sans_bold_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(TEXT_PRIMARY), 0);
    lv_obj_set_style_pad_top(title, 16, 0);

    // Crash details
    lv_obj_t* details = lv_label_create(container);
    if (crash.was_signaled) {
        lv_label_set_text_fmt(details, "Signal: %d (%s)", crash.signal_num,
                              crash.signal_name.c_str());
    } else {
        lv_label_set_text_fmt(details, "Exit code: %d", crash.exit_code);
    }
    lv_obj_set_style_text_font(details, &noto_sans_14, 0);
    lv_obj_set_style_text_color(details, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_pad_top(details, 8, 0);

    // Countdown timer (hidden if auto_restart_sec == 0)
    g_countdown_label = lv_label_create(container);
    if (auto_restart_sec > 0) {
        g_countdown_seconds = auto_restart_sec;
        lv_label_set_text_fmt(g_countdown_label, "Auto-restart in %d seconds...",
                              g_countdown_seconds);
        lv_obj_set_style_text_font(g_countdown_label, &noto_sans_14, 0);
        lv_obj_set_style_text_color(g_countdown_label, lv_color_hex(TEXT_MUTED), 0);
        lv_obj_set_style_pad_top(g_countdown_label, 12, 0);
    } else {
        lv_obj_add_flag(g_countdown_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Button container
    lv_obj_t* btn_container = lv_obj_create(container);
    lv_obj_set_size(btn_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_style_pad_all(btn_container, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(btn_container, 24, 0);
    lv_obj_set_style_pad_column(btn_container, 24, 0);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

    // Restart App button (primary action)
    create_button(btn_container, "Restart App", BUTTON_PRIMARY, on_restart_app_clicked);

    // Restart System button (danger action)
    create_button(btn_container, "Restart System", BUTTON_DANGER, on_restart_system_clicked);
}

/**
 * @brief Show crash dialog and wait for user choice
 * @return User's choice
 */
static DialogChoice show_crash_dialog(int width, int height, int rotation, const CrashInfo& crash) {
    int auto_restart_sec = read_auto_restart_timeout();

    spdlog::info("[Watchdog] Showing crash dialog (auto_restart={}s)", auto_restart_sec);

    // Initialize config so touch calibration data is available
    helix::Config::get_instance()->init("config/helixconfig.json");

    // Initialize LVGL
    lv_init();

    // Create display backend (with fbdev fallback for crash resilience)
    auto backend = DisplayBackend::create();
    if (!backend) {
        spdlog::error("[Watchdog] Failed to create display backend");
        return DialogChoice::RESTART_APP;
    }

    lv_display_t* display = backend->create_display(width, height);
#ifdef HELIX_DISPLAY_FBDEV
    if (!display) {
        // DRM may fail (e.g., mmap contention). Try fbdev as fallback so the
        // crash recovery screen is visible instead of looping silently.
        spdlog::warn("[Watchdog] Primary display failed, trying fbdev fallback");
        backend = std::make_unique<DisplayBackendFbdev>();
        if (backend->is_available()) {
            display = backend->create_display(width, height);
            if (display) {
                spdlog::info("[Watchdog] Crash dialog using fbdev fallback");
            }
        }
    }
#endif
    if (!display) {
        spdlog::error("[Watchdog] Failed to create display");
        return DialogChoice::RESTART_APP;
    }

    // Apply rotation to crash dialog display
    if (rotation != 0) {
        lv_display_set_rotation(display, degrees_to_lv_rotation(rotation));
        spdlog::info("[Watchdog] Crash dialog rotated {}°", rotation);
    }

    // Turn on backlight
    auto backlight = BacklightBackend::create();
    if (backlight && backlight->is_available()) {
        int brightness = read_config_brightness(100);
        backlight->set_brightness(brightness);
    }

    // Create touch input
    backend->create_input_pointer();

    // Create dialog UI
    lv_obj_t* screen = lv_screen_active();
    create_crash_dialog(screen, width, height, crash, auto_restart_sec);

    // Event loop with countdown
    g_dialog_choice = DialogChoice::NONE;
    uint32_t last_second = get_ticks_ms() / 1000;

    while (g_dialog_choice == DialogChoice::NONE && !g_quit) {
        lv_timer_handler();
        usleep(FRAME_DELAY_US);

        // Update countdown every second
        uint32_t current_second = get_ticks_ms() / 1000;
        if (g_countdown_seconds > 0 && current_second != last_second) {
            last_second = current_second;
            g_countdown_seconds--;

            if (g_countdown_seconds > 0) {
                lv_label_set_text_fmt(g_countdown_label, "Auto-restart in %d seconds...",
                                      g_countdown_seconds);
            } else {
                // Countdown reached zero - auto restart
                spdlog::info("[Watchdog] Countdown expired, auto-restarting app");
                g_dialog_choice = DialogChoice::RESTART_APP;
            }
        }
    }

    DialogChoice result = g_dialog_choice;

    // Cleanup LVGL
    lv_deinit();

    spdlog::info("[Watchdog] User choice: {}",
                 result == DialogChoice::RESTART_SYSTEM ? "restart system" : "restart app");

    return result == DialogChoice::NONE ? DialogChoice::RESTART_APP : result;
}

// =============================================================================
// Main Watchdog Loop
// =============================================================================

static int run_watchdog(const WatchdogArgs& args) {
    spdlog::info("[Watchdog] Starting watchdog supervisor");
    spdlog::info("[Watchdog] Child binary: {}", args.child_binary);
    if (!args.splash_binary.empty()) {
        spdlog::info("[Watchdog] Splash binary: {}", args.splash_binary);
    }
    if (args.splash_pid > 0) {
        spdlog::info("[Watchdog] External splash PID: {}", args.splash_pid);
    }

    bool first_launch = true;

    while (!g_quit) {
        // Start or adopt splash screen before launching helix-screen.
        // On first launch, prefer an externally-started splash (args.splash_pid)
        // over starting a new one. On restarts, always start fresh (the original
        // PID is stale).
        //
        // Skip the external splash entirely when DRM will be used — the fbdev-based
        // splash conflicts with DRM (both fight over display hardware, causing mmap
        // failures). The main app will show its internal LVGL splash instead.
        pid_t splash_pid = 0;
        bool use_drm = would_use_drm();
        if (use_drm) {
            spdlog::info("[Watchdog] DRM backend detected, skipping external splash");
        } else if (first_launch && args.splash_pid > 0) {
            // Adopt externally-started splash (e.g. from init script)
            splash_pid = args.splash_pid;
            g_splash_pid = splash_pid;
            spdlog::info("[Watchdog] Adopting external splash (PID {})", splash_pid);
        } else if (first_launch || !args.splash_binary.empty()) {
            splash_pid = start_splash_process(args);
        }
        first_launch = false;

        // Launch and monitor child process
        CrashInfo crash = run_child_process(args, splash_pid);

        // Clean up splash if still running (safety net)
        cleanup_splash(splash_pid);

        // Check if we're shutting down
        if (g_quit) {
            spdlog::info("[Watchdog] Shutting down");
            break;
        }

        // Normal exit (code 0) - just restart silently
        if (!crash.was_signaled && crash.exit_code == 0) {
            spdlog::info("[Watchdog] Child exited normally, restarting");
            continue;
        }

        // Graceful shutdown signals (SIGTERM, SIGINT) - exit watchdog, don't treat as crash
        // These are intentional termination requests (systemctl stop, kill, Ctrl+C)
        if (crash.was_signaled && (crash.signal_num == SIGTERM || crash.signal_num == SIGINT)) {
            spdlog::info("[Watchdog] Child received {} ({}), shutting down gracefully",
                         crash.signal_num, crash.signal_name);
            break;
        }

        // Crash detected - show recovery dialog (no splash during dialog)
        spdlog::warn("[Watchdog] Crash detected, showing recovery dialog");

        DialogChoice choice = show_crash_dialog(args.width, args.height, args.rotation, crash);

        if (choice == DialogChoice::RESTART_SYSTEM) {
            perform_system_restart();
            // Never returns
        }

        // RESTART_APP: loop continues, will fork new child with splash
        spdlog::info("[Watchdog] Restarting helix-screen");
    }

    // Final cleanup
    cleanup_splash(g_splash_pid);

    return 0;
}

// =============================================================================
// Main Entry Point
// =============================================================================

int main(int argc, char** argv) {
    // Set up signal handlers
    setup_signal_handlers();

    // Parse command line arguments
    WatchdogArgs args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    // Initialize logging (auto-detect journal/syslog/console)
    helix::logging::LogConfig log_config;
    log_config.level = spdlog::level::info;
    log_config.target = helix::logging::LogTarget::Auto;
    log_config.enable_console = true;
    helix::logging::init(log_config);

    // Auto-detect resolution from display hardware if not overridden via CLI
    if (args.width == 0 || args.height == 0) {
        auto backend = DisplayBackend::create();
        if (backend) {
            auto res = backend->detect_resolution();
            if (res.valid) {
                args.width = res.width;
                args.height = res.height;
                spdlog::info("[Watchdog] Auto-detected resolution: {}x{}", args.width, args.height);
            }
        }
        // Fall back to defaults if detection failed
        if (args.width == 0 || args.height == 0) {
            args.width = DEFAULT_WIDTH;
            args.height = DEFAULT_HEIGHT;
            spdlog::info("[Watchdog] Using default resolution: {}x{}", args.width, args.height);
        }
    }

    // Read display rotation from config if not set via CLI
    if (args.rotation == 0) {
        args.rotation = read_config_rotation(0);
    }
    if (args.rotation != 0) {
        spdlog::info("[Watchdog] Display rotation: {}°", args.rotation);
    }

    // Run the watchdog
    return run_watchdog(args);
}
