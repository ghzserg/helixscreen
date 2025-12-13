// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_modal_manager.h"

#include "ui_keyboard.h"

#include "settings_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>

// ============================================================================
// ANIMATION CONSTANTS
// ============================================================================
// Duration values match globals.xml tokens for consistency
static constexpr int32_t MODAL_ENTRANCE_DURATION_MS = 250; // anim_normal
static constexpr int32_t MODAL_EXIT_DURATION_MS = 150;     // anim_fast

// Scale animation uses percentage values (0-256 in LVGL)
// We animate from 85% (218) to 100% (256), with slight overshoot for bounce
static constexpr int32_t MODAL_SCALE_START = 218; // ~85% scale
static constexpr int32_t MODAL_SCALE_END = 256;   // 100% scale

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

ModalManager& ModalManager::instance() {
    static ModalManager instance;
    return instance;
}

ModalManager::~ModalManager() {
    // Clean up any remaining keyboard configs
    for (auto& meta : modal_stack_) {
        if (meta.config.keyboard) {
            delete meta.config.keyboard;
            meta.config.keyboard = nullptr;
        }
    }
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================

void ModalManager::backdrop_click_event_cb(lv_event_t* e) {
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* current = static_cast<lv_obj_t*>(lv_event_get_current_target(e));

    // Only respond if click was directly on backdrop (not bubbled from child)
    if (target != current) {
        return;
    }

    auto& mgr = ModalManager::instance();

    // Only allow closing topmost modal
    if (!mgr.modal_stack_.empty() && mgr.modal_stack_.back().modal_obj == current) {
        spdlog::debug("[ModalManager] Backdrop clicked on topmost modal - closing");
        mgr.hide(current);
    }
}

void ModalManager::modal_key_event_cb(lv_event_t* e) {
    uint32_t key = lv_event_get_key(e);

    auto& mgr = ModalManager::instance();

    if (key == LV_KEY_ESC && !mgr.modal_stack_.empty()) {
        spdlog::debug("[ModalManager] ESC key pressed - closing topmost modal");
        mgr.hide(mgr.modal_stack_.back().modal_obj);
    }
}

// ============================================================================
// ANIMATION HELPERS
// ============================================================================

void ModalManager::animate_entrance(lv_obj_t* modal) {
    // Get the dialog card inside the modal backdrop
    lv_obj_t* dialog = lv_obj_find_by_name(modal, "dialog_card");

    // Skip animation if disabled - show modal in final state
    if (!SettingsManager::instance().get_animations_enabled()) {
        lv_obj_set_style_opa(modal, LV_OPA_COVER, LV_PART_MAIN);
        if (dialog) {
            lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_END, LV_PART_MAIN);
            lv_obj_set_style_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
        }
        spdlog::debug("[ModalManager] Animations disabled - showing modal instantly");
        return;
    }

    // Start backdrop transparent
    lv_obj_set_style_opa(modal, LV_OPA_TRANSP, LV_PART_MAIN);

    // If dialog card exists, start it scaled down and transparent
    if (dialog) {
        lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_START, LV_PART_MAIN);
        lv_obj_set_style_opa(dialog, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    // Fade in backdrop (0 → configured opacity, stored in style)
    // Note: We fade to full cover, the backdrop_opa was already set
    lv_anim_t backdrop_anim;
    lv_anim_init(&backdrop_anim);
    lv_anim_set_var(&backdrop_anim, modal);
    lv_anim_set_values(&backdrop_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&backdrop_anim, MODAL_ENTRANCE_DURATION_MS);
    lv_anim_set_path_cb(&backdrop_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&backdrop_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    lv_anim_start(&backdrop_anim);

    // Animate dialog card if found
    if (dialog) {
        // Scale up animation (85% → 100%) with overshoot for bounce effect
        lv_anim_t scale_anim;
        lv_anim_init(&scale_anim);
        lv_anim_set_var(&scale_anim, dialog);
        lv_anim_set_values(&scale_anim, MODAL_SCALE_START, MODAL_SCALE_END);
        lv_anim_set_duration(&scale_anim, MODAL_ENTRANCE_DURATION_MS);
        lv_anim_set_path_cb(&scale_anim, lv_anim_path_overshoot);
        lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
            lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj),
                                             static_cast<int16_t>(value), LV_PART_MAIN);
        });
        lv_anim_start(&scale_anim);

        // Fade in dialog card (0 → 255)
        lv_anim_t fade_anim;
        lv_anim_init(&fade_anim);
        lv_anim_set_var(&fade_anim, dialog);
        lv_anim_set_values(&fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&fade_anim, MODAL_ENTRANCE_DURATION_MS);
        lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
            lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                                 LV_PART_MAIN);
        });
        lv_anim_start(&fade_anim);
    }

    spdlog::debug("[ModalManager] Started entrance animation");
}

