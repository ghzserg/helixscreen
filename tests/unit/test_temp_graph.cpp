/*
 * Copyright (C) 2025 356C LLC
 * Author: Preston Brown <pbrown@brown-house.net>
 *
 * This file is part of HelixScreen.
 *
 * HelixScreen is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HelixScreen is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with HelixScreen. If not, see <https://www.gnu.org/licenses/>.
 */

#include "../../include/ui_temp_graph.h"
#include "../../include/ui_theme.h"
#include "lvgl/lvgl.h"

#include "../catch_amalgamated.hpp"

// Test fixture for temperature graph tests
class TempGraphTestFixture {
  public:
    TempGraphTestFixture() {
        // Initialize LVGL for testing
        lv_init();

        // Create a display for testing (headless)
        lv_display_t* disp = lv_display_create(800, 480);
        alignas(64) static lv_color_t buf1[800 * 10];
        lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

        // Create a screen object to use as parent
        screen = lv_obj_create(NULL);
    }

    ~TempGraphTestFixture() {
        // Cleanup is handled by LVGL
    }

    lv_obj_t* screen;
};

// ============================================================================
// Core API Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphTestFixture, "Create and destroy graph", "[temp_graph][core]") {
    SECTION("Create graph with valid parent") {
        ui_temp_graph_t* graph = ui_temp_graph_create(screen);

        REQUIRE(graph != nullptr);
        REQUIRE(ui_temp_graph_get_chart(graph) != nullptr);
        REQUIRE(graph->series_count == 0);
        REQUIRE(graph->next_series_id == 0);
        REQUIRE(graph->point_count == UI_TEMP_GRAPH_DEFAULT_POINTS);
        REQUIRE(graph->min_temp == UI_TEMP_GRAPH_DEFAULT_MIN_TEMP);
        REQUIRE(graph->max_temp == UI_TEMP_GRAPH_DEFAULT_MAX_TEMP);

        ui_temp_graph_destroy(graph);
    }

    SECTION("Create graph with NULL parent returns NULL") {
        ui_temp_graph_t* graph = ui_temp_graph_create(nullptr);
        REQUIRE(graph == nullptr);
    }

    SECTION("Destroy NULL graph is safe") {
        ui_temp_graph_destroy(nullptr);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Get chart from NULL graph returns NULL") {
        lv_obj_t* chart = ui_temp_graph_get_chart(nullptr);
        REQUIRE(chart == nullptr);
    }
}

