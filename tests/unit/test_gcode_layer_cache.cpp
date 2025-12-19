// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_layer_cache.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;
using Catch::Approx;

namespace {

// Helper to create test segments
std::vector<ToolpathSegment> make_test_segments(size_t count) {
    std::vector<ToolpathSegment> segs;
    segs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ToolpathSegment s;
        s.start = glm::vec3(i * 1.0f, 0, 0);
        s.end = glm::vec3(i * 1.0f + 1.0f, 0, 0);
        s.is_extrusion = true;
        segs.push_back(s);
    }
    return segs;
}

// Loader that creates segments on demand
auto test_loader(size_t segments_per_layer) {
    return
        [segments_per_layer](size_t layer_index) { return make_test_segments(segments_per_layer); };
}

// Loader that tracks which layers were loaded
auto tracking_loader(std::vector<size_t>& loaded_layers, size_t segments_per_layer) {
    return [&loaded_layers, segments_per_layer](size_t layer_index) {
        loaded_layers.push_back(layer_index);
        return make_test_segments(segments_per_layer);
    };
}

} // namespace

TEST_CASE("GCodeLayerCache basic operations", "[gcode][cache]") {
    // Small budget for testing: 10KB
    GCodeLayerCache cache(10 * 1024);

    SECTION("empty cache has no layers") {
        REQUIRE(cache.cached_layer_count() == 0);
        REQUIRE(cache.memory_usage_bytes() == 0);
        REQUIRE_FALSE(cache.is_cached(0));
    }

    SECTION("get_or_load caches and returns data") {
        auto result = cache.get_or_load(0, test_loader(10));

        REQUIRE(result.segments != nullptr);
        REQUIRE(result.was_hit == false); // First access is a miss
        REQUIRE(result.load_failed == false);
        REQUIRE(result.segments->size() == 10);
        REQUIRE(cache.is_cached(0));
        REQUIRE(cache.cached_layer_count() == 1);
    }

    SECTION("second access is a cache hit") {
        cache.get_or_load(0, test_loader(10));
        auto result = cache.get_or_load(0, test_loader(10));

        REQUIRE(result.was_hit == true);
        REQUIRE(result.segments != nullptr);
    }

    SECTION("hit rate tracking works") {
        cache.reset_stats();

        // 1 miss
        cache.get_or_load(0, test_loader(10));
        // 2 hits
        cache.get_or_load(0, test_loader(10));
        cache.get_or_load(0, test_loader(10));

        auto [hits, misses] = cache.hit_stats();
        REQUIRE(hits == 2);
        REQUIRE(misses == 1);
        REQUIRE(cache.hit_rate() == Catch::Approx(2.0f / 3.0f));
    }
}

TEST_CASE("GCodeLayerCache LRU eviction", "[gcode][cache]") {
    // Budget that fits ~2 layers of 50 segments each
    // 50 segments * 80 bytes = 4KB per layer + overhead
    // Budget of 10KB should fit ~2 layers
    GCodeLayerCache cache(10 * 1024);

    std::vector<size_t> loaded;

    SECTION("evicts oldest layer when over budget") {
        // Load layers 0, 1, 2 - should evict 0 to make room for 2
        cache.get_or_load(0, tracking_loader(loaded, 50));
        cache.get_or_load(1, tracking_loader(loaded, 50));
        cache.get_or_load(2, tracking_loader(loaded, 50));

        // Layer 0 should have been evicted
        REQUIRE_FALSE(cache.is_cached(0));
        // Layers 1 and 2 should still be cached
        REQUIRE(cache.is_cached(1));
        REQUIRE(cache.is_cached(2));
    }

    SECTION("touching a layer prevents eviction") {
        cache.get_or_load(0, tracking_loader(loaded, 50));
        cache.get_or_load(1, tracking_loader(loaded, 50));

        // Touch layer 0 (makes it most recent)
        cache.get_or_load(0, tracking_loader(loaded, 50));

        // Now add layer 2 - should evict 1, not 0
        cache.get_or_load(2, tracking_loader(loaded, 50));

        REQUIRE(cache.is_cached(0));       // Was touched, kept
        REQUIRE_FALSE(cache.is_cached(1)); // Oldest, evicted
        REQUIRE(cache.is_cached(2));       // Newest
    }

    SECTION("explicit eviction works") {
        cache.get_or_load(0, tracking_loader(loaded, 50));
        REQUIRE(cache.is_cached(0));

        bool evicted = cache.evict(0);
        REQUIRE(evicted);
        REQUIRE_FALSE(cache.is_cached(0));

        // Evicting non-existent layer returns false
        REQUIRE_FALSE(cache.evict(999));
    }
}

TEST_CASE("GCodeLayerCache memory tracking", "[gcode][cache]") {
    GCodeLayerCache cache(100 * 1024); // 100KB

    SECTION("memory usage increases with cached layers") {
        size_t initial = cache.memory_usage_bytes();
        REQUIRE(initial == 0);

        cache.get_or_load(0, test_loader(100));
        size_t after_one = cache.memory_usage_bytes();
        REQUIRE(after_one > initial);

        cache.get_or_load(1, test_loader(100));
        size_t after_two = cache.memory_usage_bytes();
        REQUIRE(after_two > after_one);
    }

    SECTION("clear resets memory usage") {
        cache.get_or_load(0, test_loader(100));
        cache.get_or_load(1, test_loader(100));
        REQUIRE(cache.memory_usage_bytes() > 0);

        cache.clear();
        REQUIRE(cache.memory_usage_bytes() == 0);
        REQUIRE(cache.cached_layer_count() == 0);
    }

    SECTION("set_memory_budget evicts excess") {
        // Start with generous budget
        cache.get_or_load(0, test_loader(100));
        cache.get_or_load(1, test_loader(100));
        cache.get_or_load(2, test_loader(100));
        REQUIRE(cache.cached_layer_count() == 3);

        // Reduce budget to fit only 1 layer
        cache.set_memory_budget(10 * 1024);

        // Should have evicted down to fit
        REQUIRE(cache.cached_layer_count() <= 2);
        REQUIRE(cache.memory_usage_bytes() <= 10 * 1024);
    }
}

