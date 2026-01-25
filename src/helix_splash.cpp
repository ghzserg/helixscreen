// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file helix_splash.cpp
 * @brief Minimal splash screen binary for embedded targets
 *
 * This is a lightweight splash screen that starts instantly while the main
 * helix-screen application initializes in parallel. It displays the
 * HelixScreen logo with a fade-in animation (on capable hardware) and
 * automatically exits when the main app takes over the framebuffer.
 *
 * Design goals:
 * - Minimal dependencies (LVGL + display backend only, no libhv/spdlog/etc)
 * - Fast startup (~50ms to first frame)
 * - Automatic handoff when main app opens display
 * - Graceful exit on SIGTERM/SIGINT
 *
 * For desktop development, the main app uses its own splash screen.
 * This binary is only built and used on embedded Linux targets.
 */

#include "backlight_backend.h"
#include "display_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <lvgl.h>
#include <memory>
#include <regex>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Signal handling for graceful shutdown
// SIGTERM/SIGINT: graceful shutdown (e.g., system shutdown)
// SIGUSR1: main app is ready, hand off display immediately
static volatile sig_atomic_t g_quit = 0;

// Define the LVGL assert callback pointer for splash binary
// (normally defined in logging_init.cpp, but splash doesn't link that)
#include "lvgl_assert_handler.h"
helix_assert_callback_t g_helix_assert_cpp_callback = nullptr;

static void signal_handler(int sig) {
    (void)sig;
    g_quit = 1;
}

// Default screen dimensions (can be overridden via command line)
static constexpr int DEFAULT_WIDTH = 800;
static constexpr int DEFAULT_HEIGHT = 480;

// Splash timing
static constexpr int FADE_DURATION_MS = 300; // Fast fade-in (disabled on slow hardware)
static constexpr int FRAME_DELAY_US = 16000; // ~60 FPS

// Read brightness from config file (simple parsing, no JSON library)
// Returns configured brightness (10-100) or default_value on failure
static int read_config_brightness(int default_value = 100) {
    // Try config paths (new location first, then legacy)
    const char* paths[] = {"config/helixconfig.json", "helixconfig.json",
                           "/opt/helixscreen/helixconfig.json"};

    for (const char* path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Simple regex to find "brightness": <number>
        std::regex brightness_regex(R"("brightness"\s*:\s*(\d+))");
        std::smatch match;
        if (std::regex_search(content, match, brightness_regex) && match.size() > 1) {
            int brightness = std::stoi(match[1].str());
            // Clamp to valid range
            if (brightness < 10)
                brightness = 10;
            if (brightness > 100)
                brightness = 100;
            return brightness;
        }
    }

    return default_value;
}

// Dark theme background color (matches app theme)
static constexpr uint32_t BG_COLOR_DARK = 0x121212;

/**
 * @brief Parse command line arguments
 */
static void parse_args(int argc, char** argv, int& width, int& height) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: helix-splash [-w width] [-h height]\n");
            printf("  -w <width>   Screen width (default: %d)\n", DEFAULT_WIDTH);
            printf("  -h <height>  Screen height (default: %d)\n", DEFAULT_HEIGHT);
            exit(0);
        }
    }
}

/**
 * @brief Animation callback for fade-in effect
 */
static void fade_anim_cb(void* obj, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
}

/**
 * @brief Create and configure the splash screen UI
 */