// ============================================================================
// Series Management Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphTestFixture, "Add series", "[temp_graph][series]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Add single series returns valid ID") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        REQUIRE(id >= 0);
        REQUIRE(graph->series_count == 1);
        REQUIRE(graph->next_series_id == 1);
        REQUIRE(graph->series_meta[0].id == 0);
        REQUIRE(graph->series_meta[0].chart_series != nullptr);
        REQUIRE(graph->series_meta[0].visible == true);
        REQUIRE(graph->series_meta[0].show_target == false);
        REQUIRE(strcmp(graph->series_meta[0].name, "Nozzle") == 0);
    }

    SECTION("Add multiple series with unique IDs") {
        int id1 = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        int id2 = ui_temp_graph_add_series(graph, "Bed", lv_color_hex(0x2196F3));
        int id3 = ui_temp_graph_add_series(graph, "Chamber", lv_color_hex(0x4CAF50));

        REQUIRE(id1 >= 0);
        REQUIRE(id2 >= 0);
        REQUIRE(id3 >= 0);
        REQUIRE(id1 != id2);
        REQUIRE(id2 != id3);
        REQUIRE(id1 != id3);
        REQUIRE(graph->series_count == 3);
        REQUIRE(graph->next_series_id == 3);
    }

    SECTION("Add series with NULL name fails") {
        int id = ui_temp_graph_add_series(graph, nullptr, lv_color_hex(0xFF5722));
        REQUIRE(id == -1);
        REQUIRE(graph->series_count == 0);
    }

    SECTION("Add series to NULL graph fails") {
        int id = ui_temp_graph_add_series(nullptr, "Nozzle", lv_color_hex(0xFF5722));
        REQUIRE(id == -1);
    }

    SECTION("Add up to max series") {
        int ids[UI_TEMP_GRAPH_MAX_SERIES];

        for (int i = 0; i < UI_TEMP_GRAPH_MAX_SERIES; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Series%d", i);
            ids[i] = ui_temp_graph_add_series(graph, name, lv_color_hex(0xFF5722 + i));
            REQUIRE(ids[i] >= 0);
        }

        REQUIRE(graph->series_count == UI_TEMP_GRAPH_MAX_SERIES);

        // Verify all IDs are unique
        for (int i = 0; i < UI_TEMP_GRAPH_MAX_SERIES; i++) {
            for (int j = i + 1; j < UI_TEMP_GRAPH_MAX_SERIES; j++) {
                REQUIRE(ids[i] != ids[j]);
            }
        }
    }

    SECTION("Exceeding max series fails") {
        // Add max series
        for (int i = 0; i < UI_TEMP_GRAPH_MAX_SERIES; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Series%d", i);
            ui_temp_graph_add_series(graph, name, lv_color_hex(0xFF5722));
        }

        // Try to add one more
        int id = ui_temp_graph_add_series(graph, "Overflow", lv_color_hex(0xFF5722));
        REQUIRE(id == -1);
        REQUIRE(graph->series_count == UI_TEMP_GRAPH_MAX_SERIES);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Remove series", "[temp_graph][series]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Remove existing series") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        REQUIRE(id >= 0);
        REQUIRE(graph->series_count == 1);

        ui_temp_graph_remove_series(graph, id);
        REQUIRE(graph->series_count == 0);
    }

    SECTION("Remove series from middle") {
        int id1 = ui_temp_graph_add_series(graph, "Series1", lv_color_hex(0xFF5722));
        int id2 = ui_temp_graph_add_series(graph, "Series2", lv_color_hex(0x2196F3));
        int id3 = ui_temp_graph_add_series(graph, "Series3", lv_color_hex(0x4CAF50));

        REQUIRE(graph->series_count == 3);

        ui_temp_graph_remove_series(graph, id2);
        REQUIRE(graph->series_count == 2);

        // Verify we can still use remaining series
        ui_temp_graph_update_series(graph, id1, 100.0f);
        ui_temp_graph_update_series(graph, id3, 200.0f);
    }

    SECTION("Remove invalid series ID does nothing") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        REQUIRE(graph->series_count == 1);

        ui_temp_graph_remove_series(graph, 999);
        REQUIRE(graph->series_count == 1);
    }

    SECTION("Remove from NULL graph is safe") {
        ui_temp_graph_remove_series(nullptr, 0);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Remove already removed series is safe") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        ui_temp_graph_remove_series(graph, id);
        ui_temp_graph_remove_series(graph, id); // Remove again
        REQUIRE(graph->series_count == 0);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Show/hide series", "[temp_graph][series]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Hide visible series") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        REQUIRE(graph->series_meta[0].visible == true);

        ui_temp_graph_show_series(graph, id, false);
        REQUIRE(graph->series_meta[0].visible == false);
    }

    SECTION("Show hidden series") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        ui_temp_graph_show_series(graph, id, false);
        REQUIRE(graph->series_meta[0].visible == false);

        ui_temp_graph_show_series(graph, id, true);
        REQUIRE(graph->series_meta[0].visible == true);
    }

    SECTION("Show/hide invalid series ID does nothing") {
        ui_temp_graph_show_series(graph, 999, false);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Show/hide on NULL graph is safe") {
        ui_temp_graph_show_series(nullptr, 0, false);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

// ============================================================================
// Data Update Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphTestFixture, "Update series data (push mode)", "[temp_graph][data]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Update single series with single value") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_update_series(graph, id, 210.5f);
        // No crash = success
        REQUIRE(true);
    }

    SECTION("Update series multiple times") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        for (int i = 0; i < 10; i++) {
            ui_temp_graph_update_series(graph, id, 200.0f + i);
        }
        // No crash = success
        REQUIRE(true);
    }

    SECTION("Update invalid series ID is safe") {
        ui_temp_graph_update_series(graph, 999, 100.0f);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Update NULL graph is safe") {
        ui_temp_graph_update_series(nullptr, 0, 100.0f);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Update with boundary values") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_update_series(graph, id, 0.0f);
        ui_temp_graph_update_series(graph, id, 300.0f);
        ui_temp_graph_update_series(graph, id, -50.0f);
        ui_temp_graph_update_series(graph, id, 500.0f);
        // No crash = success
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Set series data (array mode)", "[temp_graph][data]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Set data with valid array") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        float temps[] = {20.0f, 50.0f, 100.0f, 150.0f, 200.0f, 210.5f};
        ui_temp_graph_set_series_data(graph, id, temps, 6);
        // No crash = success
        REQUIRE(true);
    }

    SECTION("Set data with array larger than point count") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        // Create array larger than default point count
        float* temps = new float[UI_TEMP_GRAPH_DEFAULT_POINTS + 100];
        for (int i = 0; i < UI_TEMP_GRAPH_DEFAULT_POINTS + 100; i++) {
            temps[i] = 20.0f + i * 0.5f;
        }

        ui_temp_graph_set_series_data(graph, id, temps, UI_TEMP_GRAPH_DEFAULT_POINTS + 100);
        // Should truncate to point_count
        delete[] temps;
        REQUIRE(true);
    }

    SECTION("Set data with NULL array fails gracefully") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        ui_temp_graph_set_series_data(graph, id, nullptr, 10);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Set data with zero count fails gracefully") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        float temps[] = {100.0f};
        ui_temp_graph_set_series_data(graph, id, temps, 0);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Set data with negative count fails gracefully") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        float temps[] = {100.0f};
        ui_temp_graph_set_series_data(graph, id, temps, -5);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Set data on NULL graph is safe") {
        float temps[] = {100.0f};
        ui_temp_graph_set_series_data(nullptr, 0, temps, 1);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Clear graph data", "[temp_graph][data]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Clear all series data") {
        int id1 = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        int id2 = ui_temp_graph_add_series(graph, "Bed", lv_color_hex(0x2196F3));

        // Add some data
        ui_temp_graph_update_series(graph, id1, 210.0f);
        ui_temp_graph_update_series(graph, id2, 60.0f);

        ui_temp_graph_clear(graph);

        // Series should still exist, just data cleared
        REQUIRE(graph->series_count == 2);
    }

    SECTION("Clear NULL graph is safe") {
        ui_temp_graph_clear(nullptr);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Clear empty graph is safe") {
        ui_temp_graph_clear(graph);
        REQUIRE(graph->series_count == 0);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Clear individual series data", "[temp_graph][data]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Clear single series") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        ui_temp_graph_update_series(graph, id, 210.0f);

        ui_temp_graph_clear_series(graph, id);

        // Series should still exist
        REQUIRE(graph->series_count == 1);
    }

    SECTION("Clear one series leaves others intact") {
        int id1 = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        int id2 = ui_temp_graph_add_series(graph, "Bed", lv_color_hex(0x2196F3));

        ui_temp_graph_update_series(graph, id1, 210.0f);
        ui_temp_graph_update_series(graph, id2, 60.0f);

        ui_temp_graph_clear_series(graph, id1);

        REQUIRE(graph->series_count == 2);
    }

    SECTION("Clear invalid series ID is safe") {
        ui_temp_graph_clear_series(graph, 999);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Clear on NULL graph is safe") {
        ui_temp_graph_clear_series(nullptr, 0);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

// ============================================================================
// Target Temperature Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphTestFixture, "Set series target temperature", "[temp_graph][target]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Set target temperature with visibility") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_target(graph, id, 210.0f, true);

        REQUIRE(graph->series_meta[0].target_temp == 210.0f);
        REQUIRE(graph->series_meta[0].show_target == true);
    }

    SECTION("Set target temperature without showing") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_target(graph, id, 210.0f, false);

        REQUIRE(graph->series_meta[0].target_temp == 210.0f);
        REQUIRE(graph->series_meta[0].show_target == false);
    }

    SECTION("Update target temperature") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_target(graph, id, 200.0f, true);
        REQUIRE(graph->series_meta[0].target_temp == 200.0f);

        ui_temp_graph_set_series_target(graph, id, 220.0f, true);
        REQUIRE(graph->series_meta[0].target_temp == 220.0f);
    }

    SECTION("Set target with boundary values") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_target(graph, id, 0.0f, true);
        REQUIRE(graph->series_meta[0].target_temp == 0.0f);

        ui_temp_graph_set_series_target(graph, id, 300.0f, true);
        REQUIRE(graph->series_meta[0].target_temp == 300.0f);
    }

    SECTION("Set target on invalid series ID is safe") {
        ui_temp_graph_set_series_target(graph, 999, 210.0f, true);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Set target on NULL graph is safe") {
        ui_temp_graph_set_series_target(nullptr, 0, 210.0f, true);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Show/hide target temperature", "[temp_graph][target]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Show target temperature") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        ui_temp_graph_set_series_target(graph, id, 210.0f, false);
        REQUIRE(graph->series_meta[0].show_target == false);

        ui_temp_graph_show_target(graph, id, true);
        REQUIRE(graph->series_meta[0].show_target == true);
    }

    SECTION("Hide target temperature") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        ui_temp_graph_set_series_target(graph, id, 210.0f, true);
        REQUIRE(graph->series_meta[0].show_target == true);

        ui_temp_graph_show_target(graph, id, false);
        REQUIRE(graph->series_meta[0].show_target == false);
    }

    SECTION("Show/hide on invalid series ID is safe") {
        ui_temp_graph_show_target(graph, 999, true);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Show/hide on NULL graph is safe") {
        ui_temp_graph_show_target(nullptr, 0, true);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphTestFixture, "Set temperature range", "[temp_graph][config]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Set valid temperature range") {
        ui_temp_graph_set_temp_range(graph, 0.0f, 250.0f);

        REQUIRE(graph->min_temp == 0.0f);
        REQUIRE(graph->max_temp == 250.0f);
    }

    SECTION("Set custom temperature range") {
        ui_temp_graph_set_temp_range(graph, -50.0f, 500.0f);

        REQUIRE(graph->min_temp == -50.0f);
        REQUIRE(graph->max_temp == 500.0f);
    }

    SECTION("Invalid range (min >= max) is rejected") {
        float original_min = graph->min_temp;
        float original_max = graph->max_temp;

        ui_temp_graph_set_temp_range(graph, 100.0f, 50.0f);

        // Should not change
        REQUIRE(graph->min_temp == original_min);
        REQUIRE(graph->max_temp == original_max);
    }

    SECTION("Invalid range (min == max) is rejected") {
        float original_min = graph->min_temp;
        float original_max = graph->max_temp;

        ui_temp_graph_set_temp_range(graph, 100.0f, 100.0f);

        // Should not change
        REQUIRE(graph->min_temp == original_min);
        REQUIRE(graph->max_temp == original_max);
    }

    SECTION("Set range on NULL graph is safe") {
        ui_temp_graph_set_temp_range(nullptr, 0.0f, 250.0f);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Set point count", "[temp_graph][config]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Set valid point count") {
        ui_temp_graph_set_point_count(graph, 600);
        REQUIRE(graph->point_count == 600);
    }

    SECTION("Set point count to 1") {
        ui_temp_graph_set_point_count(graph, 1);
        REQUIRE(graph->point_count == 1);
    }

    SECTION("Set point count to large value") {
        ui_temp_graph_set_point_count(graph, 10000);
        REQUIRE(graph->point_count == 10000);
    }

    SECTION("Invalid point count (zero) is rejected") {
        int original_count = graph->point_count;

        ui_temp_graph_set_point_count(graph, 0);

        // Should not change
        REQUIRE(graph->point_count == original_count);
    }

    SECTION("Invalid point count (negative) is rejected") {
        int original_count = graph->point_count;

        ui_temp_graph_set_point_count(graph, -100);

        // Should not change
        REQUIRE(graph->point_count == original_count);
    }

    SECTION("Set point count on NULL graph is safe") {
        ui_temp_graph_set_point_count(nullptr, 600);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Set series gradient", "[temp_graph][config]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Set custom gradient opacities") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_gradient(graph, id, LV_OPA_80, LV_OPA_20);

        REQUIRE(graph->series_meta[0].gradient_bottom_opa == LV_OPA_80);
        REQUIRE(graph->series_meta[0].gradient_top_opa == LV_OPA_20);
    }

    SECTION("Set gradient to full opacity") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_gradient(graph, id, LV_OPA_COVER, LV_OPA_COVER);

        REQUIRE(graph->series_meta[0].gradient_bottom_opa == LV_OPA_COVER);
        REQUIRE(graph->series_meta[0].gradient_top_opa == LV_OPA_COVER);
    }

    SECTION("Set gradient to transparent") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        ui_temp_graph_set_series_gradient(graph, id, LV_OPA_TRANSP, LV_OPA_TRANSP);

        REQUIRE(graph->series_meta[0].gradient_bottom_opa == LV_OPA_TRANSP);
        REQUIRE(graph->series_meta[0].gradient_top_opa == LV_OPA_TRANSP);
    }

    SECTION("Set gradient on invalid series ID is safe") {
        ui_temp_graph_set_series_gradient(graph, 999, LV_OPA_50, LV_OPA_10);
        // Should not crash
        REQUIRE(true);
    }

    SECTION("Set gradient on NULL graph is safe") {
        ui_temp_graph_set_series_gradient(nullptr, 0, LV_OPA_50, LV_OPA_10);
        // Should not crash
        REQUIRE(true);
    }

    ui_temp_graph_destroy(graph);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE_METHOD(TempGraphTestFixture, "Complete workflow scenarios", "[temp_graph][integration]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Typical heating profile") {
        // Add nozzle series
        int nozzle_id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        REQUIRE(nozzle_id >= 0);

        // Set target temperature
        ui_temp_graph_set_series_target(graph, nozzle_id, 210.0f, true);

        // Simulate heating from 20°C to 210°C
        for (int temp = 20; temp <= 210; temp += 10) {
            ui_temp_graph_update_series(graph, nozzle_id, (float)temp);
        }

        // Verify state
        REQUIRE(graph->series_count == 1);
        REQUIRE(graph->series_meta[0].target_temp == 210.0f);
        REQUIRE(graph->series_meta[0].show_target == true);
    }

    SECTION("Multi-heater monitoring") {
        // Add multiple heaters
        int nozzle_id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        int bed_id = ui_temp_graph_add_series(graph, "Bed", lv_color_hex(0x2196F3));
        int chamber_id = ui_temp_graph_add_series(graph, "Chamber", lv_color_hex(0x4CAF50));

        REQUIRE(nozzle_id >= 0);
        REQUIRE(bed_id >= 0);
        REQUIRE(chamber_id >= 0);

        // Set targets
        ui_temp_graph_set_series_target(graph, nozzle_id, 210.0f, true);
        ui_temp_graph_set_series_target(graph, bed_id, 60.0f, true);
        ui_temp_graph_set_series_target(graph, chamber_id, 40.0f, false);

        // Update temperatures
        ui_temp_graph_update_series(graph, nozzle_id, 205.3f);
        ui_temp_graph_update_series(graph, bed_id, 58.7f);
        ui_temp_graph_update_series(graph, chamber_id, 35.2f);

        REQUIRE(graph->series_count == 3);
    }

    SECTION("Series removal and re-addition") {
        int id1 = ui_temp_graph_add_series(graph, "Series1", lv_color_hex(0xFF5722));
        int id2 = ui_temp_graph_add_series(graph, "Series2", lv_color_hex(0x2196F3));

        // Remove first series
        ui_temp_graph_remove_series(graph, id1);
        REQUIRE(graph->series_count == 1);

        // Add new series (should reuse slot)
        int id3 = ui_temp_graph_add_series(graph, "Series3", lv_color_hex(0x4CAF50));
        REQUIRE(id3 >= 0);
        REQUIRE(graph->series_count == 2);

        // Verify second series still works
        ui_temp_graph_update_series(graph, id2, 100.0f);
        ui_temp_graph_update_series(graph, id3, 200.0f);
    }

    SECTION("Bulk data update") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));

        // Create historical temperature data
        const int count = 100;
        float temps[count];
        for (int i = 0; i < count; i++) {
            temps[i] = 20.0f + (190.0f / count) * i; // Heat from 20 to 210
        }

        // Set all at once
        ui_temp_graph_set_series_data(graph, id, temps, count);

        REQUIRE(graph->series_count == 1);
    }

    ui_temp_graph_destroy(graph);
}

