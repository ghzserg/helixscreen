// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file display_backend.h
 * @brief Abstract platform-independent interface for display and input initialization
 *
 * @pattern Pure virtual interface + static create()/create_auto() factory methods
 * @threading Implementation-dependent; see concrete implementations
 *
 * @see display_backend_sdl.cpp, display_backend_fbdev.cpp
 */

#pragma once

#include <fstream>
#include <lvgl.h>
#include <memory>
#include <regex>
#include <string>

/**
 * @brief Display backend types supported by HelixScreen
 */
enum class DisplayBackendType {
    SDL,   ///< SDL2 for desktop development (macOS/Linux with X11/Wayland)
    FBDEV, ///< Linux framebuffer (/dev/fb0) - works on most embedded Linux
    DRM,   ///< Linux DRM/KMS - modern display API, better for Pi
    AUTO   ///< Auto-detect best available backend
};

/**
 * @brief Result of display resolution auto-detection
 *
 * Used by detect_resolution() to return hardware-detected display dimensions.
 * Only valid for fbdev/DRM backends; SDL always returns invalid.
 */
struct DetectedResolution {
    int width = 0;
    int height = 0;
    bool valid = false;
};

/**
 * @brief Convert DisplayBackendType to string for logging
 */
inline const char* display_backend_type_to_string(DisplayBackendType type) {
    switch (type) {
    case DisplayBackendType::SDL:
        return "SDL";
    case DisplayBackendType::FBDEV:
        return "Framebuffer";
    case DisplayBackendType::DRM:
        return "DRM/KMS";
    case DisplayBackendType::AUTO:
        return "Auto";
    default:
        return "Unknown";
    }
}

/**
 * @brief Convert rotation degrees to LVGL rotation enum
 *
 * Maps user-facing degree values (0, 90, 180, 270) to LVGL's
 * LV_DISPLAY_ROTATION_* constants. Invalid values default to 0.
 *
 * @param degrees Rotation in degrees (0, 90, 180, 270)
 * @return LVGL rotation enum value
 */
inline lv_display_rotation_t degrees_to_lv_rotation(int degrees) {
    switch (degrees) {
    case 90:
        return LV_DISPLAY_ROTATION_90;
    case 180:
        return LV_DISPLAY_ROTATION_180;
    case 270:
        return LV_DISPLAY_ROTATION_270;
    default:
        return LV_DISPLAY_ROTATION_0;
    }
}

/**
 * @brief Read display rotation from helixconfig.json
 *
 * Searches standard config paths for the /display/rotate field.
 * Used by watchdog and splash binaries which don't use the full Config system.
 *
 * @param default_value Fallback rotation in degrees (default: 0)
 * @return Rotation in degrees (0, 90, 180, 270)
 */
inline int read_config_rotation(int default_value = 0) {
    const char* paths[] = {"config/helixconfig.json", "helixconfig.json",
                           "/opt/helixscreen/helixconfig.json"};

    for (const char* path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Look for "rotate" inside "display" section
        // Simple regex approach matching existing pattern (read_config_brightness)
        std::regex rotate_regex(R"("rotate"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(content, match, rotate_regex) && match.size() > 1) {
            int rotation = std::stoi(match[1].str());
            // Validate: only 0, 90, 180, 270 are valid
            if (rotation == 90 || rotation == 180 || rotation == 270) {
                return rotation;
            }
            return 0; // Invalid value → no rotation
        }
    }

    return default_value;
}

/**
 * @brief Abstract display backend interface
 *
 * Provides platform-agnostic display and input initialization.
 * Follows the same factory pattern as WifiBackend.
 *
 * Lifecycle:
 * 1. Factory creates backend via DisplayBackend::create() or create_auto()
 * 2. Call create_display() to initialize display hardware
 * 3. Call create_input_pointer() to initialize touch/mouse input
 * 4. Optionally call create_input_keyboard() for keyboard support
 * 5. Backend is destroyed when unique_ptr goes out of scope
 *
 * Thread safety: Backend creation and destruction should be done from
 * the main thread. Display operations are typically single-threaded.
 */
class DisplayBackend {
  public:
    virtual ~DisplayBackend() = default;

    // ========================================================================
    // Display Creation
    // ========================================================================

    /**
     * @brief Initialize the display
     *
     * Creates the LVGL display object for this backend. This allocates
     * display buffers and initializes the underlying display hardware.
     *
     * @param width Display width in pixels
     * @param height Display height in pixels
     * @return LVGL display object, or nullptr on failure
     */
    virtual lv_display_t* create_display(int width, int height) = 0;

    // ========================================================================
    // Input Device Creation
    // ========================================================================

    /**
     * @brief Create pointer input device (mouse/touchscreen)
     *
     * Initializes the primary input device for the display.
     * For desktop: mouse input via SDL
     * For embedded: touchscreen via evdev
     *
     * @return LVGL input device, or nullptr on failure
     */
    virtual lv_indev_t* create_input_pointer() = 0;

