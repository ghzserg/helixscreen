// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "backlight_backend.h"
#include "display_backend.h"
#include "touch_calibration.h"

#include <lvgl.h>
#include <memory>
#include <vector>

/**
 * @brief Manages LVGL display initialization and lifecycle
 *
 * Encapsulates display backend creation, LVGL initialization, and input device
 * setup. Extracted from main.cpp init_lvgl() to enable isolated testing and
 * cleaner application startup.
 *
 * Lifecycle:
 * 1. Create DisplayManager instance
 * 2. Call init() with desired configuration
 * 3. Use display(), pointer_input(), keyboard_input() as needed
 * 4. Call shutdown() or let destructor clean up
 *
 * Thread safety: All methods should be called from the main thread.
 *
 * @code
 * DisplayManager display_mgr;
 * DisplayManager::Config config;
 * config.width = 800;
 * config.height = 480;
 *
 * if (!display_mgr.init(config)) {
 *     spdlog::error("Failed to initialize display");
 *     return 1;
 * }
 *
 * // Use display_mgr.display() for LVGL operations
 * // ...
 *
 * display_mgr.shutdown();
 * @endcode
 */
class DisplayManager {
  public:
    /**
     * @brief Display configuration options
     */
    struct Config {
        int width = 0;               ///< Display width in pixels (0 = auto-detect)
        int height = 0;              ///< Display height in pixels (0 = auto-detect)
        int rotation = 0;            ///< Display rotation in degrees (0, 90, 180, 270)
        int scroll_throw = 25;       ///< Scroll momentum decay (1-99, higher = faster decay)
        int scroll_limit = 10;       ///< Pixels before scrolling starts
        bool require_pointer = true; ///< Fail init if no pointer device (embedded only)
    };

    DisplayManager();
    ~DisplayManager();

    // Non-copyable, non-movable (owns unique resources)
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;
    DisplayManager(DisplayManager&&) = delete;
    DisplayManager& operator=(DisplayManager&&) = delete;

    /**
     * @brief Get the current DisplayManager instance
     *
     * Returns the most recently initialized DisplayManager. Typically there is
     * only one instance owned by Application. Returns nullptr if none exists.
     *
     * @return Pointer to current instance, or nullptr
     */
    static DisplayManager* instance();

    /**
     * @brief Initialize LVGL and display backend
     *
     * Creates display backend (auto-detected), initializes LVGL,
     * creates display and input devices.
     *
     * @param config Display configuration
     * @return true on success, false on failure (logs error details)
     */
    bool init(const Config& config);

    /**
     * @brief Shutdown display and release resources
     *
     * Safe to call multiple times. Called automatically by destructor.
     */
    void shutdown();

    /**
     * @brief Check if display is initialized
     * @return true if init() succeeded and shutdown() not called
     */
    bool is_initialized() const {
        return m_initialized;
    }

    /**
     * @brief Get LVGL display object
     * @return Display pointer, or nullptr if not initialized
     */
    lv_display_t* display() const {
        return m_display;
    }

    /**
     * @brief Get pointer input device (mouse/touch)
     * @return Input device pointer, or nullptr if not available
     */
    lv_indev_t* pointer_input() const {
        return m_pointer;
    }

    /**
     * @brief Get keyboard input device
     * @return Input device pointer, or nullptr if not available
     */
    lv_indev_t* keyboard_input() const {
        return m_keyboard;
    }

    /**
     * @brief Get display backend
     * @return Backend pointer, or nullptr if not initialized
     */
    DisplayBackend* backend() const {
        return m_backend.get();
    }

    /**
     * @brief Get current display width
     * @return Width in pixels, or 0 if not initialized
     */
    int width() const {
        return m_width;
    }

    /**
     * @brief Get current display height
     * @return Height in pixels, or 0 if not initialized
     */
    int height() const {
        return m_height;
    }

    // ========================================================================
    // Display Sleep Management
    // ========================================================================

    /**
     * @brief Check inactivity and trigger display sleep if timeout exceeded
     *
     * Call this from the main event loop. Uses LVGL's built-in inactivity
     * tracking (lv_display_get_inactive_time) and the configured sleep timeout.
     *
     * Sleep states:
     * - Awake: Full brightness
     * - Dimmed: Reduced brightness after dim timeout
     * - Sleeping: Backlight off after sleep timeout, first touch only wakes
     */
    void check_display_sleep();

    /**
     * @brief Manually wake the display
     *
     * Restores brightness to saved level. When waking from full sleep (not dim),
     * input is disabled for 200ms so the wake touch doesn't trigger UI actions.
     */
    void wake_display();

    /**
     * @brief Force display ON at startup
     *
     * Called early in app initialization to ensure display is visible regardless
     * of previous app's sleep state.
     */
    void ensure_display_on();

    /**
     * @brief Set dim timeout for immediate effect
     *
     * Called by SettingsManager when user changes dim timeout setting.
     *
     * @param seconds Dim timeout (0 to disable)
     */
    void set_dim_timeout(int seconds);

    /**
     * @brief Restore display to usable state on shutdown
     *
     * Called during app cleanup to ensure display is awake before exiting.
     * Prevents next app from starting with a black screen.
     */
    void restore_display_on_shutdown();

    /**
     * @brief Check if display is currently sleeping
     * @return true if backlight is off due to inactivity
     */
    bool is_display_sleeping() const {
        return m_display_sleeping;
    }

    /**
     * @brief Check if display is currently dimmed
     * @return true if backlight is at reduced brightness
     */
    bool is_display_dimmed() const {
        return m_display_dimmed;
    }

    /**
     * @brief Set backlight brightness directly
     * @param percent Brightness 0-100 (clamped to 10-100 minimum)
     */
    void set_backlight_brightness(int percent);

