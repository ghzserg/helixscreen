// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file filament_database.h
 * @brief Static database of filament materials with temperature recommendations
 *
 * Provides a comprehensive list of common 3D printing materials with their
 * recommended temperature ranges. Used by the Edit Filament modal to auto-derive
 * temperatures when a material is selected.
 *
 * Temperature sources:
 * - Manufacturer recommendations from major brands (Bambu, Polymaker, eSUN, etc.)
 * - Community consensus from r/3Dprinting and Voron Discord
 * - Tested ranges from the author's Voron 2.4
 */

namespace filament {

/**
 * @brief Material information with temperature recommendations
 */
struct MaterialInfo {
    const char* name;     ///< Material name (e.g., "PLA", "PETG")
    int nozzle_min;       ///< Minimum nozzle temperature (°C)
    int nozzle_max;       ///< Maximum nozzle temperature (°C)
    int bed_temp;         ///< Recommended bed temperature (°C)
    const char* category; ///< Category for grouping (e.g., "Standard", "Engineering")

    // Drying parameters
    int dry_temp_c;   ///< Drying temperature (0 = not hygroscopic)
    int dry_time_min; ///< Drying duration in minutes

    // Physical properties
    float density_g_cm3; ///< Material density (g/cm³)

    // Classification
    int chamber_temp_c;       ///< Recommended chamber temp (0 = none/open)
    const char* compat_group; ///< "PLA", "PETG", "ABS_ASA", "PA", "TPU", "PC", "HIGH_TEMP"

    /**
     * @brief Get recommended nozzle temperature (midpoint of range)
     */
    [[nodiscard]] constexpr int nozzle_recommended() const {
        return (nozzle_min + nozzle_max) / 2;
    }

    /**
     * @brief Check if material requires an enclosure
     */
    [[nodiscard]] constexpr bool needs_enclosure() const {
        return chamber_temp_c > 0;
    }