void ModalManager::animate_exit(lv_obj_t* modal, bool is_persistent) {
    // Get the dialog card inside the modal backdrop
    lv_obj_t* dialog = lv_obj_find_by_name(modal, "dialog_card");

    // Skip animation if disabled - directly hide/delete
    if (!SettingsManager::instance().get_animations_enabled()) {
        // Reset dialog scale for potential reuse (persistent modals)
        if (dialog) {
            lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_END, LV_PART_MAIN);
            lv_obj_set_style_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
        }

        if (is_persistent) {
            spdlog::debug("[ModalManager] Animations disabled - hiding persistent modal instantly");
            lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(modal, LV_OPA_COVER, LV_PART_MAIN);
        } else {
            spdlog::debug("[ModalManager] Animations disabled - deleting modal instantly");
            lv_obj_delete_async(modal);
        }
        return;
    }

    // Fade out backdrop
    lv_anim_t backdrop_anim;
    lv_anim_init(&backdrop_anim);
    lv_anim_set_var(&backdrop_anim, modal);
    lv_anim_set_values(&backdrop_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&backdrop_anim, MODAL_EXIT_DURATION_MS);
    lv_anim_set_path_cb(&backdrop_anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&backdrop_anim, [](void* obj, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                             LV_PART_MAIN);
    });
    // Set completion callback on backdrop (it will handle cleanup)
    lv_anim_set_completed_cb(&backdrop_anim, exit_animation_complete_cb);
    // Store persistent flag in user_data (low bit: is_persistent)
    lv_anim_set_user_data(&backdrop_anim,
                          reinterpret_cast<void*>(static_cast<intptr_t>(is_persistent)));
    lv_anim_start(&backdrop_anim);

    // Animate dialog card if found
    if (dialog) {
        // Scale down animation (100% → 90%)
        lv_anim_t scale_anim;
        lv_anim_init(&scale_anim);
        lv_anim_set_var(&scale_anim, dialog);
        lv_anim_set_values(&scale_anim, MODAL_SCALE_END, MODAL_SCALE_START);
        lv_anim_set_duration(&scale_anim, MODAL_EXIT_DURATION_MS);
        lv_anim_set_path_cb(&scale_anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&scale_anim, [](void* obj, int32_t value) {
            lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj),
                                             static_cast<int16_t>(value), LV_PART_MAIN);
        });
        lv_anim_start(&scale_anim);

        // Fade out dialog card
        lv_anim_t fade_anim;
        lv_anim_init(&fade_anim);
        lv_anim_set_var(&fade_anim, dialog);
        lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_duration(&fade_anim, MODAL_EXIT_DURATION_MS);
        lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&fade_anim, [](void* obj, int32_t value) {
            lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value),
                                 LV_PART_MAIN);
        });
        lv_anim_start(&fade_anim);
    }

    spdlog::debug("[ModalManager] Started exit animation (persistent={})", is_persistent);
}

void ModalManager::exit_animation_complete_cb(lv_anim_t* anim) {
    lv_obj_t* modal = static_cast<lv_obj_t*>(anim->var);
    bool is_persistent = static_cast<bool>(reinterpret_cast<intptr_t>(anim->user_data));

    // Reset dialog scale for potential reuse (persistent modals)
    lv_obj_t* dialog = lv_obj_find_by_name(modal, "dialog_card");
    if (dialog) {
        lv_obj_set_style_transform_scale(dialog, MODAL_SCALE_END, LV_PART_MAIN);
        lv_obj_set_style_opa(dialog, LV_OPA_COVER, LV_PART_MAIN);
    }

    if (is_persistent) {
        spdlog::debug("[ModalManager] Exit animation complete - hiding persistent modal");
        lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(modal, LV_OPA_COVER, LV_PART_MAIN); // Reset for next show
    } else {
        spdlog::debug("[ModalManager] Exit animation complete - deleting modal");
        lv_obj_delete_async(modal);
    }
}

