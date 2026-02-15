// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_exclude_objects_list_overlay.h"

#include "ui_gcode_viewer.h"
#include "ui_nav.h"
#include "ui_nav_manager.h"
#include "ui_print_exclude_object_manager.h"

#include "gcode_object_thumbnail_renderer.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "static_panel_registry.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

namespace helix::ui {

// Thumbnail dimensions in pixels
static constexpr int kThumbnailSize = 40;

// ============================================================================
// SINGLETON ACCESSOR
// ============================================================================

static std::unique_ptr<ExcludeObjectsListOverlay> g_exclude_objects_list_overlay;

ExcludeObjectsListOverlay& get_exclude_objects_list_overlay() {
    if (!g_exclude_objects_list_overlay) {
        g_exclude_objects_list_overlay = std::make_unique<ExcludeObjectsListOverlay>();
        StaticPanelRegistry::instance().register_destroy(
            "ExcludeObjectsListOverlay", []() { g_exclude_objects_list_overlay.reset(); });
    }
    return *g_exclude_objects_list_overlay;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ExcludeObjectsListOverlay::ExcludeObjectsListOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

ExcludeObjectsListOverlay::~ExcludeObjectsListOverlay() {
    cleanup_thumbnails();
    spdlog::trace("[{}] Destroyed", get_name());
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void ExcludeObjectsListOverlay::init_subjects() {
    // No local subjects needed - we observe PrinterState subjects
    subjects_initialized_ = true;
}

void ExcludeObjectsListOverlay::register_callbacks() {
    // No XML event callbacks - rows use lv_obj_add_event_cb (dynamic creation exception)
    spdlog::debug("[{}] Callbacks registered (none needed)", get_name());
}

// ============================================================================
// UI CREATION
// ============================================================================

lv_obj_t* ExcludeObjectsListOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::warn("[{}] create() called but overlay already exists", get_name());
        return overlay_root_;
    }

    spdlog::debug("[{}] Creating overlay...", get_name());

    // Use base class helper for standard overlay setup (header, content padding, hidden)
    if (!create_overlay_from_xml(parent, "exclude_objects_list_overlay")) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }

    // Find the dynamic list container
    objects_list_ = lv_obj_find_by_name(overlay_root_, "objects_list");
    if (!objects_list_) {
        spdlog::error("[{}] Could not find objects_list container", get_name());
    }

    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void ExcludeObjectsListOverlay::show(lv_obj_t* parent_screen, MoonrakerAPI* api,
                                     PrinterState& printer_state,
                                     PrintExcludeObjectManager* manager, lv_obj_t* gcode_viewer) {
    spdlog::debug("[{}] show() called", get_name());

    api_ = api;
    printer_state_ = &printer_state;
    manager_ = manager;
    gcode_viewer_ = gcode_viewer;

    // Lazy create
    if (!overlay_root_ && parent_screen) {
        if (!are_subjects_initialized()) {
            init_subjects();
        }
        register_callbacks();
        create(parent_screen);
    }

    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show - overlay not created", get_name());
        return;
    }

    // Register with NavigationManager for lifecycle callbacks
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);

    // Push onto navigation stack (on_activate will populate the list)
    ui_nav_push_overlay(overlay_root_, false /* hide_previous */);
}

// ============================================================================
// LIFECYCLE HOOKS
// ============================================================================

void ExcludeObjectsListOverlay::on_activate() {
    OverlayBase::on_activate();

    if (!printer_state_)
        return;

    // Observe excluded objects changes - repopulate on change
    excluded_observer_ = ObserverGuard(
        printer_state_->get_excluded_objects_version_subject(),
        [](lv_observer_t* obs, lv_subject_t*) {
            auto* self = static_cast<ExcludeObjectsListOverlay*>(lv_observer_get_user_data(obs));
            if (self && self->is_visible()) {
                self->populate_list();
            }
        },
        this);

    // Observe defined objects changes - repopulate on change
    defined_observer_ = ObserverGuard(
        printer_state_->get_defined_objects_version_subject(),
        [](lv_observer_t* obs, lv_subject_t*) {
            auto* self = static_cast<ExcludeObjectsListOverlay*>(lv_observer_get_user_data(obs));
            if (self && self->is_visible()) {
                self->populate_list();
            }
        },
        this);

    // Repopulate to get fresh data
    populate_list();

    // Start async thumbnail rendering (if gcode data is available)
    start_thumbnail_render();
}

