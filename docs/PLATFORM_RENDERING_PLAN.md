# Platform-Specific Rendering Architecture

## Overview

Implement platform-appropriate rendering for HelixScreen based on hardware capabilities:
- **Raspberry Pi**: Full LVGL OpenGL ES rendering (GPU-accelerated), 3D visualizations
- **Adventurer 5M**: Disable TinyGL, NEON-optimized 2D rendering, **2D heatmap + layer view**
- **Desktop (SDL)**: Unchanged (TinyGL continues to work)

## Background

AD5M has Allwinner R528 SoC (Dual Cortex-A7, **NO GPU**, 37MB free RAM). TinyGL produces 3-4 FPS - unusable.

## Architecture Summary

```
┌─────────────────────────────────────────────────────┐
│                   LVGL 9.x UI                       │
└─────────────────────┬───────────────────────────────┘
                      │
        ┌─────────────┼─────────────┐
        │             │             │
        ▼             ▼             ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│   OpenGL ES  │ │  2D Fallback │ │   TinyGL     │
│  (Pi + GPU)  │ │   (AD5M)     │ │   (Desktop)  │
├──────────────┤ ├──────────────┤ ├──────────────┤
│ • 3D bed mesh│ │ • 2D heatmap │ │ • 3D preview │
│ • 3D g-code  │ │ • 2D layers  │ │ • Dev/test   │
│ • 30+ FPS    │ │ • 15+ FPS    │ │              │
└──────────────┘ └──────────────┘ └──────────────┘
```

## Git Setup & Plan Persistence

**IMPORTANT**: This plan lives in `docs/PLATFORM_RENDERING_PLAN.md` in the worktree for cross-session reference.

```bash
# Step 1: Create feature branch and worktree
git checkout -b feature/platform-render-architecture
git worktree add ../helixscreen-render feature/platform-render-architecture

# Step 2: Copy this plan to docs/ for persistence
cd ../helixscreen-render
cp ~/.claude/plans/lucky-marinating-thimble.md docs/PLATFORM_RENDERING_PLAN.md
git add docs/PLATFORM_RENDERING_PLAN.md
git commit -m "docs: add platform rendering architecture plan"

# Step 3: Work from the worktree
# All implementation happens here, updating the plan after each phase
```

### Plan Update Workflow

After completing each phase:
1. Update `docs/PLATFORM_RENDERING_PLAN.md` with status
2. Mark completed items with ✅
3. Add any learnings or adjustments
4. Commit the updated plan

### Code Review Requirements

**CRITICAL**: Run DETAILED, COMPREHENSIVE code reviews at each stage:
- Check for project-specific patterns (see CLAUDE.md rules)
- Verify modularity and maintainability
- Ensure new code follows existing backend patterns (Manager → Interface → Implementation)
- Use `critical-reviewer` agent for significant new code
- Document any deviations from existing patterns with justification

---

## Phase 1: AD5M - Disable TinyGL (Simple)

### 1.1 Build System Change

**File: `mk/cross.mk` (line 69)**
```diff
- ENABLE_TINYGL_3D := yes
+ ENABLE_TINYGL_3D := no
```

### 1.2 Enable LVGL NEON Blending

**File: `lv_conf.h` (line 184)**
```diff
- #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE
+ /* Enable NEON SIMD for ARM platforms with NEON support */
+ #if defined(__ARM_NEON) || defined(__ARM_NEON__)
+     #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NEON
+ #else
+     #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE
+ #endif
```

### 1.3 Verification
```bash
make ad5m-docker
# Check no TinyGL symbols: nm build/ad5m/bin/helix-screen | grep -i tinygl
# Deploy and verify 2D G-code preview works
```

---

## Phase 2: Raspberry Pi - OpenGL ES Integration (Complex)

### 2.1 Docker Toolchain Update

**File: `docker/Dockerfile.pi`** - Add OpenGL ES libraries:
```dockerfile
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends \
    # ... existing packages ...
    libgbm-dev:arm64 \
    libgles2-mesa-dev:arm64 \
    libegl1-mesa-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*
```

### 2.2 Build System Changes

