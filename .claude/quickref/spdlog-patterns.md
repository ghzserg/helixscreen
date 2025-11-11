# Logging Quick Reference (spdlog)

**Ultra-condensed guide for spdlog usage. For complete patterns, see `CONTRIBUTING.md`**

---

## ⚠️ CRITICAL RULE

**NEVER use:** `printf()`, `std::cout`, `std::cerr`, `LV_LOG_USER()`, `LV_LOG_WARN()`, etc.

**ALWAYS use:** `spdlog` functions exclusively

---

## Log Levels

| Level | Use Case | Example | When to Use |
|-------|----------|---------|-------------|
| `trace()` | Very detailed debugging | `spdlog::trace("Entering function: {}", __func__)` | `-vvv` flag, development only |
| `debug()` | Development debugging | `spdlog::debug("Variable x={}, y={}", x, y)` | `-vv` flag, detailed diagnostics |
| `info()` | Normal operational events | `spdlog::info("Connection established to {}", host)` | `-v` flag, important events |
| `warn()` | Recoverable issues | `spdlog::warn("Retrying connection, attempt {}", n)` | Always shown, potential problems |
| `error()` | Failures and errors | `spdlog::error("Failed to connect: {}", err)` | Always shown, critical failures |

---

## Format String Syntax (fmt library)

| Pattern | Example | Output |
|---------|---------|--------|
| `{}` | `spdlog::info("Value: {}", 42)` | `Value: 42` |
| `{:.2f}` | `spdlog::info("Temp: {:.2f}°C", 23.456)` | `Temp: 23.46°C` |
| `{:#x}` | `spdlog::info("Color: {:#x}", 0xFF0000)` | `Color: 0xff0000` |
| `{:04d}` | `spdlog::info("ID: {:04d}", 42)` | `ID: 0042` |

---

## Common Patterns

### Basic Logging
```cpp
#include <spdlog/spdlog.h>

spdlog::info("Application started");
spdlog::warn("Config file not found, using defaults");
spdlog::error("Failed to initialize: {}", error_msg);
```

### With Context Tags
```cpp
spdlog::info("[WiFi] Scanning for networks...");
spdlog::debug("[WiFi] Found {} networks", count);
spdlog::error("[WiFi] Connection failed: {}", reason);
```

### Multiple Arguments
```cpp
spdlog::info("Connected to {} on port {} ({}ms latency)",
             host, port, latency);
```

### Conditional Logging
```cpp
if (verbose) {
    spdlog::debug("Detailed state: temp={}, pressure={}", t, p);
}
```

---

## Verbosity Control

**Command-line flags:**
- *(default)* - Only `warn()` and `error()` shown
- `-v` - Show `info()`, `warn()`, `error()`
- `-vv` - Show `debug()`, `info()`, `warn()`, `error()`
- `-vvv` - Show all including `trace()`

**In code (main.cpp):**
```cpp
// Set based on command-line flags
if (verbose_level >= 3) spdlog::set_level(spdlog::level::trace);
else if (verbose_level == 2) spdlog::set_level(spdlog::level::debug);
else if (verbose_level == 1) spdlog::set_level(spdlog::level::info);
else spdlog::set_level(spdlog::level::warn);
```

---

## Why spdlog?

| Feature | Benefit |
|---------|---------|
| **Configurable verbosity** | Users control detail level without recompiling |
| **Consistent formatting** | All logs follow same pattern |
| **Performance** | Fast, minimal overhead |
| **Type-safe** | Compile-time format string checking |

---

## Migration from printf/cout

```cpp
// ❌ BEFORE
printf("[Temp] Temperature: %d°C\n", temp);
std::cout << "Error: " << msg << std::endl;
LV_LOG_USER("Panel initialized");

// ✅ AFTER
spdlog::info("[Temp] Temperature: {}°C", temp);
spdlog::error("Error: {}", msg);
spdlog::info("Panel initialized");
```

---

**Reference:** CONTRIBUTING.md "Logging Requirements"
**Pre-commit check:** `.claude/hooks/pre-commit-check.sh` catches printf/cout usage
