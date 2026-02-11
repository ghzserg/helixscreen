// SPDX-License-Identifier: GPL-3.0-or-later

#include "layout_manager.h"

#include "../catch_amalgamated.hpp"

using helix::LayoutManager;
using helix::LayoutType;

class LayoutManagerTestAccess {
  public:
    static void reset(LayoutManager& lm) {
        lm.type_ = LayoutType::STANDARD;
        lm.name_ = "standard";
        lm.override_name_.clear();
        lm.initialized_ = false;
    }
};

// Reset singleton state between tests
struct LayoutFixture {
    LayoutFixture() {
        LayoutManagerTestAccess::reset(LayoutManager::instance());
    }
    ~LayoutFixture() {
        LayoutManagerTestAccess::reset(LayoutManager::instance());
    }
};

// ============================================================================
// Detection via init()
// ============================================================================

TEST_CASE_METHOD(LayoutFixture, "Standard landscape resolutions", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("800x480") {
        lm.init(800, 480);
        REQUIRE(lm.type() == LayoutType::STANDARD);
    }
    SECTION("1024x600") {
        lm.init(1024, 600);
        REQUIRE(lm.type() == LayoutType::STANDARD);
    }
    SECTION("1920x1080") {
        lm.init(1920, 1080);
        REQUIRE(lm.type() == LayoutType::STANDARD);
    }
}

TEST_CASE_METHOD(LayoutFixture, "Ultrawide resolutions", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("1920x480 ratio=4.0") {
        lm.init(1920, 480);
        REQUIRE(lm.type() == LayoutType::ULTRAWIDE);
    }
    SECTION("2560x600 ratio=4.27") {
        lm.init(2560, 600);
        REQUIRE(lm.type() == LayoutType::ULTRAWIDE);
    }
}

TEST_CASE_METHOD(LayoutFixture, "Portrait resolutions", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("480x800") {
        lm.init(480, 800);
        REQUIRE(lm.type() == LayoutType::PORTRAIT);
    }
    SECTION("600x1024") {
        lm.init(600, 1024);
        REQUIRE(lm.type() == LayoutType::PORTRAIT);
    }
}

TEST_CASE_METHOD(LayoutFixture, "Tiny landscape resolutions", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("480x320") {
        lm.init(480, 320);
        REQUIRE(lm.type() == LayoutType::TINY);
    }
    SECTION("320x240") {
        lm.init(320, 240);
        REQUIRE(lm.type() == LayoutType::TINY);
    }
}

TEST_CASE_METHOD(LayoutFixture, "Tiny portrait resolutions", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("320x480") {
        lm.init(320, 480);
        REQUIRE(lm.type() == LayoutType::TINY_PORTRAIT);
    }
    SECTION("240x320") {
        lm.init(240, 320);
        REQUIRE(lm.type() == LayoutType::TINY_PORTRAIT);
    }
}

TEST_CASE_METHOD(LayoutFixture, "Square resolution is STANDARD", "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.init(500, 500);
    REQUIRE(lm.type() == LayoutType::STANDARD);
}

// ============================================================================
// Override
// ============================================================================

TEST_CASE_METHOD(LayoutFixture, "Override forces layout type", "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.set_override("ultrawide");
    lm.init(800, 480);
    REQUIRE(lm.type() == LayoutType::ULTRAWIDE);
    REQUIRE(lm.name() == "ultrawide");
}

TEST_CASE_METHOD(LayoutFixture, "Override normalizes hyphens to underscores", "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.set_override("tiny-portrait");
    lm.init(800, 480);
    REQUIRE(lm.type() == LayoutType::TINY_PORTRAIT);
    REQUIRE(lm.name() == "tiny_portrait");
}

TEST_CASE_METHOD(LayoutFixture, "Unknown override name defaults to standard", "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.set_override("bogus");
    lm.init(800, 480);
    REQUIRE(lm.type() == LayoutType::STANDARD);
    REQUIRE(lm.name() == "standard");
}