**File: `mk/cross.mk`** - Add OpenGL ES flag for Pi (after line 34):
```makefile
ifeq ($(PLATFORM_TARGET),pi)
    # ... existing settings ...
    ENABLE_OPENGLES := yes
endif
```

**File: `mk/cross.mk`** - Add defines (around line 166, after display backend section):
```makefile
# OpenGL ES support for GPU-accelerated rendering
ifeq ($(ENABLE_OPENGLES),yes)
    CFLAGS += -DHELIX_ENABLE_OPENGLES
    CXXFLAGS += -DHELIX_ENABLE_OPENGLES
    SUBMODULE_CFLAGS += -DHELIX_ENABLE_OPENGLES
    SUBMODULE_CXXFLAGS += -DHELIX_ENABLE_OPENGLES
endif
```

**File: `Makefile`** - Add linker flags for Pi (in cross-compile section):
```makefile
ifeq ($(PLATFORM_TARGET),pi)
    ifeq ($(ENABLE_OPENGLES),yes)
        LDFLAGS += -lGLESv2 -lEGL -lgbm
    endif
endif
```

### 2.3 LVGL Configuration

**File: `lv_conf.h`** - Enable DRM+EGL and OpenGL ES draw backend:

Near the DRM section (around line 1078):
```c
#ifdef HELIX_DISPLAY_DRM
    #define LV_USE_LINUX_DRM        1

    /* Enable EGL/OpenGL ES on Pi for GPU acceleration */
    #ifdef HELIX_ENABLE_OPENGLES
        #define LV_LINUX_DRM_USE_EGL         1
        #define LV_USE_LINUX_DRM_GBM_BUFFERS 1
    #else
        #define LV_LINUX_DRM_USE_EGL         0
    #endif
#else
    #define LV_USE_LINUX_DRM        0
#endif
```

Near the draw backends section (after LV_USE_DRAW_SDL):
```c
/* Use OpenGL ES draw backend for Pi GPU acceleration */
#ifdef HELIX_ENABLE_OPENGLES
    #define LV_USE_DRAW_OPENGLES 1
    #if LV_USE_DRAW_OPENGLES
        #define LV_DRAW_OPENGLES_TEXTURE_CACHE_COUNT 64
    #endif
#else
    #define LV_USE_DRAW_OPENGLES 0
#endif
```

### 2.4 Verification
```bash
# Rebuild Docker toolchain with new libraries
make docker-toolchain-pi

# Cross-compile with OpenGL ES
make pi-docker

# Deploy and test
make deploy-pi-fg
# Look for: "[info] Egl version 1.4" and "[OPENGLES]" in logs
```

---

## Phase 3: Testing & Validation

### Build Matrix
| Target | Command | Expected Result |
|--------|---------|-----------------|
| Native/SDL | `make -j` | TinyGL works, unchanged |
| AD5M | `make ad5m-docker` | No TinyGL, NEON blending enabled |
| Pi | `make pi-docker` | OpenGL ES acceleration |

### Visual Verification
- [ ] Native: G-code 3D preview renders (TinyGL)
- [ ] AD5M: G-code shows 2D layer preview or thumbnail
- [ ] Pi: G-code 3D preview renders with GPU acceleration
- [ ] All platforms: Touch input works
- [ ] All platforms: Dark/light mode switching works

### Performance Targets
| Platform | Before | After |
|----------|--------|-------|
| AD5M G-code | 3-4 FPS (TinyGL) | 15-20 FPS (2D + NEON) |
| Pi UI | 20-30 FPS (SW) | 30-60 FPS (OpenGL ES) |

---

## Phase 4: 2D Bed Mesh Heatmap (Runtime Detection)

**Goal**: Single renderer that auto-detects platform and uses 3D (Pi) or 2D heatmap (AD5M).

### 4.1 Add Platform Detection

**File: `include/platform_capabilities.h`** (new):
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

enum class RenderingCapability {
    HARDWARE_OPENGL,  // Pi with GPU
    SOFTWARE_NEON,    // AD5M with NEON
    SOFTWARE_BASIC    // Fallback
};

