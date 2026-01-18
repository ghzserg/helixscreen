// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen - Linux DRM/KMS Display Backend Implementation

#ifdef HELIX_DISPLAY_DRM

#include "display_backend_drm.h"

#include "config.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

// System includes for device access checks and DRM capability detection
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace {

/**
 * @brief Check if a DRM device supports dumb buffers and has a connected display
 *
 * Pi 5 has multiple DRM cards:
 * - card0: v3d (3D only, no display output)
 * - card1: drm-rp1-dsi (DSI touchscreen)
 * - card2: vc4-drm (HDMI output)
 *
 * We need to find one that supports dumb buffers for framebuffer allocation.
 */
bool drm_device_supports_display(const std::string& device_path) {
    int fd = open(device_path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    // Check for dumb buffer support
    uint64_t has_dumb = 0;
    if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 || !has_dumb) {
        close(fd);
        spdlog::debug("[DRM Backend] {}: no dumb buffer support", device_path);
        return false;
    }

    // Check if there's at least one connected connector
    drmModeRes* resources = drmModeGetResources(fd);
    if (!resources) {
        close(fd);
        spdlog::debug("[DRM Backend] {}: failed to get DRM resources", device_path);
        return false;
    }

    bool has_connected = false;
    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (connector) {
            if (connector->connection == DRM_MODE_CONNECTED) {
                has_connected = true;
                spdlog::debug("[DRM Backend] {}: found connected connector type {}", device_path,
                              connector->connector_type);
            }
            drmModeFreeConnector(connector);
            if (has_connected)
                break;
        }
    }

    drmModeFreeResources(resources);
    close(fd);

    if (!has_connected) {
        spdlog::debug("[DRM Backend] {}: no connected displays", device_path);
    }

    return has_connected;
}

/**
 * @brief Auto-detect the best DRM device
 *
 * Priority order for device selection:
 * 1. Environment variable HELIX_DRM_DEVICE (for debugging/testing)
 * 2. Config file /display/drm_device (user preference)
 * 3. Auto-detection: scan /dev/dri/card* for first with dumb buffers + connected display
 *
 * Pi 5 has multiple DRM cards: card0 (v3d, 3D only), card1 (DSI), card2 (vc4/HDMI)
 */
std::string auto_detect_drm_device() {
    // Priority 1: Environment variable override (for debugging/testing)
    const char* env_device = std::getenv("HELIX_DRM_DEVICE");
    if (env_device && env_device[0] != '\0') {
        spdlog::info("[DRM Backend] Using DRM device from HELIX_DRM_DEVICE: {}", env_device);
        return env_device;
    }

    // Priority 2: Config file override
    Config* cfg = Config::get_instance();
    std::string config_device = cfg->get<std::string>("/display/drm_device", "");
    if (!config_device.empty()) {
        spdlog::info("[DRM Backend] Using DRM device from config: {}", config_device);
        return config_device;
    }

    // Priority 3: Auto-detection
    spdlog::info("[DRM Backend] Auto-detecting DRM device...");

    // Scan /dev/dri/card* in order
    DIR* dir = opendir("/dev/dri");
    if (!dir) {
        spdlog::warn("[DRM Backend] Cannot open /dev/dri, falling back to card0");
        return "/dev/dri/card0";
    }

    std::vector<std::string> candidates;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "card", 4) == 0) {
            candidates.push_back(std::string("/dev/dri/") + entry->d_name);
        }
    }
    closedir(dir);

    // Sort to ensure consistent order (card0, card1, card2...)
    std::sort(candidates.begin(), candidates.end());

    for (const auto& candidate : candidates) {
        spdlog::debug("[DRM Backend] Checking DRM device: {}", candidate);
        if (drm_device_supports_display(candidate)) {
            spdlog::info("[DRM Backend] Auto-detected DRM device: {}", candidate);
            return candidate;
        }
    }

    spdlog::warn("[DRM Backend] No suitable DRM device found, falling back to card0");
    return "/dev/dri/card0";
}

} // namespace

DisplayBackendDRM::DisplayBackendDRM() : drm_device_(auto_detect_drm_device()) {}

DisplayBackendDRM::DisplayBackendDRM(const std::string& drm_device) : drm_device_(drm_device) {}

bool DisplayBackendDRM::is_available() const {
    struct stat st;

    // Check if DRM device exists
    if (stat(drm_device_.c_str(), &st) != 0) {
        spdlog::debug("[DRM Backend] DRM device {} not found", drm_device_);
        return false;
    }

    // Check if we can access it
    if (access(drm_device_.c_str(), R_OK | W_OK) != 0) {
        spdlog::debug(
            "[DRM Backend] DRM device {} not accessible (need R/W permissions, check video group)",
            drm_device_);
        return false;
    }

    return true;
}

lv_display_t* DisplayBackendDRM::create_display(int width, int height) {
    spdlog::info("[DRM Backend] Creating DRM display on {}", drm_device_);

    // LVGL's DRM driver
    display_ = lv_linux_drm_create();

    if (display_ == nullptr) {
        spdlog::error("[DRM Backend] Failed to create DRM display");
        return nullptr;
    }

    // Set the DRM device path
    lv_linux_drm_set_file(display_, drm_device_.c_str(), -1);

    spdlog::info("[DRM Backend] DRM display created: {}x{} on {}", width, height, drm_device_);
    return display_;
}

