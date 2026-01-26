// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backlight_backend.h"

#include "runtime_config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

// RAII guard for file descriptors to prevent leaks
class FdGuard {
    int fd_;

  public:
    explicit FdGuard(int fd) : fd_(fd) {}
    ~FdGuard() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    int get() const {
        return fd_;
    }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
};
#endif

// ============================================================================
// BacklightBackendNone - No hardware control (or simulated for test mode)
// ============================================================================

/**
 * @brief No-op backlight backend for platforms without hardware control
 *
 * In test mode: Simulates brightness for UI testing (is_available = true)
 * In production: No hardware control (is_available = false)
 */
class BacklightBackendNone : public BacklightBackend {
  public:
    explicit BacklightBackendNone(bool simulate) : simulate_(simulate), cached_brightness_(50) {
        if (simulate_) {
            spdlog::info("[Backlight] Using simulated backend for testing");
        }
    }

    bool set_brightness(int percent) override {
        cached_brightness_ = std::clamp(percent, 0, 100);
        spdlog::debug("[Backlight-{}] set_brightness({}) - {}", name(), percent,
                      simulate_ ? "simulated" : "no hardware");
        return simulate_; // Success only in simulation mode
    }

    int get_brightness() const override {
        return simulate_ ? cached_brightness_ : -1;
    }

    bool is_available() const override {
        return simulate_; // Available for UI testing, not in production
    }

    const char* name() const override {
        return simulate_ ? "Simulated" : "None";
    }

  private:
    bool simulate_;
    int cached_brightness_;
};

// ============================================================================
// BacklightBackendSysfs - Linux sysfs interface (/sys/class/backlight/*)
// ============================================================================

#ifdef __linux__
/**
 * @brief Linux sysfs backlight backend
 *
 * Scans /sys/class/backlight/ for the first available device and uses
 * standard brightness/max_brightness files. Works on Raspberry Pi and
 * other Linux systems with properly configured backlight drivers.
 */
class BacklightBackendSysfs : public BacklightBackend {
  public:
    BacklightBackendSysfs() {
        probe_device();
    }

    bool set_brightness(int percent) override {
        if (device_path_.empty() || max_brightness_ <= 0) {
            return false;
        }

        // Allow 0% for sleep mode (full off)
        int target = (percent * max_brightness_) / 100;
        target = std::clamp(target, 0, max_brightness_);

        std::string brightness_path = device_path_ + "/brightness";
        std::ofstream f(brightness_path);
        if (!f.is_open()) {
            spdlog::warn("[Backlight-Sysfs] Cannot write to {} (permission denied?)",
                         brightness_path);
            return false;
        }

        f << target;
        if (!f.good()) {
            spdlog::warn("[Backlight-Sysfs] Failed to write brightness value to {}",
                         brightness_path);
            return false;
        }
        f.close();

        spdlog::debug("[Backlight-Sysfs] Set {} to {}/{} ({}%)", device_name_, target,
                      max_brightness_, percent);
        return true;
    }

    int get_brightness() const override {
        if (device_path_.empty() || max_brightness_ <= 0) {
            return -1;
        }

        std::string brightness_path = device_path_ + "/brightness";
        std::ifstream f(brightness_path);
        if (!f.is_open()) {
            return -1;
        }

        int current = 0;
        f >> current;
        f.close();

        return (current * 100) / max_brightness_;
    }

    bool is_available() const override {
        return !device_path_.empty() && max_brightness_ > 0;
    }

    const char* name() const override {
        return "Sysfs";
    }

  private:
    void probe_device() {
        const char* backlight_base = "/sys/class/backlight";

        DIR* dir = opendir(backlight_base);
        if (!dir) {
            spdlog::debug("[Backlight-Sysfs] No backlight class at {}", backlight_base);
            return;
        }

        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip . and ..
            if (entry->d_name[0] == '.') {
                continue;
            }

            std::string path = std::string(backlight_base) + "/" + entry->d_name;
            std::string brightness_path = path + "/brightness";
            std::string max_path = path + "/max_brightness";

            // Check if brightness file exists
            struct stat st{};
            if (stat(brightness_path.c_str(), &st) != 0) {
                continue;
            }

            // Read max brightness
            std::ifstream max_file(max_path);
            if (!max_file.is_open()) {
                continue;
            }

            int max_val = 0;
            max_file >> max_val;
            max_file.close();

            if (max_val <= 0) {
                continue;
            }

            // Found valid device
            device_path_ = path;
            device_name_ = entry->d_name;
            max_brightness_ = max_val;

            spdlog::info("[Backlight-Sysfs] Found device: {} (max={})", device_name_,
                         max_brightness_);
            break;
        }

        closedir(dir);
    }

    std::string device_path_;
    std::string device_name_;
    int max_brightness_ = 0;
};
#endif // __linux__

