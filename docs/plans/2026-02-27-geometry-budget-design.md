# Geometry Budget System Design

## Problem

The 3D geometry builder has no resource limits. Complex gcode files (500K+ segments)
can generate 500MB-1GB of geometry data, causing OOM on memory-constrained devices
(Pi4 1GB, BTT Pi, AD5M). A file called FDM_Geo_Rounded nearly killed a Pi 5 4GB.

## Hardware Reality

- Pi 5 4GB: ~3.7GB available → comfortable
- Pi 4 2GB: ~800MB available → tight
- Pi 4 1GB / BTT Pi: ~200-400MB available → critical
- AD5M: ~300MB available → tight

After OS + Klipper + Moonraker + LVGL + textures, geometry budget must be conservative.

## Design

### Memory Budget Calculation

```
available_mb = /proc/meminfo MemAvailable
geometry_budget = min(available_mb * 0.25, 256MB)
```

- 25% of MemAvailable adapts to actual system state
- 256MB hard cap prevents waste even on 8GB devices
- On 1GB Pi with 300MB free → 75MB budget
- On 4GB Pi with 3.7GB free → 256MB budget (capped)

### Quality Tiers

| Tier | tube_sides | Simplification | Travels | ~Bytes/seg | Description |
|------|-----------|----------------|---------|------------|-------------|
| 1 | 16 | default (0.01mm) | yes | ~1100 | Full quality |
| 2 | 8 | moderate (0.1mm) | yes | ~550 | Medium quality |
| 3 | 4 | aggressive (0.5mm) | no | ~280 | Low quality |
| 4 | — | — | — | — | 2D layer renderer |
| 5 | — | — | — | — | Thumbnail + metadata |

### Pre-flight Tier Selection (after parsing, before building)

```
segment_count = parsed_gcode.total_segments
// Apply 0.7 factor for expected simplification reduction
estimated_n16 = segment_count * 1100 * 0.7
estimated_n8  = segment_count * 550 * 0.7
estimated_n4  = segment_count * 280 * 0.7

if estimated_n16 < budget → Tier 1
elif estimated_n8 < budget → Tier 2
elif estimated_n4 < budget → Tier 3
elif estimated_n4 < budget*2 → Tier 3 with extra-aggressive simplification
else → Tier 4 (2D)
```

### Progressive Budget Check (during building)

Every 5000 segments during `GeometryBuilder::build()`:
1. Check `geometry.memory_usage()` against budget
2. If > 90% of budget:
   - If N=16 or N=8: degrade to N=4 in-place (no restart)
   - If already N=4: abort, signal fallback to 2D
3. Every 20K segments: also check `/proc/meminfo` MemAvailable
   - If system MemAvailable < 100MB: abort regardless of geometry budget

Key insight: **never restart a build**. Variable quality within a model (top layers
at lower quality) is better than wasting a full build pass on slow CPUs.

### System Memory Safety Net

Independent of geometry budget, monitor actual system memory during build:
- MemAvailable < 100MB → emergency abort, fall to Tier 4/5
- This catches cases where other processes (Klipper, Moonraker) spike memory usage
  during a geometry build

### No User Override

Safety is absolute. The system picks the best tier it can afford. Users on capable
hardware get full quality automatically; users on constrained hardware get graceful
degradation. No "render anyway" footgun.

### UI Feedback

- Tiers 1-3: No notification, user sees 3D preview at whatever quality fits
- Tier 4: Toast "Complex model — showing 2D layer view"
- Tier 5: Toast "Model too complex for preview"
- No change to loading spinner behavior

## Architecture

### New: `GeometryBudgetManager` class

Location: `include/geometry_budget_manager.h`, `src/rendering/geometry_budget_manager.cpp`

```cpp
class GeometryBudgetManager {
public:
    struct SystemMemory {
        size_t total_kb;
        size_t available_kb;
    };

    struct BudgetConfig {
        size_t budget_bytes;           // Geometry memory ceiling
        int tier;                      // Selected quality tier (1-5)
        int tube_sides;                // 4, 8, or 16
        float simplification_tolerance; // mm
        bool include_travels;
    };

    // Read /proc/meminfo (or mock for testing)
    SystemMemory assess_system_memory() const;

    // Calculate geometry budget from available memory
    size_t calculate_budget(size_t available_kb) const;

    // Select quality tier based on segment count and budget
    BudgetConfig select_tier(size_t segment_count, size_t budget_bytes) const;

    enum class BudgetAction { CONTINUE, DEGRADE, ABORT };

    // Check during build: should we continue, degrade, or abort?
    BudgetAction check_budget(size_t current_usage_bytes,
                              size_t budget_bytes,
                              int current_tier) const;

    // System-level safety: is the machine running out of memory?
    bool is_system_memory_critical() const;
};
```

### Modified: `GeometryBuilder::build()`

Add optional `BudgetConfig` parameter. Every 5K segments, call budget check.
If DEGRADE: change tube_sides mid-build. If ABORT: return partial geometry + abort flag.

### Modified: `ui_gcode_viewer.cpp`

Before calling `builder.build()`:
1. Instantiate `GeometryBudgetManager`
2. `assess_system_memory()` + `calculate_budget()`
3. `select_tier(segment_count, budget)`
4. If tier >= 4: skip geometry build, use 2D renderer or thumbnail
5. If tier 1-3: pass `BudgetConfig` to builder, handle abort → fallback

## Testing Strategy (TDD)

### Unit Tests (GeometryBudgetManager)

- `calculate_budget()` with various MemAvailable values (1GB, 2GB, 4GB, 8GB)
- `calculate_budget()` respects 256MB hard cap
- `select_tier()` picks correct tier for various segment counts
- `select_tier()` boundary cases (exactly at tier thresholds)
- `check_budget()` returns CONTINUE when under budget
- `check_budget()` returns DEGRADE when over 90% at high tier
- `check_budget()` returns ABORT when over 90% at lowest tier
- `is_system_memory_critical()` with mocked /proc/meminfo

### Integration Tests (GeometryBuilder + Budget)

- Build with budget that allows full quality → verify N=16 throughout
- Build with budget that forces degradation mid-build → verify N changes
- Build with budget that forces abort → verify partial geometry returned
- Build with tiny budget → verify immediate Tier 4/5 selection

### Memory estimation accuracy tests

- Parse known gcode files, estimate memory, build geometry, compare estimate vs actual
- Verify estimation is conservative (never under-estimates by > 30%)
