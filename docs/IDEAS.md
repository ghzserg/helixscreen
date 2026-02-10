# Ideas & Future Improvements

* Have libhv use spdlog for logging if possible
* **Lazy panel initialization** - Defer `init_subjects()` and `setup()` until first navigation. Challenge: LVGL XML binding requires subjects to exist before parsing. Solution: register empty subjects at startup, populate on first use. Would reduce startup time and memory.
* **Belt tension visualization** - Use Klipper's accelerometer (ADXL345) to measure belt resonance frequencies on CoreXY printers. Comparing X and Y frequency peaks reveals belt tension imbalance. MVP: run `TEST_RESONANCES` on each axis, display a frequency chart comparing peaks (reuses `ui_frequency_response_chart.cpp` from input shaper). A "matched" result means belts are balanced. Future: live spectrum analyzer mode for real-time tuning while adjusting tensioners.
* **Z-axis direction flip toggle** - Settings option to invert Z movement buttons for printers where the heuristic gets it wrong
* **Customizable home screen layout** - Let users configure which widgets/cards appear on the home screen (e.g., hide WiFi if not needed)
* **Custom printer image on home page** - Allow manually choosing the printer picture shown on the home screen, and optionally uploading a custom image to replace the auto-detected one
* **2D renderer crease/edge shading** - Estimate surface normals per extrusion segment using inter-layer contour analysis. Where normals change sharply between adjacent layers (creases, ridges, corners), darken segments to simulate light direction changes. Would give the 2D view a much more 3D appearance without the full 3D renderer. Requires a preprocessing pass after gcode parsing to build per-layer contour maps and detect normal discontinuities.
* **Power device setup wizard** - Guided Moonraker config editing to help users set up smart plugs, relay boards, and other power devices through the UI instead of manually editing `moonraker.conf`. Basic power device control already works (see `ui_panel_power.cpp`); this adds the setup/onboarding flow.

---

## Known Issues

* **LVGL lv_bar value=0 bug** (upstream issue) - Bar shows FULL instead of empty when created with cur_value=0 and XML sets value=0. `lv_bar_set_value()` returns early without invalidation. Workaround: set to 1 then 0.

---

## Design Philosophy: Local vs Remote UI

HelixScreen is a **local touchscreen** - users are physically present at the printer. This fundamentally differs from web UIs (Mainsail/Fluidd) designed for remote monitoring.

**What this means:**
- **Camera** is low priority - you can see the printer with your eyes
- **Job Queue** is not useful - you need to manually remove prints between jobs
- **System stats** (CPU/memory) are less important - you're not diagnosing remote issues
- **Remote access features** don't apply to this form factor

**What we prioritize instead:**
- Tactile controls optimized for touch
- At-a-glance information for the user standing at the machine
- Calibration workflows (PID, Z-offset, screws tilt, input shaper)
- Real-time tuning (speed, flow, firmware retraction)

Don't copy features from web UIs just because "competitors have it" - evaluate whether it makes sense for a local touchscreen.
