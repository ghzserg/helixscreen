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
#include "helix_version.h"

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
static constexpr int FADE_DURATION_MS = 1000; // Fade-in duration
static constexpr int FRAME_DELAY_US = 16000;  // ~60 FPS

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

// Background colors for each mode
static constexpr uint32_t BG_COLOR_DARK = 0x121212;    // App theme dark background
static constexpr uint32_t BG_COLOR_3D_DARK = 0x2D2D2D; // 3D splash dark (sampled from image edges)
static constexpr uint32_t BG_COLOR_3D_LIGHT =
    0xDBDBDF; // 3D splash light (sampled from image edges)

// Read dark_mode setting from config file (same parsing approach as brightness)
// Returns configured value or default_value on failure
static bool read_config_dark_mode(bool default_value = true) {
    const char* paths[] = {"config/helixconfig.json", "helixconfig.json",
                           "/opt/helixscreen/helixconfig.json"};

    for (const char* path : paths) {
        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Simple regex to find "dark_mode": true/false
        std::regex dark_mode_regex(R"("dark_mode"\s*:\s*(true|false))");
        std::smatch match;
        if (std::regex_search(content, match, dark_mode_regex) && match.size() > 1) {
            bool result = (match[1].str() == "true");
            fprintf(stderr, "helix-splash: dark_mode=%s (from %s)\n", result ? "true" : "false",
                    path);
            return result;
        }
    }

    return default_value;
}

// Get size name for a screen width (matches prerendered_images.cpp logic)
static const char* get_splash_3d_size_name(int screen_width, int screen_height) {
    if (screen_width < 600) {
        // Distinguish K1 (480x400) from generic tiny (480x320)
        return (screen_height >= 380) ? "tiny_alt" : "tiny";
    }
    if (screen_width < 900)
        return "small";
    if (screen_width < 1100)
        return "medium";
    return "large";
}

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
 *
 * Tries 3D full-screen splash first (dark/light based on config),
 * falls back to pre-rendered logo, then to PNG with runtime scaling.
 */