lv_indev_t* DisplayBackendDRM::create_input_pointer() {
    std::string device_override;

    // Priority 1: Environment variable override (for debugging/testing)
    const char* env_device = std::getenv("HELIX_TOUCH_DEVICE");
    if (env_device && env_device[0] != '\0') {
        device_override = env_device;
        spdlog::info("[DRM Backend] Using touch device from HELIX_TOUCH_DEVICE: {}",
                     device_override);
    }

    // Priority 2: Config file override
    if (device_override.empty()) {
        Config* cfg = Config::get_instance();
        device_override = cfg->get<std::string>("/input/touch_device", "");
        if (!device_override.empty()) {
            spdlog::info("[DRM Backend] Using touch device from config: {}", device_override);
        }
    }

    // If we have an explicit device, try it first
    if (!device_override.empty()) {
        pointer_ = lv_libinput_create(LV_INDEV_TYPE_POINTER, device_override.c_str());
        if (pointer_ != nullptr) {
            spdlog::info("[DRM Backend] Libinput pointer device created on {}", device_override);
            return pointer_;
        }
        // Try evdev as fallback for the specified device
        pointer_ = lv_evdev_create(LV_INDEV_TYPE_POINTER, device_override.c_str());
        if (pointer_ != nullptr) {
            spdlog::info("[DRM Backend] Evdev pointer device created on {}", device_override);
            return pointer_;
        }
        spdlog::warn("[DRM Backend] Could not open specified touch device: {}", device_override);
    }

    // Priority 3: Auto-discover using libinput
    // Look for touch or pointer capability devices
    spdlog::info("[DRM Backend] Auto-detecting touch/pointer device via libinput...");

    // Try to find a touch device first (for touchscreens like DSI displays)
    char* touch_path = lv_libinput_find_dev(LV_LIBINPUT_CAPABILITY_TOUCH, true);
    if (touch_path) {
        spdlog::info("[DRM Backend] Found touch device: {}", touch_path);
        pointer_ = lv_libinput_create(LV_INDEV_TYPE_POINTER, touch_path);
        if (pointer_ != nullptr) {
            spdlog::info("[DRM Backend] Libinput touch device created on {}", touch_path);
            return pointer_;
        }
        spdlog::warn("[DRM Backend] Failed to create libinput device for: {}", touch_path);
    }

    // Try pointer devices (mouse, trackpad)
    char* pointer_path = lv_libinput_find_dev(LV_LIBINPUT_CAPABILITY_POINTER, false);
    if (pointer_path) {
        spdlog::info("[DRM Backend] Found pointer device: {}", pointer_path);
        pointer_ = lv_libinput_create(LV_INDEV_TYPE_POINTER, pointer_path);
        if (pointer_ != nullptr) {
            spdlog::info("[DRM Backend] Libinput pointer device created on {}", pointer_path);
            return pointer_;
        }
        spdlog::warn("[DRM Backend] Failed to create libinput device for: {}", pointer_path);
    }

    // Priority 4: Fallback to evdev on common device paths
    spdlog::warn("[DRM Backend] Libinput auto-detection failed, trying evdev fallback");

    // Try event1 first (common for touchscreens on Pi)
    const char* fallback_devices[] = {"/dev/input/event1", "/dev/input/event0"};
    for (const char* dev : fallback_devices) {
        pointer_ = lv_evdev_create(LV_INDEV_TYPE_POINTER, dev);
        if (pointer_ != nullptr) {
            spdlog::info("[DRM Backend] Evdev pointer device created on {}", dev);
            return pointer_;
        }
    }

    spdlog::error("[DRM Backend] Failed to create any input device");
    return nullptr;
}

bool DisplayBackendDRM::clear_framebuffer(uint32_t color) {
    // For DRM, we can try to clear via /dev/fb0 if it exists (legacy fbdev emulation)
    // Many DRM systems provide /dev/fb0 as a compatibility layer
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        spdlog::debug("[DRM Backend] Cannot open /dev/fb0 for clearing (DRM-only system)");
        return false;
    }

    // Get variable screen info
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        spdlog::warn("[DRM Backend] Cannot get vscreeninfo from /dev/fb0");
        close(fd);
        return false;
    }

    // Get fixed screen info
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        spdlog::warn("[DRM Backend] Cannot get fscreeninfo from /dev/fb0");
        close(fd);
        return false;
    }

    // Calculate framebuffer size
    size_t screensize = finfo.smem_len;

    // Map framebuffer to memory
    void* fbp = mmap(nullptr, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbp == MAP_FAILED) {
        spdlog::warn("[DRM Backend] Cannot mmap /dev/fb0 for clearing");
        close(fd);
        return false;
    }

    // Determine bytes per pixel from stride
    uint32_t bpp = 32;
    if (vinfo.xres > 0) {
        bpp = (finfo.line_length * 8) / vinfo.xres;
    }

    // Fill framebuffer with the specified color
    if (bpp == 32) {
        uint32_t* pixels = static_cast<uint32_t*>(fbp);
        size_t pixel_count = screensize / 4;
        for (size_t i = 0; i < pixel_count; i++) {
            pixels[i] = color;
        }
    } else if (bpp == 16) {
        uint16_t r = ((color >> 16) & 0xFF) >> 3;
        uint16_t g = ((color >> 8) & 0xFF) >> 2;
        uint16_t b = (color & 0xFF) >> 3;
        uint16_t rgb565 = (r << 11) | (g << 5) | b;

        uint16_t* pixels = static_cast<uint16_t*>(fbp);
        size_t pixel_count = screensize / 2;
        for (size_t i = 0; i < pixel_count; i++) {
            pixels[i] = rgb565;
        }
    } else {
        memset(fbp, 0, screensize);
    }

    spdlog::info("[DRM Backend] Cleared framebuffer via /dev/fb0 to 0x{:08X}", color);

    munmap(fbp, screensize);
    close(fd);
    return true;
}

#endif // HELIX_DISPLAY_DRM
