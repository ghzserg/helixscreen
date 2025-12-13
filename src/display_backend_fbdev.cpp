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
#include <sys/stat.h>
#include <unistd.h>

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
        spdlog::debug("[Fbdev Backend] Framebuffer device {} not accessible (need R/W permissions)", fb_device_);
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

    spdlog::info("[Fbdev Backend] Framebuffer display created: {}x{} on {}", width, height, fb_device_);
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
    // Check environment variable first
    const char* env_device = std::getenv("HELIX_TOUCH_DEVICE");
    if (env_device != nullptr && strlen(env_device) > 0) {
        spdlog::debug("[Fbdev Backend] Using touch device from HELIX_TOUCH_DEVICE: {}", env_device);
        return env_device;
    }

    // Scan /dev/input/ for event devices
    // On AD5M, /dev/input/event4 is typically the touch device
    // We look for the most recently accessed event device as a heuristic

    const char* input_dir = "/dev/input";
    DIR* dir = opendir(input_dir);
    if (dir == nullptr) {
        spdlog::debug("[Fbdev Backend] Cannot open {}", input_dir);
        return "/dev/input/event0"; // Default fallback
    }

    std::string best_device;
    time_t best_time = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Look for eventN devices
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        std::string device_path = std::string(input_dir) + "/" + entry->d_name;

        struct stat st;
        if (stat(device_path.c_str(), &st) == 0) {
            // Check if accessible
            if (access(device_path.c_str(), R_OK) == 0) {
                // Use the most recently accessed device
                if (st.st_atime > best_time) {
                    best_time = st.st_atime;
                    best_device = device_path;
                }
            }
        }
    }

    closedir(dir);

    if (best_device.empty()) {
        spdlog::debug("[Fbdev Backend] No accessible event device found, using default");
        return "/dev/input/event0";
    }

    spdlog::debug("[Fbdev Backend] Auto-detected touch device: {}", best_device);
    return best_device;
}

#endif // HELIX_DISPLAY_FBDEV
