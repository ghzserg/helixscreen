# Plugin System - Future Ideas

This document captures ideas for future plugin system enhancements that were considered but deferred from the initial implementation.

---

## Filter System

**Status:** Documented, not implemented
**Priority:** Low - implement when a real use case emerges

### Concept

Allow plugins to intercept and modify data, not just observe. Events are fire-and-forget; filters allow transformation.

### Example Use Cases

1. **G-code preprocessing** - Modify commands before sending to printer
2. **Temperature adjustment** - Apply calibration offsets
3. **Notification filtering** - Suppress or modify certain notifications

### Proposed API

```cpp
// Plugin registers a filter
api->add_filter("gcode_before_send", [](std::string& gcode) -> bool {
    // Modify gcode in place
    if (gcode.starts_with("M600")) {
        gcode = "LED_EFFECT EFFECT=filament_change\n" + gcode;
    }
    // Return true to continue filter chain, false to abort
    return true;
});

// Core code applies filters
std::string gcode = "G28";
filter_manager.apply("gcode_before_send", gcode);
moonraker->send_gcode(gcode);
```

### Implementation Notes

- New `FilterManager` class (~200 lines)
- Filters run synchronously in registration order
- Priority system for ordering (optional)
- Thread safety considerations

---

## AMS Extraction to Plugin

**Status:** Future consideration
**Priority:** Low - large refactor, post-1.0

### Concept

Move AMS (Automatic Material System / filament management) support from core into an optional plugin. Not all printers have AMS hardware.

### Benefits

- Reduces core complexity
- Faster startup for non-AMS printers
- AMS updates independent of core releases
- Reference implementation for complex plugins

### Challenges

- AMS is deeply integrated (UI panels, printer state, Moonraker subscriptions)
- Would need stable internal APIs for plugin to hook into
- Testing matrix increases

### Migration Path

1. Create AMS plugin with current functionality
2. Add feature flag to disable core AMS
3. Deprecate core AMS over 2-3 releases
4. Remove core AMS code

---

## Plugin Hot-Reload

**Status:** Not planned
**Priority:** Low - complexity vs benefit

### Concept

Reload plugins without restarting the application.

### Challenges

- LVGL widgets must be cleaned up properly
- Subjects and observers need careful lifecycle management
- Service dependencies between plugins
- State preservation across reload

### Alternative

Current approach: restart app after plugin changes. This is simple and reliable.

---

## Plugin Marketplace

**Status:** Dream feature
**Priority:** Very low - requires infrastructure

### Concept

In-app discovery and installation of community plugins.

### Requirements

- Plugin registry/catalog service
- Code signing for security
- Version compatibility matrix
- Update notifications

### Simpler Alternative

- Document manual installation process
- Curated list of community plugins in docs
- GitHub releases for distribution

---

## Plugin Sandboxing

**Status:** Not planned
**Priority:** Low - trust-based model is simpler

### Current Model

Plugins are trusted code with full access to the application. This is similar to VS Code extensions, Klipper macros, etc.

### If Sandboxing Were Needed

- Separate process with IPC
- Capability-based permissions
- Significant complexity increase

### Recommendation

Keep trust-based model. Document security considerations for users installing third-party plugins.

---

## Notes

These ideas are captured for future reference. Implementation should be driven by actual user needs rather than speculative features.

To propose new ideas, add them to this document with:
- Clear description
- Use cases
- Implementation complexity estimate
- Priority recommendation
