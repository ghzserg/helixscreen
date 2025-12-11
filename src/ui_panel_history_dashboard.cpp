// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_history_dashboard.h"

#include "ui_nav.h"
#include "ui_panel_common.h"
#include "ui_panel_history_list.h"
#include "ui_theme.h"
#include "ui_toast.h"

#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

// Global instance (singleton pattern)
static std::unique_ptr<HistoryDashboardPanel> g_history_dashboard_panel;

HistoryDashboardPanel& get_global_history_dashboard_panel() {
    if (!g_history_dashboard_panel) {
        spdlog::error("get_global_history_dashboard_panel() called before initialization!");
        throw std::runtime_error("HistoryDashboardPanel not initialized");
    }
    return *g_history_dashboard_panel;
}

void init_global_history_dashboard_panel(PrinterState& printer_state, MoonrakerAPI* api) {
    g_history_dashboard_panel = std::make_unique<HistoryDashboardPanel>(printer_state, api);

    // Register XML event callbacks (must be done before XML is created)
    lv_xml_register_event_cb(nullptr, "history_filter_day_clicked",
                             HistoryDashboardPanel::on_filter_day_clicked);
    lv_xml_register_event_cb(nullptr, "history_filter_week_clicked",
                             HistoryDashboardPanel::on_filter_week_clicked);
    lv_xml_register_event_cb(nullptr, "history_filter_month_clicked",
                             HistoryDashboardPanel::on_filter_month_clicked);
    lv_xml_register_event_cb(nullptr, "history_filter_year_clicked",
                             HistoryDashboardPanel::on_filter_year_clicked);
    lv_xml_register_event_cb(nullptr, "history_filter_all_clicked",
                             HistoryDashboardPanel::on_filter_all_clicked);
    lv_xml_register_event_cb(nullptr, "history_view_full_clicked",
                             HistoryDashboardPanel::on_view_history_clicked);

    spdlog::debug("[History Dashboard] Event callbacks registered");
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

HistoryDashboardPanel::HistoryDashboardPanel(PrinterState& printer_state, MoonrakerAPI* api)
    : PanelBase(printer_state, api) {
    spdlog::trace("[{}] Constructor", get_name());
}

// ============================================================================
// PANELBASE IMPLEMENTATION
// ============================================================================

void HistoryDashboardPanel::init_subjects() {
    // Initialize subject for empty state visibility binding
    // 0 = no history (show empty state), 1 = has history (show stats grid)
    lv_subject_init_int(&history_has_jobs_subject_, 0);
    lv_xml_register_subject(nullptr, "history_has_jobs", &history_has_jobs_subject_);

    // Initialize subject for filter button state binding
    // Values: 0=Day, 1=Week, 2=Month, 3=Year, 4=All (matches HistoryTimeFilter enum)
    lv_subject_init_int(&history_filter_subject_, 4); // Default to ALL_TIME
    lv_xml_register_subject(nullptr, "history_filter", &history_filter_subject_);

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

void HistoryDashboardPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);

    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }

    // Find widget references - Filter buttons
    filter_day_ = lv_obj_find_by_name(panel_, "filter_day");
    filter_week_ = lv_obj_find_by_name(panel_, "filter_week");
    filter_month_ = lv_obj_find_by_name(panel_, "filter_month");
    filter_year_ = lv_obj_find_by_name(panel_, "filter_year");
    filter_all_ = lv_obj_find_by_name(panel_, "filter_all");

    // Stat labels (2x2 grid)
    stat_total_prints_ = lv_obj_find_by_name(panel_, "stat_total_prints");
    stat_print_time_ = lv_obj_find_by_name(panel_, "stat_print_time");
    stat_filament_ = lv_obj_find_by_name(panel_, "stat_filament");
    stat_success_rate_ = lv_obj_find_by_name(panel_, "stat_success_rate");

    // Containers
    stats_grid_ = lv_obj_find_by_name(panel_, "stats_grid");
    charts_section_ = lv_obj_find_by_name(panel_, "charts_section");
    empty_state_ = lv_obj_find_by_name(panel_, "empty_state");
    btn_view_history_ = lv_obj_find_by_name(panel_, "btn_view_history");

    // Chart containers
    trend_chart_container_ = lv_obj_find_by_name(panel_, "trend_chart_container");
    trend_period_label_ = lv_obj_find_by_name(panel_, "trend_period");
    filament_chart_container_ = lv_obj_find_by_name(panel_, "filament_chart_container");

    // Log found widgets
    spdlog::debug("[{}] Widget refs - filters: {}/{}/{}/{}/{}, stats: {}/{}/{}/{}", get_name(),
                  filter_day_ != nullptr, filter_week_ != nullptr, filter_month_ != nullptr,
                  filter_year_ != nullptr, filter_all_ != nullptr, stat_total_prints_ != nullptr,
                  stat_print_time_ != nullptr, stat_filament_ != nullptr,
                  stat_success_rate_ != nullptr);
    spdlog::debug("[{}] Chart containers: trend={}, filament={}", get_name(),
                  trend_chart_container_ != nullptr, filament_chart_container_ != nullptr);

    // Wire up the back button in header_bar to navigate back
    ui_panel_setup_back_button(panel_);

    // Create charts inside their containers
    create_trend_chart();
    create_filament_chart();

    // Register connection state observer to auto-refresh when connected
    // This handles the case where the panel is opened before connection is established
    lv_subject_t* conn_subject = printer_state_.get_printer_connection_state_subject();
    connection_observer_ = lv_subject_add_observer(
        conn_subject,
        [](lv_observer_t* observer, lv_subject_t* subject) {
            auto* self = static_cast<HistoryDashboardPanel*>(lv_observer_get_user_data(observer));
            int32_t state = lv_subject_get_int(subject);

            // state 2 = CONNECTED
            if (state == 2 && self->is_active_) {
                spdlog::debug("[{}] Connection established - refreshing data", self->get_name());
                self->refresh_data();
            }
        },
        this);

    spdlog::info("[{}] Setup complete", get_name());
}

