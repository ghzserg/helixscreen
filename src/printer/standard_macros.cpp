// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "standard_macros.h"

#include "config.h"
#include "moonraker_api.h"
#include "printer_discovery.h"

#include <spdlog/spdlog.h>

#include <algorithm>

using namespace helix;

// ============================================================================
// Slot Definition Data
// ============================================================================

namespace {

/**
 * @brief Detection patterns for each slot
 *
 * Patterns are matched case-insensitively against available macros.
 * First match wins, so order patterns by specificity.
 */
struct SlotPatterns {
    StandardMacroSlot slot;
    std::vector<std::string> patterns;
};

// clang-format off
const std::vector<SlotPatterns> DETECTION_PATTERNS = {
    {StandardMacroSlot::LoadFilament,   {"LOAD_FILAMENT", "M701"}},
    {StandardMacroSlot::UnloadFilament, {"UNLOAD_FILAMENT", "M702"}},
    {StandardMacroSlot::Purge,          {"PURGE", "PURGE_LINE", "PRIME_LINE", "PURGE_FILAMENT", "LINE_PURGE"}},
    {StandardMacroSlot::Pause,          {"PAUSE", "M601"}},
    {StandardMacroSlot::Resume,         {"RESUME", "M602"}},
    {StandardMacroSlot::Cancel,         {"CANCEL_PRINT"}},
    {StandardMacroSlot::BedMesh,        {"BED_MESH_CALIBRATE", "G29"}},
    {StandardMacroSlot::BedLevel,       {"QUAD_GANTRY_LEVEL", "QGL", "Z_TILT_ADJUST"}},
    {StandardMacroSlot::CleanNozzle,    {"CLEAN_NOZZLE", "NOZZLE_WIPE", "WIPE_NOZZLE", "CLEAR_NOZZLE"}},
    {StandardMacroSlot::HeatSoak,       {"HEAT_SOAK", "CHAMBER_SOAK", "SOAK"}},
};
// clang-format on

/**
 * @brief HELIX fallback macros for each slot
 *
 * These are installed by HelixScreen's macro installer.
 * Empty string means no fallback is available.
 */
// clang-format off
const std::map<StandardMacroSlot, std::string> FALLBACK_MACROS = {
    {StandardMacroSlot::LoadFilament,   ""},
    {StandardMacroSlot::UnloadFilament, ""},
    {StandardMacroSlot::Purge,          ""},
    {StandardMacroSlot::Pause,          ""},
    {StandardMacroSlot::Resume,         ""},
    {StandardMacroSlot::Cancel,         ""},
    {StandardMacroSlot::BedMesh,        "HELIX_BED_MESH_IF_NEEDED"},
    {StandardMacroSlot::BedLevel,       ""},
    {StandardMacroSlot::CleanNozzle,    "HELIX_CLEAN_NOZZLE"},
    {StandardMacroSlot::HeatSoak,       ""},
};
// clang-format on

/**
 * @brief Slot metadata
 */
struct SlotMeta {
    std::string name;         ///< Config key: "load_filament"
    std::string display_name; ///< UI label: "Load Filament"
};

// clang-format off
const std::map<StandardMacroSlot, SlotMeta> SLOT_METADATA = {
    {StandardMacroSlot::LoadFilament,   {"load_filament",   "Load Filament"}},
    {StandardMacroSlot::UnloadFilament, {"unload_filament", "Unload Filament"}},
    {StandardMacroSlot::Purge,          {"purge",           "Purge"}},
    {StandardMacroSlot::Pause,          {"pause",           "Pause Print"}},
    {StandardMacroSlot::Resume,         {"resume",          "Resume Print"}},
    {StandardMacroSlot::Cancel,         {"cancel",          "Cancel Print"}},
    {StandardMacroSlot::BedMesh,        {"bed_mesh",        "Bed Mesh"}},
    {StandardMacroSlot::BedLevel,       {"bed_level",       "Bed Level"}},
    {StandardMacroSlot::CleanNozzle,    {"clean_nozzle",    "Clean Nozzle"}},
    {StandardMacroSlot::HeatSoak,       {"heat_soak",       "Heat Soak"}},
};
// clang-format on

/**
 * @brief Convert string to uppercase for case-insensitive comparison
 */
std::string to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

} // namespace

