#pragma once

#include "lvgl/lvgl.h"

// FontAwesome 6 Free Solid icons - multiple sizes
LV_FONT_DECLARE(fa_icons_64);  // Navigation bar icons
LV_FONT_DECLARE(fa_icons_48);  // Status card icons (large displays)
LV_FONT_DECLARE(fa_icons_32);  // Status card icons (small displays)

// Navigation icons (available in all sizes)
#define ICON_HOME              "\xEF\x80\x95"      // U+F015 house
#define ICON_CONTROLS          "\xEF\x87\x9E"      // U+F1DE sliders
#define ICON_FILAMENT          "\xEF\x95\xB6"      // U+F576 fill-drip
#define ICON_SETTINGS          "\xEF\x80\x93"      // U+F013 gear
#define ICON_ADVANCED          "\xEF\x85\x82"      // U+F142 ellipsis-vertical

// Status card icons (available in 48px and 32px)
#define ICON_TEMPERATURE       "\xEF\x8B\x87"      // U+F2C7 thermometer-half
#define ICON_WIFI              "\xEF\x87\xAB"      // U+F1EB wifi
#define ICON_ETHERNET          "\xEF\x9E\x96"      // U+F796 ethernet
#define ICON_WIFI_SLASH        "\xEF\x84\xA7"      // U+F127 wifi-slash
#define ICON_LIGHTBULB         "\xEF\x83\xAB"      // U+F0EB lightbulb