void ExcludeObjectsListOverlay::on_deactivate() {
    OverlayBase::on_deactivate();

    // Release observers when not visible
    excluded_observer_.reset();
    defined_observer_.reset();

    // Cancel any in-progress thumbnail render (but DON'T free draw buffers yet —
    // the overlay widget tree is still alive during the slide-out animation and
    // lv_image widgets reference the draw buffers. Freeing now would cause LVGL
    // to read freed memory as file paths. Buffers are freed on next on_activate
    // via start_thumbnail_render, or in the destructor.)
    if (thumbnail_renderer_) {
        thumbnail_renderer_->cancel();
        thumbnail_renderer_.reset();
    }
}

// ============================================================================
// THUMBNAIL RENDERING
// ============================================================================

void ExcludeObjectsListOverlay::start_thumbnail_render() {
    if (!gcode_viewer_) {
        spdlog::debug("[{}] No gcode viewer - skipping thumbnails", get_name());
        return;
    }

    const auto* parsed = ui_gcode_viewer_get_parsed_file(gcode_viewer_);
    if (!parsed || parsed->layers.empty()) {
        spdlog::debug("[{}] No parsed gcode data - skipping thumbnails", get_name());
        return;
    }

    // Check if segments are available (they get cleared after geometry build)
    bool has_segments = false;
    for (const auto& layer : parsed->layers) {
        if (!layer.segments.empty()) {
            has_segments = true;
            break;
        }
    }
    if (!has_segments) {
        spdlog::debug("[{}] Segments cleared - skipping thumbnails", get_name());
        return;
    }

    // Determine filament color for rendering
    uint32_t color = 0xFF26A69A; // Default teal
    const char* color_hex = ui_gcode_viewer_get_filament_color(gcode_viewer_);
    if (color_hex && color_hex[0] == '#' && strlen(color_hex) >= 7) {
        uint32_t rgb = static_cast<uint32_t>(strtol(color_hex + 1, nullptr, 16));
        // Convert RGB to ARGB8888
        color = 0xFF000000 | rgb;
    }

    spdlog::debug("[{}] Starting async thumbnail render for {} objects", get_name(),
                  parsed->objects.size());

    thumbnail_renderer_ = std::make_unique<helix::gcode::GCodeObjectThumbnailRenderer>();
    thumbnail_renderer_->render_async(
        parsed, kThumbnailSize, kThumbnailSize, color,
        [this](std::unique_ptr<helix::gcode::ObjectThumbnailSet> result) {
            if (!result || !is_visible()) {
                return;
            }

            spdlog::debug("[{}] Thumbnails ready: {} objects", get_name(),
                          result->thumbnails.size());

            // Clear list first to destroy lv_image widgets referencing old draw buffers,
            // then free the old buffers before creating new ones
            if (objects_list_) {
                lv_obj_clean(objects_list_);
            }
            for (auto& [name, buf] : object_thumbnails_) {
                if (buf) {
                    lv_draw_buf_destroy(buf);
                }
            }
            object_thumbnails_.clear();

            // Convert raw pixel buffers to LVGL draw buffers
            for (auto& thumb : result->thumbnails) {
                if (!thumb.is_valid())
                    continue;

                auto* buf = lv_draw_buf_create(thumb.width, thumb.height, LV_COLOR_FORMAT_ARGB8888,
                                               LV_STRIDE_AUTO);
                if (!buf)
                    continue;

                // Copy raw pixels into LVGL draw buffer
                const int lvgl_stride = buf->header.stride;
                for (int y = 0; y < thumb.height; ++y) {
                    memcpy(buf->data + y * lvgl_stride, thumb.pixels.get() + y * thumb.stride,
                           static_cast<size_t>(thumb.width) * 4);
                }
                lv_draw_buf_invalidate_cache(buf, nullptr);

                object_thumbnails_[thumb.object_name] = buf;
            }

            thumbnails_available_ = true;

            // Re-populate list to show thumbnails
            populate_list();
        });
}

void ExcludeObjectsListOverlay::apply_thumbnails() {
    // Called during populate_list when thumbnails are available
    // Thumbnails are applied inline in create_object_row
}

void ExcludeObjectsListOverlay::cleanup_thumbnails() {
    // Cancel any in-progress render
    if (thumbnail_renderer_) {
        thumbnail_renderer_->cancel();
        thumbnail_renderer_.reset();
    }

    // Free all LVGL draw buffers
    for (auto& [name, buf] : object_thumbnails_) {
        if (buf) {
            lv_draw_buf_destroy(buf);
        }
    }
    object_thumbnails_.clear();
    thumbnails_available_ = false;
}

// ============================================================================
// LIST POPULATION
// ============================================================================