void HistoryDashboardPanel::on_activate() {
    is_active_ = true;
    spdlog::debug("[{}] Activated - refreshing data with filter {}", get_name(),
                  static_cast<int>(current_filter_));
    refresh_data();
}

void HistoryDashboardPanel::on_deactivate() {
    is_active_ = false;
    spdlog::debug("[{}] Deactivated", get_name());
}

// ============================================================================
// PUBLIC API
// ============================================================================

void HistoryDashboardPanel::set_time_filter(HistoryTimeFilter filter) {
    if (current_filter_ == filter) {
        return;
    }

    current_filter_ = filter;

    // Update the subject to trigger reactive binding updates on buttons
    // Values: 0=Day, 1=Week, 2=Month, 3=Year, 4=All
    lv_subject_set_int(&history_filter_subject_, static_cast<int>(filter));

    refresh_data();
}

// ============================================================================
// DATA FETCHING
// ============================================================================

void HistoryDashboardPanel::refresh_data() {
    if (!api_) {
        spdlog::warn("[{}] No API available", get_name());
        return;
    }

    // Check if WebSocket is actually connected before attempting to send requests
    // This prevents the race condition where the panel is opened before connection is established
    ConnectionState state = api_->get_client().get_connection_state();
    if (state != ConnectionState::CONNECTED) {
        spdlog::debug("[{}] Cannot fetch history: not connected (state={})", get_name(),
                      static_cast<int>(state));
        return;
    }

    // Calculate time range based on filter
    double since = 0.0;
    double now = static_cast<double>(std::time(nullptr));

    switch (current_filter_) {
    case HistoryTimeFilter::DAY:
        since = now - (24 * 60 * 60);
        break;
    case HistoryTimeFilter::WEEK:
        since = now - (7 * 24 * 60 * 60);
        break;
    case HistoryTimeFilter::MONTH:
        since = now - (30 * 24 * 60 * 60);
        break;
    case HistoryTimeFilter::YEAR:
        since = now - (365 * 24 * 60 * 60);
        break;
    case HistoryTimeFilter::ALL_TIME:
    default:
        since = 0.0;
        break;
    }

    spdlog::debug("[{}] Fetching history since {} (filter={})", get_name(), since,
                  static_cast<int>(current_filter_));

    // Fetch with reasonable limit - dashboard just needs aggregate stats
    // Keep at 100 to ensure small response size (~160KB max)
    api_->get_history_list(
        100, // limit
        0,   // start
        since, 0.0,
        [this](const std::vector<PrintHistoryJob>& jobs, uint64_t total_count) {
            spdlog::info("[{}] Received {} jobs (total: {})", get_name(), jobs.size(), total_count);
            cached_jobs_ = jobs;
            update_statistics(jobs);
        },
        [this](const MoonrakerError& error) {
            spdlog::error("[{}] Failed to fetch history: {}", get_name(), error.message);
            ui_toast_show(ToastSeverity::ERROR, "Failed to load print history", 3000);
        });
}

