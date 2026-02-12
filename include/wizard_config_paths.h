// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file wizard_config_paths.h
 * @brief Centralized configuration paths for wizard screens
 *
 * Defines all JSON configuration paths used by wizard screens to eliminate
 * hardcoded string literals and reduce typo risk.
 *
 * All printer-specific paths are under /printer/...
 */

namespace helix {
namespace wizard {
// Printer identification
constexpr const char* PRINTER_NAME = "/printer/name";
constexpr const char* PRINTER_TYPE = "/printer/type";

// Bed hardware
constexpr const char* BED_HEATER = "/printer/heaters/bed";
constexpr const char* BED_SENSOR = "/printer/temp_sensors/bed";

// Hotend hardware
constexpr const char* HOTEND_HEATER = "/printer/heaters/hotend";
constexpr const char* HOTEND_SENSOR = "/printer/temp_sensors/hotend";

// Fan hardware
constexpr const char* HOTEND_FAN = "/printer/fans/hotend";
constexpr const char* PART_FAN = "/printer/fans/part";
constexpr const char* CHAMBER_FAN = "/printer/fans/chamber";
constexpr const char* EXHAUST_FAN = "/printer/fans/exhaust";

// LED hardware
constexpr const char* LED_STRIP = "/printer/leds/strip";
constexpr const char* LED_SELECTED = "/printer/leds/selected";

// Network configuration
constexpr const char* MOONRAKER_HOST = "/printer/moonraker_host";
constexpr const char* MOONRAKER_PORT = "/printer/moonraker_port";
constexpr const char* WIFI_SSID = "/wifi/ssid";
constexpr const char* WIFI_PASSWORD = "/wifi/password";
} // namespace wizard

// Display settings
constexpr const char* PRINTER_IMAGE = "/display/printer_image";

} // namespace helix
