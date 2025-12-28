// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstring>

// =============================================================================
// Material Design Icons - Codepoint Mapping for Font-Based Icon System
//
// This file maps icon short names to their UTF-8 encoded MDI codepoints.
// MDI uses Unicode Plane 15 Private Use Area (0xF0000+)
// UTF-8 encoding: 4 bytes per codepoint
//
// Formula: F3 B0+((cp>>12)&0x0F) 80+((cp>>6)&0x3F) 80+(cp&0x3F)
// Example: F02DC -> F3 B0 8B 9C
//
// Source: https://pictogrammers.com/library/mdi/
// =============================================================================

namespace ui_icon {

// Icon codepoint mapping structure
struct IconMapping {
    const char* name;
    const char* codepoint;
};

// clang-format off

// Complete icon mapping table
// Sorted alphabetically by name for efficient binary search
static const IconMapping ICON_MAP[] = {
    // Alert/Status icons
    {"alert",              "\xF3\xB0\x80\xA6"},  // F0026 alert/triangle-exclamation
    {"alert_octagon",      "\xF3\xB0\x80\xA9"},  // F0029 alert-octagon
    {"animation",          "\xF3\xB0\x97\x98"},  // F05D8 animation (stacked rectangles)
    {"animation_play",     "\xF3\xB0\xA4\xBA"},  // F093A animation-play (framerate)

    // Arrow icons
    {"arrow_down",         "\xF3\xB0\x81\x85"},  // F0045 arrow-down (head descends)
    {"arrow_expand_down",  "\xF3\xB0\x9E\x93"},  // F0793 arrow-expand-down (bed drops - CoreXY)
    {"arrow_expand_up",    "\xF3\xB0\x9E\x96"},  // F0796 arrow-expand-up (bed rises - CoreXY)
    {"arrow_left",         "\xF3\xB0\x81\x8D"},  // F004D arrow-left
    {"arrow_left_bold",    "\xF3\xB0\x9C\xB1"},  // F0731 arrow-left-bold
    {"arrow_right",        "\xF3\xB0\x81\x94"},  // F0054 arrow-right
    {"arrow_right_bold",   "\xF3\xB0\x9C\xB4"},  // F0734 arrow-right-bold
    {"arrow_up",           "\xF3\xB0\x81\x9D"},  // F005D arrow-up (head ascends)
    {"arrow_up_circle",    "\xF3\xB0\xB3\xA1"},  // F0CE1 arrow-up-circle (load/activate)
    {"arrow_up_right",     "\xF3\xB1\x9E\xBF"},  // F17BF arrow-up-right (retraction)
    {"axis_arrow",         "\xF3\xB0\xB5\x89"},  // F0D49 axis-arrow (all 3 axes)
    {"axis_x_arrow",       "\xF3\xB0\xB5\x8C"},  // F0D4C axis-x-arrow
    {"axis_y_arrow",       "\xF3\xB0\xB5\x91"},  // F0D51 axis-y-arrow
    {"axis_z_arrow",       "\xF3\xB0\xB5\x95"},  // F0D55 axis-z-arrow

    // Back/navigation
    {"back",               "\xF3\xB0\x85\x81"},  // F0141 chevron-left (back)
    {"bed",                "\xF3\xB0\x8B\xA3"},  // F02E3 bed
    {"bell",               "\xF3\xB0\x82\x9A"},  // F009A bell/notifications
    {"block_helper",       "\xF3\xB0\x82\xAD"},  // F00AD block-helper/prohibited

    // Cancel/Close/Check
    {"cancel",             "\xF3\xB0\x9C\xBA"},  // F073A cancel
    {"chart_line",         "\xF3\xB0\x84\xAA"},  // F012A chart-line
    {"check",              "\xF3\xB0\x84\xAC"},  // F012C check
    {"check_circle",       "\xF3\xB0\x97\xA1"},  // F05E1 check-circle-outline
    {"chevron_down",       "\xF3\xB0\x85\x80"},  // F0140 chevron-down
    {"chevron_left",       "\xF3\xB0\x85\x81"},  // F0141 chevron-left
    {"chevron_right",      "\xF3\xB0\x85\x82"},  // F0142 chevron-right
    {"chevron_up",         "\xF3\xB0\x85\x83"},  // F0143 chevron-up
    {"clock",              "\xF3\xB0\x85\x90"},  // F0150 clock-outline
    {"close",              "\xF3\xB0\x85\x96"},  // F0156 close/xmark
    {"close_circle",       "\xF3\xB0\x85\x99"},  // F0159 close-circle
    {"code_braces",        "\xF3\xB0\x85\xA9"},  // F0169 code-braces
    {"code_tags",          "\xF3\xB0\x85\xB4"},  // F0174 code-tags
    {"cog",                "\xF3\xB0\x92\x93"},  // F0493 cog/settings
    {"console",            "\xF3\xB0\x86\x8D"},  // F018D console/terminal
    {"cooldown",           "\xF3\xB0\x9C\x97"},  // F0717 snowflake
    {"cube",               "\xF3\xB1\xA8\xAF"},  // F1A2F cube-outline
    {"cursor_move",        "\xF3\xB0\x86\xBE"},  // F01BE cursor-move (4-way arrows)

    // Dashboard
    {"dashboard",          "\xF3\xB0\x95\xAE"},  // F056E view-dashboard
    {"delete",             "\xF3\xB0\x86\xB4"},  // F01B4 delete/trash
    {"dots_vertical",      "\xF3\xB0\x87\x99"},  // F01D9 dots-vertical/advanced
    {"download",           "\xF3\xB0\x87\x9A"},  // F01DA download

    // Engine/Motor
    {"engine",             "\xF3\xB0\x87\xBA"},  // F01FA engine/motor
    {"engine_off",         "\xF3\xB0\xA9\x86"},  // F0A46 engine-off/motor-off
    {"ethernet",           "\xF3\xB0\x88\x80"},  // F0200 ethernet
    {"expand_all",         "\xF3\xB0\x81\x8C"},  // F004C arrow-expand-all/move

    // Fan icons
    {"fan",                "\xF3\xB0\x88\x90"},  // F0210 fan
    {"fan_off",            "\xF3\xB0\xA0\x9D"},  // F081D fan-off
    {"filament",           "\xF3\xB0\xB9\x9B"},  // F0E5B printer-3d-nozzle
    {"fine_tune",          "\xF3\xB0\x98\xAE"},  // F062E tune
    {"fire",               "\xF3\xB0\x88\xB8"},  // F0238 fire
    {"folder",             "\xF3\xB0\x89\x8B"},  // F024B folder
    {"folder_open",        "\xF3\xB0\x9D\xB0"},  // F0770 folder-open
    {"folder_upload",      "\xF3\xB0\x89\x99"},  // F0259 folder-upload

    // Grid icons
    {"grid_large",         "\xF3\xB0\x9D\x98"},  // F0758 grid-large
    {"grid_view",          "\xF3\xB0\x95\xB0"},  // F0570 view-grid

    // Heat/Heating
    {"heat_wave",          "\xF3\xB1\xA9\x85"},  // F1A45 heat-wave (thermal lines)
    {"heater",             "\xF3\xB1\xA2\xB8"},  // F18B8 printer-3d-nozzle-heat
    {"help_circle",        "\xF3\xB0\x8B\x97"},  // F02D7 help-circle/question
    {"home",               "\xF3\xB0\x8B\x9C"},  // F02DC home
    {"home_import",        "\xF3\xB0\xBE\x9C"},  // F0F9C home-import-outline (home-z)
    {"hourglass",          "\xF3\xB0\x8F\x94"},  // F03D4 hourglass (approx, need exact)

    // Info/Image
    {"image_broken_variant", "\xF3\xB0\x8B\xAE"}, // F02EE image-broken-variant (fallback)
    {"info",               "\xF3\xB0\x8B\xBC"},  // F02FC information
    {"info_outline",       "\xF3\xB0\x8B\xBD"},  // F02FD information-outline
    {"inputshaper",        "\xF3\xB0\xA5\x9B"},  // F095B sine-wave
    {"inventory",          "\xF3\xB0\x8F\x96"},  // F03D6 package-variant

    // LAN/Network
    {"lan",                "\xF3\xB0\x8C\x97"},  // F0317 lan
    {"layers",             "\xF3\xB0\x8C\xA8"},  // F0328 layers
    {"leaf",               "\xF3\xB0\x90\xB6"},  // F0436 leaf
    {"light",              "\xF3\xB0\x8C\xB5"},  // F0335 lightbulb
    {"light_off",          "\xF3\xB0\xB9\x8F"},  // F0E4F lightbulb-off
    {"limit",              "\xF3\xB0\xB9\x8F"},  // Use same as light_off or find better
    {"list",               "\xF3\xB0\x89\xB9"},  // F0279 format-list-bulleted
    {"lock",               "\xF3\xB0\x8C\xBE"},  // F033E lock

    // Misc/Math
    {"minus",              "\xF3\xB0\x8D\xB4"},  // F0374 minus
    {"move",               "\xF3\xB0\x81\x8C"},  // F004C arrow-expand-all
    {"network",            "\xF3\xB0\x88\x80"},  // F0200 ethernet (network)
    {"notifications",      "\xF3\xB0\x82\x9A"},  // F009A bell

    // Pause/Pencil/Play/Plus
    {"pause",              "\xF3\xB0\x8F\xA4"},  // F03E4 pause
    {"pencil",             "\xF3\xB0\x8F\xAB"},  // F03EB pencil (edit)
    {"play",               "\xF3\xB0\x90\x8A"},  // F040A play
    {"play_circle",        "\xF3\xB0\x90\x8C"},  // F040C play-circle
    {"plus",               "\xF3\xB0\x90\x95"},  // F0415 plus
    {"power",              "\xF3\xB0\x90\xA5"},  // F0425 power
    {"power_plug",         "\xF3\xB0\x9A\xA5"},  // F06A5 power-plug
    {"print",              "\xF3\xB0\x90\xAA"},  // F042A printer
    {"printer_3d",         "\xF3\xB0\x90\xAB"},  // F042B printer-3d
    {"progress_clock",     "\xF3\xB0\xA6\x96"},  // F0996 progress-clock (phase tracking)
    {"prohibited",         "\xF3\xB0\x82\xAD"},  // F00AD block-helper
    {"puzzle_outline",     "\xF3\xB0\xA9\xA6"},  // F0A66 puzzle-outline (plugin)
    {"rabbit",             "\xF3\xB0\xA4\x87"},  // F0907 rabbit (Happy Hare logo)

    // Radiator/Heating
    {"radiator",           "\xF3\xB0\x90\xB8"},  // F0438 radiator
    {"redo",               "\xF3\xB0\x91\x8E"},  // F044E redo (clockwise arrow - tighten)
    {"refresh",            "\xF3\xB0\x91\x90"},  // F0450 refresh
    {"resume",             "\xF3\xB0\x90\x8C"},  // F040C play-circle (resume)
    {"rotate_3d",          "\xF3\xB0\xBB\x87"},  // F0EC7 rotate-3d (orbit view)
    {"rotate_left",        "\xF3\xB0\x91\xA5"},  // F0465 rotate-left (CCW)
    {"rotate_right",       "\xF3\xB0\x91\xA7"},  // F0467 rotate-right (CW)
    {"router",             "\xF3\xB0\x91\xA9"},  // F0469 router-wireless
    {"ruler",              "\xF3\xB0\x91\xAD"},  // F046D ruler (print height)

    // SD card / Send
    {"sd",                 "\xF3\xB0\x91\xB9"},  // F0479 sd
    {"send",               "\xF3\xB0\x92\x8A"},  // F048A send
    {"settings",           "\xF3\xB0\x92\x93"},  // F0493 cog
    {"sine_wave",          "\xF3\xB0\xA5\x9B"},  // F095B sine-wave
    {"source_branch",      "\xF3\xB0\x98\xAC"},  // F062C source-branch (bypass/fork)
    {"speed",              "\xF3\xB0\x93\x85"},  // F04C5 speedometer
    {"speed_down",         "\xF3\xB0\xBE\x86"},  // F0F86 speedometer-slow
    {"speed_up",           "\xF3\xB0\xBE\x85"},  // F0F85 speedometer-medium
    {"spoolman",           "\xF3\xB0\x88\xAF"},  // F022F film (same as filament)
    {"stop",               "\xF3\xB0\x93\x9B"},  // F04DB stop
    {"sysinfo",            "\xF3\xB0\x8B\xBC"},  // F02FC information

    // Temperature
    {"thermometer",        "\xF3\xB0\x94\x8F"},  // F050F thermometer
    {"thermometer_minus",  "\xF3\xB0\xB8\x84"},  // F0E04 thermometer-minus
    {"thermometer_plus",   "\xF3\xB0\xB8\x85"},  // F0E05 thermometer-plus
    {"tortoise",           "\xF3\xB0\xB4\x97"},  // F0D17 tortoise (AFC/Box Turtle logo)
    {"train_flatbed",      "\xF3\xB1\xAC\xB5"},  // F1B35 train-car-flatbed (print bed base)
    {"trash_can_outline",  "\xF3\xB0\xA9\xBA"},  // F0A7A trash-can-outline (delete)
    {"tray_arrow_up",      "\xF3\xB0\x84\xA0"},  // F0120 tray-arrow-up (unload/eject)
    {"tune",               "\xF3\xB0\x98\xAE"},  // F062E tune
    {"undo",               "\xF3\xB0\x95\x8C"},  // F054C undo (counter-clockwise arrow - loosen)
    {"tune_variant",       "\xF3\xB0\xA3\xA9"},  // F08E9 tune-variant
    {"tune_vertical_variant", "\xF3\xB1\x95\x83"},  // F1543 tune-vertical-variant

    // Update
    {"update",             "\xF3\xB0\x9A\xB0"},  // F06B0 update

    // Vibrate/Video/View icons
    {"vibrate",            "\xF3\xB0\x95\xA6"},  // F0566 vibrate
    {"video",              "\xF3\xB0\x95\xA7"},  // F0567 video (timelapse)
    {"view_dashboard",     "\xF3\xB0\x95\xAE"},  // F056E view-dashboard
    {"view_grid",          "\xF3\xB0\x95\xB0"},  // F0570 view-grid
    {"view_list",          "\xF3\xB0\x95\xB2"},  // F0572 view-list

    // Water/Weight/WiFi icons
    {"water",              "\xF3\xB0\x96\x8C"},  // F058C water/droplet
    {"weight",             "\xF3\xB0\x96\xA1"},  // F05A1 weight
    {"wifi",               "\xF3\xB0\x96\xA9"},  // F05A9 wifi
    {"wifi_alert",         "\xF3\xB1\x9A\xB5"},  // F16B5 wifi-alert
    {"wifi_check",         "\xF3\xB1\x9A\xBD"},  // F16BD wifi-check
    {"wifi_lock",          "\xF3\xB1\x9A\xBF"},  // F16BF wifi-lock
    {"wifi_off",           "\xF3\xB0\x96\xAA"},  // F05AA wifi-off
    {"wifi_strength_1",    "\xF3\xB0\xA4\x9F"},  // F091F wifi-strength-1
    {"wifi_strength_1_alert", "\xF3\xB0\xA4\xA0"}, // F0920 wifi-strength-1-alert
    {"wifi_strength_1_lock", "\xF3\xB0\xA4\xA1"},  // F0921 wifi-strength-1-lock
    {"wifi_strength_2",    "\xF3\xB0\xA4\xA2"},  // F0922 wifi-strength-2
    {"wifi_strength_2_lock", "\xF3\xB0\xA4\xA4"},  // F0924 wifi-strength-2-lock
    {"wifi_strength_3",    "\xF3\xB0\xA4\xA5"},  // F0925 wifi-strength-3
    {"wifi_strength_3_lock", "\xF3\xB0\xA4\xA7"},  // F0927 wifi-strength-3-lock
    {"wifi_strength_4",    "\xF3\xB0\xA4\xA8"},  // F0928 wifi-strength-4
    {"wifi_strength_4_lock", "\xF3\xB0\xA4\xAA"},  // F092A wifi-strength-4-lock

    // Wrench/Tools
    {"wrench",             "\xF3\xB1\x8C\xA3"},  // F1323 hammer-wrench

    // Z-axis
    {"z_closer",           "\xF3\xB0\x9C\xAE"},  // F072E arrow-down-bold (z closer)
    {"z_farther",          "\xF3\xB0\x9C\xB7"},  // F0737 arrow-up-bold (z farther)
};

// clang-format on

// Number of icons in the mapping table
static constexpr size_t ICON_MAP_SIZE = sizeof(ICON_MAP) / sizeof(ICON_MAP[0]);

/**
 * @brief Look up an icon codepoint by name
 * @param name Icon short name (e.g., "home", "wifi", "settings")
 * @return UTF-8 encoded codepoint string, or nullptr if not found
 */
inline const char* lookup_codepoint(const char* name) {
    if (!name)
        return nullptr;

    // Binary search for efficiency (table is sorted alphabetically)
    size_t left = 0;
    size_t right = ICON_MAP_SIZE;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        int cmp = strcmp(name, ICON_MAP[mid].name);

        if (cmp == 0) {
            return ICON_MAP[mid].codepoint;
        } else if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }

    return nullptr;
}

/**
 * @brief Legacy name mapping - converts mat_*_img names to short names
 * @param legacy_name Name like "mat_home_img"
 * @return Short name like "home", or original name if not legacy format
 *
 * This function is provided for transition purposes but should not be used
 * in new code. Use short names directly.
 */
inline const char* strip_legacy_prefix(const char* legacy_name) {
    if (!legacy_name)
        return nullptr;

    // Check for "mat_" prefix
    if (strncmp(legacy_name, "mat_", 4) != 0) {
        return legacy_name; // Not a legacy name
    }

    // Strip "mat_" prefix
    const char* stripped = legacy_name + 4;

    // Check for "_img" suffix and strip it
    size_t len = strlen(stripped);
    if (len > 4 && strcmp(stripped + len - 4, "_img") == 0) {
        // Return static buffer - NOT thread safe, but OK for single-threaded LVGL
        static char buffer[64];
        size_t copy_len = len - 4;
        if (copy_len >= sizeof(buffer))
            copy_len = sizeof(buffer) - 1;
        strncpy(buffer, stripped, copy_len);
        buffer[copy_len] = '\0';
        return buffer;
    }

    return stripped;
}

} // namespace ui_icon
