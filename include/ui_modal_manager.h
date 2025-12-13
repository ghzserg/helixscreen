// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"

#include <string>
#include <vector>

/**
 * @brief Modal positioning configuration
 *
 * Supports either alignment presets (center, right_mid, etc.) or manual
 * x/y coordinates for precise positioning.
 */
struct ui_modal_position_t {
    bool use_alignment;   /**< true = use alignment, false = use x/y */
    lv_align_t alignment; /**< Alignment preset (if use_alignment=true) */
    int32_t x;            /**< Manual x position (if use_alignment=false) */
    int32_t y;            /**< Manual y position (if use_alignment=false) */
};

/**
 * @brief Keyboard positioning configuration
 *
 * By default, keyboard position is automatically determined based on modal
 * alignment (e.g., left side for right-aligned modals). Manual override
 * available when needed.
 */
struct ui_modal_keyboard_config_t {
    bool auto_position;   /**< true = auto based on modal, false = manual */
    lv_align_t alignment; /**< Manual alignment (if auto_position=false) */
    int32_t x;            /**< Manual x offset (if auto_position=false) */
    int32_t y;            /**< Manual y offset (if auto_position=false) */
};

/**
 * @brief Complete modal configuration
 */
struct ui_modal_config_t {
    ui_modal_position_t position;         /**< Modal positioning */
    uint8_t backdrop_opa;                 /**< Backdrop opacity (0-255) */
    ui_modal_keyboard_config_t* keyboard; /**< Keyboard config (nullptr = no keyboard) */
    bool persistent;                      /**< true = persistent, false = create-on-demand */
    lv_event_cb_t on_close;               /**< Optional close callback */
};

/**
 * @brief Severity levels for modal dialogs
 *
 * Controls which icon is displayed in modal_dialog:
 * - INFO (0): Blue info icon - informational messages
 * - WARNING (1): Orange warning icon - caution messages
 * - ERROR (2): Red error icon - error messages
 */
enum ui_modal_severity {
    UI_MODAL_SEVERITY_INFO = 0,
    UI_MODAL_SEVERITY_WARNING = 1,
    UI_MODAL_SEVERITY_ERROR = 2,
};

/**
 * @brief Singleton manager for modal dialogs
 *
 * Provides a consistent API for creating and managing modal dialogs with support for:
 * - Modal stacking (multiple modals layered on top of each other)
 * - Flexible positioning (alignment presets or manual x/y coordinates)
 * - Automatic keyboard positioning based on modal location
 * - Configurable lifecycle (persistent vs. create-on-demand)
 * - Backdrop click-to-dismiss and ESC key handling
 *
 * Usage:
 *   ModalManager::instance().init_subjects();  // Call once at startup
 *   ModalManager::instance().configure(severity, show_cancel, "OK", "Cancel");
 *   lv_obj_t* modal = ModalManager::instance().show("modal_dialog", &config, nullptr);
 */
class ModalManager {
  public:
    /**
     * @brief Get singleton instance
     * @return Reference to the ModalManager singleton
     */
    static ModalManager& instance();

    // Non-copyable, non-movable (singleton)
    ModalManager(const ModalManager&) = delete;
    ModalManager& operator=(const ModalManager&) = delete;
    ModalManager(ModalManager&&) = delete;
    ModalManager& operator=(ModalManager&&) = delete;

    /**
     * @brief Initialize modal dialog subjects
     *
     * Creates and registers LVGL subjects used by modal_dialog.xml.
     * Call ONCE during app startup, before creating any modal_dialog components.
     */
    void init_subjects();

    /**
     * @brief Configure modal dialog before showing
     *
     * Sets all subject values atomically. Call BEFORE lv_xml_create("modal_dialog", ...).
     *
     * @param severity Dialog severity (UI_MODAL_SEVERITY_INFO/WARNING/ERROR)
     * @param show_cancel Whether to show cancel button
     * @param primary_text Primary button label (e.g., "OK", "Delete")
     * @param cancel_text Cancel button label (e.g., "Cancel", "No")
     */
    void configure(ui_modal_severity severity, bool show_cancel, const char* primary_text,
                   const char* cancel_text);

    /**
     * @brief Show a modal dialog
     *
     * Creates and displays a modal with the specified configuration.
     *
     * @param component_name XML component name (e.g., "confirmation_dialog")
     * @param config Modal configuration
     * @param attrs Optional XML attributes (NULL-terminated array, can be NULL)
     * @return Pointer to the created modal object, or NULL on error
     */
    lv_obj_t* show(const char* component_name, const ui_modal_config_t* config, const char** attrs);

    /**
     * @brief Hide a specific modal
     *
     * @param modal The modal object to hide
     */
    void hide(lv_obj_t* modal);

    /**
     * @brief Hide all modals
     */
    void hide_all();