RenderingCapability detect_rendering_capability();
bool has_gpu_support();

} // namespace helix
```

### 4.2 Modify Bed Mesh Renderer

**File: `src/bed_mesh_renderer.cpp`** - Add rendering mode:
```cpp
enum class BedMeshRenderMode { MODE_3D, MODE_2D_HEATMAP };

// At render time:
if (helix::has_gpu_support()) {
    render_3d_perspective(...);  // Existing 3D code
} else {
    render_2d_heatmap(...);      // New simplified path
}
```

### 4.3 Implement 2D Heatmap Rendering

**New function in `bed_mesh_renderer.cpp`**:
```cpp
void render_2d_heatmap(lv_layer_t* layer, const BedMeshData& mesh) {
    // Simple grid of colored rectangles
    // - Uniform cell size based on mesh dimensions
    // - Color gradient: blue (low) → green → yellow → red (high)
    // - No perspective, no rotation
    // - Touch shows Z value tooltip
}
```

**Features**:
- Grid of `mesh_width × mesh_height` rectangles
- Color mapped from Z deviation: purple (-) → green (0) → red (+)
- Cell labels showing Z value on touch/hover
- 60+ FPS on AD5M (no 3D math)

### 4.4 Files to Modify

| File | Change |
|------|--------|
| `include/platform_capabilities.h` | New - runtime detection |
| `src/platform_capabilities.cpp` | New - detection implementation |
| `src/bed_mesh_renderer.cpp` | Add `render_2d_heatmap()` function |
| `src/ui_bed_mesh.cpp` | Remove rotation handlers when 2D mode |

**Estimated effort**: 1-2 days

---

## Phase 5: 2D G-code Layer View (Basic)

**Goal**: Top-down orthographic view of single layer with extrusion/travel coloring.

### 5.1 Create Layer Preview Renderer

**File: `include/gcode_layer_renderer.h`** (new):
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class GCodeLayerRenderer {
public:
    void set_gcode(const ParsedGCodeFile* gcode);
    void set_current_layer(int layer);
    void render(lv_layer_t* layer, const lv_area_t* area);

    // Options
    void set_show_travels(bool show);
    void set_extrusion_color(lv_color_t color);
    void set_travel_color(lv_color_t color);

private:
    const ParsedGCodeFile* gcode_ = nullptr;
    int current_layer_ = 0;
    bool show_travels_ = false;

    // Viewport/zoom
    float scale_ = 1.0f;
    float offset_x_ = 0.0f;
    float offset_y_ = 0.0f;

    void compute_layer_bounds();
    void render_segment_2d(lv_layer_t* layer, const ToolpathSegment& seg);
};
```

### 5.2 Rendering Approach

**Top-down orthographic projection**:
```cpp
void GCodeLayerRenderer::render_segment_2d(lv_layer_t* layer, const ToolpathSegment& seg) {
    // Direct X/Y mapping (ignore Z for 2D view)
    int x1 = (seg.start.x - offset_x_) * scale_;
    int y1 = (seg.start.y - offset_y_) * scale_;
    int x2 = (seg.end.x - offset_x_) * scale_;
    int y2 = (seg.end.y - offset_y_) * scale_;

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = seg.is_extrusion ? color_extrusion_ : color_travel_;
    dsc.width = seg.is_extrusion ? 2 : 1;
    dsc.opa = seg.is_extrusion ? LV_OPA_COVER : LV_OPA_50;

    lv_draw_line(layer, &dsc, x1, y1, x2, y2);
}
```

### 5.3 Integrate with UI

**File: `src/ui_gcode_viewer.cpp`** - Add mode switching:
```cpp
// Runtime selection based on platform
if (helix::has_gpu_support()) {
    // Pi: Use existing 3D renderer (TinyGL or OpenGL ES)
    renderer_3d_->render(layer, gcode, camera);
} else {
    // AD5M: Use new 2D layer renderer
    layer_renderer_->set_current_layer(current_layer_);
    layer_renderer_->render(layer, area);
}
```

### 5.4 UI Controls Needed

**Layer slider** (already exists in gcode_test_panel.xml):
- Min: 0, Max: total_layers - 1
- Updates `layer_renderer_->set_current_layer()`

**Layer info display**:
- Current layer number / total layers
- Layer height (Z)
- Segment count

### 5.5 Files to Create/Modify