void HistoryDashboardPanel::update_statistics(const std::vector<PrintHistoryJob>& jobs) {
    // Update subject to drive XML bindings (0=no jobs, 1=has jobs)
    // XML bindings will automatically show/hide stats, charts, and empty state
    lv_subject_set_int(&history_has_jobs_subject_, jobs.empty() ? 0 : 1);

    if (jobs.empty()) {
        // Clear stats labels (bindings hide the containers, but labels keep old values)
        if (stat_total_prints_)
            lv_label_set_text(stat_total_prints_, "0");
        if (stat_print_time_)
            lv_label_set_text(stat_print_time_, "0h");
        if (stat_filament_)
            lv_label_set_text(stat_filament_, "0m");
        if (stat_success_rate_)
            lv_label_set_text(stat_success_rate_, "0%");
        return;
    }

    // Calculate statistics
    uint64_t total_prints = jobs.size();
    double total_time = 0.0;
    double total_filament = 0.0;
    uint64_t completed_count = 0;

    for (const auto& job : jobs) {
        total_time += job.print_duration;
        total_filament += job.filament_used;

        if (job.status == PrintJobStatus::COMPLETED) {
            completed_count++;
        }
    }

    // Calculate success rate
    double success_rate = 0.0;
    if (total_prints > 0) {
        success_rate =
            (static_cast<double>(completed_count) / static_cast<double>(total_prints)) * 100.0;
    }

    // Update stat labels
    if (stat_total_prints_) {
        lv_label_set_text_fmt(stat_total_prints_, "%llu",
                              static_cast<unsigned long long>(total_prints));
    }

    if (stat_print_time_) {
        std::string time_str = format_duration(total_time);
        lv_label_set_text(stat_print_time_, time_str.c_str());
    }

    if (stat_filament_) {
        std::string filament_str = format_filament(total_filament);
        lv_label_set_text(stat_filament_, filament_str.c_str());
    }

    if (stat_success_rate_) {
        lv_label_set_text_fmt(stat_success_rate_, "%.0f%%", success_rate);
    }

    // Update charts
    update_trend_chart(jobs);
    update_filament_chart(jobs);

    spdlog::debug("[{}] Stats updated: {} prints, {} time, {} filament, {:.0f}% success",
                  get_name(), total_prints, format_duration(total_time),
                  format_filament(total_filament), success_rate);
}

// ============================================================================
// FORMATTING HELPERS
// ============================================================================