    /**
     * @brief Check if hardware backlight control is available
     * @return true if brightness can be controlled
     */
    bool has_backlight_control() const;

    /**
     * @brief Check if hardware blanking is used for display sleep
     *
     * When true, sleep uses FBIOBLANK + backlight off (AD5M/Allwinner).
     * When false, sleep uses a software black overlay (safe for all displays).
     * Determined by backlight backend capability or config override.
     *
     * @return true if using hardware blank, false if using software overlay
     */
    bool uses_hardware_blank() const {
        return m_use_hardware_blank;
    }

    // ========================================================================
    // Touch Calibration
    // ========================================================================

    /**
     * @brief Apply touch calibration at runtime
     *
     * Called by calibration wizard after user accepts calibration.
     * Immediately applies the affine transform to touch input without
     * requiring a restart.
     *
     * @param cal Valid calibration coefficients
     * @return true if applied successfully, false if backend doesn't support
     *         calibration or validation failed
     */
    bool apply_touch_calibration(const helix::TouchCalibration& cal);

    /**
     * @brief Get current touch calibration from backend
     *
     * Used to backup calibration before applying a new one.
     *
     * @return Current calibration, or invalid calibration if not calibrated/not fbdev
     */
    helix::TouchCalibration get_current_calibration() const;

    /**
     * @brief Check if the touch device needs calibration
     *
     * USB HID touchscreens (HDMI displays) report mapped coordinates natively
     * and don't need calibration. Only resistive/platform touchscreens do.
     *
     * @return true if calibration wizard should be offered
     */
    bool needs_touch_calibration() const;

    // ========================================================================
    // Static Timing Functions (portable across platforms)
    // ========================================================================

    /**
     * @brief Get current tick count in milliseconds
     *
     * Uses SDL_GetTicks() on desktop, clock_gettime() on embedded.
     *
     * @return Milliseconds since some fixed point (wraps at ~49 days)
     */
    static uint32_t get_ticks();

    /**
     * @brief Delay for specified milliseconds
     *
     * Uses SDL_Delay() on desktop, nanosleep() on embedded.
     *
     * @param ms Milliseconds to delay
     */
    static void delay(uint32_t ms);

    // ========================================================================
    // Window Resize Handler (Desktop/SDL)
    // ========================================================================

    /**
     * @brief Callback type for resize notifications
     */
    using ResizeCallback = void (*)();

    /**
     * @brief Initialize resize handler on the given screen
     *
     * Sets up SIZE_CHANGED event listener with debouncing. Call once during
     * application startup after the screen is created.
     *
     * @param screen Root screen object to monitor
     */
    void init_resize_handler(lv_obj_t* screen);

    /**
     * @brief Register callback for resize events
     *
     * Callbacks are invoked after 250ms debounce to avoid excessive
     * redraws during continuous resize operations.
     *
     * @param callback Function to call when resize completes
     */
    void register_resize_callback(ResizeCallback callback);

  private:
    bool m_initialized = false;
    int m_width = 0;
    int m_height = 0;

    std::unique_ptr<DisplayBackend> m_backend;
    lv_display_t* m_display = nullptr;
    lv_indev_t* m_pointer = nullptr;
    lv_indev_t* m_keyboard = nullptr;
    lv_group_t* m_input_group = nullptr;

    // Backlight control
    std::unique_ptr<BacklightBackend> m_backlight;

    // Display sleep state
    bool m_display_sleeping = false;
    bool m_display_dimmed = false;
    bool m_wake_requested = false; // Set by input wrapper when touch detected while sleeping
    int m_dim_timeout_sec = 300;
    int m_dim_brightness_percent = 30;

    // Hardware vs software blank strategy
    bool m_use_hardware_blank = false;
    lv_obj_t* m_sleep_overlay = nullptr;

    // Original pointer read callback (before sleep-aware wrapper)
    lv_indev_read_cb_t m_original_pointer_read_cb = nullptr;

    // Resize handler state
    std::vector<ResizeCallback> m_resize_callbacks;
    lv_timer_t* m_resize_debounce_timer = nullptr;
    static constexpr uint32_t RESIZE_DEBOUNCE_MS = 250;

    static void resize_event_cb(lv_event_t* e);
    static void resize_timer_cb(lv_timer_t* timer);

    /**
     * @brief Transition display to sleep state (hardware blank or software overlay)
     * @param timeout_sec Sleep timeout for logging
     */
    void enter_sleep(int timeout_sec);

    /**
     * @brief Create fullscreen black overlay on lv_layer_top() for software sleep
     */
    void create_sleep_overlay();

    /**
     * @brief Destroy the software sleep overlay
     */
    void destroy_sleep_overlay();

    /**
     * @brief Configure scroll behavior on pointer device
     */
    void configure_scroll(int scroll_throw, int scroll_limit);

    /**
     * @brief Set up keyboard input group
     */
    void setup_keyboard_group();

    /**
     * @brief Temporarily disable pointer input after wake
     *
     * Prevents the wake touch from triggering UI actions.
     * Re-enables automatically after 200ms via LVGL timer.
     */
    void disable_input_briefly();

    /**
     * @brief Timer callback to re-enable input after wake
     */
    static void reenable_input_cb(lv_timer_t* timer);

    /**
     * @brief Sleep-aware input wrapper callback
     *
     * Wraps original read callback to absorb touches when sleeping.
     * Sets m_wake_requested flag and returns RELEASED state, preventing
     * UI events from firing while the display wakes.
     */
    static void sleep_aware_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

    /**
     * @brief Install sleep-aware wrapper on pointer input device
     *
     * Called during init() to wrap the backend's read callback.
     */
    void install_sleep_aware_input_wrapper();
};
