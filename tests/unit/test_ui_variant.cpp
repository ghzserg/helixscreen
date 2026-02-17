// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_variant.h"

#include "../catch_amalgamated.hpp"

using helix::ui::parse_variant;
using helix::ui::Variant;
using helix::ui::variant_opa;

TEST_CASE("Variant parsing", "[ui][variant]") {
    SECTION("known variants parse correctly") {
        CHECK(parse_variant("success") == Variant::SUCCESS);
        CHECK(parse_variant("warning") == Variant::WARNING);
        CHECK(parse_variant("danger") == Variant::DANGER);
        CHECK(parse_variant("info") == Variant::INFO);
        CHECK(parse_variant("muted") == Variant::MUTED);
        CHECK(parse_variant("primary") == Variant::PRIMARY);
        CHECK(parse_variant("secondary") == Variant::SECONDARY);
        CHECK(parse_variant("tertiary") == Variant::TERTIARY);
        CHECK(parse_variant("disabled") == Variant::DISABLED);
        CHECK(parse_variant("text") == Variant::TEXT);
        CHECK(parse_variant("none") == Variant::NONE);
    }

    SECTION("null and empty return NONE") {
        CHECK(parse_variant(nullptr) == Variant::NONE);
        CHECK(parse_variant("") == Variant::NONE);
    }

    SECTION("unknown string returns NONE") {
        CHECK(parse_variant("bogus") == Variant::NONE);
        CHECK(parse_variant("SUCCESS") == Variant::NONE); // case-sensitive
    }
}

TEST_CASE("Variant opacity", "[ui][variant]") {
    CHECK(variant_opa(Variant::SUCCESS) == LV_OPA_COVER);
    CHECK(variant_opa(Variant::DANGER) == LV_OPA_COVER);
    CHECK(variant_opa(Variant::DISABLED) == LV_OPA_50);
    CHECK(variant_opa(Variant::NONE) == LV_OPA_COVER);
}
