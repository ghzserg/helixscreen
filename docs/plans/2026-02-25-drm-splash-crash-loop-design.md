# Fix DRM Splash Crash Loop

**Date:** 2026-02-25
**Status:** Approved

## Problem

On systems where DRM is the display backend (forced via `HELIX_DISPLAY_BACKEND=drm` or auto-detected), the external `helix-splash` process grabs `/dev/fb0` via fbdev. This prevents the main `helix-screen` app from `mmap`-ing DRM dumb buffers on `/dev/dri/card1`, causing `drm_allocate_dumb: mmap fail` at `lv_linux_drm.c:930`.

The result is an infinite crash-restart loop:
1. Watchdog starts external splash (fbdev) + main app (DRM)
2. Main app fails DRM init → exits code 1
3. Watchdog shows crash dialog → also fails DRM → returns RESTART_APP
4. Goto 1

This affects any system with both `/dev/fb0` and `/dev/dri/card*` (e.g., Pi 5 with DSI display).

## Solution

### 1. Skip external splash when DRM will be used

Add a `would_use_drm()` helper to the watchdog that checks:
- `HELIX_DISPLAY_BACKEND=drm` env var → true
- Auto mode → probe DRM availability via `DisplayBackendDRM::is_available()` (cheap stat/access check)
- Otherwise → false

Guard `start_splash_process()` with this check. When DRM is selected, `splash_pid` stays 0, and the main app shows its internal LVGL splash screen (`application.cpp:782`).

### 2. Crash dialog fbdev fallback

In `show_crash_dialog()`, if the auto-detected backend fails `create_display()`, explicitly try fbdev as a fallback before giving up. This ensures the crash recovery screen is visible even when DRM is broken, breaking the infinite restart loop.

## Files Changed

- `src/helix_watchdog.cpp` — add `would_use_drm()`, guard splash startup, add crash dialog fbdev fallback

## Risk

Low. The internal LVGL splash already works — it's the default when no external splash PID is passed. The fbdev fallback for the crash dialog is additive.
