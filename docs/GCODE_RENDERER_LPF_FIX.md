# Fix: G-Code Renderer Blocking Main Thread on Constrained Devices

**Date**: 2024-12-24
**Status**: Implementing
**Issue**: UI freeze for 141 seconds when opening print status panel on AD5M

## Problem

On AD5M (Cortex-A7, 128MB RAM), the G-code layer renderer blocked the main thread for **141 seconds** trying to render 30,701 segments in a single frame. This caused the UI to freeze mid-transition to the print status panel.

**Root Cause**: The adaptive "layers per frame" (lpf) algorithm spirals from 15 → 63 in ~5 frames due to:
1. Aggressive 2x growth cap per frame
2. No device-aware limits (MAX_LAYERS_PER_FRAME = 100 for all devices)
3. Adaptation happens AFTER render completes (too late to prevent blocking)

### Evidence from Logs

```
[2025-12-24 17:06:07.278] Adaptive lpf: 63 -> 1 (render=141505ms, target=16ms)
```

The renderer tried to render 63 layers (30,701 segments) targeting 16ms but took 141 seconds.

---

## Solution: Device-Aware LPF Limits

Add constrained device detection using existing `memory_utils::is_constrained_device()` and apply conservative limits.

### Changes to `src/gcode_layer_renderer.cpp`

**1. Add include** (near top):
```cpp
#include "memory_utils.h"
```

**2. Add device-aware constants** (around line 515):
```cpp
// Constrained device limits (AD5M, low-RAM embedded)
static constexpr int CONSTRAINED_START_LPF = 5;
static constexpr int CONSTRAINED_MAX_LPF = 15;
static constexpr float CONSTRAINED_GROWTH_CAP = 1.3f;  // vs 2.0f normal
```

**3. Detect device tier in `load_config()`** (around line 1314):
```cpp
// Detect device tier and apply appropriate limits
auto mem_info = memory_utils::get_system_memory_info();
is_constrained_device_ = mem_info.is_constrained_device();

if (is_constrained_device_) {
    max_layers_per_frame_ = CONSTRAINED_MAX_LPF;
    if (config_layers_per_frame_ == 0) {  // Adaptive mode
        layers_per_frame_ = CONSTRAINED_START_LPF;
    }
    spdlog::info("[GCodeLayerRenderer] Constrained device: lpf capped at {}", max_layers_per_frame_);
}
```

**4. Modify `adapt_layers_per_frame()`** (around line 1363):
```cpp
// Use conservative growth on constrained devices
float max_growth = is_constrained_device_ ? CONSTRAINED_GROWTH_CAP : 2.0f;
ratio = std::min(ratio, max_growth);
```

**5. Clamp using device-aware max** (around line 1385):
```cpp
int max_lpf = is_constrained_device_ ? CONSTRAINED_MAX_LPF : MAX_LAYERS_PER_FRAME;
layers_per_frame_ = std::clamp(layers_per_frame_, MIN_LAYERS_PER_FRAME, max_lpf);
```

### Changes to `include/gcode_layer_renderer.h`

**Add member variables:**
```cpp
bool is_constrained_device_ = false;
int max_layers_per_frame_ = MAX_LAYERS_PER_FRAME;  // Device-adjusted
```

---

## Expected Behavior After Fix

| Metric | Before (AD5M) | After (AD5M) | Desktop (unchanged) |
|--------|---------------|--------------|---------------------|
| Start lpf | 15 | 5 | 15 |
| Max lpf | 100 | 15 | 100 |
| Growth cap | 2.0x/frame | 1.3x/frame | 2.0x/frame |
| Worst case render | 141 seconds | ~2-3 seconds | N/A |
| Frame rate during cache | Frozen | Slightly choppy but responsive | Smooth |

---

## Device Tier Detection

The fix uses existing infrastructure from `memory_utils.h`:

```cpp
// RAM tier thresholds (total system RAM)
static constexpr size_t TIER_CONSTRAINED_KB = 256 * 1024; // < 256MB = constrained
static constexpr size_t TIER_NORMAL_KB = 512 * 1024;      // < 512MB = normal

bool is_constrained_device() const {
    return total_kb < TIER_CONSTRAINED_KB;
}
```

**AD5M specs**: Cortex-A7, ~128MB RAM → `is_constrained_device() == true`

---

## Files Modified

1. `src/gcode_layer_renderer.cpp` - Add device detection, constrained limits
2. `include/gcode_layer_renderer.h` - Add member variables

---

## Testing

1. Build for AD5M: `make ad5m-docker`
2. Deploy: `make deploy-ad5m AD5M_HOST=ad5m-pc.lan`
3. Run with trace logging: `./helix-screen -vvv`
4. Navigate to print status with active print
5. Verify UI remains responsive (no multi-second freezes)
6. Check logs for: `[GCodeLayerRenderer] Constrained device: lpf capped at 15`

---

## Future Improvements (Not in this fix)

- **Yield during long renders**: Break out of draw callback every 50ms to let LVGL breathe
- **Per-layer complexity check**: Skip layers with >5000 segments in single frame
- **Background caching**: Pre-render layers on background thread
