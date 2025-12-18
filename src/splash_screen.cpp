// SPDX-License-Identifier: GPL-3.0-or-later

#include "splash_screen.h"

#include "ui_theme.h"

#include "helix_timing.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <lvgl.h>

namespace helix {

namespace {

/**
 * @brief Get the path to the pre-rendered splash image for the given screen size
 *
 * Pre-rendered images are EXACT pixel sizes matching splash screen calculations:
 *   - tiny (480x320):   240x240 logo (50% of 480)
 *   - small (800x480):  400x400 logo (50% of 800) - AD5M
 *   - medium (1024x600): 614x614 logo (60% of 1024)
 *   - large (1280x720): 768x768 logo (60% of 1280)
 *
 * @param screen_width Display width in pixels
 * @return Path to pre-rendered .bin file, or empty string if none found
 */
std::string get_prerendered_splash_path(int screen_width) {
    // Select size category based on screen width
    const char* size_name = nullptr;
    if (screen_width <= 480) {
        size_name = "tiny";
    } else if (screen_width <= 800) {
        size_name = "small";
    } else if (screen_width <= 1024) {
        size_name = "medium";
    } else {
        size_name = "large";
    }

    // Build path to pre-rendered image
    std::string path = "build/assets/images/prerendered/splash-logo-";
    path += size_name;
    path += ".bin"; // Must be .bin for LVGL's bin decoder

    // Check if file exists (without A: prefix for filesystem check)
    if (std::filesystem::exists(path)) {
        spdlog::debug("[Splash Screen] Found pre-rendered image: {}", path);
        return "A:" + path; // Add LVGL filesystem prefix
    }

    spdlog::debug("[Splash Screen] No pre-rendered image for size '{}', will use PNG fallback",
                  size_name);
    return "";
}

} // namespace

void show_splash_screen(int screen_width, int screen_height) {
    spdlog::debug("[Splash Screen] Showing splash screen ({}x{})", screen_width, screen_height);

    // Get the active screen
    lv_obj_t* screen = lv_screen_active();

    // Apply theme background color (app_bg_color runtime constant set by ui_theme_init)
    ui_theme_apply_bg_color(screen, "app_bg_color", LV_PART_MAIN);

    // Disable scrollbars on screen
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Create centered container for logo (disable scrolling)
    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);         // Disable scrollbars
    lv_obj_set_style_opa(container, LV_OPA_TRANSP, LV_PART_MAIN); // Start invisible for fade-in
    lv_obj_center(container);

    // Create image widget for logo
    lv_obj_t* logo = lv_image_create(container);

    // Try to use pre-rendered image first (instant display, no PNG decode overhead)
    std::string prerendered_path = get_prerendered_splash_path(screen_width);
    bool using_prerendered = !prerendered_path.empty();

    if (using_prerendered) {
        // Pre-rendered image: already at exact pixel size, NO scaling needed!
        lv_image_set_src(logo, prerendered_path.c_str());
        spdlog::info("[Splash Screen] Using pre-rendered splash (instant display)");
    } else {
        // Fallback to PNG with runtime scaling (slower, but works for any screen size)
        const char* png_path = "A:assets/images/helixscreen-logo.png";
        lv_image_set_src(logo, png_path);

        // Get actual image dimensions for scaling
        lv_image_header_t header;
        lv_result_t res = lv_image_decoder_get_info(png_path, &header);

        if (res == LV_RESULT_OK) {
            // Scale logo to fill more of the screen (60% of screen width)
            lv_coord_t target_size = (screen_width * 3) / 5; // 60% of screen width
            if (screen_height < 500) {                       // Tiny screen
                target_size = screen_width / 2;              // 50% on tiny screens
            }

            // Calculate scale: (target_size * 256) / actual_width
            // LVGL uses 1/256 scale units (256 = 100%, 128 = 50%, etc.)
            uint32_t width = header.w;  // Copy bit-field to local var for logging
            uint32_t height = header.h; // Copy bit-field to local var for logging
            uint32_t scale = (static_cast<uint32_t>(target_size) * 256U) / width;
            lv_image_set_scale(logo, static_cast<uint16_t>(scale));

            spdlog::info("[Splash Screen] PNG fallback: {}x{} scaled to {} (scale factor: {})",
                         width, height, target_size, scale);
        } else {
            spdlog::warn("[Splash Screen] Could not get logo dimensions, using default scale");
            lv_image_set_scale(logo, 128); // 50% scale as fallback
        }
    }

    // Create fade-in animation (0.5 seconds)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, 500); // 500ms = 0.5 seconds
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&anim);

    // Run LVGL timer to process fade-in animation and keep splash visible
    // Total display time: 2 seconds (including 0.5s fade-in)
    uint32_t splash_start = helix::timing::get_ticks();
    uint32_t splash_duration = 2000; // 2 seconds total

    while (helix::timing::get_ticks() - splash_start < splash_duration) {
        lv_timer_handler(); // Process animations and rendering
        helix::timing::delay(5);
    }

    // Clean up splash screen
    lv_obj_delete(container);

    spdlog::debug("[Splash Screen] complete");
}

} // namespace helix