// ============================================================================
// MODAL MANAGER IMPLEMENTATION
// ============================================================================

void ModalManager::init_subjects() {
    if (subjects_initialized_) {
        spdlog::warn("[ModalManager] Subjects already initialized - skipping");
        return;
    }

    spdlog::debug("[ModalManager] Initializing modal dialog subjects");

    // Initialize integer subjects
    lv_subject_init_int(&dialog_severity_, UI_MODAL_SEVERITY_INFO);
    lv_subject_init_int(&dialog_show_cancel_, 0);

    // Initialize string subjects (pointer type)
    lv_subject_init_pointer(&dialog_primary_text_, (void*)default_primary_text_);
    lv_subject_init_pointer(&dialog_cancel_text_, (void*)default_cancel_text_);

    // Register with LVGL XML system for binding
    lv_xml_register_subject(nullptr, "dialog_severity", &dialog_severity_);
    lv_xml_register_subject(nullptr, "dialog_show_cancel", &dialog_show_cancel_);
    lv_xml_register_subject(nullptr, "dialog_primary_text", &dialog_primary_text_);
    lv_xml_register_subject(nullptr, "dialog_cancel_text", &dialog_cancel_text_);

    subjects_initialized_ = true;
    spdlog::debug("[ModalManager] Modal dialog subjects registered");
}

void ModalManager::configure(ui_modal_severity severity, bool show_cancel, const char* primary_text,
                             const char* cancel_text) {
    if (!subjects_initialized_) {
        spdlog::error("[ModalManager] Cannot configure - subjects not initialized!");
        return;
    }

    spdlog::debug("[ModalManager] Configuring dialog: severity={}, show_cancel={}, "
                  "primary='{}', cancel='{}'",
                  (int)severity, show_cancel, primary_text ? primary_text : "(null)",
                  cancel_text ? cancel_text : "(null)");

    lv_subject_set_int(&dialog_severity_, severity);
    lv_subject_set_int(&dialog_show_cancel_, show_cancel ? 1 : 0);

    if (primary_text) {
        lv_subject_set_pointer(&dialog_primary_text_, (void*)primary_text);
    }
    if (cancel_text) {
        lv_subject_set_pointer(&dialog_cancel_text_, (void*)cancel_text);
    }
}

ModalManager::ModalMetadata* ModalManager::find_modal_metadata(lv_obj_t* modal) {
    auto it = std::find_if(modal_stack_.begin(), modal_stack_.end(),
                           [modal](const ModalMetadata& meta) { return meta.modal_obj == modal; });
    return (it != modal_stack_.end()) ? &(*it) : nullptr;
}

void ModalManager::get_auto_keyboard_position(const ui_modal_position_t& modal_pos,
                                              lv_align_t* out_align, int32_t* out_x,
                                              int32_t* out_y) {
    // Default: bottom-center
    *out_align = LV_ALIGN_BOTTOM_MID;
    *out_x = 0;
    *out_y = 0;

    if (!modal_pos.use_alignment) {
        // Manual positioning - use heuristic based on x position
        lv_obj_t* screen = lv_screen_active();
        int32_t screen_w = lv_obj_get_width(screen);

        if (modal_pos.x > screen_w / 2) {
            spdlog::debug("[ModalManager] Manual position on right side - keyboard on left");
            *out_align = LV_ALIGN_LEFT_MID;
            *out_x = 0;
            *out_y = 0;
        }
        return;
    }

    // Alignment-based positioning
    switch (modal_pos.alignment) {
    case LV_ALIGN_RIGHT_MID:
    case LV_ALIGN_TOP_RIGHT:
    case LV_ALIGN_BOTTOM_RIGHT:
        spdlog::debug("[ModalManager] Right-aligned modal - keyboard on left");
        *out_align = LV_ALIGN_LEFT_MID;
        *out_x = 0;
        *out_y = 0;
        break;

    case LV_ALIGN_LEFT_MID:
    case LV_ALIGN_TOP_LEFT:
    case LV_ALIGN_BOTTOM_LEFT:
        spdlog::debug("[ModalManager] Left-aligned modal - keyboard on right");
        *out_align = LV_ALIGN_RIGHT_MID;
        *out_x = 0;
        *out_y = 0;
        break;

    default:
        spdlog::debug("[ModalManager] Center/top/bottom modal - keyboard at bottom");
        *out_align = LV_ALIGN_BOTTOM_MID;
        *out_x = 0;
        *out_y = 0;
        break;
    }
}