TEST_CASE_METHOD(TempGraphTestFixture, "Stress tests", "[temp_graph][stress]") {
    ui_temp_graph_t* graph = ui_temp_graph_create(screen);
    REQUIRE(graph != nullptr);

    SECTION("Large data updates") {
        int id = ui_temp_graph_add_series(graph, "Nozzle", lv_color_hex(0xFF5722));
        REQUIRE(id >= 0);

        // Push many data points
        for (int i = 0; i < 1000; i++) {
            ui_temp_graph_update_series(graph, id, 20.0f + (i % 200));
        }

        // No crash = success
        REQUIRE(graph->series_count == 1);
    }

    SECTION("Rapid configuration changes") {
        int id = ui_temp_graph_add_series(graph, "Test", lv_color_hex(0xFF5722));
        REQUIRE(id >= 0);

        // Rapidly change configuration
        for (int i = 0; i < 100; i++) {
            ui_temp_graph_set_series_target(graph, id, 100.0f + i, true);
            ui_temp_graph_show_series(graph, id, i % 2 == 0);
            ui_temp_graph_set_series_gradient(graph, id, LV_OPA_50 + i % 50, LV_OPA_10);
            ui_temp_graph_update_series(graph, id, 50.0f + i);
        }

        REQUIRE(graph->series_count == 1);
    }

    ui_temp_graph_destroy(graph);
}
