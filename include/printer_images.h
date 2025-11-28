// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file printer_images.h
 * @brief Printer type to image path mapping
 *
 * Maps printer type indices (from printer_types.h) to their corresponding
 * image asset paths. Uses generic fallback for unknown/unmapped printers.
 */

namespace PrinterImages {

/// Default fallback image for unknown/unmapped printers
inline constexpr const char* DEFAULT_IMAGE = "A:assets/images/printers/generic-corexy-750x930.png";

/**
 * @brief Printer type indices (must match printer_types.h order)
 */
enum PrinterTypeIndex {
    ANYCUBIC_CHIRON = 0,
    ANYCUBIC_I3_MEGA = 1,
    ANYCUBIC_KOBRA = 2,
    ANYCUBIC_VYPER = 3,
    BAMBU_LAB_P1P = 4,
    BAMBU_LAB_X1 = 5,
    CREALITY_CR10 = 6,
    CREALITY_ENDER_3 = 7,
    CREALITY_ENDER_5 = 8,
    CREALITY_K1 = 9,
    DORON_VELTA = 10,
    FLASHFORGE_ADVENTURER_5M = 11,
    FLASHFORGE_ADVENTURER_5M_PRO = 12,
    FLASHFORGE_CREATOR_PRO = 13,
    FLASHFORGE_DREAMER = 14,
    FLSUN_DELTA = 15,
    LULZBOT_MINI = 16,
    LULZBOT_TAZ = 17,
    MAKERBOT_REPLICATOR = 18,
    PRUSA_I3_MK3 = 19,
    PRUSA_I3_MK4 = 20,
    PRUSA_MINI = 21,
    PRUSA_XL = 22,
    QIDI_TECH_X_MAX = 23,
    QIDI_TECH_X_PLUS = 24,
    RAISE3D_E2 = 25,
    RAISE3D_PRO2 = 26,
    RATRIG_VCORE3 = 27,
    RATRIG_VMINION = 28,
    SOVOL_SV01 = 29,
    SOVOL_SV06 = 30,
    ULTIMAKER_2_PLUS = 31,
    ULTIMAKER_3 = 32,
    ULTIMAKER_S3 = 33,
    VORON_0_1 = 34,
    VORON_2_4 = 35,
    VORON_SWITCHWIRE = 36,
    VORON_TRIDENT = 37,
    CUSTOM_OTHER = 38,
    UNKNOWN = 39
};

/**
 * @brief Get image path for a printer type index
 *
 * @param printer_type_index Index from printer type roller (0-39)
 * @return Path to printer image asset (uses "A:" prefix for LVGL filesystem)
 *
 * Returns DEFAULT_IMAGE for unknown indices or printers without specific images.
 * Custom/Other printers will later support user-uploaded images.
 */
inline const char* get_image_path(int printer_type_index) {
    switch (printer_type_index) {
    // Anycubic
    case ANYCUBIC_CHIRON:
        return "A:assets/images/printers/anycubic-chiron.png";
    case ANYCUBIC_KOBRA:
        return "A:assets/images/printers/anycubic-kobra.png";
    case ANYCUBIC_VYPER:
        return "A:assets/images/printers/anycubic-vyper.png";

    // Creality
    case CREALITY_K1:
        return "A:assets/images/printers/creality-k1-2-750x930.png";

    // Doron
    case DORON_VELTA:
        return "A:assets/images/printers/doron_velta_794x794.png";

    // FlashForge
    case FLASHFORGE_ADVENTURER_5M:
        return "A:assets/images/printers/flashforge-adventurer-5m-1-750x930.png";
    case FLASHFORGE_ADVENTURER_5M_PRO:
        return "A:assets/images/printers/flashforge-adventurer-5m-pro-2-750x930.png";

    // FLSUN
    case FLSUN_DELTA:
        return "A:assets/images/printers/flsun-delta.png";

    // RatRig
    case RATRIG_VCORE3:
        return "A:assets/images/printers/ratrig-vcore3.png";
    case RATRIG_VMINION:
        return "A:assets/images/printers/ratrig-vminion.png";

    // Voron
    case VORON_0_1:
        return "A:assets/images/printers/voron-0-2-4-750x930.png";
    case VORON_2_4:
        return "A:assets/images/printers/voron-24r2-pro-5-750x930.png";
    case VORON_TRIDENT:
        return "A:assets/images/printers/voron-trident-pro-1-750x930.png";

    // All others use generic fallback
    default:
        return DEFAULT_IMAGE;
    }
}

} // namespace PrinterImages
