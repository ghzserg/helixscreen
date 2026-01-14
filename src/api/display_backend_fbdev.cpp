// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen - Linux Framebuffer Display Backend Implementation

#ifdef HELIX_DISPLAY_FBDEV

#include "display_backend_fbdev.h"

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

#endif // HELIX_DISPLAY_FBDEV