// ============================================================================
// StandardMacros Implementation
// ============================================================================

StandardMacros& StandardMacros::instance() {
    static StandardMacros instance;
    return instance;
}

StandardMacros::StandardMacros() {
    init_slot_definitions();
}

void StandardMacros::init_slot_definitions() {
    slots_.clear();
    slots_.reserve(static_cast<size_t>(StandardMacroSlot::COUNT));

    // Initialize all slots with metadata
    for (int i = 0; i < static_cast<int>(StandardMacroSlot::COUNT); ++i) {
        auto slot = static_cast<StandardMacroSlot>(i);
        StandardMacroInfo info;
        info.slot = slot;

        auto meta_it = SLOT_METADATA.find(slot);
        if (meta_it != SLOT_METADATA.end()) {
            info.slot_name = meta_it->second.name;
            info.display_name = meta_it->second.display_name;
        }

        auto fallback_it = FALLBACK_MACROS.find(slot);
        if (fallback_it != FALLBACK_MACROS.end()) {
            info.fallback_macro = fallback_it->second;
        }

        slots_.push_back(std::move(info));
    }
}

void StandardMacros::reset() {
    spdlog::debug("[StandardMacros] Resetting");
    for (auto& slot : slots_) {
        slot.detected_macro.clear();
        // Don't clear configured_macro - that's user config
        // Don't clear fallback_macro - that's static
    }
    initialized_ = false;
}

const StandardMacroInfo& StandardMacros::get(StandardMacroSlot slot) const {
    auto index = static_cast<size_t>(slot);
    if (index >= slots_.size()) {
        spdlog::error("[StandardMacros] Invalid slot index: {}", index);
        static StandardMacroInfo empty_info;
        return empty_info;
    }
    return slots_[index];
}

std::optional<StandardMacroSlot> StandardMacros::slot_from_name(const std::string& name) {
    for (const auto& [slot, meta] : SLOT_METADATA) {
        if (meta.name == name) {
            return slot;
        }
    }
    return std::nullopt;
}

std::string StandardMacros::slot_to_name(StandardMacroSlot slot) {
    auto it = SLOT_METADATA.find(slot);
    if (it != SLOT_METADATA.end()) {
        return it->second.name;
    }
    return "";
}

void StandardMacros::set_macro(StandardMacroSlot slot, const std::string& macro) {
    auto index = static_cast<size_t>(slot);
    if (index >= slots_.size()) {
        spdlog::error("[StandardMacros] set_macro: invalid slot index {}", index);
        return;
    }

    auto& info = slots_[index];
    info.configured_macro = macro;

    spdlog::info("[StandardMacros] Set {} = '{}'", info.slot_name, macro);
    save_to_config();
}

void StandardMacros::load_from_config() {
    auto* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[StandardMacros] Config not available");
        return;
    }

    for (auto& slot : slots_) {
        std::string path = "/standard_macros/" + slot.slot_name;
        slot.configured_macro = config->get<std::string>(path, "");
        if (!slot.configured_macro.empty()) {
            spdlog::debug("[StandardMacros] Loaded config: {} = {}", slot.slot_name,
                          slot.configured_macro);
        }
    }
}

void StandardMacros::save_to_config() {
    auto* config = Config::get_instance();
    if (!config) {
        spdlog::warn("[StandardMacros] Config not available for save");
        return;
    }

    for (const auto& slot : slots_) {
        std::string path = "/standard_macros/" + slot.slot_name;
        config->set<std::string>(path, slot.configured_macro);
    }

    if (!config->save()) {
        spdlog::error("[StandardMacros] Failed to save config");
    }
}

