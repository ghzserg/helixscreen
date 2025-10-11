#include "lvgl/lvgl.h"
#include "lvgl/src/others/xml/lv_xml.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "ui_panel_home.h"
#include <SDL.h>
#include <cstdio>
#include <ctime>

// LVGL display and input
static lv_display_t* display = nullptr;
static lv_indev_t* indev_mouse = nullptr;

// Screen dimensions (default to medium size)
static const int SCREEN_WIDTH = UI_SCREEN_MEDIUM_W;
static const int SCREEN_HEIGHT = UI_SCREEN_MEDIUM_H;

// Initialize LVGL with SDL
static bool init_lvgl() {
    lv_init();

    // LVGL's SDL driver handles window creation internally
    display = lv_sdl_window_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!display) {
        LV_LOG_ERROR("Failed to create LVGL SDL display");
        return false;
    }

    // Create mouse input device
    indev_mouse = lv_sdl_mouse_create();
    if (!indev_mouse) {
        LV_LOG_ERROR("Failed to create LVGL SDL mouse input");
        return false;
    }

    LV_LOG_USER("LVGL initialized: %dx%d", SCREEN_WIDTH, SCREEN_HEIGHT);
    return true;
}

// Save screenshot using SDL renderer
// Simple BMP file writer for ARGB8888 format
static bool write_bmp(const char* filename, const uint8_t* data, int width, int height) {
    FILE* f = fopen(filename, "wb");
    if (!f) return false;

    // BMP header (54 bytes total)
    uint32_t file_size = 54 + (width * height * 4);
    uint32_t pixel_offset = 54;
    uint32_t dib_size = 40;
    uint16_t planes = 1;
    uint16_t bpp = 32;

    // BMP file header (14 bytes)
    fputc('B', f); fputc('M', f);                        // Signature
    fwrite(&file_size, 4, 1, f);                         // File size
    fwrite((uint32_t[]){0}, 4, 1, f);                    // Reserved
    fwrite(&pixel_offset, 4, 1, f);                      // Pixel data offset

    // DIB header (40 bytes)
    fwrite(&dib_size, 4, 1, f);                          // DIB header size
    fwrite(&width, 4, 1, f);                             // Width
    fwrite(&height, 4, 1, f);                            // Height
    fwrite(&planes, 2, 1, f);                            // Planes
    fwrite(&bpp, 2, 1, f);                               // Bits per pixel
    fwrite((uint32_t[]){0}, 4, 1, f);                    // Compression (none)
    uint32_t image_size = width * height * 4;
    fwrite(&image_size, 4, 1, f);                        // Image size
    fwrite((uint32_t[]){2835}, 4, 1, f);                 // X pixels per meter
    fwrite((uint32_t[]){2835}, 4, 1, f);                 // Y pixels per meter
    fwrite((uint32_t[]){0}, 4, 1, f);                    // Colors in palette
    fwrite((uint32_t[]){0}, 4, 1, f);                    // Important colors

    // Write pixel data (BMP is bottom-up, so flip rows)
    for (int y = height - 1; y >= 0; y--) {
        fwrite(data + (y * width * 4), 4, width, f);
    }

    fclose(f);
    return true;
}

static void save_screenshot() {
    // Generate unique filename with timestamp
    char filename[256];
    snprintf(filename, sizeof(filename), "/tmp/ui-screenshot-%lu.bmp",
             (unsigned long)time(NULL));

    // Take snapshot using LVGL's native API (platform-independent)
    lv_obj_t* screen = lv_screen_active();
    lv_draw_buf_t* snapshot = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);

    if (!snapshot) {
        LV_LOG_ERROR("Failed to take screenshot");
        return;
    }

    // Write BMP file
    if (write_bmp(filename, snapshot->data, snapshot->header.w, snapshot->header.h)) {
        LV_LOG_USER("Screenshot saved: %s", filename);
    } else {
        LV_LOG_ERROR("Failed to save screenshot");
    }

    // Free snapshot buffer
    lv_draw_buf_destroy(snapshot);
}

