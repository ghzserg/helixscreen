// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen - Linux Framebuffer Display Backend Implementation

#ifdef HELIX_DISPLAY_FBDEV

#include "display_backend_fbdev.h"

#include "config.h"
#include "touch_calibration.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

// System includes for device access checks
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

/**
 * @brief Read a line from a sysfs file
 * @param path Path to the sysfs file
 * @return File contents (first line) or empty string on error
 */
std::string read_sysfs_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::string line;
    std::getline(file, line);
    return line;
}

/**
 * @brief Get the device name from sysfs
 * @param event_num Event device number (e.g., 0 for event0)
 * @return Device name or empty string on error
 */
std::string get_device_name(int event_num) {
    std::string path = "/sys/class/input/event" + std::to_string(event_num) + "/device/name";
    return read_sysfs_file(path);
}

/**
 * @brief Check if an event device has touch/absolute input capabilities
 *
 * Reads /sys/class/input/eventN/device/capabilities/abs and checks for
 * ABS_X (bit 0) and ABS_Y (bit 1) capabilities.
 *
 * @param event_num Event device number
 * @return true if device has ABS_X and ABS_Y capabilities
 */
bool has_touch_capabilities(int event_num) {
    std::string path =
        "/sys/class/input/event" + std::to_string(event_num) + "/device/capabilities/abs";
    std::string caps = read_sysfs_file(path);

    if (caps.empty()) {
        return false;
    }

    // The capabilities file contains space-separated hex values
    // The first value contains ABS_X (bit 0) and ABS_Y (bit 1)
    // We need both bits set (0x3) for a touchscreen
    try {
        // Find the last hex value (rightmost = lowest bits)
        size_t last_space = caps.rfind(' ');
        std::string last_hex =
            (last_space != std::string::npos) ? caps.substr(last_space + 1) : caps;

        unsigned long value = std::stoul(last_hex, nullptr, 16);
        // Check for ABS_X (bit 0) and ABS_Y (bit 1)
        return (value & 0x3) == 0x3;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Check if device name matches known touchscreen patterns
 * @param name Device name to check
 * @return true if name matches a known touchscreen pattern
 */
bool is_known_touchscreen_name(const std::string& name) {
    // Known touchscreen name patterns (case-insensitive substrings)
    // Note: Avoid overly broad patterns like "ts" which match "events", "buttons", etc.
    static const char* patterns[] = {"rtp",    // Resistive touch panel (sun4i_ts on AD5M)
                                     "touch",  // Generic touchscreen
                                     "sun4i",  // Allwinner touch controller
                                     "ft5x",   // FocalTech touch controllers
                                     "goodix", // Goodix touch controllers
                                     "gt9",    // Goodix GT9xx series
                                     "ili2",   // ILI touch controllers
                                     "atmel",  // Atmel touch controllers
                                     "edt-ft", // EDT FocalTech displays
                                     "tsc",    // Touch screen controller
                                     nullptr};

    // Convert name to lowercase for case-insensitive matching
    std::string lower_name = name;
    for (auto& c : lower_name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    for (int i = 0; patterns[i] != nullptr; ++i) {
        if (lower_name.find(patterns[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Load affine touch calibration coefficients from config
 *
 * Reads the calibration data saved by the touch calibration wizard.
 * Returns an invalid calibration if no valid data is stored.
 *
 * @return Calibration coefficients (check .valid before use)
 */
helix::TouchCalibration load_touch_calibration() {
    Config* cfg = Config::get_instance();
    helix::TouchCalibration cal;

    if (!cfg) {
        spdlog::debug("[Fbdev Backend] Config not available for calibration load");
        return cal;
    }

    cal.valid = cfg->get<bool>("/input/calibration/valid", false);
    if (!cal.valid) {
        spdlog::debug("[Fbdev Backend] No valid calibration in config");
        return cal;
    }

    cal.a = static_cast<float>(cfg->get<double>("/input/calibration/a", 1.0));
    cal.b = static_cast<float>(cfg->get<double>("/input/calibration/b", 0.0));
    cal.c = static_cast<float>(cfg->get<double>("/input/calibration/c", 0.0));
    cal.d = static_cast<float>(cfg->get<double>("/input/calibration/d", 0.0));
    cal.e = static_cast<float>(cfg->get<double>("/input/calibration/e", 1.0));
    cal.f = static_cast<float>(cfg->get<double>("/input/calibration/f", 0.0));

    if (!helix::is_calibration_valid(cal)) {
        spdlog::warn("[Fbdev Backend] Stored calibration failed validation");
        cal.valid = false;
    }

    return cal;
}

/**
 * @brief Custom read callback that applies affine calibration
 *
 * Wraps the original evdev read callback, applying the affine transform
 * to touch coordinates after the linear calibration is done.
 */
void calibrated_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* ctx = static_cast<CalibrationContext*>(lv_indev_get_user_data(indev));
    if (!ctx) {
        return;
    }

    // Call the original evdev read callback first
    if (ctx->original_read_cb) {
        ctx->original_read_cb(indev, data);
    }

    // Apply affine calibration if valid and touch is active
    if (ctx->calibration.valid && data->state == LV_INDEV_STATE_PRESSED) {
        helix::Point raw{static_cast<int>(data->point.x), static_cast<int>(data->point.y)};
        helix::Point transformed = helix::transform_point(
            ctx->calibration, raw, ctx->screen_width - 1, ctx->screen_height - 1);
        data->point.x = transformed.x;
        data->point.y = transformed.y;
    }
}

} // anonymous namespace

DisplayBackendFbdev::DisplayBackendFbdev() = default;

DisplayBackendFbdev::DisplayBackendFbdev(const std::string& fb_device,
                                         const std::string& touch_device)
    : fb_device_(fb_device), touch_device_(touch_device) {}

bool DisplayBackendFbdev::is_available() const {
    struct stat st;

    // Check if framebuffer device exists and is accessible
    if (stat(fb_device_.c_str(), &st) != 0) {
        spdlog::debug("[Fbdev Backend] Framebuffer device {} not found", fb_device_);
        return false;
    }

    // Check if we can read it (need read access for display)
    if (access(fb_device_.c_str(), R_OK | W_OK) != 0) {
        spdlog::debug("[Fbdev Backend] Framebuffer device {} not accessible (need R/W permissions)",
                      fb_device_);
        return false;
    }

    return true;
}

lv_display_t* DisplayBackendFbdev::create_display(int width, int height) {
    spdlog::info("[Fbdev Backend] Creating framebuffer display on {}", fb_device_);

    // Store screen dimensions for touch coordinate clamping
    screen_width_ = width;
    screen_height_ = height;

    // LVGL's framebuffer driver
    // Note: LVGL 9.x uses lv_linux_fbdev_create()
    display_ = lv_linux_fbdev_create();

    if (display_ == nullptr) {
        spdlog::error("[Fbdev Backend] Failed to create framebuffer display");
        return nullptr;
    }

    // Set the framebuffer device path
    lv_linux_fbdev_set_file(display_, fb_device_.c_str());

    // CRITICAL: AD5M's LCD controller interprets XRGB8888's X byte as alpha.
    // By default, LVGL uses XRGB8888 for 32bpp and sets X=0x00 (transparent).
    // We must use ARGB8888 format so LVGL sets alpha=0xFF (fully opaque).
    // Without this, the display shows pink/magenta ghost overlay.
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_ARGB8888);
    spdlog::info("[Fbdev Backend] Set color format to ARGB8888 (AD5M alpha fix)");

    spdlog::info("[Fbdev Backend] Framebuffer display created: {}x{} on {}", width, height,
                 fb_device_);
    return display_;
}

lv_indev_t* DisplayBackendFbdev::create_input_pointer() {
    // Determine touch device path
    std::string touch_path = touch_device_;
    if (touch_path.empty()) {
        touch_path = auto_detect_touch_device();
    }

    if (touch_path.empty()) {
        spdlog::warn("[Fbdev Backend] No touch device found - pointer input disabled");
        return nullptr;
    }

    spdlog::info("[Fbdev Backend] Creating evdev touch input on {}", touch_path);

    // LVGL's evdev driver for touch input
    touch_ = lv_evdev_create(LV_INDEV_TYPE_POINTER, touch_path.c_str());

    if (touch_ == nullptr) {
        spdlog::error("[Fbdev Backend] Failed to create evdev touch input on {}", touch_path);
        return nullptr;
    }

    // Check for touch axis configuration via environment variables
    // HELIX_TOUCH_SWAP_AXES=1 - swap X and Y axes
    const char* swap_axes = std::getenv("HELIX_TOUCH_SWAP_AXES");
    if (swap_axes != nullptr && strcmp(swap_axes, "1") == 0) {
        spdlog::info("[Fbdev Backend] Touch axes swapped (HELIX_TOUCH_SWAP_AXES=1)");
        lv_evdev_set_swap_axes(touch_, true);
    }

    // Check for explicit touch calibration values
    // These override the kernel-reported EVIOCGABS values which may be incorrect
    // (e.g., kernel reports 0-4095 but actual hardware uses a subset)
    // To invert an axis, swap min and max values (e.g., MIN_Y=3200, MAX_Y=900)
    const char* env_min_x = std::getenv("HELIX_TOUCH_MIN_X");
    const char* env_max_x = std::getenv("HELIX_TOUCH_MAX_X");
    const char* env_min_y = std::getenv("HELIX_TOUCH_MIN_Y");
    const char* env_max_y = std::getenv("HELIX_TOUCH_MAX_Y");

    if (env_min_x && env_max_x && env_min_y && env_max_y) {
        int min_x = std::atoi(env_min_x);
        int max_x = std::atoi(env_max_x);
        int min_y = std::atoi(env_min_y);
        int max_y = std::atoi(env_max_y);

        spdlog::info("[Fbdev Backend] Touch calibration from env: X({}->{}) Y({}->{})", min_x,
                     max_x, min_y, max_y);
        lv_evdev_set_calibration(touch_, min_x, min_y, max_x, max_y);
    }

    // Load affine calibration from config (saved by calibration wizard)
    calibration_ = load_touch_calibration();
    if (calibration_.valid) {
        spdlog::info("[Fbdev Backend] Affine calibration loaded: "
                     "a={:.4f} b={:.4f} c={:.4f} d={:.4f} e={:.4f} f={:.4f}",
                     calibration_.a, calibration_.b, calibration_.c, calibration_.d, calibration_.e,
                     calibration_.f);

        // Set up the custom read callback to apply affine calibration
        // We wrap the original evdev callback with our calibrated version
        calibration_context_.calibration = calibration_;
        calibration_context_.original_read_cb = lv_indev_get_read_cb(touch_);
        calibration_context_.screen_width = screen_width_;
        calibration_context_.screen_height = screen_height_;

        lv_indev_set_user_data(touch_, &calibration_context_);
        lv_indev_set_read_cb(touch_, calibrated_read_cb);

        spdlog::info("[Fbdev Backend] Affine calibration callback installed");
    }

    spdlog::info("[Fbdev Backend] Evdev touch input created on {}", touch_path);
    return touch_;
}

std::string DisplayBackendFbdev::auto_detect_touch_device() const {
    // Priority 1: Environment variable override
    const char* env_device = std::getenv("HELIX_TOUCH_DEVICE");
    if (env_device != nullptr && strlen(env_device) > 0) {
        spdlog::debug("[Fbdev Backend] Using touch device from HELIX_TOUCH_DEVICE: {}", env_device);
        return env_device;
    }

    // Priority 2: Capability-based detection using Linux sysfs
    // Scan /dev/input/eventN devices and check for touch capabilities
    const char* input_dir = "/dev/input";
    DIR* dir = opendir(input_dir);
    if (dir == nullptr) {
        spdlog::debug("[Fbdev Backend] Cannot open {}", input_dir);
        return "/dev/input/event0";
    }

    std::string best_device;
    std::string best_name;
    bool best_is_known = false;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Look for eventN devices
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        // Extract event number
        int event_num = -1;
        if (sscanf(entry->d_name, "event%d", &event_num) != 1 || event_num < 0) {
            continue;
        }

        std::string device_path = std::string(input_dir) + "/" + entry->d_name;

        // Check if accessible
        if (access(device_path.c_str(), R_OK) != 0) {
            continue;
        }

        // Get device name from sysfs (do this once, before capability check)
        std::string name = get_device_name(event_num);

        // Check for ABS_X and ABS_Y capabilities (required for touchscreen)
        if (!has_touch_capabilities(event_num)) {
            spdlog::trace("[Fbdev Backend] {} ({}) - no touch capabilities", device_path, name);
            continue;
        }

        bool is_known = is_known_touchscreen_name(name);

        spdlog::debug("[Fbdev Backend] {} ({}) - has touch capabilities{}", device_path, name,
                      is_known ? " [known touchscreen]" : "");

        // Prefer devices with known touchscreen names
        if (best_device.empty() || (is_known && !best_is_known)) {
            best_device = device_path;
            best_name = name;
            best_is_known = is_known;
        }
    }

    closedir(dir);

    if (best_device.empty()) {
        spdlog::debug("[Fbdev Backend] No touch-capable device found, using default");
        return "/dev/input/event0";
    }

    spdlog::info("[Fbdev Backend] Found touchscreen: {} ({})", best_device, best_name);
    return best_device;
}

bool DisplayBackendFbdev::clear_framebuffer(uint32_t color) {
    // Open framebuffer to get info and clear it
    int fd = open(fb_device_.c_str(), O_RDWR);
    if (fd < 0) {
        spdlog::error("[Fbdev Backend] Cannot open {} for clearing: {}", fb_device_,
                      strerror(errno));
        return false;
    }

    // Get variable screen info
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        spdlog::error("[Fbdev Backend] Cannot get vscreeninfo: {}", strerror(errno));
        close(fd);
        return false;
    }

    // Get fixed screen info
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        spdlog::error("[Fbdev Backend] Cannot get fscreeninfo: {}", strerror(errno));
        close(fd);
        return false;
    }

    // Calculate framebuffer size
    size_t screensize = finfo.smem_len;

    // Map framebuffer to memory
    void* fbp = mmap(nullptr, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        spdlog::error("[Fbdev Backend] Cannot mmap framebuffer: {}", strerror(errno));
        close(fd);
        return false;
    }

    // Determine bytes per pixel from stride
    uint32_t bpp = 32; // Default assumption
    if (vinfo.xres > 0) {
        bpp = (finfo.line_length * 8) / vinfo.xres;
    }

    // Fill framebuffer with the specified color
    // Color is in ARGB format (0xAARRGGBB)
    if (bpp == 32) {
        // 32-bit: fill with ARGB pixels
        uint32_t* pixels = static_cast<uint32_t*>(fbp);
        size_t pixel_count = screensize / 4;
        for (size_t i = 0; i < pixel_count; i++) {
            pixels[i] = color;
        }
    } else if (bpp == 16) {
        // 16-bit RGB565: convert ARGB to RGB565
        uint16_t r = ((color >> 16) & 0xFF) >> 3; // 5 bits
        uint16_t g = ((color >> 8) & 0xFF) >> 2;  // 6 bits
        uint16_t b = (color & 0xFF) >> 3;         // 5 bits
        uint16_t rgb565 = (r << 11) | (g << 5) | b;

        uint16_t* pixels = static_cast<uint16_t*>(fbp);
        size_t pixel_count = screensize / 2;
        for (size_t i = 0; i < pixel_count; i++) {
            pixels[i] = rgb565;
        }
    } else {
        // Fallback: just zero the buffer (black)
        memset(fbp, 0, screensize);
    }

    spdlog::info("[Fbdev Backend] Cleared framebuffer to 0x{:08X} ({}x{}, {}bpp)", color,
                 vinfo.xres, vinfo.yres, bpp);

    // Unmap and close
    munmap(fbp, screensize);
    close(fd);

    return true;
}

bool DisplayBackendFbdev::unblank_display() {
    // Unblank the display using standard Linux framebuffer ioctls.
    // This approach is borrowed from GuppyScreen's lv_drivers patch.
    // Essential on AD5M where other processes may blank the display during boot.

    int fd = open(fb_device_.c_str(), O_RDWR);
    if (fd < 0) {
        spdlog::warn("[Fbdev Backend] Cannot open {} for unblank: {}", fb_device_, strerror(errno));
        return false;
    }

    // 1. Unblank the display via framebuffer ioctl
    if (ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK) != 0) {
        spdlog::warn("[Fbdev Backend] FBIOBLANK unblank failed: {}", strerror(errno));
        // Continue anyway - some drivers don't support this but pan may still work
    } else {
        spdlog::info("[Fbdev Backend] Display unblanked via FBIOBLANK");
    }

    // 2. Get screen info and reset pan position to (0,0)
    // This ensures we're drawing to the visible portion of the framebuffer
    struct fb_var_screeninfo var_info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var_info) != 0) {
        spdlog::warn("[Fbdev Backend] FBIOGET_VSCREENINFO failed: {}", strerror(errno));
        close(fd);
        // Don't return - try Allwinner backlight below
    } else {
        var_info.yoffset = 0;
        if (ioctl(fd, FBIOPAN_DISPLAY, &var_info) != 0) {
            spdlog::debug("[Fbdev Backend] FBIOPAN_DISPLAY failed: {} (may be unsupported)",
                          strerror(errno));
        } else {
            spdlog::info("[Fbdev Backend] Display pan reset to yoffset=0");
        }
    }

    close(fd);

    // NOTE: Allwinner backlight control is NOT done here!
    // BacklightBackend handles all backlight control via /dev/disp ioctls.
    // Having duplicate ioctl calls here and in BacklightBackend can put the
    // Allwinner DISP2 driver into an inverted state where higher values = dimmer.
    // Let DisplayManager's m_backlight->set_brightness() handle backlight.

    return true;
}

