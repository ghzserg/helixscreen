# Timelapse Feature Plan

Integration with [moonraker-timelapse](https://github.com/mainsail-crew/moonraker-timelapse) plugin for automated timelapse recording during prints.

---

## Phase 1: Plugin Detection & Guided Installation — COMPLETE

**Branch:** `feature/timelapse`
**Status:** All 6 steps implemented, reviewed, tested, committed.
**Commits:** 6 (25693be2..740f7bfe)

### What was built

| Component | Files | Description |
|-----------|-------|-------------|
| Webcam detection API | `moonraker_api.h`, `moonraker_api_history.cpp`, `moonraker_types.h` | `get_webcam_list()` via `server.webcams.list`, `WebcamInfo` struct |
| Webcam capability subject | `printer_capabilities_state.h/.cpp`, `printer_state.h/.cpp` | `printer_has_webcam` subject (0/1), async setter, discovery integration |
| Moonraker restart API | `moonraker_api.h`, `moonraker_api_motion.cpp` | `restart_moonraker()` via `server.restart` |
| Timelapse capability setter | `printer_capabilities_state.h/.cpp`, `printer_state.h/.cpp` | `set_timelapse_available(bool)` for post-install capability update |
| Advanced panel row | `advanced_panel.xml`, `ui_panel_advanced.h/.cpp` | "Setup Timelapse" row with dual visibility binding (webcam=1 AND timelapse=0) |
| Install wizard overlay | `ui_overlay_timelapse_install.h/.cpp`, `timelapse_install_overlay.xml` | 6-step wizard: check webcam → check plugin → SSH instructions → configure moonraker.conf → restart → verify |
| Integration | `subject_initializer.cpp`, `xml_registration.cpp` | Global init + XML component registration |
| Tests | `test_timelapse_install.cpp` | 23 tests / 47 assertions: config parsing, trailing whitespace, Windows line endings, round-trip |

### Architecture decisions

- **Thread safety:** All API callbacks use `helix::async::invoke()` to marshal LVGL calls to the main thread.
- **Config modification:** Download moonraker.conf → check for `[timelapse]` section → append if missing → upload. Hardcoded config snippet (no user input injection risk).
- **Disconnect suppression:** 15s suppression during intentional Moonraker restart to prevent scary disconnect modal.
- **Alive guard pattern:** `std::shared_ptr<bool>` for async callback safety, matching `PrintPreparationManager`.
- **Wizard action button:** Uses `std::function<void()> action_callback_` for context-dependent behavior rather than a state switch.

### Known limitations

- No mock override for `get_webcam_list()` on `MoonrakerAPIMock` (falls through to base — works but not ideal for controlled testing)
- Mock always reports webcam available (no toggle for "no webcam" path testing)
- Imperative LVGL calls in wizard (justified: step-based wizard is inherently procedural)
- `lv_timer_create` for 8s verify delay uses global pointer check rather than `alive_guard_` capture

### Validation needed

- [ ] Connect to Voron (192.168.1.112) — detect webcam, show install option, attempt config-only install
- [ ] Connect to Forge-X (192.168.1.67:7125) — detect webcam, show install option with SSH instructions
- [ ] Mock mode (`--test -vv`) — verify "Setup Timelapse" row appears in Advanced panel
- [ ] Install moonraker-timelapse on one test printer, verify full wizard flow

---

## Phase 2: Enhanced Timelapse Features — COMPLETE

**Status:** Implemented
**Goal:** Make the timelapse feature fully functional with real-time events, render progress, and video management.

### What was built

| Component | Files | Description |
|-----------|-------|-------------|
| TimelapseState singleton | `include/timelapse_state.h`, `src/printer/timelapse_state.cpp` | Event-driven state manager with subjects for render progress (int 0-100), render status (string), and frame count (int) |
| WebSocket event subscription | `src/application/application.cpp` | `notify_timelapse_event` callback registered on connect, unregistered on shutdown; dispatches to `TimelapseState::handle_timelapse_event()` |
| Event dispatch | `src/printer/timelapse_state.cpp` | Parses `action` field: "newframe" increments frame count, "render" updates progress/status with throttled notifications at 25% boundaries |
| Render progress notifications | `src/printer/timelapse_state.cpp` | Toast notifications for render start, progress (25/50/75%), completion, and errors via `NOTIFY_INFO`/`NOTIFY_ERROR` |
| New API methods | `include/moonraker_api.h`, `src/api/moonraker_api_history.cpp` | `render_timelapse()`, `save_timelapse_frames()`, `get_last_frame_info()` |
| Video management UI | `ui_xml/timelapse_settings_overlay.xml`, `src/ui/ui_overlay_timelapse_settings.cpp` | Video list via `list_files("timelapse", ...)`, manual render button, delete videos via `delete_file("timelapse/...")` |
| Subject initialization | `src/ui/subject_initializer.cpp` | TimelapseState subjects registered in global init, reset on new print |

### Architecture decisions

- **Singleton pattern:** `TimelapseState::instance()` follows the same pattern as `AmsState`, `ToolState`, and other domain state managers.
- **Throttled notifications:** Render progress notifications fire only at 25% boundaries (25/50/75/100) to avoid spamming the UI with frequent updates.
- **Thread safety:** WebSocket event callback uses `ui_queue_update()` to marshal subject updates to the LVGL thread, consistent with all other WebSocket handlers.
- **Subject lifecycle:** Subjects initialized in `subject_initializer.cpp` alongside other printer state, frame count reset when a new print starts.
- **Event dispatch:** `handle_timelapse_event()` uses a simple action-string dispatch ("newframe" vs "render") matching the moonraker-timelapse plugin's event format.

---

## Phase 3: Remote Rendering (Future)

**Status:** Design only — will be refined after Phase 2 learnings.
**Goal:** For memory-constrained devices (Forge-X 128MB), offload ffmpeg rendering to the HelixScreen host.

### Architecture

```
Printer (128MB) ──frames──> HelixScreen Host (Pi 4/5) ──ffmpeg──> Video
                                    │
                                    └──upload──> Printer /timelapse/video.mp4
```

### Approach

1. Disable autorender on the plugin (`autorender: false`)
2. HelixScreen detects print completion + frames available
3. Download frames via streaming (`download_file_to_path()`)
4. Run ffmpeg locally on the HelixScreen host
5. Upload rendered video back to printer's timelapse directory

### Config Option

Add "Remote rendering" toggle to timelapse settings. Auto-suggest based on detected available memory from `/machine/system_info`.

### Open Questions

- How to detect available RAM reliably across different Moonraker versions?
- Should remote rendering be opt-in only, or auto-enabled below a memory threshold?
- Frame download bandwidth — is streaming practical over WiFi for hundreds of frames?
- ffmpeg availability on host — need to check/install at runtime?

---

## Reusable Infrastructure (Reference)

| Pattern | Location | Used By |
|---------|----------|---------|
| Config download/modify/upload | `print_start_enhancer.cpp:581-696` | Phase 1 (moonraker.conf) |
| Disconnect suppression | `print_start_enhancer.cpp:700-705` | Phase 1 (Moonraker restart) |
| Component detection in discovery | `moonraker_client.cpp:1146-1180` | Phase 1 (webcam detection) |
| Step progress UI | `ui_step_progress.h` | Phase 1 (install wizard) |
| Overlay pattern | `OverlayBase` / `ui_overlay_timelapse_settings.cpp` | Phase 1 (install overlay) |
| WebSocket method callbacks | `register_method_callback()` | Phase 2 (timelapse events) |
| HTTP file operations | `download_file()`, `upload_file()` | Phase 1 (config), Phase 3 (frames) |
| File listing | `list_files()`, `get_directory()` | Phase 2 (video management) |
| Toast notifications | `ui_toast_show()`, `NOTIFY_INFO()`, `NOTIFY_ERROR()` | Phase 2 (render status) |
| State manager pattern | `PrinterPrintState` | Phase 2 (TimelapseState) |
| Async call pattern | `helix::async::invoke()` | All phases (thread safety) |

---

## Printer Reference

| Printer | IP | Moonraker | Webcam | RAM | Timelapse Plugin |
|---------|-----|-----------|--------|-----|-----------------|
| Voron 2.4 | 192.168.1.112 | v0.9.3 (BTT Pi) | Mini Camera (mjpeg) | ~1GB+ | Not installed |
| Forge-X AD5M Pro | 192.168.1.67:7125 | DrA1ex ff5m mod | cam (mjpeg) | 128MB | Not installed, not bundled |
