# Logging Guidelines

This document defines the logging standards for HelixScreen. All new code should follow these patterns; existing code will be migrated incrementally.

## Log Levels

| Level | CLI Flag | Purpose | Examples |
|-------|----------|---------|----------|
| **ERROR** | (always) | Unrecoverable failures | "Failed to connect", "NULL pointer" |
| **WARN** | (always) | Recoverable issues, guards | "Double init detected", "Fallback used" |
| **INFO** | `-v` | User-visible milestones | "Connected to Moonraker", "Setup complete" |
| **DEBUG** | `-vv` | Troubleshooting info | Component init summaries, state changes |
| **TRACE** | `-vvv` | Wire-level details | Per-item loops, JSON-RPC protocol, observer plumbing |

## When to Use Each Level

### ERROR
- Unrecoverable failures that require user action
- NULL pointer dereferences (guarded)
- Missing critical configuration
- Failed operations that cannot proceed

### WARN
- Recoverable issues with automatic fallbacks
- Guard checks (double initialization, missing optional components)
- Deprecated API usage
- Situations that might indicate a problem but don't prevent operation

### INFO
Use INFO sparingly for **milestones the user cares about**:
- System startup completion: "HelixScreen UI Prototype"
- Connection events: "✓ Connected to Moonraker"
- Discovery summaries: "Capabilities: QGL, bed_mesh, chamber_sensor"
- Panel setup completion: "[Home Panel] Setup complete!"
- Major operations: "File list updated: 126 G-code files"

**NOT INFO**: Internal initialization, backend selection, per-item processing

### DEBUG
Use DEBUG for **troubleshooting information**:
- Component initialization summaries: "Subjects initialized"
- Configuration details: "Target: 800x480, DPI: 160"
- State change summaries: "Printer connection state changed: Connected"
- Batch operation summaries: "Auto-registered 21 theme-aware color pairs"
- Discovery details: "Detected probe: probe"

**NOT DEBUG**: Per-item details in loops, wire protocol, observer plumbing

### TRACE
Use TRACE for **deep debugging only**:
- Per-item processing in loops: "Registering color graph_bg: selected=#2D2D2D"
- Wire protocol: "send_jsonrpc: {...}", "Registered request 5 for method X"
- Observer/callback registration: "Registering observer on X at 0x..."
- Subject value changes: "Subject value now: 2"
- Per-widget creation: "Created widget for slot 3"

## Message Format

### Prefix Standard: `[ComponentName]`

**Correct:**
```cpp
spdlog::debug("[Theme] Auto-registered 21 color pairs");
spdlog::info("[Home Panel] Setup complete!");
spdlog::trace("[Moonraker Client] send_jsonrpc: {}");
```

**Incorrect:**
```cpp
spdlog::debug("Theme: Auto-registered 21 color pairs");  // NO colon
spdlog::info("[Home Panel]: Setup complete!");           // NO double colon
spdlog::debug("Home Panel] Setup complete!");            // Missing bracket
```

### Prefix Naming Rules

| Pattern | Use For | Example |
|---------|---------|---------|
| `[ClassName]` | Classes | `[MoonrakerClient]`, `[PrinterState]` |
| `[Feature Name]` | Multi-word features | `[Home Panel]`, `[Print Select Panel]` |
| `[Subsystem]` | Subsystems | `[Theme]`, `[AMS AFC]` |

### Message Content

- **Be specific**: Include relevant data (counts, states, identifiers)
- **Be consistent**: Use the same wording for similar events
- **Be actionable**: For errors/warnings, include what went wrong

**Good:**
```cpp
spdlog::info("[PrinterState] Initialized 6 fans (version 1)");
spdlog::debug("[Controls Panel] Populated 3 secondary fans");
spdlog::trace("[Theme] Registering spacing space_lg: selected=16");
```

**Bad:**
```cpp
spdlog::info("fans initialized");  // No context
spdlog::debug("Done");             // Not actionable
```

## Loop Logging Pattern

When logging inside loops, use TRACE for per-item and DEBUG for summaries:

```cpp
int registered = 0;
for (const auto& item : items) {
    spdlog::trace("[Theme] Registering {}: value={}", item.name, item.value);
    // ... registration logic ...
    registered++;
}
spdlog::debug("[Theme] Auto-registered {} items", registered);
```

## Common Patterns

### Initialization Guards
```cpp
if (initialized_) {
    spdlog::warn("[MyComponent] init_subjects() called twice - ignoring");
    return;
}
// ... initialization ...
spdlog::debug("[MyComponent] Subjects initialized");
```

### Error Handling
```cpp
if (!ptr) {
    spdlog::error("[MyComponent] Cannot process: NULL pointer");
    return;
}
```

### Milestone Completion
```cpp
// After significant setup work
spdlog::info("[MyComponent] Setup complete!");
```

### Wire Protocol
```cpp
spdlog::trace("[Moonraker Client] send_jsonrpc: {}", rpc.dump());
spdlog::trace("[Moonraker Client] Registered request {} for method {}", id, method);
```

## Implementation Notes

- Use `spdlog` exclusively (not `printf`, `std::cout`, or `LV_LOG_*`)
- Include timestamps automatically via spdlog configuration
- For errors that should notify the user, use `NOTIFY_ERROR()` macro

## Related Documentation

- `CONTRIBUTING.md` - Code standards
- `DEVELOPMENT.md` - Build and debug workflow
- `CLAUDE.md` - AI assistant rules (includes verbosity flags)