// Main application
int main(int, char**) {
    printf("GuppyScreen UI Prototype\n");
    printf("========================\n");
    printf("Target: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("Nav Width: %d pixels\n", UI_NAV_WIDTH(SCREEN_WIDTH));
    printf("\n");

    // Initialize LVGL (handles SDL internally)
    if (!init_lvgl()) {
        return 1;
    }

    // Create main screen
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, UI_COLOR_PANEL_BG, LV_PART_MAIN);

    // Register fonts and images for XML (must be done before loading components)
    LV_LOG_USER("Registering fonts and images...");
    lv_xml_register_font(NULL, "fa_icons_64", &fa_icons_64);
    lv_xml_register_font(NULL, "fa_icons_48", &fa_icons_48);
    lv_xml_register_font(NULL, "fa_icons_32", &fa_icons_32);
    lv_xml_register_font(NULL, "montserrat_16", &lv_font_montserrat_16);
    lv_xml_register_font(NULL, "montserrat_20", &lv_font_montserrat_20);
    lv_xml_register_font(NULL, "montserrat_28", &lv_font_montserrat_28);
    lv_xml_register_image(NULL, "A:/Users/pbrown/code/guppyscreen/prototype-ui9/assets/images/printer_400.png",
                          "A:/Users/pbrown/code/guppyscreen/prototype-ui9/assets/images/printer_400.png");
    lv_xml_register_image(NULL, "filament_spool",
                          "A:/Users/pbrown/code/guppyscreen/prototype-ui9/assets/images/filament_spool.png");

    // Register XML components (globals first to make constants available)
    LV_LOG_USER("Registering XML components...");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/globals.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/navigation_bar.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/home_panel.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/controls_panel.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/filament_panel.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/settings_panel.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/advanced_panel.xml");
    lv_xml_component_register_from_file("A:/Users/pbrown/code/guppyscreen/prototype-ui9/ui_xml/app_layout.xml");

    // Initialize reactive subjects BEFORE creating XML
    LV_LOG_USER("Initializing reactive subjects...");
    ui_nav_init();  // Navigation system (icon colors, active panel)
    ui_panel_home_init_subjects();  // Home panel data bindings

    // Create entire UI from XML (single component contains everything)
    lv_obj_t* app_layout = (lv_obj_t*)lv_xml_create(screen, "app_layout", NULL);

    // Find navbar and panel widgets
    // app_layout > navbar (child 0), content_area (child 1)
    lv_obj_t* navbar = lv_obj_get_child(app_layout, 0);
    lv_obj_t* content_area = lv_obj_get_child(app_layout, 1);

    // Wire up navigation button click handlers and trigger initial color update
    ui_nav_wire_events(navbar);

    // Find all panel widgets in content area
    lv_obj_t* panels[UI_PANEL_COUNT];
    for (int i = 0; i < UI_PANEL_COUNT; i++) {
        panels[i] = lv_obj_get_child(content_area, i);
    }

    // Register panels with navigation system for show/hide management
    ui_nav_set_panels(panels);

    // Setup home panel observers (panels[0] is home panel)
    ui_panel_home_setup_observers(panels[0]);

    LV_LOG_USER("XML UI created successfully with reactive navigation");

    // Auto-screenshot timer (2 seconds after UI creation)
    uint32_t screenshot_time = SDL_GetTicks() + 2000;
    bool screenshot_taken = false;

    // Main event loop - Let LVGL handle SDL events internally via lv_timer_handler()
    // Loop continues while display exists (exits when window closed)
    while (lv_display_get_next(NULL)) {
        // Auto-screenshot after 2 seconds
        if (!screenshot_taken && SDL_GetTicks() >= screenshot_time) {
            save_screenshot();
            screenshot_taken = true;
        }

        // Run LVGL tasks - internally polls SDL events and processes input
        lv_timer_handler();
        fflush(stdout);
        SDL_Delay(5);  // Small delay to prevent 100% CPU usage
    }

    // Cleanup
    LV_LOG_USER("Shutting down...");
    lv_deinit();  // LVGL handles SDL cleanup internally

    return 0;
}