| File | Change |
|------|--------|
| `include/gcode_layer_renderer.h` | New - 2D layer renderer interface |
| `src/gcode_layer_renderer.cpp` | New - implementation |
| `src/ui_gcode_viewer.cpp` | Add runtime mode selection |
| `ui_xml/gcode_viewer_panel.xml` | Add layer slider (if not present) |

**Estimated effort**: 2-3 days

---

## Phase 6: Image Asset Optimization

### Background

**Critical Discovery**: Benchmark testing revealed that PNG decoding is the primary performance bottleneck on AD5M:
- Controls panel (no images): **116 FPS**
- Home panel (large images): **2 FPS**

The ~500ms per frame is spent decoding and scaling large PNG files at runtime, not rendering UI or pushing pixels to the framebuffer.

### 6.1 Supported Screen Sizes

| Size | Dimensions | Typical Hardware |
|------|------------|------------------|
| TINY | 480×320 | Small SPI displays |
| SMALL | 800×480 | AD5M, budget screens |
| MEDIUM | 1024×600 | Standard touch panels |
| LARGE | 1280×720 | Pi official display, larger screens |

### 6.2 Images Requiring Optimization

**Priority 1 - Splash Screen**:
| Current Image | Current Size | Target Sizes |
|---------------|--------------|--------------|
| `helixscreen-logo.png` | 1.5MB (1024×1024?) | 4 sizes matching screen widths |
| `helixscreen-logo-transparent.png` | 1.5MB | Same |

**Priority 2 - 3D Benchy Placeholder**:
| Current Image | Current Size | Target Sizes |
|---------------|--------------|--------------|
| `thumbnail-placeholder.png` | 192KB | 4 sizes for thumbnail areas |
| `benchy_thumbnail_white.png` | 9.4KB | Already small, may be fine |

**Priority 3 - Printer Images** (when home panel layout is finalized):
| Current Image | Current Size | Notes |
|---------------|--------------|-------|
| `printer.png` | 2.3MB | Full resolution master |
| `printer_400.png` | 107KB | Existing 400px version |
| `printer_200.png` | 36KB | Existing 200px version |

### 6.3 Theme System Auto-Selection

**Goal**: Load appropriately-sized image based on current screen size without code changes.

**Proposed API**:
```cpp
// In ui_theme.h / ui_theme.cpp
std::string ui_theme_get_image_path(const std::string& base_name);

// Usage:
// ui_theme_get_image_path("splash-logo")
// Returns: "assets/images/splash-logo-small.png" (on 800x480)
//          "assets/images/splash-logo-large.png" (on 1280x720)
```

**File Naming Convention**:
```
assets/images/
├── splash-logo-tiny.png     # 480px wide
├── splash-logo-small.png    # 800px wide
├── splash-logo-medium.png   # 1024px wide
├── splash-logo-large.png    # 1280px wide
├── benchy-placeholder-tiny.png
├── benchy-placeholder-small.png
├── benchy-placeholder-medium.png
├── benchy-placeholder-large.png
└── ... (originals kept for reference)
```

**Implementation**:
```cpp
std::string ui_theme_get_image_path(const std::string& base_name) {
    // Get current screen size suffix
    const char* suffix = nullptr;
    switch (g_screen_size) {
        case ScreenSize::TINY:   suffix = "-tiny";   break;
        case ScreenSize::SMALL:  suffix = "-small";  break;
        case ScreenSize::MEDIUM: suffix = "-medium"; break;
        case ScreenSize::LARGE:  suffix = "-large";  break;
    }

    std::string sized_path = "assets/images/" + base_name + suffix + ".png";

    // Fallback to base name if sized version doesn't exist
    if (access(sized_path.c_str(), R_OK) == 0) {
        return sized_path;
    }
    return "assets/images/" + base_name + ".png";
}
```

### 6.4 Build-Time Image Generation

**Script**: `scripts/generate-sized-images.sh`
```bash
#!/bin/bash
# Generate screen-size-specific images from high-res originals

SIZES="tiny:480 small:800 medium:1024 large:1280"

for img in splash-logo benchy-placeholder; do
    for size_spec in $SIZES; do
        name="${size_spec%%:*}"
        width="${size_spec##*:}"

        magick "assets/images/${img}-original.png" \
            -resize "${width}x" \
            -strip \
            "assets/images/${img}-${name}.png"
    done
done
```