TEST_CASE("GCodeLayerCache insert", "[gcode][cache]") {
    GCodeLayerCache cache(10 * 1024);

    SECTION("insert adds layer to cache") {
        auto segments = make_test_segments(20);
        bool success = cache.insert(5, std::move(segments));

        REQUIRE(success);
        REQUIRE(cache.is_cached(5));

        auto result = cache.get_or_load(5, test_loader(0));
        REQUIRE(result.was_hit == true);
        REQUIRE(result.segments->size() == 20);
    }

    SECTION("insert rejects oversized layer") {
        // Try to insert a huge layer
        auto segments = make_test_segments(10000); // Way over 10KB budget

        bool success = cache.insert(0, std::move(segments));
        REQUIRE_FALSE(success);
        REQUIRE_FALSE(cache.is_cached(0));
    }
}

TEST_CASE("GCodeLayerCache prefetch", "[gcode][cache]") {
    GCodeLayerCache cache(100 * 1024);

    std::vector<size_t> loaded;

    SECTION("prefetch loads layers around center") {
        cache.prefetch(5, 2, tracking_loader(loaded, 20), 100);

        // Should have loaded layers 3, 4, 5, 6, 7
        REQUIRE(loaded.size() == 5);
        REQUIRE(cache.is_cached(3));
        REQUIRE(cache.is_cached(4));
        REQUIRE(cache.is_cached(5));
        REQUIRE(cache.is_cached(6));
        REQUIRE(cache.is_cached(7));
    }

    SECTION("prefetch respects max_layer") {
        cache.prefetch(2, 5, tracking_loader(loaded, 20), 4);

        // Should load 0, 1, 2, 3, 4 (not beyond max_layer=4)
        REQUIRE(loaded.size() == 5);
        for (size_t i = 0; i <= 4; ++i) {
            REQUIRE(cache.is_cached(i));
        }
    }

    SECTION("prefetch handles already cached layers efficiently") {
        // Pre-cache layer 5
        cache.get_or_load(5, tracking_loader(loaded, 20));
        loaded.clear();

        cache.prefetch(5, 1, tracking_loader(loaded, 20), 100);

        // Should load 4, 6; layer 5 was already cached so loader won't be called for it
        // (get_or_load returns cached data without calling loader)
        REQUIRE(loaded.size() == 2);
        REQUIRE(std::find(loaded.begin(), loaded.end(), 5) == loaded.end());
    }
}

TEST_CASE("GCodeLayerCache adaptive mode", "[gcode][cache]") {
    GCodeLayerCache cache(100 * 1024);

    SECTION("adaptive mode can be enabled") {
        REQUIRE_FALSE(cache.is_adaptive_mode());

        cache.set_adaptive_mode(true, 15, 1024, 50 * 1024);

        REQUIRE(cache.is_adaptive_mode());
    }

    SECTION("respond_to_pressure evicts entries") {
        // Fill the cache
        for (size_t i = 0; i < 10; ++i) {
            cache.get_or_load(i, test_loader(50));
        }
        size_t before = cache.cached_layer_count();
        REQUIRE(before > 0);

        // Trigger emergency pressure response
        cache.respond_to_pressure(0.1f); // Reduce to 10% of budget

        size_t after = cache.cached_layer_count();
        REQUIRE(after < before);
    }

    SECTION("check_memory_pressure rate limits") {
        cache.set_adaptive_mode(true);

        // First check should work
        // (Note: actual adjustment depends on system memory, so we just check it doesn't crash)
        cache.check_memory_pressure();

        // Immediate second check should be skipped (rate limited)
        int64_t ms = cache.ms_since_last_pressure_check();
        REQUIRE(ms < 100); // Should be very recent
    }
}

TEST_CASE("GCodeLayerCache thread safety", "[gcode][cache][thread]") {
    GCodeLayerCache cache(100 * 1024);

    SECTION("concurrent reads don't crash") {
        // Pre-populate
        cache.get_or_load(0, test_loader(50));

        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&cache]() {
                for (int j = 0; j < 100; ++j) {
                    auto result = cache.get_or_load(0, test_loader(50));
                    REQUIRE(result.segments != nullptr);
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(cache.is_cached(0));
    }

    SECTION("concurrent reads and writes don't crash") {
        std::atomic<bool> stop{false};
        std::vector<std::thread> threads;

        // Reader threads
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&cache, &stop]() {
                while (!stop.load()) {
                    cache.get_or_load(rand() % 20, test_loader(20));
                }
            });
        }

        // Writer thread (evictions)
        threads.emplace_back([&cache, &stop]() {
            while (!stop.load()) {
                cache.evict(rand() % 20);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        // Let it run briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.store(true);

        for (auto& t : threads) {
            t.join();
        }
    }
}
