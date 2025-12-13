# Log Level Refactor Plan

**Status**: In Progress
**Created**: 2025-12-13
**Reference**: `docs/LOGGING.md` (the authoritative logging guidelines)

## Overview

This plan outlines the work to standardize logging across the entire HelixScreen codebase. The goal is to make each verbosity level (`-v`, `-vv`, `-vvv`) useful and consistent.

## Immediate Work (Current Session)

### Phase 1: DEBUG → TRACE (High-Volume Noise)

Move per-item loop logging to TRACE to clean up `-vv` output.

| File | Status | Change |
|------|--------|--------|
| `src/ui_theme.cpp` | ✅ Done | Per-color/spacing/font registration |
| `src/moonraker_client.cpp` | ✅ Done | Wire protocol (send_jsonrpc, request registration) |
| `src/moonraker_api_files.cpp` | ⏳ Pending | Per-file metadata requests, thumbnail downloads |
| `src/ui_panel_print_select.cpp` | ⏳ Pending | Per-file metadata/thumbnail logs |
| `src/ui_status_bar_manager.cpp` | ⏳ Pending | Observer registration, state machine |
| `src/printer_state.cpp` | ⏳ Pending | Subject value plumbing |
| `src/main.cpp` | ⏳ Pending | State change callback queueing |
| `src/ui_icon.cpp` | ⏳ Pending | Icon source/variant changes |
| `src/temp_display_widget.cpp` | ⏳ Pending | Widget creation, binding |
| `src/ui_nav_manager.cpp` | ⏳ Pending | Per-button/icon wiring |
| `src/ams_state.cpp` | ⏳ Pending | Per-gate updates |
| `src/ui_ams_slot.cpp` | ⏳ Pending | Per-slot observer creation |
| `src/gcode_viewer.cpp` | ⏳ Pending | SIZE_CHANGED events |
| `src/thumbnail_cache.cpp` | ⏳ Pending | Per-download logs |

### Phase 2: INFO → DEBUG (Demote)

Move internal details from INFO to DEBUG.

| File | Message Pattern | Reason |
|------|-----------------|--------|
| `src/ui_modal_manager.cpp` | "Initializing/registered modal dialog subjects" | Internal |
| `src/ams_state.cpp` | "Registered N system subjects..." | Registration detail |
| `src/printer_state.cpp` | "Tracking LED: neopixel X" | Internal |
| `src/ui_panel_home.cpp` | "Configured LED", "Started tip rotation timer" | Internal |
| `src/usb_manager.cpp` | "macOS platform detected", "mock USB monitoring" | Backend |
| `src/wifi_manager.cpp` | "Starting CoreWLAN backend" | Backend |
| `src/ams_backend_afc.cpp` | "Creating AFC backend" | Internal |
| `src/gcode_viewer.cpp` | "using TinyGL 3D renderer" | Implementation |
| `src/settings_manager.cpp` | "Initializing subjects" | Start log |
| `src/ui_panel_print_select.cpp` | "Refreshing file list", "Received N items" | Frequent |

### Phase 3: DEBUG → INFO (Promote)

Promote important milestones to INFO.

| File | Message | Reason |
|------|---------|--------|
| `src/main.cpp` | "XML UI created successfully" | Startup milestone |
| `src/moonraker_client.cpp` | "Subscription complete: N objects subscribed" | ✅ Already done |

---

## Future Work (Full Codebase Refactor)

### Prefix Standardization

Current inconsistencies found in codebase:

| Pattern | Count | Files |
|---------|-------|-------|
| `[ComponentName]` | ~80% | Most files |
| `ComponentName:` | ~10% | Legacy patterns |
| `[Component Name]` | ~10% | Multi-word names |

**Target**: 100% use of `[ComponentName]` format with spaces for multi-word names.

### Files Needing Prefix Updates

```bash
# Find non-standard prefixes
grep -rn 'spdlog::\(debug\|info\|trace\|warn\|error\)("[A-Za-z].*:' src/ | head -50
```

Identified patterns to fix:
- `AmsState:` → `[AMS State]`
- `ConsolePanel initialized` → `[Console Panel] Initialized`
- `Keypad subjects initialized` → `[Keypad] Subjects initialized`
- `Notification system initialized` → `[NotificationSystem] Initialized`

### Systematic Audit Script

Create a script to audit all logging:

```bash
#!/bin/bash
# scripts/audit-logs.sh

echo "=== Log Level Distribution ==="
for level in trace debug info warn error; do
    count=$(grep -r "spdlog::$level" src/ | wc -l)
    echo "$level: $count"
done

echo ""
echo "=== Non-standard Prefixes ==="
grep -rn 'spdlog::\(debug\|info\|trace\|warn\|error\)("[A-Za-z]' src/ | \
    grep -v '\[' | head -20

echo ""
echo "=== DEBUG logs that might need TRACE ==="
grep -rn 'spdlog::debug.*for.*{' src/ | head -10
```

### Estimated Scope

| Category | Estimated Changes |
|----------|-------------------|
| DEBUG → TRACE | ~200 lines |
| INFO → DEBUG | ~30 lines |
| DEBUG → INFO | ~10 lines |
| Prefix standardization | ~100 lines |
| **Total** | ~340 lines |

### Testing Strategy

After each phase:

1. **Build**: `make -j`
2. **Test INFO**: `./build/bin/helix-screen --test -v 2>&1 | wc -l` (should be ~50-100 lines)
3. **Test DEBUG**: `./build/bin/helix-screen --test -vv 2>&1 | wc -l` (should be ~200-400 lines)
4. **Test TRACE**: `./build/bin/helix-screen --test -vvv 2>&1 | wc -l` (can be 1000+ lines)

### Success Criteria

- `-v` output is scannable in <50 lines (milestones only)
- `-vv` output is useful for troubleshooting without drowning in noise
- `-vvv` output has full wire-level detail for deep debugging
- All prefixes follow `[ComponentName]` pattern
- No `printf`, `std::cout`, or `LV_LOG_*` usage

---

## Progress Tracking

Update this section as work progresses:

- [x] Create `docs/LOGGING.md` with guidelines
- [x] Create this refactor plan
- [x] Phase 1: ui_theme.cpp
- [x] Phase 1: moonraker_client.cpp (wire protocol + INFO promotion)
- [ ] Phase 1: Remaining files
- [ ] Phase 2: INFO → DEBUG demotions
- [ ] Phase 3: DEBUG → INFO promotions
- [ ] Verification testing
- [ ] Prefix standardization (future sprint)
