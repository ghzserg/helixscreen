// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lvgl_init.h"

#include "ui_fatal_error.h"

#include "config.h"
#include "helix-xml/src/xml/lv_xml.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

namespace helix {

bool init_lvgl(int width, int height, LvglContext& ctx) {
    lv_init();
    lv_xml_init();

    // Create display backend (auto-detects: DRM → framebuffer → SDL)
    ctx.backend = DisplayBackend::create_auto();
    if (!ctx.backend) {
        spdlog::error("[LVGL] No display backend available");
        lv_deinit();
        return false;
    }

    spdlog::info("[LVGL] Using display backend: {}", ctx.backend->name());

    // Create display
    ctx.display = ctx.backend->create_display(width, height);
    if (!ctx.display) {
        spdlog::error("[LVGL] Failed to create display");
        ctx.backend.reset();
        lv_deinit();
        return false;
    }

    // Create pointer input device (mouse/touch)
    ctx.pointer = ctx.backend->create_input_pointer();
    if (!ctx.pointer) {
#if defined(HELIX_DISPLAY_DRM) || defined(HELIX_DISPLAY_FBDEV)
        // On embedded platforms (DRM/fbdev), no input device is fatal - show error screen
        spdlog::error("[LVGL] No input device found - cannot operate touchscreen UI");

        static const char* suggestions[] = {
            "Check /dev/input/event* devices exist",
            "Ensure user is in 'input' group: sudo usermod -aG input $USER",
            "Check touchscreen driver is loaded: dmesg | grep -i touch",
            "Set HELIX_TOUCH_DEVICE=/dev/input/eventX to override",
            "Add \"touch_device\": \"/dev/input/event1\" to helixconfig.json",
            nullptr};

        ui_show_fatal_error("No Input Device",
                            "Could not find or open a touch/pointer input device.\n"
                            "The UI requires an input device to function.",
                            suggestions,
                            30000 // Show for 30 seconds then exit
        );

        return false;
#else
        // On desktop (SDL), continue without pointer - mouse is optional
        spdlog::warn("[LVGL] No pointer input device created - touch/mouse disabled");
#endif
    }

    // Configure scroll behavior from config (improves touchpad/touchscreen scrolling feel)
    // scroll_throw: momentum decay rate (1-99), higher = faster decay, default LVGL is 10
    // scroll_limit: pixels before scrolling starts, lower = more responsive, default LVGL is 10
    if (ctx.pointer) {
        Config* cfg = Config::get_instance();
        int scroll_throw = cfg->get<int>("/input/scroll_throw", 25);
        int scroll_limit = cfg->get<int>("/input/scroll_limit", 15);
        lv_indev_set_scroll_throw(ctx.pointer, static_cast<uint8_t>(scroll_throw));
        lv_indev_set_scroll_limit(ctx.pointer, static_cast<uint8_t>(scroll_limit));
        spdlog::debug("[LVGL] Scroll config: throw={}, limit={}", scroll_throw, scroll_limit);
    }

    // Create keyboard input device (optional - enables physical keyboard input)
    lv_indev_t* indev_keyboard = ctx.backend->create_input_keyboard();
    if (indev_keyboard) {
        spdlog::debug("[LVGL] Physical keyboard input enabled");

        // Create input group for keyboard navigation and text input
        lv_group_t* input_group = lv_group_create();
        lv_group_set_default(input_group);
        lv_indev_set_group(indev_keyboard, input_group);
        spdlog::debug("[LVGL] Created default input group for keyboard");
    }

    spdlog::debug("[LVGL] Initialized: {}x{}", width, height);

    return true;
}

void deinit_lvgl(LvglContext& ctx) {
    ctx.backend.reset();
    ctx.display = nullptr;
    ctx.pointer = nullptr;
    lv_xml_deinit();
    lv_deinit();
}

} // namespace helix