bool DisplayBackendFbdev::blank_display() {
    // Blank the display using standard Linux framebuffer ioctl.
    // Counterpart to unblank_display() for proper display sleep.

    int fd = open(fb_device_.c_str(), O_RDWR);
    if (fd < 0) {
        spdlog::warn("[Fbdev Backend] Cannot open {} for blank: {}", fb_device_, strerror(errno));
        return false;
    }

    if (ioctl(fd, FBIOBLANK, FB_BLANK_NORMAL) != 0) {
        spdlog::warn("[Fbdev Backend] FBIOBLANK blank failed: {}", strerror(errno));
        close(fd);
        return false;
    }

    spdlog::info("[Fbdev Backend] Display blanked via FBIOBLANK");
    close(fd);
    return true;
}

bool DisplayBackendFbdev::set_calibration(const helix::TouchCalibration& cal) {
    if (!helix::is_calibration_valid(cal)) {
        spdlog::warn("[Fbdev Backend] Invalid calibration rejected");
        return false;
    }

    // Update stored calibration
    calibration_ = cal;

    // If touch input exists with our custom callback, update the context
    if (touch_) {
        auto* ctx = static_cast<CalibrationContext*>(lv_indev_get_user_data(touch_));
        if (ctx) {
            // Update existing context (points to our member variable)
            ctx->calibration = cal;
            spdlog::info("[Fbdev Backend] Calibration updated at runtime: "
                         "a={:.4f} b={:.4f} c={:.4f} d={:.4f} e={:.4f} f={:.4f}",
                         cal.a, cal.b, cal.c, cal.d, cal.e, cal.f);
        } else {
            // Need to install the callback wrapper for the first time
            calibration_context_.calibration = cal;
            calibration_context_.original_read_cb = lv_indev_get_read_cb(touch_);
            calibration_context_.screen_width = screen_width_;
            calibration_context_.screen_height = screen_height_;

            lv_indev_set_user_data(touch_, &calibration_context_);
            lv_indev_set_read_cb(touch_, calibrated_read_cb);

            spdlog::info("[Fbdev Backend] Calibration callback installed at runtime: "
                         "a={:.4f} b={:.4f} c={:.4f} d={:.4f} e={:.4f} f={:.4f}",
                         cal.a, cal.b, cal.c, cal.d, cal.e, cal.f);
        }
    }

    return true;
}

#endif // HELIX_DISPLAY_FBDEV
