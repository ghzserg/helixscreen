# Plugin System - Follow-up Items

**Last Updated:** January 2025

This document tracks deferred improvements, known limitations, and future enhancement ideas for the HelixScreen plugin system. Items are organized by priority and include implementation guidance.

---

## Table of Contents

1. [Deferred Code Review Items](#deferred-code-review-items)
2. [Future Feature Ideas](#future-feature-ideas)
3. [Known Limitations](#known-limitations)

---

## Deferred Code Review Items

These items were identified during code review but deferred due to low immediate impact.

### 1. Subject Unregistration from LVGL XML System

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Small |
| **Risk** | Low - only affects plugin unload scenarios |

**Current Behavior:**
Subjects registered via `api->register_subject()` are tracked in `registered_subjects_` but NOT actually unregistered from LVGL's XML system during cleanup.

**Code Location:**
- `/Users/pbrown/code/helixscreen-plugins/src/plugin/plugin_api.cpp` lines 180-192, 389-391

**Current Code:**
```cpp
bool PluginAPI::unregister_subject(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(registered_subjects_.begin(), registered_subjects_.end(), name);
    if (it != registered_subjects_.end()) {
        registered_subjects_.erase(it);
        // TODO: Unregister from LVGL XML system
        spdlog::debug("[plugin:{}] Subject unregistered: {}", plugin_id_, name);
        return true;
    }
    return false;
}
```

**Why Deferred:**
- LVGL 9.4 does not provide `lv_xml_unregister_subject()`
- Subjects registered globally remain until `lv_deinit()`
- For typical usage (plugin loaded for application lifetime), this is not a problem

**Implementation When Ready:**
1. Check if LVGL adds `lv_xml_unregister_subject()` in a future release
2. If not available, consider maintaining a shadow map of registered subjects that can be cleared
3. Alternative: destroy and recreate the subject registry on plugin unload (heavy-handed)

**Impact of Not Fixing:**
- Memory for subject name strings is not reclaimed on plugin unload
- Re-loading a plugin that uses the same subject names works fine (registration is idempotent)
- Only affects development/testing scenarios with frequent plugin reloads

---

### 2. Path Traversal Protection for Plugin Loading

| Attribute | Value |
|-----------|-------|
| **Priority** | Medium |
| **Effort** | Small |
| **Risk** | Security - could allow malicious manifest to escape plugin directory |

**Current Behavior:**
Plugin paths from `discover_plugins()` are not validated for path traversal attacks. A malicious manifest could potentially reference `../../../etc/passwd` or similar.

**Code Locations to Protect:**
- `/Users/pbrown/code/helixscreen-plugins/src/plugin/plugin_manager.cpp` - `find_library()` (line 601)
- `/Users/pbrown/code/helixscreen-plugins/src/plugin/plugin_api.cpp` - `register_xml_component()` (line 266)

**Recommended Implementation:**
```cpp
#include <filesystem>

bool is_path_contained(const std::string& base_dir, const std::string& path) {
    namespace fs = std::filesystem;

    // Resolve both paths to absolute canonical form
    fs::path base = fs::canonical(fs::absolute(base_dir));
    fs::path target = fs::weakly_canonical(fs::absolute(path));

    // Check that target starts with base
    auto [base_end, target_it] = std::mismatch(
        base.begin(), base.end(), target.begin(), target.end()
    );

    return base_end == base.end();
}

// Use before dlopen() or file operations:
std::string library_path = find_library(plugin_dir, plugin_id);
if (!library_path.empty() && !is_path_contained(plugins_dir_, library_path)) {
    add_error(plugin_id, PluginError::Type::LOAD_FAILED,
              "Library path escapes plugin directory");
    return false;
}
```

**Attack Vectors Mitigated:**
1. Manifest specifying `entry_point: "../../../bin/malware"`
2. XML component path like `"../../core/secrets.xml"`
3. Symlink attacks within plugin directory

**Why Not Critical:**
- Plugins are trusted code (same as VS Code extensions, Klipper macros)
- User must manually install plugins
- Not a remote attack vector

---

### 3. Callback Lifetime Edge Cases

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Medium |
| **Risk** | Low - already mitigated by alive_flag_ pattern |

**Current Mitigation:**
The `alive_flag_` pattern in `PluginAPI` (shared_ptr<bool>) prevents use-after-free in Moonraker callbacks.

**Code Location:**
- `/Users/pbrown/code/helixscreen-plugins/include/plugin_api.h` line 415
- `/Users/pbrown/code/helixscreen-plugins/src/plugin/plugin_api.cpp` lines 22, 97-104, 334-341, 365-369

**How It Works:**
```cpp
// In constructor:
alive_flag_(std::make_shared<bool>(true))

// In subscribe_moonraker():
std::weak_ptr<bool> weak_alive = alive_flag_;
client->register_notify_update([callback, weak_alive](const json& update) {
    auto alive = weak_alive.lock();
    if (!alive || !*alive) {
        return;  // Plugin unloaded, skip callback
    }
    // Process update...
});

// In cleanup():
*alive_flag_ = false;  // Invalidate all callbacks
```

**Edge Cases Still Theoretically Possible:**
1. **Race condition window:** Between `weak_alive.lock()` succeeding and callback completing, plugin could unload
2. **Moonraker event during unload:** Event arrives after `cleanup()` starts but before `dlclose()`

**Why Acceptable:**
- The window is extremely small (microseconds)
- `ui_async_call()` marshals to main thread where unload also happens
- No crashes observed in testing

**If Enhancement Needed:**
- Add reference counting for in-flight callbacks
- Use a mutex/condition variable to wait for callbacks to complete during unload
- Complexity likely not worth the edge case coverage

---

### 4. Performance: O(V*E) Topological Sort

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Small |
| **Risk** | None - only affects startup with 50+ plugins |

**Current Implementation:**
The `build_load_order()` function in `PluginManager` uses Kahn's algorithm but with a suboptimal inner loop.

**Code Location:**
- `/Users/pbrown/code/helixscreen-plugins/src/plugin/plugin_manager.cpp` lines 514-599

**Current Complexity Analysis:**
```cpp
// Lines 564-572: For each plugin removed from ready queue,
// scan ALL plugins to find dependents
for (const auto& [other_id, other_deps] : deps) {
    if (std::find(other_deps.begin(), other_deps.end(), id) != other_deps.end()) {
        in_degree[other_id]--;
        // ...
    }
}
```

- V = number of plugins
- E = total dependency edges
- Current: O(V * V) for the outer scan, O(E) for find operations = O(V^2 + V*E)

**Optimized Implementation:**
```cpp
bool PluginManager::build_load_order(std::vector<std::string>& load_order) {
    // Build reverse dependency map: who depends on me?
    std::unordered_map<std::string, std::vector<std::string>> dependents;
    std::unordered_map<std::string, int> in_degree;

    for (const auto& [id, info] : discovered_) {
        if (!info.enabled) continue;
        in_degree[id] = 0;
        for (const auto& dep_id : info.manifest.dependencies) {
            dependents[dep_id].push_back(id);  // dep_id -> id depends on it
            in_degree[id]++;
        }
    }

    std::queue<std::string> ready;
    for (const auto& [id, degree] : in_degree) {
        if (degree == 0) ready.push(id);
    }

    while (!ready.empty()) {
        std::string id = ready.front();
        ready.pop();
        load_order.push_back(id);

        // O(1) lookup + iterate only actual dependents
        for (const auto& dependent_id : dependents[id]) {
            if (--in_degree[dependent_id] == 0) {
                ready.push(dependent_id);
            }
        }
    }

    return load_order.size() == in_degree.size();
}
```

**Optimized Complexity:** O(V + E) - optimal for topological sort

**Why Not Critical:**
- Typical plugin count: 1-10
- Even at 50 plugins with 100 dependencies, current impl is <1ms
- Only runs once at startup

---

## Future Feature Ideas

These are enhancement ideas that may be valuable for future versions.

### Filter System

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Medium (~200 lines) |
| **Status** | Documented, not implemented |

**Concept:**
Allow plugins to intercept and modify data, not just observe. Events are fire-and-forget; filters allow transformation.

**Example Use Cases:**
1. **G-code preprocessing** - Modify commands before sending to printer
2. **Temperature adjustment** - Apply calibration offsets
3. **Notification filtering** - Suppress or modify certain notifications

**Proposed API:**
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

**Implementation Notes:**
- New `FilterManager` class similar to `EventDispatcher`
- Filters run synchronously in registration order
- Priority system for ordering (optional)
- Thread safety: filters should run on main thread

**When to Implement:**
- When a real plugin needs this capability
- Currently no identified use case that can't use events

---

### AMS Extraction to Plugin

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Large (significant refactor) |
| **Status** | Future consideration, post-1.0 |

**Concept:**
Move AMS (Automatic Material System / filament management) support from core into an optional plugin. Not all printers have AMS hardware.

**Benefits:**
- Reduces core complexity and binary size
- Faster startup for non-AMS printers
- AMS updates independent of core releases
- Reference implementation for complex plugins

**Challenges:**
- AMS is deeply integrated (UI panels, printer state, Moonraker subscriptions)
- Would need stable internal APIs for plugin hooks
- Testing matrix increases

**Migration Path:**
1. Create AMS plugin with current functionality
2. Add feature flag to disable core AMS
3. Deprecate core AMS over 2-3 releases
4. Remove core AMS code

---

### Plugin Hot-Reload

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Large |
| **Status** | Not planned |

**Concept:**
Reload plugins without restarting the application.

**Challenges:**
- LVGL widgets must be cleaned up properly
- Subjects and observers need careful lifecycle management
- Service dependencies between plugins
- State preservation across reload
- `dlclose()` doesn't guarantee library is unloaded if references exist

**Recommendation:**
Current approach (restart app after plugin changes) is simple and reliable. Hot-reload complexity is not justified for the development convenience it provides.

---

### Plugin Marketplace

| Attribute | Value |
|-----------|-------|
| **Priority** | Very Low |
| **Effort** | Large (infrastructure required) |
| **Status** | Dream feature |

**Concept:**
In-app discovery and installation of community plugins.

**Requirements:**
- Plugin registry/catalog service (backend infrastructure)
- Code signing for security
- Version compatibility matrix
- Update notifications
- Secure download mechanism

**Simpler Alternatives:**
1. Document manual installation process (done in PLUGIN_DEVELOPMENT.md)
2. Curated list of community plugins in docs or wiki
3. GitHub releases for distribution
4. CLI tool for plugin installation

---

### Plugin Sandboxing

| Attribute | Value |
|-----------|-------|
| **Priority** | Low |
| **Effort** | Very Large |
| **Status** | Not planned |

**Current Model:**
Plugins are trusted code with full access to the application. This is similar to VS Code extensions, Klipper macros, OctoPrint plugins, etc.

**If Sandboxing Were Needed:**
- Separate process with IPC (significant performance overhead)
- Capability-based permissions system
- Restricted file system access
- Would fundamentally change plugin architecture

**Recommendation:**
Keep trust-based model. Document security considerations for users installing third-party plugins. The user base (3D printer enthusiasts) is accustomed to this model from Klipper, OctoPrint, etc.

---

## Known Limitations

These are documented limitations that are accepted for the current design.

### 1. Moonraker Subscription Persistence

**Limitation:** Once a plugin registers a Moonraker subscription via `MoonrakerClient::register_notify_update()`, the subscription cannot be removed until the client is destroyed.

**Code Reference:**
- `/Users/pbrown/code/helixscreen-plugins/src/plugin/plugin_api.cpp` lines 148-155

**Impact:**
- Calling `unsubscribe_moonraker()` marks the subscription as removed in plugin tracking
- The `alive_flag_` pattern ensures callbacks are skipped after unsubscribe
- Actual WebSocket subscription persists until reconnect

**Mitigation:**
- Callbacks are skipped (no wasted processing beyond the skip check)
- Subscription is cleared on Moonraker reconnect
- For long-running sessions, this is minor overhead

### 2. Subject Name Collisions

**Limitation:** Subjects are registered globally. Two plugins using the same subject name will conflict.

**Convention:**
- Always prefix with plugin ID: `"my-plugin.subject_name"`
- Document in PLUGIN_DEVELOPMENT.md

**No Technical Enforcement:**
- Adding namespace enforcement would complicate the API
- Convention is sufficient for community plugins

### 3. Single Injection Point Instance

**Limitation:** Each injection point can only exist once in the UI at a time.

**Impact:**
- Cannot inject the same widget into a point that appears on multiple panels simultaneously
- Injected widgets are removed when navigating away from the panel

**Acceptable Because:**
- Matches HelixScreen's single-panel navigation model
- Plugins can re-inject on `NAVIGATION_CHANGED` events

---

## Changelog

| Date | Author | Changes |
|------|--------|---------|
| 2025-01 | Initial | Created from code review items and PLUGIN_IDEAS.md |

---

*This document should be updated when items are addressed or new follow-up items are identified.*