TEST_CASE_METHOD(LayoutFixture, "Empty override clears override", "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.set_override("ultrawide");
    lm.set_override("");
    lm.init(800, 480);
    REQUIRE(lm.type() == LayoutType::STANDARD);
}

// ============================================================================
// is_standard()
// ============================================================================

TEST_CASE_METHOD(LayoutFixture, "is_standard returns true only for STANDARD", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("STANDARD") {
        lm.init(1024, 600);
        REQUIRE(lm.is_standard() == true);
    }
    SECTION("ULTRAWIDE") {
        lm.init(1920, 480);
        REQUIRE(lm.is_standard() == false);
    }
    SECTION("PORTRAIT") {
        lm.init(480, 800);
        REQUIRE(lm.is_standard() == false);
    }
    SECTION("TINY") {
        lm.init(480, 320);
        REQUIRE(lm.is_standard() == false);
    }
    SECTION("TINY_PORTRAIT") {
        lm.init(320, 480);
        REQUIRE(lm.is_standard() == false);
    }
}

// ============================================================================
// name()
// ============================================================================

TEST_CASE_METHOD(LayoutFixture, "name returns correct string for each type", "[layout-manager]") {
    auto& lm = LayoutManager::instance();

    SECTION("standard") {
        lm.init(1024, 600);
        REQUIRE(lm.name() == "standard");
    }
    SECTION("ultrawide") {
        lm.init(1920, 480);
        REQUIRE(lm.name() == "ultrawide");
    }
    SECTION("portrait") {
        lm.init(480, 800);
        REQUIRE(lm.name() == "portrait");
    }
    SECTION("tiny") {
        lm.init(480, 320);
        REQUIRE(lm.name() == "tiny");
    }
    SECTION("tiny_portrait") {
        lm.init(320, 480);
        REQUIRE(lm.name() == "tiny_portrait");
    }
}

// ============================================================================
// resolve_xml_path()
// ============================================================================

TEST_CASE_METHOD(LayoutFixture, "resolve_xml_path returns base path for standard",
                 "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.init(1024, 600);
    REQUIRE(lm.resolve_xml_path("home_panel.xml") == "ui_xml/home_panel.xml");
}

TEST_CASE_METHOD(LayoutFixture, "resolve_xml_path falls back to base for non-standard",
                 "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.init(1920, 480);
    // No variant file exists on disk, so should fall back
    REQUIRE(lm.resolve_xml_path("nonexistent_panel.xml") == "ui_xml/nonexistent_panel.xml");
}

// ============================================================================
// Ultrawide override integration (requires ui_xml/ultrawide/home_panel.xml on disk)
// ============================================================================

TEST_CASE_METHOD(LayoutFixture,
                 "resolve_xml_path returns ultrawide override when file exists on disk",
                 "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.init(1920, 480);
    REQUIRE(lm.type() == LayoutType::ULTRAWIDE);

    // home_panel.xml has an ultrawide override -> should resolve to ultrawide path
    REQUIRE(lm.resolve_xml_path("home_panel.xml") == "ui_xml/ultrawide/home_panel.xml");
}

TEST_CASE_METHOD(LayoutFixture, "resolve_xml_path falls back for panels without ultrawide override",
                 "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.init(1920, 480);
    REQUIRE(lm.type() == LayoutType::ULTRAWIDE);

    // controls_panel.xml has no ultrawide override -> should fall back to standard
    REQUIRE(lm.resolve_xml_path("controls_panel.xml") == "ui_xml/controls_panel.xml");
}

TEST_CASE_METHOD(LayoutFixture, "has_override returns true for ultrawide home_panel",
                 "[layout-manager]") {
    auto& lm = LayoutManager::instance();
    lm.init(1920, 480);

    REQUIRE(lm.has_override("home_panel.xml") == true);
    REQUIRE(lm.has_override("controls_panel.xml") == false);
}