void ExcludeObjectsListOverlay::populate_list() {
    if (!objects_list_ || !printer_state_) {
        return;
    }

    // Clear existing rows
    lv_obj_clean(objects_list_);

    const auto& defined = printer_state_->get_defined_objects();
    const auto& excluded = printer_state_->get_excluded_objects();
    const auto& current = printer_state_->get_current_object();

    spdlog::debug("[{}] Populating list: {} defined, {} excluded, current='{}'", get_name(),
                  defined.size(), excluded.size(), current);

    for (const auto& name : defined) {
        bool is_excluded = excluded.count(name) > 0;
        bool is_current = (name == current);
        create_object_row(objects_list_, name, is_excluded, is_current);
    }
}

lv_obj_t* ExcludeObjectsListOverlay::create_object_row(lv_obj_t* parent, const std::string& name,
                                                       bool is_excluded, bool is_current) {
    // Row container
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, theme_manager_get_spacing("space_sm"), 0);
    lv_obj_set_style_pad_gap(row, theme_manager_get_spacing("space_sm"), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_color(row, theme_manager_get_color("card_bg"), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Thumbnail image (if available) — no background container, transparent blend
    auto thumb_it = object_thumbnails_.find(name);
    if (thumb_it != object_thumbnails_.end() && thumb_it->second) {
        lv_obj_t* img = lv_image_create(row);
        lv_image_set_src(img, thumb_it->second);
        lv_obj_set_size(img, kThumbnailSize, kThumbnailSize);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    // Status indicator dot (12x12 circle)
    lv_obj_t* dot = lv_obj_create(row);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, 6, 0); // circle
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (is_excluded) {
        lv_obj_set_style_bg_color(dot, theme_manager_get_color("danger"), 0);
    } else if (is_current) {
        lv_obj_set_style_bg_color(dot, theme_manager_get_color("success"), 0);
    } else {
        lv_obj_set_style_bg_color(dot, theme_manager_get_color("success"), 0);
    }
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

    // Object name label
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, name.c_str());
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_text_font(label, theme_manager_get_font("font_body"), 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Status text (right side)
    lv_obj_t* status_label = lv_label_create(row);
    lv_obj_set_style_text_font(status_label, theme_manager_get_font("font_small"), 0);
    lv_obj_set_style_text_color(status_label, theme_manager_get_color("text_muted"), 0);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (is_excluded) {
        lv_label_set_text(status_label, "Excluded");
        lv_obj_set_style_text_color(label, theme_manager_get_color("text_muted"), 0);
        lv_obj_set_style_opa(row, 150, 0); // Reduced opacity for excluded
    } else if (is_current) {
        lv_label_set_text(status_label, "Printing");
        lv_obj_set_style_text_color(status_label, theme_manager_get_color("success"), 0);
    } else {
        lv_label_set_text(status_label, "");
    }

    // Click handler for non-excluded objects
    if (!is_excluded && manager_) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Allocate name string for callback
        // TODO: lv_obj user_data is safe here ONLY because row is created via
        // lv_obj_create() (not XML). If row is ever changed to lv_xml_create(),
        // user_data may already be claimed by the XML widget — move to event
        // callback user_data or a C++ side container instead. See L069.
        char* name_copy = static_cast<char*>(lv_malloc(name.size() + 1));
        if (name_copy) {
            memcpy(name_copy, name.c_str(), name.size() + 1);
            lv_obj_set_user_data(row, name_copy);

            // Click handler - uses singleton accessor to avoid capturing 'this'
            lv_obj_add_event_cb(
                row,
                [](lv_event_t* e) {
                    lv_obj_t* target = lv_event_get_target_obj(e);
                    char* obj_name = static_cast<char*>(lv_obj_get_user_data(target));
                    if (obj_name) {
                        auto& overlay = get_exclude_objects_list_overlay();
                        if (overlay.manager_) {
                            spdlog::info("[Exclude Objects List] Row clicked: '{}'", obj_name);
                            overlay.manager_->request_exclude(std::string(obj_name));
                        }
                    }
                },
                LV_EVENT_CLICKED, nullptr);

            // Cleanup handler to free allocated name on widget deletion
            lv_obj_add_event_cb(
                row,
                [](lv_event_t* e) {
                    lv_obj_t* obj = lv_event_get_target_obj(e);
                    char* data = static_cast<char*>(lv_obj_get_user_data(obj));
                    if (data) {
                        lv_free(data);
                        lv_obj_set_user_data(obj, nullptr);
                    }
                },
                LV_EVENT_DELETE, nullptr);
        }

        // Press feedback style
        lv_obj_set_style_bg_color(row, theme_manager_get_color("primary"), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, 40, LV_STATE_PRESSED);
    }

    return row;
}

} // namespace helix::ui
