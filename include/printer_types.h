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

/**
 * @brief Shared printer type definitions for wizard and auto-detection
 *
 * This file defines the canonical list of supported printer types.
 * Used by:
 * - ui_wizard_printer_identify.cpp (roller options, config persistence)
 * - ui_xml/wizard_printer_identify.xml (roller display)
 * - Future: printer_detector.cpp (auto-detection heuristics)
 *
 * Order matters: Index is stored in config as /printer/type_index
 */

namespace PrinterTypes {

/**
 * @brief Printer type names as newline-separated string for LVGL roller
 *
 * Format: Each line is a printer type option
 * Default selection: Index 32 ("Unknown")
 */
inline constexpr const char* PRINTER_TYPES_ROLLER =
    "Anycubic i3 Mega\n"
    "Anycubic Kobra\n"
    "Anycubic Vyper\n"
    "Bambu Lab X1\n"
    "Bambu Lab P1P\n"
    "Creality CR-10\n"
    "Creality Ender 3\n"
    "Creality Ender 5\n"
    "Creality K1\n"
    "FlashForge Creator Pro\n"
    "FlashForge Dreamer\n"
    "LulzBot TAZ\n"
    "LulzBot Mini\n"
    "MakerBot Replicator\n"
    "Prusa i3 MK3\n"
    "Prusa i3 MK4\n"
    "Prusa Mini\n"
    "Prusa XL\n"
    "Qidi Tech X-Max\n"
    "Qidi Tech X-Plus\n"
    "Raise3D Pro2\n"
    "Raise3D E2\n"
    "Sovol SV01\n"
    "Sovol SV06\n"
    "Ultimaker 2+\n"
    "Ultimaker 3\n"
    "Ultimaker S3\n"
    "Voron 0.1\n"
    "Voron 2.4\n"
    "Voron Trident\n"
    "Voron Switchwire\n"
    "Custom/Other\n"
    "Unknown";

/**
 * @brief Number of printer types in the list
 */
inline constexpr int PRINTER_TYPE_COUNT = 33;

/**
 * @brief Default printer type index (Unknown)
 */
inline constexpr int DEFAULT_PRINTER_TYPE_INDEX = 32;

} // namespace PrinterTypes