// ============================================================================
// BacklightBackendAllwinner - Allwinner DISP2 ioctl (/dev/disp)
// ============================================================================

#ifdef __linux__
/**
 * @brief Allwinner DISP2 backlight backend
 *
 * Uses ioctl on /dev/disp to control backlight on Allwinner SoCs (AD5M, sunxi).
 * This is used when the kernel doesn't expose backlight via sysfs.
 *
 * Ioctl commands:
 * - 0x102 (DISP_LCD_SET_BRIGHTNESS): args = [screen, brightness, 0, 0]
 * - 0x103 (DISP_LCD_GET_BRIGHTNESS): args = [screen, 0, 0, 0]
 *
 * Brightness range: 0-255 (device tree lcd_pwm_max_limit)
 */
class BacklightBackendAllwinner : public BacklightBackend {
  public:
    static constexpr const char* DISP_DEVICE = "/dev/disp";

    // Allwinner DISP2 ioctl commands for LCD backlight control
    // From sunxi-display2 kernel driver (not including header to avoid kernel deps)
    // See: https://linux-sunxi.org/Sunxi_disp_driver_interface
    static constexpr unsigned long DISP_LCD_SET_BRIGHTNESS = 0x102;
    static constexpr unsigned long DISP_LCD_GET_BRIGHTNESS = 0x103;
    static constexpr unsigned long DISP_LCD_BACKLIGHT_ENABLE = 0x104;
    static constexpr unsigned long DISP_LCD_BACKLIGHT_DISABLE = 0x105;

    static constexpr int MAX_BRIGHTNESS = 255;

    BacklightBackendAllwinner() {
        probe_device();
        if (available_) {
            // Reset backlight driver state by cycling through DISABLE.
            // On AD5M, the Allwinner DISP2 driver can get into an "inverted" state
            // where higher brightness values = dimmer screen. Cycling through
            // DISABLE clears this state and restores normal polarity.
            reset_driver_state();
        }
    }

    /**
     * @brief Reset the Allwinner backlight driver to a known good state
     *
     * The Allwinner DISP2 driver can get into a state where brightness values
     * are inverted. Cycling through DISABLE then back to the desired brightness
     * resets the PWM polarity to normal operation.
     */
    void reset_driver_state() {
        FdGuard fd(open(DISP_DEVICE, O_RDWR));
        if (fd.get() < 0) {
            return;
        }

        unsigned long args[4] = {0, 0, 0, 0};

        // Disable backlight first to reset driver state
        if (ioctl(fd.get(), DISP_LCD_BACKLIGHT_DISABLE, args) == 0) {
            spdlog::info("[Backlight-Allwinner] Reset: DISABLE to clear driver state");
        }

        // Brief delay for hardware to settle (10ms)
        usleep(10000);

        // Re-enable backlight
        if (ioctl(fd.get(), DISP_LCD_BACKLIGHT_ENABLE, args) == 0) {
            spdlog::info("[Backlight-Allwinner] Reset: ENABLE after state clear");
        }

        // Set to max brightness initially
        args[1] = MAX_BRIGHTNESS;
        if (ioctl(fd.get(), DISP_LCD_SET_BRIGHTNESS, args) == 0) {
            spdlog::info("[Backlight-Allwinner] Reset: brightness set to max");
        }
    }

    bool set_brightness(int percent) override {
        if (!available_) {
            return false;
        }

        FdGuard fd(open(DISP_DEVICE, O_RDWR));
        if (fd.get() < 0) {
            int err = errno; // Capture immediately
            spdlog::warn("[Backlight-Allwinner] Cannot open {}: {}", DISP_DEVICE, strerror(err));
            return false;
        }

        // Convert percentage to 0-255 range
        int brightness = (percent * MAX_BRIGHTNESS) / 100;
        brightness = std::clamp(brightness, 0, MAX_BRIGHTNESS);

        // ioctl args: [screen_id, arg1, 0, 0]
        unsigned long args[4] = {0, 0, 0, 0};

        if (brightness == 0) {
            // Disable backlight for 0% brightness
            int ret = ioctl(fd.get(), DISP_LCD_BACKLIGHT_DISABLE, args);
            if (ret < 0) {
                int err = errno;
                spdlog::warn("[Backlight-Allwinner] ioctl BACKLIGHT_DISABLE failed: {}",
                             strerror(err));
                return false;
            }
            spdlog::debug("[Backlight-Allwinner] Backlight disabled");
        } else {
            // Enable backlight first (required on AD5M before SET_BRIGHTNESS works)
            int ret = ioctl(fd.get(), DISP_LCD_BACKLIGHT_ENABLE, args);
            if (ret < 0) {
                int err = errno;
                spdlog::warn("[Backlight-Allwinner] ioctl BACKLIGHT_ENABLE failed: {}",
                             strerror(err));
                // Continue anyway - some devices may not need explicit enable
            }

            // Set brightness level
            args[1] = static_cast<unsigned long>(brightness);
            ret = ioctl(fd.get(), DISP_LCD_SET_BRIGHTNESS, args);
            if (ret < 0) {
                int err = errno;
                spdlog::warn("[Backlight-Allwinner] ioctl SET_BRIGHTNESS failed: {}",
                             strerror(err));
                return false;
            }
            spdlog::debug("[Backlight-Allwinner] Set brightness to {}/255 ({}%)", brightness,
                          percent);
        }

        return true;
    }