    /**
     * @brief Create keyboard input device (optional)
     *
     * Not all backends support keyboard input. Returns nullptr
     * if keyboard is not available or not applicable.
     *
     * @return LVGL input device, or nullptr if not supported
     */
    virtual lv_indev_t* create_input_keyboard() {
        return nullptr;
    }

    // ========================================================================
    // Backend Information
    // ========================================================================

    /**
     * @brief Get the backend type
     */
    virtual DisplayBackendType type() const = 0;

    /**
     * @brief Get backend name for logging/display
     */
    virtual const char* name() const = 0;

    /**
     * @brief Check if this backend is available on the current system
     *
     * For SDL: checks if display can be opened
     * For FBDEV: checks if /dev/fb0 exists and is accessible
     * For DRM: checks if /dev/dri/card0 exists and is accessible
     *
     * @return true if backend can be used
     */
    virtual bool is_available() const = 0;

    /**
     * @brief Detect the native display resolution from hardware
     *
     * Queries the display hardware for its native resolution. This allows
     * auto-configuration without requiring explicit CLI size arguments.
     *
     * For FBDEV: queries FBIOGET_VSCREENINFO for xres/yres
     * For DRM: queries the connector's preferred mode
     * For SDL: returns invalid (desktop uses presets/CLI)
     *
     * @return DetectedResolution with valid=true if detection succeeded
     */
    virtual DetectedResolution detect_resolution() const {
        return {}; // Default: not supported
    }

    /**
     * @brief Check if the display is still active/owned by this process
     *
     * Used by the splash screen to detect when the main app takes over
     * the display. For framebuffer/DRM backends, this checks if another
     * process has opened the display device.
     *
     * @return true if display is still active, false if taken over
     */
    virtual bool is_active() const {
        return true;
    }

    /**
     * @brief Clear the entire framebuffer to a solid color
     *
     * Used by splash screen to wipe any pre-existing content (like Linux
     * console text) before rendering the UI. This writes directly to the
     * framebuffer, bypassing LVGL's dirty region tracking.
     *
     * Must be called AFTER create_display() and before any LVGL rendering.
     *
     * @param color 32-bit ARGB color (0xAARRGGBB format, use 0xFF for full opacity)
     * @return true if framebuffer was cleared, false on error or not supported
     */
    virtual bool clear_framebuffer(uint32_t color) {
        (void)color;
        return false; // Not supported by default
    }

    /**
     * @brief Unblank the display and reset pan position
     *
     * Explicitly enables the display backlight and resets the framebuffer
     * pan position to (0,0). This is essential on some embedded systems
     * (like AD5M) where the display may be blanked by other processes
     * during boot.
     *
     * Uses standard Linux framebuffer ioctls:
     * - FBIOBLANK with FB_BLANK_UNBLANK to enable display
     * - FBIOPAN_DISPLAY with yoffset=0 to reset pan position
     *
     * Should be called early in startup, before or after create_display().
     *
     * @return true if unblank succeeded, false on error or not supported
     */
    virtual bool unblank_display() {
        return false; // Not supported by default
    }

    /**
     * @brief Blank the display (turn off backlight via framebuffer ioctl)
     *
     * Blanks the display using the FBIOBLANK ioctl with FB_BLANK_NORMAL.
     * This is the counterpart to unblank_display() and should be called
     * when putting the display to sleep.
     *
     * @return true if blank succeeded, false on error or not supported
     */
    virtual bool blank_display() {
        return false; // Not supported by default
    }

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /**
     * @brief Create a specific backend type
     *
     * @param type Backend type to create
     * @return Backend instance, or nullptr if type not available/compiled
     */
    static std::unique_ptr<DisplayBackend> create(DisplayBackendType type);

    /**
     * @brief Auto-detect and create the best available backend
     *
     * Detection order (first available wins):
     * 1. Check HELIX_DISPLAY_BACKEND environment variable override
     * 2. DRM (if compiled and /dev/dri/card0 accessible)
     * 3. Framebuffer (if compiled and /dev/fb0 accessible)
     * 4. SDL (fallback for desktop)
     *
     * @return Backend instance, or nullptr if no backend available
     */
    static std::unique_ptr<DisplayBackend> create_auto();

    /**
     * @brief Convenience: auto-detect and create backend
     *
     * Same as create_auto(), provided for simpler calling code.
     */
    static std::unique_ptr<DisplayBackend> create() {
        return create_auto();
    }
};

// ============================================================================
// Backend-Specific Headers (conditionally included)
// ============================================================================

// These are only available when the corresponding backend is compiled in.
// Check with #ifdef HELIX_DISPLAY_SDL etc.

#ifdef HELIX_DISPLAY_SDL
#include "display_backend_sdl.h"
#endif

#ifdef HELIX_DISPLAY_FBDEV
#include "display_backend_fbdev.h"
#endif

#ifdef HELIX_DISPLAY_DRM
#include "display_backend_drm.h"
#endif
