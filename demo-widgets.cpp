#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <SDL.h>
#include <cstdio>

int main(int, char**) {
    // Initialize LVGL + SDL
    lv_init();
    lv_sdl_window_create(1024, 800);
    lv_sdl_mouse_create();

    printf("\n");
    printf("╔════════════════════════════════════╗\n");
    printf("║   LVGL Widgets Demo - Explore!     ║\n");
    printf("║                                    ║\n");
    printf("║  • Buttons, sliders, switches      ║\n");
    printf("║  • Charts, meters, spinners        ║\n");
    printf("║  • Lists, dropdowns, calendars     ║\n");
    printf("║  • And much more!                  ║\n");
    printf("║                                    ║\n");
    printf("║  Close window to exit              ║\n");
    printf("╚════════════════════════════════════╝\n");
    printf("\n");

    // Launch the widgets demo
    lv_demo_widgets();

    // Main loop - let LVGL's SDL driver handle all events
    while (lv_display_get_next(NULL)) {
        lv_timer_handler();  // This calls LVGL's internal SDL event handler
        SDL_Delay(5);
    }

    printf("Demo closed. Happy coding!\n");
    lv_deinit();
    return 0;
}
