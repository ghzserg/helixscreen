// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen - SDL Display Backend Implementation

#ifdef HELIX_DISPLAY_SDL

#include "display_backend_sdl.h"

#include <spdlog/spdlog.h>

// LVGL SDL driver
#include <lvgl.h>

// SDL2 headers
#include <SDL.h>

bool DisplayBackendSDL::is_available() const {
    // SDL is always "available" on desktop - actual initialization
    // happens in create_display() which can fail more gracefully
    return true;
}

lv_display_t* DisplayBackendSDL::create_display(int width, int height) {
    spdlog::debug("[SDL Backend] Creating SDL display: {}x{}", width, height);

    // LVGL's SDL driver handles SDL_Init and window creation internally
    display_ = lv_sdl_window_create(width, height);

    if (display_ == nullptr) {
        spdlog::error("[SDL Backend] Failed to create SDL display");
        return nullptr;
    }

    spdlog::info("[SDL Backend] SDL display created: {}x{}", width, height);
    return display_;
}

lv_indev_t* DisplayBackendSDL::create_input_pointer() {
    if (display_ == nullptr) {
        spdlog::error("[SDL Backend] Cannot create input device without display");
        return nullptr;
    }

    mouse_ = lv_sdl_mouse_create();

    if (mouse_ == nullptr) {
        spdlog::error("[SDL Backend] Failed to create SDL mouse input");
        return nullptr;
    }

    spdlog::debug("[SDL Backend] SDL mouse input created");
    return mouse_;
}

lv_indev_t* DisplayBackendSDL::create_input_keyboard() {
    if (display_ == nullptr) {
        spdlog::error("[SDL Backend] Cannot create keyboard without display");
        return nullptr;
    }

    keyboard_ = lv_sdl_keyboard_create();

    if (keyboard_ == nullptr) {
        spdlog::warn("[SDL Backend] Failed to create SDL keyboard input");
        return nullptr;
    }

    spdlog::debug("[SDL Backend] SDL keyboard input created");
    return keyboard_;
}

#endif // HELIX_DISPLAY_SDL