    int get_brightness() const override {
        if (!available_) {
            return -1;
        }

        FdGuard fd(open(DISP_DEVICE, O_RDONLY));
        if (fd.get() < 0) {
            return -1;
        }

        // ioctl args: [screen_id, 0, 0, 0]
        unsigned long args[4] = {0, 0, 0, 0};
        int ret = ioctl(fd.get(), DISP_LCD_GET_BRIGHTNESS, args);

        if (ret < 0) {
            return -1;
        }

        // AD5M returns brightness in args[1] after the call (ret is 0 on success)
        // Some other Allwinner drivers return it in ret directly
        int brightness = (ret > 0) ? ret : static_cast<int>(args[1]);

        return (brightness * 100) / MAX_BRIGHTNESS;
    }

    bool is_available() const override {
        return available_;
    }

    const char* name() const override {
        return "Allwinner";
    }

  private:
    void probe_device() {
        // Check if /dev/disp exists
        struct stat st{};
        if (stat(DISP_DEVICE, &st) != 0) {
            spdlog::debug("[Backlight-Allwinner] {} not found", DISP_DEVICE);
            return;
        }

        // Try to open and verify ioctl works
        FdGuard fd(open(DISP_DEVICE, O_RDONLY));
        if (fd.get() < 0) {
            int err = errno; // Capture immediately
            spdlog::debug("[Backlight-Allwinner] Cannot open {}: {}", DISP_DEVICE, strerror(err));
            return;
        }

        // Test GET_BRIGHTNESS to verify this is a display with backlight control
        unsigned long args[4] = {0, 0, 0, 0};
        int ret = ioctl(fd.get(), DISP_LCD_GET_BRIGHTNESS, args);

        if (ret < 0) {
            int err = errno; // Capture immediately
            spdlog::debug("[Backlight-Allwinner] GET_BRIGHTNESS ioctl failed: {}", strerror(err));
            return;
        }

        available_ = true;
        spdlog::info("[Backlight-Allwinner] Found {} (current brightness: {})", DISP_DEVICE,
                     (ret > 0) ? ret : static_cast<int>(args[1]));
    }

    bool available_ = false;
};
#endif // __linux__

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<BacklightBackend> BacklightBackend::create() {
#if !defined(HELIX_SPLASH_ONLY) && !defined(HELIX_WATCHDOG)
    // 1. Test mode → Simulated backend (UI works normally)
    // (Skip in splash/watchdog builds - they don't link runtime_config and are never in test mode)
    if (get_runtime_config()->is_test_mode()) {
        spdlog::debug("[Backlight] Test mode - using simulated backend");
        return std::make_unique<BacklightBackendNone>(true); // simulate = true
    }
#endif

    // 2. Environment variable override
    const char* env = std::getenv("HELIX_BACKLIGHT_DEVICE");
    if (env != nullptr) {
        spdlog::info("[Backlight] HELIX_BACKLIGHT_DEVICE={}", env);

        if (strcmp(env, "none") == 0) {
            return std::make_unique<BacklightBackendNone>(false);
        }

#ifdef __linux__
        if (strcmp(env, "sysfs") == 0) {
            auto backend = std::make_unique<BacklightBackendSysfs>();
            if (backend->is_available()) {
                return backend;
            }
            spdlog::warn("[Backlight] Sysfs forced but not available, falling through");
        }

        if (strcmp(env, "allwinner") == 0) {
            auto backend = std::make_unique<BacklightBackendAllwinner>();
            if (backend->is_available()) {
                return backend;
            }
            spdlog::warn("[Backlight] Allwinner forced but not available, falling through");
        }
#endif
        // Unknown value or unavailable, fall through to auto-detection
    }

#ifdef __linux__
    // 3. Try Sysfs first (most portable)
    {
        auto backend = std::make_unique<BacklightBackendSysfs>();
        if (backend->is_available()) {
            spdlog::info("[Backlight] Auto-detected: Sysfs");
            return backend;
        }
    }

    // 4. Try Allwinner ioctl (AD5M/sunxi specific)
    {
        auto backend = std::make_unique<BacklightBackendAllwinner>();
        if (backend->is_available()) {
            spdlog::info("[Backlight] Auto-detected: Allwinner");
            return backend;
        }
    }
#endif

    // 5. Fallback to None (no hardware control)
    spdlog::info("[Backlight] No hardware backend available");
    return std::make_unique<BacklightBackendNone>(false);
}
