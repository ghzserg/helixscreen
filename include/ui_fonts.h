/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "lvgl/lvgl.h"

// FontAwesome 6 Free Solid icons - multiple sizes
LV_FONT_DECLARE(fa_icons_64);  // Navigation bar icons
LV_FONT_DECLARE(fa_icons_48);  // Status card icons (large displays)
LV_FONT_DECLARE(fa_icons_32);  // Status card icons (small displays)
LV_FONT_DECLARE(fa_icons_24);  // General UI icons (tiny displays)
LV_FONT_DECLARE(fa_icons_16);  // Metadata icons (small inline)

// Unicode arrows from Arial Unicode - multiple sizes
LV_FONT_DECLARE(arrows_64);    // ←↑→↓↖↗↙↘ at 64px
LV_FONT_DECLARE(arrows_48);    // ←↑→↓↖↗↙↘ at 48px
LV_FONT_DECLARE(arrows_32);    // ←↑→↓↖↗↙↘ at 32px

// Navigation icons (available in 64px)
#define ICON_HOME              "\xEF\x80\x95"      // U+F015 house
#define ICON_CONTROLS          "\xEF\x87\x9E"      // U+F1DE sliders
#define ICON_FILAMENT          "\xEF\x80\x88"      // U+F008 film
#define ICON_SETTINGS          "\xEF\x80\x93"      // U+F013 gear
#define ICON_ADVANCED          "\xEF\x85\x82"      // U+F142 ellipsis-vertical
#define ICON_FOLDER            "\xEF\x81\xBC"      // U+F07C folder-open

// Status card icons (available in 48px and 32px)
#define ICON_TEMPERATURE       "\xEF\x8B\x87"      // U+F2C7 thermometer-half
#define ICON_WIFI              "\xEF\x87\xAB"      // U+F1EB wifi
#define ICON_ETHERNET          "\xEF\x9E\x96"      // U+F796 ethernet
#define ICON_WIFI_SLASH        "\xEF\x84\xA7"      // U+F127 wifi-slash
#define ICON_LIGHTBULB         "\xEF\x83\xAB"      // U+F0EB lightbulb

// Metadata icons (available in 16px)
#define ICON_CLOCK             "\xEF\x80\x97"      // U+F017 clock-o
#define ICON_LEAF              "\xEF\x81\xAC"      // U+F06C leaf

// Detail view icons (available in 32px)
#define ICON_CHEVRON_LEFT      "\xEF\x81\x93"      // U+F053 chevron-left
#define ICON_TRASH             "\xEF\x87\xB8"      // U+F1F8 trash

// View toggle icons (available in 32px)
#define ICON_LIST              "\xEF\x80\xBA"      // U+F03A list
#define ICON_TH_LARGE          "\xEF\x80\x89"      // U+F009 th-large

// Controls panel icons (available in 64px, 32px)
#define ICON_ARROWS_ALL        "\xEF\x82\xB2"      // U+F0B2 arrows-up-down-left-right
#define ICON_FIRE              "\xEF\x81\xAD"      // U+F06D fire
#define ICON_ARROW_UP_LINE     "\xEF\x8D\x82"      // U+F342 arrow-up-from-line
#define ICON_FAN               "\xEF\xA1\xA3"      // U+F863 fan
#define ICON_POWER_OFF         "\xEF\x80\x91"      // U+F011 power-off

// Motion control icons - using Unicode arrows (from separate arrows_XX fonts)
#define ICON_ARROW_UP          "\xE2\x86\x91"      // U+2191 upwards arrow ↑
#define ICON_ARROW_DOWN        "\xE2\x86\x93"      // U+2193 downwards arrow ↓
#define ICON_ARROW_LEFT        "\xE2\x86\x90"      // U+2190 leftwards arrow ←
#define ICON_ARROW_RIGHT       "\xE2\x86\x92"      // U+2192 rightwards arrow →
#define ICON_CHEVRON_UP        "\xEF\x81\xB7"      // U+F077 chevron-up
#define ICON_CHEVRON_DOWN      "\xEF\x81\xB8"      // U+F078 chevron-down

// Diagonal direction icons (text labels for now, icons TBD)
#define ICON_ARROW_UP_LEFT     "\xE2\x86\x96"      // Unicode ↖ (fallback)
#define ICON_ARROW_UP_RIGHT    "\xE2\x86\x97"      // Unicode ↗ (fallback)
#define ICON_ARROW_DOWN_LEFT   "\xE2\x86\x99"      // Unicode ↙ (fallback)
#define ICON_ARROW_DOWN_RIGHT  "\xE2\x86\x98"      // Unicode ↘ (fallback)

// Keypad icons (available in 32px)
#define ICON_BACKSPACE         "\xEF\x95\x9A"      // U+F55A delete-left

// Filament operations icons (available in 32px)
#define ICON_ARROW_DOWN_TO_LINE   "\xEF\x8C\xBD"   // U+F33D arrow-down-to-line (load)
#define ICON_ARROW_UP_FROM_LINE   "\xEF\x8D\x82"   // U+F342 arrow-up-from-line (unload)
#define ICON_DROPLET              "\xEF\x81\x83"   // U+F043 droplet (purge)
#define ICON_CUBE                 "\xEF\x86\xB2"   // U+F1B2 cube (PETG)
#define ICON_EDIT                 "\xEF\x81\x84"   // U+F044 edit (custom)
#define ICON_TRIANGLE_EXCLAMATION "\xEF\x81\xB1"   // U+F071 triangle-exclamation (warning)
