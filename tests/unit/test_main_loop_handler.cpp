// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "main_loop_handler.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::application;

// ============================================================================
// MainLoopHandler tests
// ============================================================================

TEST_CASE("MainLoopHandler: screenshot timing", "[mainloop][application]") {
    MainLoopHandler handler;
    MainLoopHandler::Config config;
    config.screenshot_enabled = true;
    config.screenshot_delay_ms = 1000;

    SECTION("no screenshot before delay") {
        handler.init(config, 0);
        handler.on_frame(500);
        REQUIRE_FALSE(handler.should_take_screenshot());
    }

    SECTION("screenshot triggers at delay time") {
        handler.init(config, 0);
        handler.on_frame(1000);
        REQUIRE(handler.should_take_screenshot());
    }

    SECTION("screenshot triggers after delay time") {
        handler.init(config, 0);
        handler.on_frame(1500);
        REQUIRE(handler.should_take_screenshot());
    }

    SECTION("screenshot doesn't re-trigger after taken") {
        handler.init(config, 0);
        handler.on_frame(1000);
        REQUIRE(handler.should_take_screenshot());
        handler.mark_screenshot_taken();

        handler.on_frame(2000);
        REQUIRE_FALSE(handler.should_take_screenshot());
    }

    SECTION("screenshot disabled") {
        config.screenshot_enabled = false;
        handler.init(config, 0);
        handler.on_frame(5000);
        REQUIRE_FALSE(handler.should_take_screenshot());
    }
}

TEST_CASE("MainLoopHandler: auto-quit timeout", "[mainloop][application]") {
    MainLoopHandler handler;
    MainLoopHandler::Config config;
    config.timeout_sec = 5;

    SECTION("no quit before timeout") {
        handler.init(config, 0);
        handler.on_frame(4000);
        REQUIRE_FALSE(handler.should_quit());
    }

    SECTION("quit at timeout") {
        handler.init(config, 0);
        handler.on_frame(5000);
        REQUIRE(handler.should_quit());
    }

    SECTION("quit after timeout") {
        handler.init(config, 0);
        handler.on_frame(6000);
        REQUIRE(handler.should_quit());
    }

    SECTION("no timeout when disabled") {
        config.timeout_sec = 0;
        handler.init(config, 0);
        handler.on_frame(100000);
        REQUIRE_FALSE(handler.should_quit());
    }

    SECTION("timeout relative to start time") {
        handler.init(config, 1000); // Started at tick 1000
        handler.on_frame(5500);     // 4500ms elapsed, not yet 5000
        REQUIRE_FALSE(handler.should_quit());

        handler.on_frame(6000); // 5000ms elapsed
        REQUIRE(handler.should_quit());
    }
}

TEST_CASE("MainLoopHandler: benchmark mode", "[mainloop][application]") {
    MainLoopHandler handler;
    MainLoopHandler::Config config;
    config.benchmark_mode = true;
    config.benchmark_report_interval_ms = 1000;

    SECTION("tracks frame count") {
        handler.init(config, 0);
        handler.on_frame(100);
        handler.on_frame(200);
        handler.on_frame(300);
        REQUIRE(handler.benchmark_frame_count() == 3);
    }

    SECTION("reports FPS at interval") {
        handler.init(config, 0);
        // 9 frames under 1 second - not ready yet
        for (int i = 1; i <= 9; i++) {
            handler.on_frame(i * 100); // 100, 200, ..., 900
        }
        REQUIRE_FALSE(handler.benchmark_should_report());

        // 10th frame at exactly 1 second - report ready
        handler.on_frame(1000);
        REQUIRE(handler.benchmark_should_report());

        auto report = handler.benchmark_get_report();
        REQUIRE(report.fps == Catch::Approx(10.0).epsilon(0.1));
        REQUIRE(report.frame_count == 10);
    }

    SECTION("report resets counters") {
        handler.init(config, 0);
        for (int i = 1; i <= 10; i++) {
            handler.on_frame(i * 100);
        }
        handler.on_frame(1000);
        handler.benchmark_get_report(); // Consumes report

        REQUIRE_FALSE(handler.benchmark_should_report());
        REQUIRE(handler.benchmark_frame_count() == 0);
    }

    SECTION("final report calculates total runtime") {
        handler.init(config, 1000); // Started at 1000ms
        for (int i = 0; i < 100; i++) {
            handler.on_frame(1000 + i * 50); // 100 frames
        }
        handler.on_frame(6000); // Now at 6000ms

        auto final_report = handler.benchmark_get_final_report();
        REQUIRE(final_report.total_runtime_sec == Catch::Approx(5.0).epsilon(0.01));
    }

    SECTION("benchmark disabled doesn't track") {
        config.benchmark_mode = false;
        handler.init(config, 0);
        handler.on_frame(100);
        handler.on_frame(200);
        REQUIRE(handler.benchmark_frame_count() == 0);
        REQUIRE_FALSE(handler.benchmark_should_report());
    }
}

TEST_CASE("MainLoopHandler: elapsed time", "[mainloop][application]") {
    MainLoopHandler handler;
    MainLoopHandler::Config config;

    handler.init(config, 500);

    SECTION("elapsed_ms returns time since start") {
        handler.on_frame(1500);
        REQUIRE(handler.elapsed_ms() == 1000);
    }
}
