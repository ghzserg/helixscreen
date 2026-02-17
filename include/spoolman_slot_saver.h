// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"
#include "spoolman_types.h"

#include <cmath>
#include <functional>

class MoonrakerAPI;

namespace helix {

/**
 * @brief Describes what changed between original and edited SlotInfo
 *
 * Filament-level changes (brand, material, color) require finding or creating
 * a matching Spoolman filament definition. Spool-level changes (weight) only
 * require updating the spool record.
 */
struct ChangeSet {
    bool filament_level = false; ///< vendor, material, or color changed
    bool spool_level = false;    ///< remaining weight changed

    /// Check if any change was detected
    [[nodiscard]] bool any() const {
        return filament_level || spool_level;
    }
};

/**
 * @brief Handles saving slot edits back to Spoolman
 *
 * Orchestrates the find-or-create filament flow:
 * 1. Detects what changed between original and edited SlotInfo
 * 2. For filament-level changes: finds matching filament or creates new one
 * 3. Re-links the spool to the correct filament definition
 * 4. Updates spool weight if changed
 */
class SpoolmanSlotSaver {
  public:
    using CompletionCallback = std::function<void(bool success)>;

    /// Weight comparison threshold for float equality
    static constexpr float WEIGHT_THRESHOLD = 0.1f;

    /**
     * @brief Construct a SpoolmanSlotSaver
     * @param api MoonrakerAPI instance for Spoolman API calls
     */
    explicit SpoolmanSlotSaver(MoonrakerAPI* api);

    /**
     * @brief Compare two SlotInfo structs and detect what changed
     *
     * @param original The slot state before editing
     * @param edited The slot state after editing
     * @return ChangeSet describing filament-level and spool-level changes
     */
    static ChangeSet detect_changes(const SlotInfo& original, const SlotInfo& edited);

    /**
     * @brief Save slot edits to Spoolman via the API
     *
     * Handles the full async orchestration:
     * - No spoolman_id or no changes: immediate success callback
     * - Only weight changed: update spool weight
     * - Filament changed: find-or-create filament, relink spool
     * - Both changed: relink first, then update weight
     *
     * @param original The slot state before editing
     * @param edited The slot state after editing
     * @param on_complete Called with true on success, false on failure
     */
    void save(const SlotInfo& original, const SlotInfo& edited, CompletionCallback on_complete);

  private:
    MoonrakerAPI* api_;

    /**
     * @brief Convert uint32_t RGB to hex string like "#FF0000"
     */
    static std::string color_to_hex(uint32_t rgb);

    /**
     * @brief Update spool weight via API
     */
    void update_weight(int spool_id, float weight_g, CompletionCallback on_complete);

    /**
     * @brief Find matching filament or create new one, then relink spool
     */
    void find_or_create_filament_and_relink(int spool_id, const SlotInfo& edited,
                                            CompletionCallback on_complete);

    /**
     * @brief Relink a spool to a different filament ID
     */
    void relink_spool(int spool_id, int filament_id, CompletionCallback on_complete);
};

} // namespace helix

// Bring SpoolmanSlotSaver into global scope for convenience (matches project convention)
using helix::ChangeSet;
using helix::SpoolmanSlotSaver;