    /**
     * @brief Get the topmost modal
     * @return Pointer to the topmost modal, or NULL if no modals are visible
     */
    lv_obj_t* get_top() const;

    /**
     * @brief Check if any modals are currently visible
     * @return true if at least one modal is visible, false otherwise
     */
    bool is_visible() const;

    /**
     * @brief Register a textarea with automatic keyboard positioning
     *
     * @param modal The modal containing the textarea
     * @param textarea The textarea widget
     */
    void register_keyboard(lv_obj_t* modal, lv_obj_t* textarea);

    // Subject accessors
    lv_subject_t* get_severity_subject();
    lv_subject_t* get_show_cancel_subject();
    lv_subject_t* get_primary_text_subject();
    lv_subject_t* get_cancel_text_subject();

  private:
    // Private constructor for singleton
    ModalManager() = default;
    ~ModalManager();

    /**
     * @brief Modal metadata stored alongside LVGL object
     */
    struct ModalMetadata {
        lv_obj_t* modal_obj;        // The modal object itself
        ui_modal_config_t config;   // Original configuration
        std::string component_name; // XML component name
    };

    // Find modal metadata by LVGL object pointer
    ModalMetadata* find_modal_metadata(lv_obj_t* modal);

    // Position keyboard based on modal configuration
    void position_keyboard_for_modal(lv_obj_t* modal);

    // Get automatic keyboard position based on modal position
    void get_auto_keyboard_position(const ui_modal_position_t& modal_pos, lv_align_t* out_align,
                                    int32_t* out_x, int32_t* out_y);

    // Animation helpers
    void animate_entrance(lv_obj_t* modal);
    void animate_exit(lv_obj_t* modal, bool is_persistent);
    static void exit_animation_complete_cb(lv_anim_t* anim);

    // Event callbacks (static)
    static void backdrop_click_event_cb(lv_event_t* e);
    static void modal_key_event_cb(lv_event_t* e);

    // Modal stack - topmost modal is at the back
    std::vector<ModalMetadata> modal_stack_;

    // Subjects for modal_dialog.xml binding
    lv_subject_t dialog_severity_{};
    lv_subject_t dialog_show_cancel_{};
    lv_subject_t dialog_primary_text_{};
    lv_subject_t dialog_cancel_text_{};

    // Default button labels
    static constexpr const char* default_primary_text_ = "OK";
    static constexpr const char* default_cancel_text_ = "Cancel";

    bool subjects_initialized_ = false;
};

// ============================================================================
// LEGACY API (forwards to ModalManager for backward compatibility)
// ============================================================================

/**
 * @brief Show a modal dialog
 * @deprecated Use ModalManager::instance().show() instead
 */
lv_obj_t* ui_modal_show(const char* component_name, const ui_modal_config_t* config,
                        const char** attrs);

/**
 * @brief Hide a specific modal
 * @deprecated Use ModalManager::instance().hide() instead
 */
void ui_modal_hide(lv_obj_t* modal);

/**
 * @brief Hide all modals
 * @deprecated Use ModalManager::instance().hide_all() instead
 */
void ui_modal_hide_all();

/**
 * @brief Get the topmost modal
 * @deprecated Use ModalManager::instance().get_top() instead
 */
lv_obj_t* ui_modal_get_top();

/**
 * @brief Check if any modals are currently visible
 * @deprecated Use ModalManager::instance().is_visible() instead
 */
bool ui_modal_is_visible();

/**
 * @brief Register a textarea with automatic keyboard positioning
 * @deprecated Use ModalManager::instance().register_keyboard() instead
 */
void ui_modal_register_keyboard(lv_obj_t* modal, lv_obj_t* textarea);

/**
 * @brief Initialize modal dialog subjects
 * @deprecated Use ModalManager::instance().init_subjects() instead
 */
void ui_modal_init_subjects();

/**
 * @brief Configure modal dialog before showing
 * @deprecated Use ModalManager::instance().configure() instead
 */
void ui_modal_configure(ui_modal_severity severity, bool show_cancel, const char* primary_text,
                        const char* cancel_text);

/**
 * @brief Get dialog_severity subject for direct access
 * @deprecated Use ModalManager::instance().get_severity_subject() instead
 */
lv_subject_t* ui_modal_get_severity_subject();

/**
 * @brief Get dialog_show_cancel subject for direct access
 * @deprecated Use ModalManager::instance().get_show_cancel_subject() instead
 */
lv_subject_t* ui_modal_get_show_cancel_subject();

/**
 * @brief Get dialog_primary_text subject for direct access
 * @deprecated Use ModalManager::instance().get_primary_text_subject() instead
 */
lv_subject_t* ui_modal_get_primary_text_subject();

/**
 * @brief Get dialog_cancel_text subject for direct access
 * @deprecated Use ModalManager::instance().get_cancel_text_subject() instead
 */
lv_subject_t* ui_modal_get_cancel_text_subject();