### 6.5 Additional Considerations

**G-code Thumbnails**:
- Same principle applies - decode at target size, not full resolution
- Moonraker provides thumbnail URLs with size parameters
- Consider caching decoded thumbnails to avoid repeated decodes

**Print History Thumbnails**:
- Gallery views need small thumbnails (64×64 or 128×128)
- Detail views may need larger versions
- Lazy loading with placeholders improves perceived performance

**Pre-decoded Formats** (future optimization):
- For splash screen (shown during startup), consider raw RGB565 `.bin` format
- Eliminates PNG decode entirely for critical path
- Trade-off: larger files but instant display

### 6.6 Files to Create/Modify

| File | Change |
|------|--------|
| `include/ui_theme.h` | Add `ui_theme_get_image_path()` declaration |
| `src/ui_theme.cpp` | Implement auto-selection logic |
| `scripts/generate-sized-images.sh` | New - build-time image generation |
| `src/splash_screen.cpp` | Use `ui_theme_get_image_path()` |
| `ui_xml/home_panel.xml` | Use generic image names |
| `Makefile` | Add `make regen-images` target |

**Estimated effort**: 1 day

---

## Critical Files Summary

### Phase 1-3 (Build/Config)
| File | Change |
|------|--------|
| `mk/cross.mk:69` | AD5M: `ENABLE_TINYGL_3D := no` |
| `mk/cross.mk:34+` | Pi: Add `ENABLE_OPENGLES := yes` |
| `mk/cross.mk:166+` | Add `HELIX_ENABLE_OPENGLES` defines |
| `lv_conf.h:184` | Conditional NEON ASM |
| `lv_conf.h:1078+` | DRM+EGL configuration |
| `lv_conf.h:238+` | OpenGL ES draw backend |
| `docker/Dockerfile.pi` | Add EGL/GLES/GBM packages |
| `Makefile` | Add -lGLESv2 -lEGL -lgbm for Pi |

### Phase 4 (2D Bed Mesh)
| File | Change |
|------|--------|
| `include/platform_capabilities.h` | New - runtime GPU detection |
| `src/platform_capabilities.cpp` | New - detection implementation |
| `src/bed_mesh_renderer.cpp` | Add `render_2d_heatmap()` |
| `src/ui_bed_mesh.cpp` | Conditional rotation handlers |

### Phase 5 (2D G-code Layer)
| File | Change |
|------|--------|
| `include/gcode_layer_renderer.h` | New - 2D layer renderer |
| `src/gcode_layer_renderer.cpp` | New - implementation |
| `src/ui_gcode_viewer.cpp` | Runtime mode selection |

### Phase 6 (Image Asset Optimization)
| File | Change |
|------|--------|
| `include/ui_theme.h` | Add `ui_theme_get_image_path()` |
| `src/ui_theme.cpp` | Screen-size-based image selection |
| `scripts/generate-sized-images.sh` | New - build-time image generation |
| `src/splash_screen.cpp` | Use themed image paths |
| `Makefile` | Add `make regen-images` target |
| `assets/images/*-{tiny,small,medium,large}.png` | Pre-sized image variants |

---

## Rollback Plan

If issues discovered:
1. **Pre-merge**: Reset feature branch, fix issues
2. **Post-merge**: `git revert <commit-sha>`
3. **Platform-specific**: Create hotfix branch for targeted fix

## Implementation Order

| Phase | Description | Effort | Risk |
|-------|-------------|--------|------|
| 1 | AD5M: Disable TinyGL + NEON | 1-2 hours | Low |
| 2 | Pi: OpenGL ES integration | 2-4 hours | Medium |
| 3 | Testing & validation | 1-2 hours | Low |
| 4 | 2D Bed Mesh Heatmap | 1-2 days | Low |
| 5 | 2D G-code Layer View | 2-3 days | Medium |
| 6 | Image Asset Optimization | 1 day | Low |

**Total estimated effort**: ~1.5 weeks

