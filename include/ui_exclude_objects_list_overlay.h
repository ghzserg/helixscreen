// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include "overlay_base.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class MoonrakerAPI;
namespace helix {
class PrinterState;
}
using helix::PrinterState;

namespace helix::gcode {
class GCodeObjectThumbnailRenderer;
struct ObjectThumbnailSet;
} // namespace helix::gcode

namespace helix::ui {
class PrintExcludeObjectManager;

/**
 * @brief Overlay listing all print objects for exclude-object feature
 *
 * Shows a scrollable list of all defined objects in the current print,
 * with status indicators (current/idle/excluded) and tap-to-exclude.
 * Uses the existing PrintExcludeObjectManager confirmation flow.
 *
 * ## Usage:
 * @code
 * auto& overlay = get_exclude_objects_list_overlay();
 * overlay.show(parent_screen, api, printer_state, manager);
 * @endcode
 */
class ExcludeObjectsListOverlay : public OverlayBase {
  public:
    ExcludeObjectsListOverlay();
    ~ExcludeObjectsListOverlay() override;

    // Non-copyable
    ExcludeObjectsListOverlay(const ExcludeObjectsListOverlay&) = delete;
    ExcludeObjectsListOverlay& operator=(const ExcludeObjectsListOverlay&) = delete;

    void init_subjects() override;
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Exclude Objects List";
    }

    void on_activate() override;
    void on_deactivate() override;

    /**
     * @brief Show the overlay
     *
     * Lazy-creates the overlay, registers with NavigationManager, and pushes.
     *
     * @param parent_screen Parent screen for overlay creation
     * @param api MoonrakerAPI pointer (for exclude commands)
     * @param printer_state Reference to PrinterState for object lists
     * @param manager Exclude object manager for confirmation flow
     * @param gcode_viewer Optional gcode viewer widget for thumbnail rendering
     */
    void show(lv_obj_t* parent_screen, MoonrakerAPI* api, PrinterState& printer_state,
              PrintExcludeObjectManager* manager, lv_obj_t* gcode_viewer = nullptr);

  private:
    void populate_list();
    lv_obj_t* create_object_row(lv_obj_t* parent, const std::string& name, bool is_excluded,
                                bool is_current);
    void start_thumbnail_render();
    void apply_thumbnails();
    void cleanup_thumbnails();

    lv_obj_t* objects_list_{nullptr};
    MoonrakerAPI* api_{nullptr};
    PrinterState* printer_state_{nullptr};
    PrintExcludeObjectManager* manager_{nullptr};
    ObserverGuard excluded_observer_;
    ObserverGuard defined_observer_;

    // Thumbnail rendering
    lv_obj_t* gcode_viewer_{nullptr};
    std::unique_ptr<helix::gcode::GCodeObjectThumbnailRenderer> thumbnail_renderer_;
    std::unordered_map<std::string, lv_draw_buf_t*> object_thumbnails_;
    bool thumbnails_available_{false};
};

/**
 * @brief Get singleton ExcludeObjectsListOverlay instance
 * @return Reference to singleton
 */
ExcludeObjectsListOverlay& get_exclude_objects_list_overlay();

} // namespace helix::ui
