// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen - Linux DRM/KMS Display Backend
//
// Modern Linux display backend using Direct Rendering Manager (DRM)
// with Kernel Mode Setting (KMS). Preferred for Raspberry Pi.

#pragma once

#ifdef HELIX_DISPLAY_DRM

#include "display_backend.h"

#include <string>

/**
 * @brief Linux DRM/KMS display backend for modern embedded systems
 *
 * Uses LVGL's DRM driver for hardware-accelerated rendering on
 * systems with GPU support (like Raspberry Pi 4/5).
 *
 * Advantages over framebuffer:
 * - Better performance with GPU acceleration
 * - Proper vsync support
 * - Multiple display support
 * - Modern display pipeline
 *
 * Features:
 * - Direct DRM/KMS access via /dev/dri/card0
 * - Touch input via libinput (preferred) or evdev
 * - Automatic display mode detection
 *
 * Requirements:
 * - /dev/dri/card0 must exist and be accessible
 * - User must be in 'video' and 'input' groups
 * - libdrm and libinput libraries
 */
class DisplayBackendDRM : public DisplayBackend {
  public:
    /**
     * @brief Construct DRM backend with default settings
     *
     * Defaults:
     * - DRM device: /dev/dri/card0
     * - Connector: auto-detect first connected
     */
    DisplayBackendDRM();

    /**
     * @brief Construct DRM backend with custom device path
     *
     * @param drm_device Path to DRM device (e.g., "/dev/dri/card0")
     */
    explicit DisplayBackendDRM(const std::string& drm_device);

    ~DisplayBackendDRM() override = default;

    // Display creation
    lv_display_t* create_display(int width, int height) override;

    // Input device creation
    lv_indev_t* create_input_pointer() override;

    // Display rotation via DRM plane property
    void set_display_rotation(lv_display_rotation_t rot, int phys_w, int phys_h) override;

    // Backend info
    DisplayBackendType type() const override {
        return DisplayBackendType::DRM;
    }
    const char* name() const override {
        return "Linux DRM/KMS";
    }
    bool is_available() const override;
    DetectedResolution detect_resolution() const override;

    // Framebuffer operations
    bool clear_framebuffer(uint32_t color) override;

    // Configuration
    void set_drm_device(const std::string& path) {
        drm_device_ = path;
    }

    /// Whether GPU-accelerated rendering (EGL/OpenGL ES) is active
    bool is_gpu_accelerated() const {
        return using_egl_;
    }

  private:
    std::string drm_device_;
    lv_display_t* display_ = nullptr;
    lv_indev_t* pointer_ = nullptr;
    bool using_egl_ = false; ///< Track if GPU-accelerated path is active
};

#endif // HELIX_DISPLAY_DRM
