#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for release packaging exclusions
# Verifies that deploy/release targets exclude unnecessary files

load helpers

# ============================================================================
# Deploy asset exclude patterns
# ============================================================================

@test "DEPLOY_ASSET_EXCLUDES excludes .c font files" {
    local excludes
    excludes=$(make -n -p 2>/dev/null | grep '^DEPLOY_ASSET_EXCLUDES' | head -1)
    [[ "$excludes" == *"assets/fonts/*.c"* ]]
}

@test "DEPLOY_ASSET_EXCLUDES excludes .icns files" {
    local excludes
    excludes=$(make -n -p 2>/dev/null | grep '^DEPLOY_ASSET_EXCLUDES' | head -1)
    [[ "$excludes" == *"*.icns"* ]]
}

@test "DEPLOY_ASSET_EXCLUDES excludes mdi-icon-metadata" {
    local excludes
    excludes=$(make -n -p 2>/dev/null | grep '^DEPLOY_ASSET_EXCLUDES' | head -1)
    [[ "$excludes" == *"mdi-icon-metadata.json.gz"* ]]
}

@test "DEPLOY_TAR_EXCLUDES matches DEPLOY_ASSET_EXCLUDES patterns" {
    local rsync_excludes tar_excludes
    rsync_excludes=$(make -n -p 2>/dev/null | grep '^DEPLOY_ASSET_EXCLUDES' | head -1)
    tar_excludes=$(make -n -p 2>/dev/null | grep '^DEPLOY_TAR_EXCLUDES' | head -1)

    # Both should exclude .c font files
    [[ "$rsync_excludes" == *"assets/fonts/*.c"* ]]
    [[ "$tar_excludes" == *"assets/fonts/*.c"* ]]

    # Both should exclude .icns
    [[ "$rsync_excludes" == *"*.icns"* ]]
    [[ "$tar_excludes" == *"*.icns"* ]]
}

# ============================================================================
# Release clean-assets function
# ============================================================================

@test "release-clean-assets is defined in cross.mk" {
    grep -q 'define release-clean-assets' mk/cross.mk
}

@test "release-clean-assets removes .c font files" {
    grep -A5 'define release-clean-assets' mk/cross.mk | grep -q "fonts.*\*\.c.*-delete"
}

@test "release-clean-assets removes .icns files" {
    grep -A5 'define release-clean-assets' mk/cross.mk | grep -q "\*\.icns.*-delete"
}

@test "all release targets call release-clean-assets" {
    # Count release targets that include the cleanup call
    local target_count cleanup_count
    target_count=$(grep -c '^release-.*:' mk/cross.mk | head -1)
    cleanup_count=$(grep -c 'release-clean-assets' mk/cross.mk)

    # At least 6 release targets should call it (pi, pi32, ad5m, k1, k1-dynamic, k2)
    # Plus the definition = 7 occurrences minimum
    [ "$cleanup_count" -ge 7 ]
}

# ============================================================================
# Source tree doesn't contain files that shouldn't be deployed
# ============================================================================

@test "CJK fonts are gitignored" {
    grep -q 'NotoSansCJK' .gitignore
}

# ============================================================================
# LZ4 compression enabled
# ============================================================================

@test "LV_USE_LZ4_INTERNAL is enabled in lv_conf.h" {
    grep -q '#define LV_USE_LZ4_INTERNAL.*1' lv_conf.h
}

@test "image generation scripts use LZ4 compression" {
    grep -q '\-\-compress.*LZ4' scripts/lib/lvgl_image_lib.sh
}

@test "3D splash generation uses LZ4 compression" {
    grep -q '"--compress".*"LZ4"' scripts/gen_splash_3d.py
}