void ModalManager::position_keyboard_for_modal(lv_obj_t* modal) {
    ModalMetadata* meta = find_modal_metadata(modal);
    if (!meta || !meta->config.keyboard) {
        return;
    }

    lv_align_t align;
    int32_t x, y;

    if (meta->config.keyboard->auto_position) {
        get_auto_keyboard_position(meta->config.position, &align, &x, &y);
    } else {
        align = meta->config.keyboard->alignment;
        x = meta->config.keyboard->x;
        y = meta->config.keyboard->y;
    }

    spdlog::debug("[ModalManager] Positioning keyboard: align={}, x={}, y={}", (int)align, x, y);
    ui_keyboard_set_position(align, x, y);
}

lv_obj_t* ModalManager::show(const char* component_name, const ui_modal_config_t* config,
                             const char** attrs) {
    if (!component_name || !config) {
        spdlog::error("[ModalManager] Invalid parameters: component_name={}, config={}",
                      (void*)component_name, (void*)config);
        return nullptr;
    }

    spdlog::info("[ModalManager] Showing modal: {}", component_name);

    // Create modal from XML
    lv_obj_t* modal =
        static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), component_name, attrs));
    if (!modal) {
        spdlog::error("[ModalManager] Failed to create modal from XML: {}", component_name);
        return nullptr;
    }

    // Apply positioning
    if (config->position.use_alignment) {
        spdlog::debug("[ModalManager] Positioning with alignment: {}",
                      (int)config->position.alignment);
        lv_obj_align(modal, config->position.alignment, 0, 0);
    } else {
        spdlog::debug("[ModalManager] Positioning at x={}, y={}", config->position.x,
                      config->position.y);
        lv_obj_set_pos(modal, config->position.x, config->position.y);
    }

    // Set backdrop opacity
    lv_obj_set_style_bg_opa(modal, config->backdrop_opa, LV_PART_MAIN);

    // Add backdrop click handler
    lv_obj_add_event_cb(modal, backdrop_click_event_cb, LV_EVENT_CLICKED, nullptr);

    // Add ESC key handler
    lv_obj_add_event_cb(modal, modal_key_event_cb, LV_EVENT_KEY, nullptr);

    // Register on_close callback if provided
    if (config->on_close) {
        lv_obj_add_event_cb(modal, config->on_close, LV_EVENT_DELETE, nullptr);
        spdlog::debug("[ModalManager] Close callback registered for DELETE event");
    }

    // Bring to foreground
    lv_obj_move_foreground(modal);

    // Add to modal stack
    ModalMetadata meta;
    meta.modal_obj = modal;
    meta.config = *config;
    meta.component_name = component_name;

    // Deep copy keyboard config if present
    if (config->keyboard) {
        meta.config.keyboard = new ui_modal_keyboard_config_t(*config->keyboard);
    }

    modal_stack_.push_back(meta);

    // Position keyboard if configured
    if (config->keyboard) {
        position_keyboard_for_modal(modal);
    }

    // Start entrance animation (backdrop fade + dialog scale/fade)
    animate_entrance(modal);

    spdlog::info("[ModalManager] Modal shown successfully (stack depth: {})", modal_stack_.size());

    return modal;
}

