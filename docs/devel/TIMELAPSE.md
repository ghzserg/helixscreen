# Timelapse Feature

Integration with [moonraker-timelapse](https://github.com/mainsail-crew/moonraker-timelapse) plugin for automated timelapse recording during prints. Currently a **beta feature** gated behind the beta features flag.

**Design doc**: [archived](../../docs/archive/plans/2026-02-15-timelapse-feature.md) (phased plan, architecture decisions, future work)

---

## What It Provides

HelixScreen does not implement timelapse recording itself. It provides:

1. **Plugin detection** -- discovers whether moonraker-timelapse is installed during printer connection
2. **Install wizard** -- guides users through SSH install, moonraker.conf configuration, and restart/verification
3. **Settings UI** -- configures the plugin (enable/disable, recording mode, framerate, auto-render)
4. **Print status toggle** -- quick enable/disable button on the print status panel
5. **Real-time event handling** -- subscribes to WebSocket `notify_timelapse_event` for frame capture and render progress tracking
6. **Render progress notifications** -- toast notifications at 25% intervals during rendering, plus success/error alerts
7. **Video management** -- list, render, and delete timelapse videos from the settings overlay

All actual frame capture, rendering, and video storage is handled by the moonraker-timelapse plugin on the printer.

---

## Beta Feature Gating

Timelapse is wrapped in `<beta_feature>` XML containers and gated at the capability level:

- **XML layer**: Advanced panel rows use `<beta_feature>` wrapper, which binds to the `show_beta_features` subject and auto-hides when beta is disabled
- **Capability layer**: `PrinterCapabilitiesState::set_hardware()` and `set_timelapse_available()` both AND the detection result with `Config::is_beta_features_enabled()` before setting the `printer_has_timelapse` subject

Users enable beta features via the 7-tap secret on the version row in Settings.

---

## Plugin Detection

Detection happens during printer hardware discovery in `PrinterDiscovery`. Moonraker reports `timelapse` as a registered component when the plugin is loaded:

```
PrinterDiscovery::parse_objects() -> name == "timelapse" -> has_timelapse_ = true
```

This flows through:
1. `PrinterDiscovery::has_timelapse()` -- raw detection result
2. `PrinterCapabilitiesState::set_hardware()` -- gates behind beta, sets `printer_has_timelapse` subject
3. XML bindings react: timelapse settings row shown (plugin installed) or setup row shown (plugin missing + webcam present)

The advanced panel uses dual visibility binding for the setup row:
```xml
<bind_flag_if_eq subject="printer_has_timelapse" flag="hidden" ref_value="1"/>
<bind_flag_if_eq subject="printer_has_webcam" flag="hidden" ref_value="0"/>
```
This means "show when timelapse is NOT installed AND webcam IS available."

---

## Install Wizard

`TimelapseInstallOverlay` is a 6-step wizard overlay opened from the "Setup Timelapse" row in the Advanced panel.

| Step | Action |
|------|--------|
| 1. Check webcam | `api->get_webcam_list()` -- abort if none found |
| 2. Check plugin | `api->get_timelapse_settings()` -- skip to done if already responding |
| 3. SSH instructions | Show git clone + make install commands; user taps "Check Again" |
| 4. Configure Moonraker | Download `moonraker.conf`, check/append `[timelapse]` + `[update_manager timelapse]` sections, upload |
| 5. Restart Moonraker | `api->restart_moonraker()` with 15s disconnect suppression |
| 6. Verify | Re-query `get_timelapse_settings()` after 8s delay, update capability state on success |

Key patterns:
- **Alive guard**: `std::shared_ptr<bool>` captures prevent callbacks from touching destroyed overlay
- **Config modification**: `has_timelapse_section()` / `append_timelapse_config()` are pure static functions (public for testability, 23 unit tests)
- **Disconnect suppression**: `EmergencyStopOverlay::instance().suppress_recovery_dialog(15000)` prevents the disconnect modal during intentional Moonraker restart

---

## Settings UI

`TimelapseSettingsOverlay` provides configuration when the plugin is installed. Opened from the "Timelapse" row in Advanced panel (visible when `printer_has_timelapse == 1`).

| Setting | Options | API |
|---------|---------|-----|
| Enable Timelapse | Toggle | `enabled` param |
| Recording Mode | Layer Macro / Hyperlapse | `mode` param |
| Framerate | 15 / 24 / 30 / 60 fps | `output_framerate` param |
| Auto-render | Toggle | `autorender` param |

Settings are read via `GET /machine/timelapse/settings` and saved via `POST /machine/timelapse/settings?param=value` (query string format, not JSON body).

---

## Print Status Toggle

`PrintLightTimelapseControls` manages the timelapse toggle button on the print status panel. It:
- Shows a video/video-off icon with On/Off label via string subjects (`timelapse_button_icon`, `timelapse_button_label`)
- Calls `api->set_timelapse_enabled(bool)` on click
- Uses `ui_async_call()` to marshal UI updates from API callback thread to LVGL thread

This class also manages the light button (LED toggle) -- they share the same helper since both appear as action buttons on the print status panel.

---

## Moonraker API Methods

All timelapse API methods are in `MoonrakerAPI` (declared in `moonraker_api.h`, implemented in `moonraker_api_history.cpp`):

| Method | HTTP | Endpoint |
|--------|------|----------|
| `get_timelapse_settings()` | GET | `/machine/timelapse/settings` |
| `set_timelapse_settings()` | POST | `/machine/timelapse/settings?params...` |
| `set_timelapse_enabled()` | POST | `/machine/timelapse/settings?enabled=True/False` |
| `get_webcam_list()` | GET | `/server/webcams/list` |
| `restart_moonraker()` | POST | `/server/restart` |
| `render_timelapse()` | POST | `/machine/timelapse/render` |
| `save_timelapse_frames()` | POST | `/machine/timelapse/saveframes` |
| `get_last_frame_info()` | GET | `/machine/timelapse/lastframeinfo` |
| `list_files("timelapse")` | GET | `/server/files/list?root=timelapse` |
| `delete_file("timelapse/...")` | DELETE | `/server/files/timelapse/...` |

---

## Phase 2: Event Handling & Video Management

Phase 2 adds real-time timelapse event handling, render progress tracking, and video management to the settings overlay.

### TimelapseState

`TimelapseState` is a singleton (`TimelapseState::instance()`) that manages timelapse-specific state via LVGL subjects:

| Subject | Type | Purpose |
|---------|------|---------|
| `timelapse_render_progress` | int (0-100) | Current render progress percentage |
| `timelapse_render_status` | string | Render state: `"idle"`, `"rendering"`, `"complete"`, `"error"` |
| `timelapse_frame_count` | int | Number of frames captured during current print |

Subjects are initialized in `subject_initializer.cpp` and frame count resets when a new print starts.

### Event Handling

The moonraker-timelapse plugin emits `notify_timelapse_event` WebSocket notifications. HelixScreen subscribes to these in `application.cpp`:

```cpp
// On connect:
register_method_callback("notify_timelapse_event", "timelapse_handler", callback);

// On shutdown:
unregister_method_callback("notify_timelapse_event", "timelapse_handler");
```

`TimelapseState::handle_timelapse_event()` dispatches based on the `action` field:

- **`"newframe"`** -- increments `timelapse_frame_count` subject
- **`"render"`** -- updates `timelapse_render_status` and `timelapse_render_progress`; triggers toast notifications at 25% progress boundaries to avoid UI spam

### Render Progress Notifications

Render events produce user-visible toast notifications:

| Event | Notification |
|-------|-------------|
| Render starts (`progress == 0`) | "Rendering timelapse..." |
| Progress at 25/50/75% | "Rendering timelapse... 25%" (throttled) |
| Render complete (`status == "success"`) | "Timelapse rendered successfully" |
| Render error (`status == "error"`) | "Timelapse render failed: {msg}" |

### Video Management

The timelapse settings overlay (Phase 1) was extended with video management capabilities:

| Action | API Call | Description |
|--------|----------|-------------|
| List videos | `list_files("timelapse", ...)` | Shows rendered timelapse videos |
| Render video | `render_timelapse()` | POST `/machine/timelapse/render` -- triggers manual render |
| Save frames | `save_timelapse_frames()` | POST `/machine/timelapse/saveframes` -- saves frames as ZIP |
| Get last frame | `get_last_frame_info()` | GET `/machine/timelapse/lastframeinfo` -- info about last captured frame |
| Delete video | `delete_file("timelapse/filename.mp4")` | Removes a rendered timelapse file |

---

## File Map

| File | Purpose |
|------|---------|
| `include/ui_overlay_timelapse_install.h` | Install wizard overlay class |
| `src/ui/ui_overlay_timelapse_install.cpp` | Wizard step implementation |
| `ui_xml/timelapse_install_overlay.xml` | Install wizard layout (step progress, SSH instructions, action button) |
| `include/ui_overlay_timelapse_settings.h` | Settings overlay class |
| `src/ui/ui_overlay_timelapse_settings.cpp` | Settings fetch/save, event handlers |
| `ui_xml/timelapse_settings_overlay.xml` | Settings layout (toggles, dropdowns) |
| `include/ui_print_light_timelapse.h` | Print status light + timelapse button helper |
| `src/ui/ui_print_light_timelapse.cpp` | Toggle handlers, subject management |
| `include/printer_capabilities_state.h` | `printer_has_timelapse` subject |
| `src/printer/printer_capabilities_state.cpp` | Beta-gated capability setter |
| `include/printer_discovery.h` | `has_timelapse()` detection from Moonraker objects |
| `include/moonraker_types.h` | `TimelapseSettings` struct |
| `src/api/moonraker_api_history.cpp` | HTTP API methods for timelapse |
| `src/api/moonraker_api_motion.cpp` | Contains `restart_moonraker()` |
| `tests/unit/test_timelapse_install.cpp` | 23 tests for config parsing |
| `ui_xml/advanced_panel.xml` | Timelapse/Setup rows (beta-gated) |
| `ui_xml/beta_feature.xml` | Beta feature wrapper component |
| `include/timelapse_state.h` | TimelapseState singleton class |
| `src/printer/timelapse_state.cpp` | Event dispatch, subject management, render notifications |
| `tests/unit/test_timelapse_state.cpp` | Unit tests for TimelapseState event handling and notifications |
| `docs/archive/plans/2026-02-15-timelapse-feature.md` | Archived design doc with future phases |