std::string HistoryDashboardPanel::format_duration(double seconds) {
    if (seconds < 60) {
        return std::to_string(static_cast<int>(seconds)) + "s";
    }

    int total_minutes = static_cast<int>(seconds / 60);
    int hours = total_minutes / 60;
    int minutes = total_minutes % 60;

    if (hours == 0) {
        return std::to_string(minutes) + "m";
    }

    if (minutes == 0) {
        return std::to_string(hours) + "h";
    }

    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string HistoryDashboardPanel::format_filament(double mm) {
    if (mm < 1000) {
        return std::to_string(static_cast<int>(mm)) + "mm";
    }

    double meters = mm / 1000.0;
    if (meters < 1000) {
        // Show 1 decimal for meters
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fm", meters);
        return buf;
    }

    // Kilometers for really large values
    double km = meters / 1000.0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1fkm", km);
    return buf;
}

// ============================================================================
// CHART HELPERS
// ============================================================================

void HistoryDashboardPanel::create_trend_chart() {
    if (!trend_chart_container_) {
        spdlog::warn("[{}] Trend chart container not found", get_name());
        return;
    }

    // Create line chart for prints trend
    trend_chart_ = lv_chart_create(trend_chart_container_);
    if (!trend_chart_) {
        spdlog::error("[{}] Failed to create trend chart", get_name());
        return;
    }

    // Configure chart - explicit height since container is height=content
    // Width fills parent, height is fixed since charts need explicit sizing
    lv_obj_set_size(trend_chart_, LV_PCT(100), 50);

    // Use line chart type
    lv_chart_set_type(trend_chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(trend_chart_, static_cast<uint32_t>(get_trend_period_count()));

    // Styling for a clean sparkline look
    lv_obj_set_style_bg_opa(trend_chart_, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(trend_chart_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(trend_chart_, 4, LV_PART_MAIN);

    // Hide division lines for sparkline effect
    lv_chart_set_div_line_count(trend_chart_, 0, 0);

    // Series line style - use secondary color (gold) for visibility
    // Must resolve XML constant first, then parse the hex string
    const char* secondary_str = lv_xml_get_const(nullptr, "secondary_color");
    lv_color_t line_color = secondary_str ? ui_theme_parse_color(secondary_str)
                                          : lv_color_hex(0xD4A84B); // Fallback gold
    lv_obj_set_style_line_width(trend_chart_, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(trend_chart_, line_color, LV_PART_ITEMS);

    // Hide point indicators for cleaner sparkline
    lv_obj_set_style_width(trend_chart_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(trend_chart_, 0, LV_PART_INDICATOR);

    // Add data series with gold color
    trend_series_ = lv_chart_add_series(trend_chart_, line_color, LV_CHART_AXIS_PRIMARY_Y);
    if (!trend_series_) {
        spdlog::error("[{}] Failed to create trend series", get_name());
        return;
    }

    // Initialize with zero data
    for (int i = 0; i < get_trend_period_count(); i++) {
        lv_chart_set_next_value(trend_chart_, trend_series_, 0);
    }

    spdlog::debug("[{}] Trend chart created with {} points", get_name(), get_trend_period_count());
}

void HistoryDashboardPanel::create_filament_chart() {
    if (!filament_chart_container_) {
        spdlog::warn("[{}] Filament chart container not found", get_name());
        return;
    }

    // Container is a flex column - rows will be added dynamically in update_filament_chart()
    spdlog::debug("[{}] Filament chart container ready for labeled bars", get_name());
}

int HistoryDashboardPanel::get_trend_period_count() const {
    // Number of data points for trend based on time filter
    switch (current_filter_) {
    case HistoryTimeFilter::DAY:
        return 24; // Hourly for day view
    case HistoryTimeFilter::WEEK:
        return 7; // Daily for week view
    case HistoryTimeFilter::MONTH:
        return 30; // Daily for month view
    case HistoryTimeFilter::YEAR:
        return 12; // Monthly for year view
    case HistoryTimeFilter::ALL_TIME:
    default:
        return 7; // Default to 7-day view
    }
}

double HistoryDashboardPanel::get_trend_period_seconds() const {
    // Seconds per time bucket based on current filter
    switch (current_filter_) {
    case HistoryTimeFilter::DAY:
        return 60 * 60; // 1 hour
    case HistoryTimeFilter::WEEK:
        return 24 * 60 * 60; // 1 day
    case HistoryTimeFilter::MONTH:
        return 24 * 60 * 60; // 1 day
    case HistoryTimeFilter::YEAR:
        return 30 * 24 * 60 * 60; // ~1 month
    case HistoryTimeFilter::ALL_TIME:
    default:
        return 24 * 60 * 60; // 1 day
    }
}

void HistoryDashboardPanel::update_trend_chart(const std::vector<PrintHistoryJob>& jobs) {
    if (!trend_chart_ || !trend_series_) {
        return;
    }

    int period_count = get_trend_period_count();
    double period_seconds = get_trend_period_seconds();
    double now = static_cast<double>(std::time(nullptr));

    // Update period label text
    if (trend_period_label_) {
        const char* period_text = "Last 7 days";
        switch (current_filter_) {
        case HistoryTimeFilter::DAY:
            period_text = "Last 24 hours";
            break;
        case HistoryTimeFilter::WEEK:
            period_text = "Last 7 days";
            break;
        case HistoryTimeFilter::MONTH:
            period_text = "Last 30 days";
            break;
        case HistoryTimeFilter::YEAR:
            period_text = "Last 12 months";
            break;
        case HistoryTimeFilter::ALL_TIME:
            period_text = "All time";
            break;
        }
        lv_label_set_text(trend_period_label_, period_text);
    }

    // Count prints per period bucket
    std::vector<int> counts(static_cast<size_t>(period_count), 0);

    for (const auto& job : jobs) {
        // Calculate which period bucket this job falls into
        double age = now - job.end_time;
        if (age < 0)
            age = 0;

        int bucket = static_cast<int>(age / period_seconds);
        // Bucket 0 is most recent, bucket (period_count-1) is oldest
        // We want to display oldest on left, newest on right
        int display_bucket = period_count - 1 - bucket;
        if (display_bucket >= 0 && display_bucket < period_count) {
            counts[static_cast<size_t>(display_bucket)]++;
        }
    }

    // Find max for Y-axis scaling
    int max_count = 1;
    for (int count : counts) {
        if (count > max_count)
            max_count = count;
    }

    // Update chart point count if it changed
    if (lv_chart_get_point_count(trend_chart_) != static_cast<uint32_t>(period_count)) {
        lv_chart_set_point_count(trend_chart_, static_cast<uint32_t>(period_count));
    }

    // Set Y-axis range
    lv_chart_set_axis_range(trend_chart_, LV_CHART_AXIS_PRIMARY_Y, 0, max_count);

    // Update series data - use lv_chart_set_value_by_id for precise control
    for (int i = 0; i < period_count; i++) {
        lv_chart_set_value_by_id(trend_chart_, trend_series_, static_cast<uint32_t>(i),
                                 counts[static_cast<size_t>(i)]);
    }

    lv_chart_refresh(trend_chart_);

    spdlog::debug("[{}] Trend chart updated: {} periods, max={}", get_name(), period_count,
                  max_count);
}

void HistoryDashboardPanel::update_filament_chart(const std::vector<PrintHistoryJob>& jobs) {
    if (!filament_chart_container_) {
        return;
    }

    // Clear existing bar rows
    for (auto* row : filament_bar_rows_) {
        lv_obj_delete(row);
    }
    filament_bar_rows_.clear();

    // Aggregate filament by type
    std::map<std::string, double> filament_by_type;

    for (const auto& job : jobs) {
        std::string type = job.filament_type.empty() ? "Unknown" : job.filament_type;
        filament_by_type[type] += job.filament_used;
    }

    if (filament_by_type.empty()) {
        return;
    }

    // Sort by usage (highest first) and take top 4 (limited space in side panel)
    std::vector<std::pair<std::string, double>> sorted_types(filament_by_type.begin(),
                                                             filament_by_type.end());
    std::sort(sorted_types.begin(), sorted_types.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    if (sorted_types.size() > 4) {
        sorted_types.resize(4);
    }

    // Find max for proportional bar widths
    double max_filament = 1.0;
    for (const auto& [type, amount] : sorted_types) {
        if (amount > max_filament)
            max_filament = amount;
    }

    // Generate complementary palette from theme's primary color
    // This creates visually harmonious colors that fit the theme
    lv_color_t primary_color = lv_color_hex(0xB83232); // Fallback red
    const char* primary_str = lv_xml_get_const(nullptr, "primary_color");
    if (primary_str) {
        primary_color = ui_theme_parse_color(primary_str);
    }

    // Convert primary to HSV to generate palette
    lv_color_hsv_t primary_hsv = lv_color_to_hsv(primary_color);

    // Generate colors by rotating hue - creates triadic/complementary harmony
    // Each filament type gets a consistent hue offset based on name hash
    auto get_filament_color = [primary_hsv](const std::string& type) -> lv_color_t {
        // Hash the type name for consistent color assignment
        uint32_t hash = 0;
        for (char c : type) {
            // Use uppercase for consistency
            char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            hash = hash * 31 + static_cast<uint32_t>(uc);
        }

        // Rotate hue by hash-based offset (evenly distributed around color wheel)
        // Use golden ratio angle (137.5°) for good distribution
        uint16_t hue_offset = static_cast<uint16_t>((hash * 137) % 360);
        uint16_t new_hue = (primary_hsv.h + hue_offset) % 360;

        // Keep saturation and value similar to primary for visual harmony
        // Slightly increase saturation for better visibility on dark backgrounds
        uint8_t sat = std::min(static_cast<uint8_t>(primary_hsv.s + 10), static_cast<uint8_t>(100));
        uint8_t val = std::max(primary_hsv.v, static_cast<uint8_t>(70)); // Ensure brightness

        return lv_color_hsv_to_rgb(new_hue, sat, val);
    };

    // Get theme colors for text (use primary for labels/amounts to match stats)
    // Must use lv_xml_get_const() to resolve theme-aware constants
    lv_color_t text_primary = lv_color_hex(0xE6E8F0); // Fallback (text_primary_dark)
    const char* text_primary_str = lv_xml_get_const(nullptr, "text_primary");
    if (text_primary_str) {
        text_primary = ui_theme_parse_color(text_primary_str);
    }

    // Create labeled bar rows
    for (const auto& [type, amount] : sorted_types) {
        // Calculate bar width as percentage of max (leaving room for label and amount)
        int bar_pct = static_cast<int>((amount / max_filament) * 100);
        if (bar_pct < 5)
            bar_pct = 5; // Minimum visible width

        // Create row container
        lv_obj_t* row = lv_obj_create(filament_chart_container_);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_gap(row, 4, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        filament_bar_rows_.push_back(row);

        // Use readable font size (small but legible)
        const lv_font_t* font_small = lv_xml_get_font(nullptr, "montserrat_14");
        if (!font_small) {
            font_small = lv_xml_get_font(nullptr, "montserrat_12");
        }

        // Type label (fixed width for alignment)
        lv_obj_t* type_label = lv_label_create(row);
        lv_label_set_text(type_label, type.c_str());
        lv_obj_set_width(type_label, 50); // Fixed width for type names
        lv_obj_set_style_text_color(type_label, text_primary, 0);
        if (font_small) {
            lv_obj_set_style_text_font(type_label, font_small, 0);
        }

        // Get line height from font to match text size
        int32_t line_height = font_small ? lv_font_get_line_height(font_small) : 16;

        // Colored bar - width proportional to value (max 50% of available space)
        // Using percentage width ensures bars are proportional across all rows
        int bar_width_pct = (bar_pct * 50) / 100; // Scale to max 50% of row
        if (bar_width_pct < 3)
            bar_width_pct = 3; // Minimum visibility

        lv_obj_t* bar = lv_obj_create(row);
        lv_obj_set_size(bar, LV_PCT(bar_width_pct), line_height);
        lv_color_t bar_color = get_filament_color(type);
        lv_obj_set_style_bg_color(bar, bar_color, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

        // Spacer to push amount to right edge (fills remaining space)
        lv_obj_t* spacer = lv_obj_create(row);
        lv_obj_set_height(spacer, 1);
        lv_obj_set_flex_grow(spacer, 1);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_0, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_set_style_pad_all(spacer, 0, 0);
        lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

        // Amount label (fixed width, right-aligned text at row's right edge)
        lv_obj_t* amount_label = lv_label_create(row);
        std::string amount_str = format_filament(amount);
        lv_label_set_text(amount_label, amount_str.c_str());
        lv_obj_set_width(amount_label, 60);
        lv_obj_set_style_text_color(amount_label, text_primary, 0);
        lv_obj_set_style_text_align(amount_label, LV_TEXT_ALIGN_RIGHT, 0);
        if (font_small) {
            lv_obj_set_style_text_font(amount_label, font_small, 0);
        }

        spdlog::debug("[{}] Filament bar: {} = {} ({}%)", get_name(), type, amount_str, bar_pct);
    }

    spdlog::debug("[{}] Filament chart updated: {} types", get_name(), sorted_types.size());
}

// ============================================================================
// STATIC EVENT CALLBACKS
// ============================================================================

void HistoryDashboardPanel::on_filter_day_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[History Dashboard] Filter: Day clicked");
    get_global_history_dashboard_panel().set_time_filter(HistoryTimeFilter::DAY);
}

void HistoryDashboardPanel::on_filter_week_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[History Dashboard] Filter: Week clicked");
    get_global_history_dashboard_panel().set_time_filter(HistoryTimeFilter::WEEK);
}

void HistoryDashboardPanel::on_filter_month_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[History Dashboard] Filter: Month clicked");
    get_global_history_dashboard_panel().set_time_filter(HistoryTimeFilter::MONTH);
}

void HistoryDashboardPanel::on_filter_year_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[History Dashboard] Filter: Year clicked");
    get_global_history_dashboard_panel().set_time_filter(HistoryTimeFilter::YEAR);
}

void HistoryDashboardPanel::on_filter_all_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[History Dashboard] Filter: All clicked");
    get_global_history_dashboard_panel().set_time_filter(HistoryTimeFilter::ALL_TIME);
}

void HistoryDashboardPanel::on_view_history_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[History Dashboard] View Full History clicked");

    // Get the list panel instance
    auto& list_panel = get_global_history_list_panel();

    // Pass the cached jobs to avoid redundant API calls
    const auto& dashboard = get_global_history_dashboard_panel();
    list_panel.set_jobs(dashboard.get_cached_jobs());

    // Create the list panel widget
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* list_widget =
        static_cast<lv_obj_t*>(lv_xml_create(screen, "history_list_panel", NULL));

    if (!list_widget) {
        spdlog::error("[History Dashboard] Failed to create history list panel widget");
        ui_toast_show(ToastSeverity::ERROR, "Failed to open history list", 2000);
        return;
    }

    // Setup the panel with the widget
    list_panel.setup(list_widget, screen);

    // Push as overlay (slides in from right)
    ui_nav_push_overlay(list_widget);

    // Manually trigger activation since ui_nav_push_overlay doesn't know about PanelBase
    list_panel.on_activate();

    spdlog::debug("[History Dashboard] History list panel opened");
}