static lv_obj_t* create_splash_ui(lv_obj_t* screen, int width, int height, bool dark_mode,
                                  bool use_fade) {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Try full-screen 3D splash first
    const char* size_name = get_splash_3d_size_name(width, height);
    const char* mode_name = dark_mode ? "dark" : "light";

    // Build path to 3D splash image
    char splash_3d_path[128];
    snprintf(splash_3d_path, sizeof(splash_3d_path),
             "assets/images/prerendered/splash-3d-%s-%s.bin", mode_name, size_name);

    struct stat st;
    bool use_3d = (stat(splash_3d_path, &st) == 0);

    // Fallback: try base "tiny" if tiny_alt not found
    if (!use_3d && strcmp(size_name, "tiny_alt") == 0) {
        snprintf(splash_3d_path, sizeof(splash_3d_path),
                 "assets/images/prerendered/splash-3d-%s-tiny.bin", mode_name);
        use_3d = (stat(splash_3d_path, &st) == 0);
    }

    // Also check for 3D source PNG fallback
    char splash_3d_png[128];
    bool use_3d_png = false;
    if (!use_3d) {
        snprintf(splash_3d_png, sizeof(splash_3d_png), "assets/images/helixscreen-logo-3d-%s.png",
                 mode_name);
        use_3d_png = (stat(splash_3d_png, &st) == 0);
    }

    if (use_3d || use_3d_png) {
        // 3D splash: prerendered bin (full-screen) or source PNG (centered + scaled)
        uint32_t bg_color = dark_mode ? BG_COLOR_3D_DARK : BG_COLOR_3D_LIGHT;
        lv_obj_set_style_bg_color(screen, lv_color_hex(bg_color), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        lv_obj_t* img = lv_image_create(screen);
        lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(img, 0, LV_PART_MAIN);

        if (use_3d) {
            // Prerendered bin: full-screen, no scaling needed
            char lvgl_path[140];
            snprintf(lvgl_path, sizeof(lvgl_path), "A:%s", splash_3d_path);
            lv_image_set_src(img, lvgl_path);
            fprintf(stderr, "helix-splash: Using 3D splash (%s, %s, fade=%s)\n", mode_name,
                    size_name, use_fade ? "yes" : "no");
        } else {
            // Source PNG fallback: scale to fit screen width
            char lvgl_path[140];
            snprintf(lvgl_path, sizeof(lvgl_path), "A:%s", splash_3d_png);
            lv_image_set_src(img, lvgl_path);

            lv_image_header_t header;
            if (lv_image_decoder_get_info(lvgl_path, &header) == LV_RESULT_OK && header.w > 0 &&
                header.h > 0) {
                // Fit to screen with 10% vertical margin (5% top + 5% bottom)
                int usable_height = (height * 9) / 10;
                int scale_w = (width * 256) / header.w;
                int scale_h = (usable_height * 256) / header.h;
                int scale = (scale_w < scale_h) ? scale_w : scale_h;
                lv_image_set_scale(img, scale);
                fprintf(stderr, "helix-splash: Using 3D PNG fallback (%s, %dx%d scale=%d)\n",
                        mode_name, (int)header.w, (int)header.h, scale);
            } else {
                fprintf(stderr, "helix-splash: 3D PNG loaded but could not get dimensions\n");
            }
        }

        lv_obj_center(img);

        if (use_fade) {
            lv_obj_set_style_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, img);
            lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_duration(&anim, FADE_DURATION_MS);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
            lv_anim_set_exec_cb(&anim, fade_anim_cb);
            lv_anim_start(&anim);
        } else {
            lv_obj_set_style_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        }
        return img;
    }

    // Fallback: original centered logo approach
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

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

    // Check for pre-rendered logo image (centered, not full-screen)
    const char* prerendered_path = "assets/images/prerendered/splash-logo-small.bin";
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

    if (use_fade) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, container);
        lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&anim, FADE_DURATION_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&anim, fade_anim_cb);
        lv_anim_start(&anim);
    } else {
        lv_obj_set_style_opa(container, LV_OPA_COVER, LV_PART_MAIN);
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

    // Parse command line arguments (CLI overrides auto-detection)
    int width = 0;
    int height = 0;
    parse_args(argc, argv, width, height);

    // Initialize LVGL
    lv_init();

    // Create display backend — force fbdev for splash to avoid DRM master contention.
    // On DRM systems, only one process can hold the master lease. If splash takes it,
    // helix-screen can't flush frames until splash dies. Using fbdev for splash avoids
    // this entirely since fbdev has no master concept. DRM is still used for resolution
    // detection (read-only, no master needed).
    auto backend = DisplayBackend::create(DisplayBackendType::FBDEV);
    if (!backend) {
        // Fallback to auto-detect if fbdev isn't available (e.g. desktop/SDL)
        backend = DisplayBackend::create();
    }
    if (!backend) {
        fprintf(stderr, "helix-splash: Failed to create display backend\n");
        return 1;
    }

    // Auto-detect resolution from display hardware if not overridden via CLI
    if (width == 0 || height == 0) {
        auto res = backend->detect_resolution();
        if (res.valid) {
            width = res.width;
            height = res.height;
            fprintf(stderr, "helix-splash: Auto-detected resolution: %dx%d\n", width, height);
        } else {
            width = DEFAULT_WIDTH;
            height = DEFAULT_HEIGHT;
            fprintf(stderr, "helix-splash: Using default resolution: %dx%d\n", width, height);
        }
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

    // Read dark mode preference from config (before framebuffer clear so we use the right color)
    bool dark_mode = read_config_dark_mode(true);

    // Clear framebuffer to remove any pre-existing content (Linux console text)
    // This must happen AFTER create_display (which opens the framebuffer)
    // but BEFORE we render the splash UI
    // Use 3D splash bg color if available, otherwise dark theme bg
    uint32_t clear_color = dark_mode ? BG_COLOR_3D_DARK : BG_COLOR_3D_LIGHT;
    if (backend->clear_framebuffer(clear_color | 0xFF000000)) {
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
    // Fade-in animation only on DRM/SDL backends (fbdev doesn't alpha-blend well)
    bool use_fade = (backend->type() != DisplayBackendType::FBDEV);
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* splash_widget = create_splash_ui(screen, width, height, dark_mode, use_fade);
    (void)splash_widget; // Used by animation, no need to track

    // Version number in lower-right corner (subtle, theme-aware)
    lv_obj_t* version_label = lv_label_create(screen);
    lv_label_set_text(version_label, "v" HELIX_VERSION);
    lv_obj_set_style_text_color(
        version_label, dark_mode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_opa(version_label, LV_OPA_40, LV_PART_MAIN);
    lv_obj_align(version_label, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
    (void)version_label;

    // On fbdev, other processes can write directly to /dev/fb0 behind LVGL's back
    // (e.g., ForgeX S99root boot messages). DRM/SDL are not susceptible since DRM
    // requires master access and SDL is windowed. Periodic full invalidation on fbdev
    // forces LVGL to repaint the entire screen, self-healing any stomped pixels.
    bool needs_fb_self_heal = (backend->type() == DisplayBackendType::FBDEV);

    // Main loop - run until signaled to quit
    // Exit signals: SIGTERM, SIGINT (shutdown), SIGUSR1 (main app ready)
    int frame_count = 0;
    while (!g_quit) {
        lv_timer_handler();
        usleep(FRAME_DELAY_US);

        // Force full redraw every ~500ms (30 frames at 60fps) on fbdev only
        if (needs_fb_self_heal && ++frame_count >= 30) {
            lv_obj_invalidate(screen);
            frame_count = 0;
        }
    }

    // Clear framebuffer to background color before exit
    // This prevents visual artifacts during handoff to helix-screen
    lv_obj_clean(screen);                                            // Remove all children
    lv_obj_set_style_bg_color(screen, lv_color_hex(clear_color), 0); // Match splash bg
    lv_obj_invalidate(screen);                                       // Mark for redraw
    lv_timer_handler();                                              // Render the clear
    lv_refr_now(nullptr);                                            // Force immediate refresh

    // Cleanup is handled automatically by destructors
    return 0;
}
