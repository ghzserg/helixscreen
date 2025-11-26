// Copyright 2025 HelixScreen
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <string>
#include <vector>

// Forward declarations
class PrinterState;
class MoonrakerAPI;

/**
 * @file ui_panel_base.h
 * @brief Abstract base class for all UI panels
 *
 * Provides shared infrastructure for panels including:
 * - Dependency injection (PrinterState, MoonrakerAPI)
 * - RAII observer management (automatic cleanup in destructor)
 * - Move semantics support for std::unique_ptr ownership
 * - Two-phase initialization (init_subjects → XML creation → setup)
 *
 * ## Usage Pattern:
 *
 * @code
 * class MyPanel : public PanelBase {
 * public:
 *     MyPanel(PrinterState& ps, MoonrakerAPI* api) : PanelBase(ps, api) {}
 *
 *     void init_subjects() override {
 *         // Register LVGL subjects for XML binding
 *     }
 *
 *     void setup(lv_obj_t* panel, lv_obj_t* parent) override {
 *         PanelBase::setup(panel, parent);  // Store references
 *         // Wire up event handlers, create widgets
 *     }
 *
 *     const char* get_name() const override { return "My Panel"; }
 *     const char* get_xml_component_name() const override { return "my_panel"; }
 *
 * protected:
 *     // Use register_observer() for RAII cleanup
 *     lv_observer_t* my_observer_ = nullptr;
 * };
 * @endcode
 *
 * ## Observer Lifecycle:
 *
 * Observers registered with register_observer() are automatically removed
 * in the destructor. This prevents use-after-free crashes when panels are
 * destroyed while subjects still exist.
 *
 * @see TempControlPanel for a complete implementation example
 */
class PanelBase {
  public:
    /**
     * @brief Construct panel with injected dependencies
     *
     * @param printer_state Reference to PrinterState singleton
     * @param api Pointer to MoonrakerAPI (may be nullptr if not connected)
     */
    PanelBase(PrinterState& printer_state, MoonrakerAPI* api);

    /**
     * @brief Virtual destructor - cleans up registered observers
     */
    virtual ~PanelBase();

    // Non-copyable (observer handles cannot be shared)
    PanelBase(const PanelBase&) = delete;
    PanelBase& operator=(const PanelBase&) = delete;

    // Movable (transfers observer ownership)
    PanelBase(PanelBase&& other) noexcept;
    PanelBase& operator=(PanelBase&& other) noexcept;

    //
    // === Core Lifecycle (must implement) ===
    //

    /**
     * @brief Initialize LVGL subjects for XML data binding
     *
     * MUST be called BEFORE lv_xml_create() for components that bind to subjects.
     * Safe to call multiple times - subsequent calls should be ignored.
     *
     * @note Implementations should set subjects_initialized_ = true
     */
    virtual void init_subjects() = 0;

    /**
     * @brief Setup panel after XML creation
     *
     * Called after lv_xml_create() returns. Wire up event handlers,
     * create child widgets, configure observers here.
     *
     * @param panel Root object of the panel (from lv_xml_create)
     * @param parent_screen Parent screen for navigation purposes
     */
    virtual void setup(lv_obj_t* panel, lv_obj_t* parent_screen);

    /**
     * @brief Get human-readable panel name
     *
     * Used in logging and debugging.
     *
     * @return Panel name (e.g., "Motion Panel", "Home Panel")
     */
    virtual const char* get_name() const = 0;

    /**
     * @brief Get XML component name for lv_xml_create()
     *
     * Must match the filename in ui_xml/ (without .xml extension).
     *
     * @return Component name (e.g., "motion_panel", "home_panel")
     */
    virtual const char* get_xml_component_name() const = 0;

    //
    // === Optional Lifecycle Hooks ===
    //

    /**
     * @brief Called when panel becomes visible
     *
     * Override to start animations, refresh data, or resume timers.
     * Default implementation does nothing.
     */
    virtual void on_activate() {}

    /**
     * @brief Called when panel is hidden
     *
     * Override to pause animations, stop timers, or cleanup temporary state.
     * Default implementation does nothing.
     */
    virtual void on_deactivate() {}

    //
    // === Public API ===
    //

    /**
     * @brief Update MoonrakerAPI pointer
     *
     * Call when API becomes available after initial construction,
     * or when reconnecting to a different printer.
     *
     * @param api New API pointer (may be nullptr)
     */
    void set_api(MoonrakerAPI* api) { api_ = api; }

    /**
     * @brief Get root panel object
     *
     * @return Panel object, or nullptr if not yet setup
     */
    lv_obj_t* get_panel() const { return panel_; }

    /**
     * @brief Check if subjects have been initialized
     *
     * @return true if init_subjects() was called
     */
    bool are_subjects_initialized() const { return subjects_initialized_; }

  protected:
    //
    // === Injected Dependencies ===
    //

    PrinterState& printer_state_;
    MoonrakerAPI* api_;

    //
    // === Panel State ===
    //

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    bool subjects_initialized_ = false;

    //
    // === Observer Management ===
    //

    /**
     * @brief Register an observer for automatic cleanup
     *
     * Call this after lv_subject_add_observer() to ensure the observer
     * is removed in the destructor. Prevents use-after-free crashes.
     *
     * @param observer Observer handle from lv_subject_add_observer()
     *
     * @note Null observers are safely ignored
     */
    void register_observer(lv_observer_t* observer);

    /**
     * @brief Remove all registered observers
     *
     * Called automatically by destructor. Can also be called manually
     * if panel needs to re-subscribe to different subjects.
     */
    void cleanup_observers();

  private:
    std::vector<lv_observer_t*> observers_;
};