bool StandardMacros::execute(StandardMacroSlot slot, MoonrakerAPI* api, SuccessCallback on_success,
                             ErrorCallback on_error) {
    return execute(slot, api, {}, std::move(on_success), std::move(on_error));
}

bool StandardMacros::execute(StandardMacroSlot slot, MoonrakerAPI* api,
                             const std::map<std::string, std::string>& params,
                             SuccessCallback on_success, ErrorCallback on_error) {
    const auto& info = get(slot);

    if (info.is_empty()) {
        spdlog::debug("[StandardMacros] Slot {} is empty, cannot execute", info.slot_name);
        return false;
    }

    std::string macro_name = info.get_macro();
    if (!api) {
        spdlog::error("[StandardMacros] Cannot execute {}: API is null", macro_name);
        return false;
    }

    spdlog::info("[StandardMacros] Executing {} via {}", info.slot_name, macro_name);
    api->execute_macro(macro_name, params, std::move(on_success), std::move(on_error));
    return true;
}

void StandardMacros::init(const helix::PrinterDiscovery& hardware) {
    spdlog::debug("[StandardMacros] Initializing with hardware discovery");

    // Reset detected macros and restore fallbacks from static table
    for (auto& slot : slots_) {
        slot.detected_macro.clear();

        // Restore fallback from static definition
        auto fallback_it = FALLBACK_MACROS.find(slot.slot);
        if (fallback_it != FALLBACK_MACROS.end()) {
            slot.fallback_macro = fallback_it->second;
        }
    }

    // Load user configuration
    load_from_config();

    // Run auto-detection
    auto_detect(hardware);

    // Check which fallbacks are actually available on this printer
    for (auto& slot : slots_) {
        if (!slot.fallback_macro.empty()) {
            if (!hardware.has_helix_macro(slot.fallback_macro)) {
                spdlog::trace("[StandardMacros] Fallback {} not installed for {}",
                              slot.fallback_macro, slot.slot_name);
                slot.fallback_macro.clear();
            }
        }
    }

    initialized_ = true;

    // Log summary
    int configured = 0, detected = 0, fallback = 0, empty = 0;
    for (const auto& slot : slots_) {
        switch (slot.get_source()) {
        case MacroSource::CONFIGURED:
            configured++;
            break;
        case MacroSource::DETECTED:
            detected++;
            break;
        case MacroSource::FALLBACK:
            fallback++;
            break;
        case MacroSource::NONE:
            empty++;
            break;
        }
    }
    spdlog::debug("[StandardMacros] Initialized: {} configured, {} detected, {} fallback, {} empty",
                  configured, detected, fallback, empty);
}

void StandardMacros::auto_detect(const helix::PrinterDiscovery& hardware) {
    spdlog::debug("[StandardMacros] Running auto-detection on {} macros", hardware.macro_count());

    for (const auto& pattern_def : DETECTION_PATTERNS) {
        auto detected = try_detect(hardware, pattern_def.slot, pattern_def.patterns);
        if (!detected.empty()) {
            auto index = static_cast<size_t>(pattern_def.slot);
            if (index < slots_.size()) {
                slots_[index].detected_macro = detected;
                spdlog::trace("[StandardMacros] Detected {} -> {}", slots_[index].slot_name,
                              detected);
            }
        }
    }
}

std::string StandardMacros::try_detect(const helix::PrinterDiscovery& hardware,
                                       [[maybe_unused]] StandardMacroSlot slot,
                                       const std::vector<std::string>& patterns) {
    const auto& macros = hardware.macros();

    for (const auto& pattern : patterns) {
        std::string upper_pattern = to_upper(pattern);
        // Check if the pattern exists as a macro (both are uppercase)
        if (macros.find(upper_pattern) != macros.end()) {
            // Return the pattern as-is (Klipper macros are case-insensitive)
            return pattern;
        }
    }

    return "";
}
