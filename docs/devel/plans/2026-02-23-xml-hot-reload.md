# XML Hot Reload — Implementation Status

**Goal:** Enable live editing of XML layouts during development without restarting the app. Edit XML, save, see changes in ~1ms.

**Status:** Phase 2 complete. Core hot reload working. Phase 3 (automatic panel rebuild) deferred as stretch goal.

---

## What Was Built

### Phase 2: XML Hot Reload (DONE)

New `XmlHotReloader` class that polls XML files for mtime changes and re-registers components live.

**How it works:**
1. `HELIX_HOT_RELOAD=1` env var activates the feature (zero overhead when disabled)
2. On startup, scans `ui_xml/` and `ui_xml/components/` — records all `.xml` file mtimes
3. Background thread polls every 500ms for changes
4. On change: queues `lv_xml_component_unregister()` + `lv_xml_register_component_from_file()` to LVGL main thread via `helix::ui::queue_update()`
5. Re-registration takes <1ms typically

**Files created:**
| File | Purpose |
|------|---------|
| `include/xml_hot_reloader.h` | Class declaration, `ReloadCallback` for test injection |
| `src/application/xml_hot_reloader.cpp` | Polling thread, mtime tracking, reload logic |
| `tests/unit/test_xml_hot_reloader.cpp` | 20 test cases (scanning, lifecycle, change detection) |

**Files modified:**
| File | Change |
|------|--------|
| `include/runtime_config.h` | Added `static bool hot_reload_enabled()` |
| `src/system/runtime_config.cpp` | Implemented with cached env var check |
| `include/application.h` | Added `unique_ptr<XmlHotReloader>` member |
| `src/application/application.cpp` | Start after XML registration, stop early in shutdown |

**Key design decisions:**
- **Polling, not inotify/kqueue** — cross-platform, zero dependencies, simple. 500ms is fast enough for dev workflow.
- **Injectable `ReloadCallback`** — tests bypass LVGL by providing a callback that records reload events.
- **Thread safety** — polling thread only reads mtimes; LVGL mutations are marshalled via `queue_update()`. Accessor methods (`tracked_file_count()`, `is_tracking()`) are documented as only safe when polling is stopped.
- **Cached env var** — `hot_reload_enabled()` uses `static bool` pattern (same as `debug_subjects()`) to avoid repeated `getenv()` calls.

**Known limitations:**
- New XML files added after startup are not detected — restart required
- Existing on-screen widgets show old layout until you navigate away and back
- Only pure XML changes are hot-reloaded — new subjects/callbacks/C++ code needs a rebuild

---

## Phase 3: Automatic Panel Rebuild (NEXT)

When a component changes, automatically rebuild the visible panel so the developer sees changes instantly without navigating away and back.

### Research Findings

**Panel lifecycle hooks available:**
- Every panel extends `PanelBase` with `get_xml_component_name()` (returns e.g. `"home_panel"`)
- `setup(lv_obj_t* panel, lv_obj_t* parent_screen)` wires up observers and state
- `on_activate()` / `on_deactivate()` for show/hide lifecycle
- Active panel tracked via `active_panel_subject_` in `NavigationManager` (observable `PanelId` enum)

**Panel factory pattern:**
- `PanelFactory` creates panels — maps `PanelId` to `PanelBase` subclass + XML component name
- Panels are created once and reused (not recreated on each navigate)

### Implementation Plan

**Step 1: Match reloaded component to active panel**

In `XmlHotReloader`, after re-registration succeeds, check if the reloaded component name matches the active panel's `get_xml_component_name()`. This requires passing a callback or querying `NavigationManager` from the reload path.

```
After reload: component_name == active_panel->get_xml_component_name()?
  YES → trigger panel rebuild
  NO  → done (component re-registered, will take effect on next navigate)
```

**Step 2: Panel rebuild sequence**

On the LVGL main thread (already there via `queue_update`):

1. Call `on_deactivate()` on the active panel — stops timers, pauses animations
2. Destroy all observers on the panel (panel's `ObserverGuard`s will be released when panel is re-setup)
3. `lv_obj_clean(panel_root)` — removes all child widgets
4. `lv_xml_create(panel_root, component_name, NULL)` — recreate children from updated XML
5. Call `setup(panel_root, parent_screen)` — re-wires observers, finds widgets by name
6. Call `on_activate()` — resumes animations, refreshes data

**Key challenge:** `setup()` creates new `ObserverGuard`s but the old ones are still alive on the `PanelBase`. Need to either:
- Add a `teardown()` or `clear_observers()` method to `PanelBase` that releases all registered observers
- Or make `setup()` idempotent (clear + re-create), which some panels may already handle

**Step 3: Fallback for complex panels**

Some panels (AMS, Print Status) have complex state that may not survive a rebuild cleanly. Options:
- Start with a whitelist of "safe to rebuild" panels
- Fall back to toast notification for complex panels: `"Reloaded X — switch panels to see changes"`
- Let panels opt-in via `virtual bool supports_hot_rebuild() const { return false; }` override

### Files to Create/Modify

| File | Action |
|------|--------|
| `include/ui_panel_base.h` | Add `virtual bool supports_hot_rebuild()` and `clear_observers()` |
| `include/xml_hot_reloader.h` | Add panel-rebuild callback type |
| `src/application/xml_hot_reloader.cpp` | Call rebuild callback after successful re-registration |
| `src/application/application.cpp` | Wire rebuild callback to NavigationManager + PanelFactory |
| Simple panels (home, controls, settings, macros) | Override `supports_hot_rebuild() → true` |

### Verification

1. `HELIX_HOT_RELOAD=1 ./build/bin/helix-screen --test -vv`
2. Edit `ui_xml/home_panel.xml` (change a color or padding)
3. Observe: panel rebuilds in-place without navigating away
4. Edit a component used by a non-supporting panel → toast notification appears
5. No crashes, no leaked observers, no dangling pointers

---

## Documentation

- `CLAUDE.md` — Quick Start mentions hot reload
- `docs/devel/DEVELOPMENT.md` — Daily Workflow and UI Development sections updated
- `docs/devel/ENVIRONMENT_VARIABLES.md` — Full `HELIX_HOT_RELOAD` reference with workflow example

## Tests

Run hot reload tests:
```bash
make test && ./build/bin/helix-tests "[hot-reload]"
```

20 test cases covering: component name derivation, initial scan (multi-dir, filtering, edge cases), start/stop lifecycle (idempotent, destructor), change detection (single/multi file, subdirs, deleted files, unmodified files), manual scan mode, LVGL path mapping.