**Recommended approach**: Complete Phases 1-3 first, verify builds work, then proceed to Phases 4-5. Phase 6 (image optimization) can be done in parallel with 4-5 and provides the biggest AD5M performance improvement.

---

## Status Tracking

Update this section after each phase:

| Phase | Status | Started | Completed | Notes |
|-------|--------|---------|-----------|-------|
| 1 | ✅ Complete | 2025-12-15 | 2025-12-15 | TinyGL disabled, NEON enabled, 2D renderer API compatibility |
| 2 | ⬜ Not Started | - | - | Pi OpenGL ES integration |
| 3 | 🔄 Partial | 2025-12-15 | - | Native/SDL ✅, AD5M ✅, Pi ⬜ (blocked on Phase 2) |
| 4 | ⬜ Not Started | - | - | 2D Bed Mesh Heatmap for AD5M |
| 5 | ⬜ Not Started | - | - | 2D G-code Layer View for AD5M |
| 6 | ✅ Complete | 2025-12-16 | 2025-12-16 | Pre-rendered splash images (~2 FPS → ~116 FPS). See docs/PRE_RENDERED_IMAGES.md |

**Status Legend**: ⬜ Not Started | 🔄 In Progress | ✅ Complete | ⚠️ Blocked

### Session Log

Record progress across sessions:

### Session 2025-12-16
- Phase 6: ✅ Complete (Image Asset Optimization)
- Implementation approach changed from "sized PNGs" to "pre-rendered LVGL binary"
- Changes made:
  - `scripts/LVGLImage.py`: Added `--resize`, `--resize-fit` options using Pillow LANCZOS
  - `scripts/regen_images.sh`: New script for build-time splash image pre-rendering
  - `mk/images.mk`: New Makefile module with platform-specific targets
  - `mk/cross.mk`: Updated deploy/release targets to include image generation
  - `src/splash_screen.cpp`: Added pre-rendered image detection with PNG fallback
  - `src/main.cpp`: Removed duplicate `show_splash_screen()`, now calls `helix::show_splash_screen()`
  - `docs/PRE_RENDERED_IMAGES.md`: Full documentation
- Key decisions:
  - **Platform-aware generation**: AD5M only gets `small` (400px), Pi gets all sizes
  - **Build artifacts**: Generated images in `build/assets/images/prerendered/`, not committed
  - **Fallback mechanism**: PNG + runtime scaling if pre-rendered not found
- Performance result: ~2 FPS → ~116 FPS during splash on AD5M

### Session 2025-12-15 (continued)
- **Image Performance Discovery**: Benchmark proved PNG decoding is the bottleneck
  - Controls panel (no images): **116 FPS** (8.6ms per frame)
  - Home panel (large images): **2 FPS** (500ms per frame)
  - **58x performance difference** - NOT memory bandwidth, NOT LVGL rendering
  - 16bpp vs 32bpp showed no improvement (bottleneck is upstream)
- NEON enabled but showed no benefit (PNG decode is not SIMD-optimized)
- Added automatic framebuffer depth configuration in `display_backend_fbdev.cpp`

### Session 2025-12-15
- Phase 1: ✅ Complete
- Changes made:
  - `mk/cross.mk:69`: Changed `ENABLE_TINYGL_3D := yes` to `no` for AD5M
  - `lv_conf.h:184`: Added conditional NEON ASM (`LV_DRAW_SW_ASM_NEON` when `__ARM_NEON` defined)
  - `include/gcode_renderer.h`: Added `GhostRenderMode` enum, `set_highlighted_objects()`, stub methods for ghost layer API
  - `src/gcode_renderer.cpp`: Updated `render()` signature, added `set_highlighted_objects()` implementation
  - `src/ui_gcode_viewer.cpp`: Wrapped TinyGL-specific code (`RibbonGeometry`, `GeometryBuilder`) in `#ifdef ENABLE_TINYGL_3D`
- Issues encountered:
  - `GhostRenderMode` enum needed to be defined in 2D renderer header for API compatibility
  - `AsyncBuildResult` struct needed conditional geometry members
  - `color_count` variable scope issue (fixed)
- Next steps: Phase 2 (Pi OpenGL ES integration)
