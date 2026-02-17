// SPDX-License-Identifier: GPL-3.0-or-later

#include "spoolman_slot_saver.h"

#include "moonraker_api.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>

#include "hv/json.hpp"

namespace helix {

SpoolmanSlotSaver::SpoolmanSlotSaver(MoonrakerAPI* api) : api_(api) {}

ChangeSet SpoolmanSlotSaver::detect_changes(const SlotInfo& original, const SlotInfo& edited) {
    ChangeSet changes;

    // Filament-level: brand, material, color_rgb
    if (original.brand != edited.brand || original.material != edited.material ||
        original.color_rgb != edited.color_rgb) {
        changes.filament_level = true;
    }

    // Spool-level: remaining_weight_g (float comparison with threshold)
    if (std::abs(original.remaining_weight_g - edited.remaining_weight_g) > WEIGHT_THRESHOLD) {
        changes.spool_level = true;
    }

    return changes;
}

std::string SpoolmanSlotSaver::color_to_hex(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
    return std::string(buf);
}

void SpoolmanSlotSaver::save(const SlotInfo& original, const SlotInfo& edited,
                             CompletionCallback on_complete) {
    // No-op for non-Spoolman slots
    if (!edited.spoolman_id) {
        spdlog::debug("[SpoolmanSlotSaver] No spoolman_id, skipping save");
        if (on_complete)
            on_complete(true);
        return;
    }

    auto changes = detect_changes(original, edited);

    // No changes detected
    if (!changes.any()) {
        spdlog::debug("[SpoolmanSlotSaver] No changes detected for spool {}", edited.spoolman_id);
        if (on_complete)
            on_complete(true);
        return;
    }

    const int spool_id = edited.spoolman_id;

    // Only spool-level (weight) change
    if (!changes.filament_level && changes.spool_level) {
        spdlog::info("[SpoolmanSlotSaver] Updating weight for spool {} to {:.1f}g", spool_id,
                     edited.remaining_weight_g);
        update_weight(spool_id, edited.remaining_weight_g, on_complete);
        return;
    }

    // Filament-level change (possibly also weight)
    if (changes.filament_level) {
        spdlog::info("[SpoolmanSlotSaver] Filament-level change for spool {} "
                     "(brand={}, material={}, color={:#08x})",
                     spool_id, edited.brand, edited.material, edited.color_rgb);

        if (changes.spool_level) {
            // Chain: relink filament first, then update weight
            auto weight = edited.remaining_weight_g;
            find_or_create_filament_and_relink(spool_id, edited,
                                               [this, spool_id, weight, on_complete](bool success) {
                                                   if (!success) {
                                                       if (on_complete)
                                                           on_complete(false);
                                                       return;
                                                   }
                                                   update_weight(spool_id, weight, on_complete);
                                               });
        } else {
            // Only filament relink
            find_or_create_filament_and_relink(spool_id, edited, on_complete);
        }
    }
}

void SpoolmanSlotSaver::update_weight(int spool_id, float weight_g,
                                      CompletionCallback on_complete) {
    api_->update_spoolman_spool_weight(
        spool_id, static_cast<double>(weight_g),
        [on_complete]() {
            spdlog::debug("[SpoolmanSlotSaver] Weight update succeeded");
            if (on_complete)
                on_complete(true);
        },
        [on_complete](const MoonrakerError& err) {
            spdlog::error("[SpoolmanSlotSaver] Weight update failed: {}", err.message);
            if (on_complete)
                on_complete(false);
        });
}

void SpoolmanSlotSaver::relink_spool(int spool_id, int filament_id,
                                     CompletionCallback on_complete) {
    nlohmann::json spool_data;
    spool_data["filament_id"] = filament_id;

    api_->update_spoolman_spool(
        spool_id, spool_data,
        [on_complete, filament_id]() {
            spdlog::debug("[SpoolmanSlotSaver] Spool relinked to filament {}", filament_id);
            if (on_complete)
                on_complete(true);
        },
        [on_complete](const MoonrakerError& err) {
            spdlog::error("[SpoolmanSlotSaver] Spool relink failed: {}", err.message);
            if (on_complete)
                on_complete(false);
        });
}

void SpoolmanSlotSaver::find_or_create_filament_and_relink(int spool_id, const SlotInfo& edited,
                                                           CompletionCallback on_complete) {
    auto edited_hex = color_to_hex(edited.color_rgb);
    auto edited_brand = edited.brand;
    auto edited_material = edited.material;
    api_->get_spoolman_filaments(
        [this, spool_id, edited_hex, edited_brand, edited_material,
         on_complete](const std::vector<FilamentInfo>& filaments) {
            // Search for matching filament
            const FilamentInfo* match = nullptr;
            for (const auto& f : filaments) {
                if (f.vendor_name == edited_brand && f.material == edited_material &&
                    f.color_hex == edited_hex) {
                    match = &f;
                    break;
                }
            }

            if (match) {
                spdlog::info("[SpoolmanSlotSaver] Found matching filament id={}", match->id);
                relink_spool(spool_id, match->id, on_complete);
            } else {
                // Create new filament
                spdlog::info("[SpoolmanSlotSaver] No matching filament found, creating new");
                nlohmann::json filament_data;
                filament_data["material"] = edited_material;
                filament_data["color_hex"] = edited_hex;
                // Vendor name is set but vendor_id lookup is not done here;
                // Spoolman may need the vendor_id. For now, pass name as a field.
                if (!edited_brand.empty()) {
                    filament_data["vendor_name"] = edited_brand;
                }

                api_->create_spoolman_filament(
                    filament_data,
                    [this, spool_id, on_complete](const FilamentInfo& created) {
                        spdlog::info("[SpoolmanSlotSaver] Created filament id={}", created.id);
                        relink_spool(spool_id, created.id, on_complete);
                    },
                    [on_complete](const MoonrakerError& err) {
                        spdlog::error("[SpoolmanSlotSaver] Failed to create filament: {}",
                                      err.message);
                        if (on_complete)
                            on_complete(false);
                    });
            }
        },
        [on_complete](const MoonrakerError& err) {
            spdlog::error("[SpoolmanSlotSaver] Failed to fetch filaments: {}", err.message);
            if (on_complete)
                on_complete(false);
        });
}

} // namespace helix