    /**
     * @brief Check if material needs drying before use
     */
    [[nodiscard]] constexpr bool needs_drying() const {
        return dry_temp_c > 0;
    }
};

/**
 * @brief Static database of common filament materials
 *
 * Materials are grouped by category:
 * - Standard: PLA, PETG - most common, beginner-friendly
 * - Engineering: ABS, ASA, PC, PA - require enclosure/higher temps
 * - Flexible: TPU, TPE - rubber-like materials
 * - Support: PVA, HIPS - dissolvable/breakaway supports
 * - Specialty: Wood-fill, Marble, Metal-fill - decorative
 * - High-Temp: PEEK, PEI - industrial applications
 */
// clang-format off
inline constexpr MaterialInfo MATERIALS[] = {
    // name           nozzle   bed   category        dry_temp dry_min density chamber compat_group
    //                min max                        °C       min     g/cm³   °C

    // === Standard Materials (No enclosure required) ===
    {"PLA",         190, 220, 60,  "Standard",      45, 240,  1.24f,  0,  "PLA"},
    {"PLA+",        200, 230, 60,  "Standard",      45, 240,  1.24f,  0,  "PLA"},
    {"PLA-CF",      200, 230, 60,  "Standard",      45, 240,  1.24f,  0,  "PLA"},       // Carbon fiber PLA
    {"PLA-GF",      200, 230, 60,  "Standard",      45, 240,  1.24f,  0,  "PLA"},       // Glass fiber PLA
    {"Silk PLA",    200, 230, 60,  "Standard",      45, 240,  1.24f,  0,  "PLA"},       // Shiny finish PLA
    {"Matte PLA",   200, 230, 60,  "Standard",      45, 240,  1.24f,  0,  "PLA"},
    {"PETG",        230, 260, 80,  "Standard",      55, 360,  1.27f,  0,  "PETG"},
    {"PETG-CF",     240, 270, 80,  "Standard",      55, 360,  1.27f,  0,  "PETG"},      // Carbon fiber PETG
    {"PETG-GF",     240, 270, 80,  "Standard",      55, 360,  1.27f,  0,  "PETG"},      // Glass fiber PETG
    {"PCTG",        240, 270, 80,  "Standard",      55, 360,  1.23f,  0,  "PETG"},      // PETG variant, clearer

    // === Engineering Materials (Enclosure recommended) ===
    {"ABS",         240, 270, 100, "Engineering",   60, 240,  1.04f,  50, "ABS_ASA"},
    {"ABS+",        240, 270, 100, "Engineering",   60, 240,  1.04f,  50, "ABS_ASA"},
    {"ASA",         240, 270, 100, "Engineering",   60, 240,  1.07f,  50, "ABS_ASA"},   // UV-resistant ABS alternative
    {"ASA+",        240, 270, 100, "Engineering",   60, 240,  1.07f,  50, "ABS_ASA"},   // Enhanced ASA
    {"ABS-CF",      240, 270, 100, "Engineering",   60, 240,  1.10f,  50, "ABS_ASA"},   // Carbon fiber ABS
    {"ABS-GF",      240, 270, 100, "Engineering",   60, 240,  1.15f,  50, "ABS_ASA"},   // Glass fiber ABS
    {"ASA-CF",      250, 280, 100, "Engineering",   60, 240,  1.12f,  50, "ABS_ASA"},   // Carbon fiber ASA
    {"ASA-GF",      250, 280, 100, "Engineering",   60, 240,  1.18f,  50, "ABS_ASA"},   // Glass fiber ASA
    {"PC",          260, 300, 110, "Engineering",   80, 480,  1.20f,  55, "PC"},        // Polycarbonate
    {"PC-CF",       270, 300, 110, "Engineering",   80, 480,  1.20f,  55, "PC"},        // Carbon fiber PC
    {"PC-GF",       270, 300, 110, "Engineering",   80, 480,  1.35f,  55, "PC"},        // Glass fiber PC
    {"PC-ABS",      250, 280, 100, "Engineering",   60, 240,  1.12f,  50, "ABS_ASA"},   // PC/ABS blend

    // === Nylon/Polyamide (Enclosure required, dry storage) ===
    {"PA",          250, 280, 80,  "Engineering",   70, 480,  1.14f,  50, "PA"},        // Generic nylon
    {"PA6",         250, 280, 80,  "Engineering",   70, 480,  1.14f,  50, "PA"},
    {"PA12",        250, 280, 80,  "Engineering",   70, 480,  1.14f,  50, "PA"},
    {"PA-CF",       260, 290, 80,  "Engineering",   70, 480,  1.14f,  50, "PA"},        // Carbon fiber nylon
    {"PA-GF",       260, 290, 80,  "Engineering",   70, 480,  1.14f,  50, "PA"},        // Glass fiber nylon
    {"PA66",        260, 290, 90,  "Engineering",   80, 480,  1.14f,  55, "PA"},        // Nylon 66
    {"PPA",         280, 320, 100, "Engineering",   80, 480,  1.18f,  60, "PA"},        // Polyphthalamide

    // === Flexible Materials ===
    {"TPU",         210, 240, 50,  "Flexible",      55, 240,  1.21f,  0,  "TPU"},       // Shore 95A typical
    {"TPU-Soft",    200, 230, 50,  "Flexible",      55, 240,  1.21f,  0,  "TPU"},       // Shore 85A or softer
    {"TPE",         200, 230, 50,  "Flexible",      55, 240,  1.21f,  0,  "TPU"},
    {"TPU-95A",     210, 240, 50,  "Flexible",      55, 240,  1.21f,  0,  "TPU"},       // Shore 95A hardness
    {"TPU-85A",     200, 230, 50,  "Flexible",      55, 240,  1.19f,  0,  "TPU"},       // Shore 85A hardness (softer)

    // === Support Materials ===
    {"PVA",         180, 210, 60,  "Support",       45, 240,  1.23f,  0,  "PLA"},       // Water-soluble
    {"HIPS",        230, 250, 100, "Support",       60, 240,  1.05f,  50, "ABS_ASA"},   // Limonene-soluble
    {"BVOH",        190, 220, 60,  "Support",       45, 240,  1.10f,  0,  "PLA"},       // Water-soluble (better than PVA)

    // === Specialty/Decorative ===
    {"Wood PLA",    190, 220, 60,  "Specialty",     45, 240,  1.24f,  0,  "PLA"},       // Wood fiber fill
    {"Marble PLA",  200, 220, 60,  "Specialty",     45, 240,  1.24f,  0,  "PLA"},       // Marble effect
    {"Metal PLA",   200, 230, 60,  "Specialty",     45, 240,  1.24f,  0,  "PLA"},       // Metal powder fill
    {"Glow PLA",    200, 230, 60,  "Specialty",     45, 240,  1.24f,  0,  "PLA"},       // Glow-in-the-dark
    {"Color-Change",200, 230, 60,  "Specialty",     45, 240,  1.24f,  0,  "PLA"},       // Temperature reactive

    // === Recycled Materials ===
    {"rPLA",        190, 220, 60,  "Recycled",      45, 240,  1.24f,  0,  "PLA"},       // Recycled PLA
    {"rPETG",       230, 260, 80,  "Recycled",      55, 360,  1.27f,  0,  "PETG"},      // Recycled PETG

    // === High-Temperature Industrial ===
    {"PEEK",        370, 420, 120, "High-Temp",     100, 720, 1.30f,  80, "HIGH_TEMP"}, // Requires all-metal hotend
    {"PEI",         340, 380, 120, "High-Temp",     100, 720, 1.27f,  80, "HIGH_TEMP"}, // ULTEM
    {"PSU",         340, 380, 120, "High-Temp",     100, 720, 1.24f,  80, "HIGH_TEMP"}, // Polysulfone
    {"PPSU",        350, 390, 140, "High-Temp",     100, 720, 1.29f,  80, "HIGH_TEMP"}, // Medical grade
};
// clang-format on

/// Number of materials in the database
inline constexpr size_t MATERIAL_COUNT = sizeof(MATERIALS) / sizeof(MATERIALS[0]);

/**
 * @brief Material name alias for common variations
 */
struct MaterialAlias {
    const char* alias;     ///< Alternative name
    const char* canonical; ///< Canonical MaterialInfo name
};

/**
 * @brief Common material name aliases
 */
// clang-format off
inline constexpr MaterialAlias MATERIAL_ALIASES[] = {
    {"Nylon",        "PA"},
    {"Nylon-CF",     "PA-CF"},
    {"Nylon-GF",     "PA-GF"},
    {"Polycarbonate","PC"},
    {"PLA Silk",     "Silk PLA"},
    {"Silk",         "Silk PLA"},
    {"Generic",      "PLA"},
    {"ULTEM",        "PEI"},
};
// clang-format on

/// Number of aliases in the database
inline constexpr size_t ALIAS_COUNT = sizeof(MATERIAL_ALIASES) / sizeof(MATERIAL_ALIASES[0]);

/**
 * @brief Resolve a material alias to its canonical name
 * @param name Material name or alias to resolve
 * @return Canonical name if alias found, original name otherwise
 */
inline std::string_view resolve_alias(std::string_view name) {
    std::string name_lower(name);
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

    for (const auto& alias : MATERIAL_ALIASES) {
        std::string alias_lower(alias.alias);
        std::transform(alias_lower.begin(), alias_lower.end(), alias_lower.begin(), ::tolower);

        if (alias_lower == name_lower) {
            return alias.canonical;
        }
    }
    return name;
}

/**
 * @brief Find material info by name (case-insensitive)
 * @param name Material name to look up (aliases are resolved automatically)
 * @return MaterialInfo if found, std::nullopt otherwise
 */
inline std::optional<MaterialInfo> find_material(std::string_view name) {
    // First resolve any alias
    std::string_view resolved = resolve_alias(name);

    for (const auto& mat : MATERIALS) {
        // Case-insensitive comparison
        std::string mat_lower(mat.name);
        std::string name_lower(resolved);
        std::transform(mat_lower.begin(), mat_lower.end(), mat_lower.begin(), ::tolower);
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

        if (mat_lower == name_lower) {
            return mat;
        }
    }
    return std::nullopt;
}

/**
 * @brief Get all materials in a category
 * @param category Category name (e.g., "Standard", "Engineering")
 * @return Vector of matching materials
 */
inline std::vector<MaterialInfo> get_materials_by_category(std::string_view category) {
    std::vector<MaterialInfo> result;
    for (const auto& mat : MATERIALS) {
        if (category == mat.category) {
            result.push_back(mat);
        }
    }
    return result;
}

/**
 * @brief Get list of all unique category names
 * @return Vector of category names in order of appearance
 */
inline std::vector<const char*> get_categories() {
    std::vector<const char*> categories;
    for (const auto& mat : MATERIALS) {
        bool found = false;
        for (const auto* cat : categories) {
            if (std::string_view(cat) == mat.category) {
                found = true;
                break;
            }
        }
        if (!found) {
            categories.push_back(mat.category);
        }
    }
    return categories;
}

/**
 * @brief Get list of all material names (for dropdown population)
 * @return Vector of material name strings
 */
inline std::vector<const char*> get_all_material_names() {
    std::vector<const char*> names;
    names.reserve(MATERIAL_COUNT);
    for (const auto& mat : MATERIALS) {
        names.push_back(mat.name);
    }
    return names;
}

/**
 * @brief Get the compatibility group for a material
 * @param material Material name to look up
 * @return Compatibility group name, or nullptr if unknown
 */
inline const char* get_compatibility_group(std::string_view material) {
    auto mat = find_material(material);
    if (mat.has_value()) {
        return mat->compat_group;
    }
    return nullptr;
}

/**
 * @brief Check if two materials are compatible for endless spool
 * @param mat1 First material name
 * @param mat2 Second material name
 * @return true if materials are compatible (same group or either unknown)
 */
inline bool are_materials_compatible(std::string_view mat1, std::string_view mat2) {
    const char* group1 = get_compatibility_group(mat1);
    const char* group2 = get_compatibility_group(mat2);

    // Unknown materials are compatible with anything
    if (group1 == nullptr || group2 == nullptr) {
        return true;
    }

    // Same group = compatible
    return std::string_view(group1) == std::string_view(group2);
}

/**
 * @brief Drying preset by compatibility group
 */
struct DryingPreset {
    const char* name; ///< Group/preset name
    int temp_c;       ///< Drying temperature in °C
    int time_min;     ///< Drying time in minutes
};

/**
 * @brief Get drying presets grouped by compatibility group (for dropdown)
 * @return Vector of unique drying presets
 */
inline std::vector<DryingPreset> get_drying_presets_by_group() {
    std::vector<DryingPreset> presets;

    for (const auto& mat : MATERIALS) {
        if (mat.dry_temp_c == 0) {
            continue; // Skip non-hygroscopic materials
        }

        // Check if we already have this group
        bool found = false;
        for (const auto& preset : presets) {
            if (std::string_view(preset.name) == mat.compat_group) {
                found = true;
                break;
            }
        }

        if (!found) {
            presets.push_back({mat.compat_group, mat.dry_temp_c, mat.dry_time_min});
        }
    }

    return presets;
}

/**
 * @brief Calculate filament length from weight
 * @param weight_g Weight in grams
 * @param density Material density in g/cm³
 * @param diameter_mm Filament diameter in mm (default 1.75)
 * @return Length in meters
 */
inline float weight_to_length_m(float weight_g, float density, float diameter_mm = 1.75f) {
    // Volume = mass / density (in cm³)
    float volume_cm3 = weight_g / density;

    // Cross-sectional area in cm² (diameter in mm -> radius in cm)
    float radius_cm = (diameter_mm / 2.0f) / 10.0f;
    float area_cm2 = static_cast<float>(M_PI) * radius_cm * radius_cm;

    // Length = volume / area (in cm, then convert to m)
    float length_cm = volume_cm3 / area_cm2;
    return length_cm / 100.0f;
}

} // namespace filament