static lv_obj_t* create_splash_ui(lv_obj_t* screen, int width, int height) {
    // Set dark background
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Create container for logo (will be animated)
    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(container, LV_OPA_TRANSP, LV_PART_MAIN); // Start invisible for fade-in
    lv_obj_center(container);

    // Create logo image
    lv_obj_t* logo = lv_image_create(container);

    // Ensure image widget has no visible background/border (fix edge artifact)
    lv_obj_set_style_bg_opa(logo, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(logo, 0, LV_PART_MAIN);

    // Check for pre-rendered image first (AD5M = 800x480 = "small" category)
    // Pre-rendered = exact 400x400 pixels, no decode/scale needed → 60+ FPS
    // PNG fallback = decode + scale each frame → 2-3 FPS on Cortex-A7
    const char* prerendered_path = "assets/images/prerendered/splash-logo-small.bin";
    struct stat st;
    bool use_prerendered = (stat(prerendered_path, &st) == 0);

    if (use_prerendered) {
        // Pre-rendered: instant display, no scaling needed!
        lv_image_set_src(logo, "A:assets/images/prerendered/splash-logo-small.bin");
        fprintf(stderr, "helix-splash: Using pre-rendered splash (fast path)\n");
    } else {
        // PNG fallback with runtime scaling (slow but works)
        const char* logo_path = "A:assets/images/helixscreen-logo.png";
        lv_image_set_src(logo, logo_path);
        fprintf(stderr, "helix-splash: Using PNG fallback (slow path)\n");

        // Scale logo to 50% of screen width (AD5M height 480 < 500)
        lv_image_header_t header;
        if (lv_image_decoder_get_info(logo_path, &header) == LV_RESULT_OK) {
            int target_size = width / 2; // 50% on small screens
            int scale = (target_size * 256) / header.w;
            lv_image_set_scale(logo, scale);
        } else {
            lv_image_set_scale(logo, 128); // Fallback: 50%
        }
    }

    // Fade-in animation: skip on slow hardware (pre-rendered = slow device)
    // Alpha blending each frame is too slow on AD5M's Cortex-A7
    if (use_prerendered) {
        // Slow hardware: show immediately at full opacity
        lv_obj_set_style_opa(container, LV_OPA_COVER, LV_PART_MAIN);
    } else {
        // Fast hardware: smooth fade-in animation
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, container);
        lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&anim, FADE_DURATION_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&anim, fade_anim_cb);
        lv_anim_start(&anim);
    }

    return container;
}

int main(int argc, char** argv) {
    // Set up signal handlers
    // SIGTERM/SIGINT: graceful shutdown
    // SIGUSR1: main app ready, hand off display
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGUSR1, signal_handler);

    // Parse command line arguments
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    parse_args(argc, argv, width, height);

    // Initialize LVGL
    lv_init();

    // Create display backend using shared library
    auto backend = DisplayBackend::create();
    if (!backend) {
        fprintf(stderr, "helix-splash: Failed to create display backend\n");
        return 1;
    }

    // Unblank display via framebuffer ioctl BEFORE creating LVGL display.
    // Essential on AD5M where ForgeX may have blanked the display during boot.
    // Uses same approach as GuppyScreen: FBIOBLANK + FBIOPAN_DISPLAY.
    if (backend->unblank_display()) {
        fprintf(stderr, "helix-splash: Display unblanked via framebuffer ioctl\n");
    }

    // Create display
    lv_display_t* display = backend->create_display(width, height);
    if (!display) {
        fprintf(stderr, "helix-splash: Failed to create display\n");
        return 1;
    }

    // Clear framebuffer to remove any pre-existing content (Linux console text)
    // This must happen AFTER create_display (which opens the framebuffer)
    // but BEFORE we render the splash UI
    if (backend->clear_framebuffer(BG_COLOR_DARK | 0xFF000000)) {
        fprintf(stderr, "helix-splash: Framebuffer cleared\n");
    }

    // Turn on backlight immediately (may have been off from sleep or crash)
    // Use configured brightness instead of hardcoded 100%
    auto backlight = BacklightBackend::create();
    if (backlight && backlight->is_available()) {
        int brightness = read_config_brightness(100);
        backlight->set_brightness(brightness);
        fprintf(stderr, "helix-splash: Backlight ON at %d%%\n", brightness);
    }

    // Create splash UI
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* container = create_splash_ui(screen, width, height);
    (void)container; // Used by animation, no need to track

    // Main loop - run until signaled to quit
    // Exit signals: SIGTERM, SIGINT (shutdown), SIGUSR1 (main app ready)
    while (!g_quit) {
        lv_timer_handler();
        usleep(FRAME_DELAY_US);
    }

    // Clear framebuffer to background color before exit
    // This prevents visual artifacts during handoff to helix-screen
    lv_obj_clean(screen);                                              // Remove all children
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR_DARK), 0); // Ensure bg color
    lv_obj_invalidate(screen);                                         // Mark for redraw
    lv_timer_handler();                                                // Render the clear
    lv_refr_now(nullptr);                                              // Force immediate refresh

    // Cleanup is handled automatically by destructors
    return 0;
}