void ModalManager::hide(lv_obj_t* modal) {
    if (!modal) {
        spdlog::error("[ModalManager] Cannot hide NULL modal");
        return;
    }

    // Find modal in stack
    auto it = std::find_if(modal_stack_.begin(), modal_stack_.end(),
                           [modal](const ModalMetadata& meta) { return meta.modal_obj == modal; });

    if (it == modal_stack_.end()) {
        spdlog::warn("[ModalManager] Modal not found in stack: {}", (void*)modal);
        if (lv_obj_is_valid(modal)) {
            lv_obj_delete_async(modal);
        }
        return;
    }

    spdlog::info("[ModalManager] Hiding modal: {}", it->component_name);

    // Clean up keyboard config if allocated
    if (it->config.keyboard) {
        delete it->config.keyboard;
        it->config.keyboard = nullptr;
    }

    // Capture persistence flag before removing from stack
    bool was_persistent = it->config.persistent;

    // Remove from stack before starting exit animation
    modal_stack_.erase(it);

    // Start exit animation (hide/delete happens in completion callback)
    animate_exit(modal, was_persistent);

    spdlog::info("[ModalManager] Modal hiding with animation (stack depth: {})",
                 modal_stack_.size());

    // If there are more modals, bring the new topmost to foreground
    if (!modal_stack_.empty()) {
        lv_obj_move_foreground(modal_stack_.back().modal_obj);
        spdlog::debug("[ModalManager] Brought previous modal to foreground");
    }
}

void ModalManager::hide_all() {
    spdlog::info("[ModalManager] Hiding all modals (count: {})", modal_stack_.size());

    while (!modal_stack_.empty()) {
        hide(modal_stack_.back().modal_obj);
    }

    spdlog::info("[ModalManager] All modals hidden");
}

lv_obj_t* ModalManager::get_top() const {
    if (modal_stack_.empty()) {
        return nullptr;
    }
    return modal_stack_.back().modal_obj;
}

bool ModalManager::is_visible() const {
    return !modal_stack_.empty();
}

void ModalManager::register_keyboard(lv_obj_t* modal, lv_obj_t* textarea) {
    if (!modal || !textarea) {
        spdlog::error("[ModalManager] Cannot register keyboard: modal={}, textarea={}",
                      (void*)modal, (void*)textarea);
        return;
    }

    position_keyboard_for_modal(modal);

    // Check if this is a password textarea
    bool is_password = lv_textarea_get_password_mode(textarea);

    if (is_password) {
        ui_keyboard_register_textarea_ex(textarea, true);
        spdlog::debug("[ModalManager] Registered PASSWORD textarea with keyboard");
    } else {
        ui_keyboard_register_textarea(textarea);
    }

    spdlog::debug("[ModalManager] Keyboard registered for modal: {}, textarea: {}", (void*)modal,
                  (void*)textarea);
}

lv_subject_t* ModalManager::get_severity_subject() {
    return &dialog_severity_;
}

lv_subject_t* ModalManager::get_show_cancel_subject() {
    return &dialog_show_cancel_;
}

lv_subject_t* ModalManager::get_primary_text_subject() {
    return &dialog_primary_text_;
}

lv_subject_t* ModalManager::get_cancel_text_subject() {
    return &dialog_cancel_text_;
}

// ============================================================================
// LEGACY API (forwards to ModalManager)
// ============================================================================

lv_obj_t* ui_modal_show(const char* component_name, const ui_modal_config_t* config,
                        const char** attrs) {
    return ModalManager::instance().show(component_name, config, attrs);
}

void ui_modal_hide(lv_obj_t* modal) {
    ModalManager::instance().hide(modal);
}

void ui_modal_hide_all() {
    ModalManager::instance().hide_all();
}

lv_obj_t* ui_modal_get_top() {
    return ModalManager::instance().get_top();
}

bool ui_modal_is_visible() {
    return ModalManager::instance().is_visible();
}

void ui_modal_register_keyboard(lv_obj_t* modal, lv_obj_t* textarea) {
    ModalManager::instance().register_keyboard(modal, textarea);
}

void ui_modal_init_subjects() {
    ModalManager::instance().init_subjects();
}

void ui_modal_configure(ui_modal_severity severity, bool show_cancel, const char* primary_text,
                        const char* cancel_text) {
    ModalManager::instance().configure(severity, show_cancel, primary_text, cancel_text);
}

lv_subject_t* ui_modal_get_severity_subject() {
    return ModalManager::instance().get_severity_subject();
}

lv_subject_t* ui_modal_get_show_cancel_subject() {
    return ModalManager::instance().get_show_cancel_subject();
}

lv_subject_t* ui_modal_get_primary_text_subject() {
    return ModalManager::instance().get_primary_text_subject();
}

lv_subject_t* ui_modal_get_cancel_text_subject() {
    return ModalManager::instance().get_cancel_text_subject();
}
