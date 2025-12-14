// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file cli_args.h
 * @brief Command-line argument parsing for HelixScreen
 *
 * Provides a clean interface for CLI parsing that replaces 27+ out-parameters
 * with a single structured result.
 */

#include "ui_nav.h" // For ui_panel_id_t

#include <optional>
#include <string>

namespace helix {

/**
 * @brief Screen size presets
 */
enum class ScreenSize { TINY, SMALL, MEDIUM, LARGE };

/**
 * @brief Overlay panel flags (grouped for clarity)
 */
struct OverlayFlags {
    bool motion = false;
    bool nozzle_temp = false;
    bool bed_temp = false;
    bool extrusion = false;
    bool fan = false;
    bool print_status = false;
    bool bed_mesh = false;
    bool zoffset = false;
    bool pid = false;
    bool screws_tilt = false;
    bool input_shaper = false;
    bool file_detail = false;
    bool keypad = false;
    bool keyboard = false;
    bool step_test = false;
    bool test_panel = false;
    bool gcode_test = false;
    bool glyphs = false;
    bool gradient_test = false;
    bool history_dashboard = false;
    bool ams = false;
    bool spoolman = false;

    /** @brief Check if any overlay requiring Moonraker data is requested */
    bool needs_moonraker() const {
        return motion || nozzle_temp || bed_temp || extrusion || fan || print_status || bed_mesh ||
               zoffset || pid || screws_tilt || input_shaper || file_detail || history_dashboard ||
               spoolman;
    }
};

/**
 * @brief Parsed command-line arguments
 *
 * Replaces 27+ function out-parameters with a clean struct.
 */
struct CliArgs {
    // Screen settings
    ScreenSize screen_size = ScreenSize::SMALL;
    int dpi = -1;         // -1 = use default
    int display_num = -1; // -1 = not set
    int x_pos = -1;       // -1 = not set
    int y_pos = -1;       // -1 = not set

    // Panel navigation
    int initial_panel = -1; // -1 = auto-select based on screen size
    bool panel_requested = false;

    // Overlay flags
    OverlayFlags overlays;

    // Wizard
    bool force_wizard = false;
    int wizard_step = -1; // -1 = not set

    // Automation
    bool screenshot_enabled = false;
    int screenshot_delay_sec = 2;
    int timeout_sec = 0;

    // Theme
    int dark_mode_cli = -1; // -1 = not set, 0 = light, 1 = dark

    // Logging
    int verbosity = 0;

    // Memory profiling (development feature)
    bool memory_report = false; // --memory-report: log memory every 30s
    bool show_memory = false;   // --show-memory: display memory overlay (M key toggle)

    // Moonraker override (for testing/development)
    std::string moonraker_url; // --moonraker: override config URL (e.g., ws://192.168.1.112:7125)

    /** @brief Check if any panels/overlays requiring Moonraker are requested */
    bool needs_moonraker_data() const {
        return overlays.needs_moonraker() || initial_panel >= 0;
    }
};

/**
 * @brief Parse command-line arguments
 *
 * @param argc Argument count
 * @param argv Argument values
 * @param args Output: parsed arguments
 * @param screen_width Output: screen width (modified based on -s flag)
 * @param screen_height Output: screen height (modified based on -s flag)
 * @return true on success, false if help was shown or error occurred
 *
 * @note Also modifies g_runtime_config for test mode flags
 */
bool parse_cli_args(int argc, char** argv, CliArgs& args, int& screen_width, int& screen_height);

/**
 * @brief Convert panel name string to panel ID
 *
 * Consolidates duplicated panel name mapping logic.
 *
 * @param name Panel name (e.g., "home", "controls", "bed-mesh")
 * @return Panel ID if valid, std::nullopt if unknown
 */
std::optional<ui_panel_id_t> panel_name_to_id(const char* name);

/**
 * @brief Print test mode configuration banner
 *
 * Shows which backends are mocked vs real.
 */
void print_test_mode_banner();

} // namespace helix
