# DRM + GPU (EGL/OpenGL ES) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable GPU-accelerated rendering on Pi and BTT CB1 via DRM + EGL + OpenGL ES, with graceful fallback to dumb buffers and fbdev.

**Architecture:** The binary is compiled with EGL/GLES support (gated by `HELIX_ENABLE_OPENGLES`). At runtime, `DisplayBackendDRM::create_display()` tries EGL first, falls back to dumb buffers, then `create_auto()` falls back to fbdev. The LVGL EGL path uses legacy modesetting (not atomic), avoiding the crash that previously forced fbdev.

**Tech Stack:** LVGL 9.5 DRM/EGL driver, Mesa OpenGL ES 2.0+, libgbm, libEGL, libdrm, libinput

**Design doc:** `docs/devel/plans/2026-02-23-drm-gpu-backend-design.md`

---

### Task 1: Update Docker Toolchain with GPU Libraries

**Files:**
- Modify: `docker/Dockerfile.pi`
- Modify: `docker/Dockerfile.pi32`

**Step 1: Add GPU dev packages to Dockerfile.pi**

In the `apt-get install` block (around line 60), add the GPU libraries. Update the comment above to reflect they're now included:

```dockerfile
# arm64 target libraries for cross-compilation
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends \
    libc6-dev:arm64 \
    libstdc++-10-dev:arm64 \
    libdrm-dev:arm64 \
    libinput-dev:arm64 \
    libudev-dev:arm64 \
    libssl-dev:arm64 \
    libsystemd-dev:arm64 \
    zlib1g-dev:arm64 \
    libgbm-dev:arm64 \
    libgles2-mesa-dev:arm64 \
    libegl1-mesa-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*
```

Remove the old comment block (lines 52-57) that said these were "specifically excluded."

**Step 2: Same change for Dockerfile.pi32**

Add the armhf equivalents:
```dockerfile
    libgbm-dev:armhf \
    libgles2-mesa-dev:armhf \
    libegl1-mesa-dev:armhf \
```

**Step 3: Rebuild the Docker toolchain**

Run: `make docker-toolchain-pi`
Expected: Toolchain image builds successfully with GPU libs

**Step 4: Commit**

```bash
git add docker/Dockerfile.pi docker/Dockerfile.pi32
git commit -m "build(docker): add GPU libraries (EGL, GLES, GBM) to Pi toolchains"
```

---

### Task 2: Build System — Enable OpenGL ES for Pi DRM Builds

**Files:**
- Modify: `mk/cross.mk` (lines 30-54 and 56-78)
- Modify: `mk/cross.mk` (lines 410-428)
- Modify: `Makefile` (lines 219-221 and 488-491)

**Step 1: Add ENABLE_OPENGLES to Pi platform configs**

In `mk/cross.mk`, after `DISPLAY_BACKEND := drm` for the `pi` target (line 45), add:

```makefile
    ENABLE_OPENGLES := yes
```

Same for `pi32` target (after line 71):

```makefile
    ENABLE_OPENGLES := yes
```

**Step 2: Add HELIX_ENABLE_OPENGLES define to the DRM backend section**

In `mk/cross.mk` around line 412, inside the `ifeq ($(DISPLAY_BACKEND),drm)` block, add the OpenGL ES defines when enabled:

```makefile
ifeq ($(DISPLAY_BACKEND),drm)
    CFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    CXXFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    SUBMODULE_CFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    SUBMODULE_CXXFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    # GPU-accelerated rendering via EGL/OpenGL ES (Pi targets)
    ifeq ($(ENABLE_OPENGLES),yes)
        CFLAGS += -DHELIX_ENABLE_OPENGLES
        CXXFLAGS += -DHELIX_ENABLE_OPENGLES
        SUBMODULE_CFLAGS += -DHELIX_ENABLE_OPENGLES
        SUBMODULE_CXXFLAGS += -DHELIX_ENABLE_OPENGLES
    endif
    # DRM backend linker flags are added in Makefile's cross-compile section
```

**Step 3: Add GPU linker flags**

In `Makefile` around line 490, expand the DRM linker section:

```makefile
    # DRM backend requires libdrm and libinput for LVGL display/input drivers
    ifeq ($(DISPLAY_BACKEND),drm)
        LDFLAGS += -ldrm -linput
        # GPU-accelerated rendering via EGL/OpenGL ES
        ifeq ($(ENABLE_OPENGLES),yes)
            LDFLAGS += -lEGL -lGLESv2 -lgbm
        endif
    endif
```

**Step 4: GLAD include path already handled**

The Makefile already has (line 219):
```makefile
ifeq ($(ENABLE_OPENGLES),yes)
    LVGL_INC += -isystem $(LVGL_DIR)/src/drivers/opengles/glad/include
endif
```

This is correct. No changes needed.

**Step 5: Commit**

```bash
git add mk/cross.mk Makefile
git commit -m "build: enable OpenGL ES (EGL/GBM) for Pi DRM builds"
```

---

### Task 3: Fix LVGL OpenGL ES Compilation (Raw Strings in .c Files)

**Files:**
- Modify: `Makefile` (source filtering and new pattern rule)
- Modify: `mk/rules.mk` (add C++ compilation rule for opengles sources)

**Problem:** `lv_opengles_shader.c` uses C++11 raw string literals (`R"(...)"`). GCC won't compile these as C. The opengles sources must be compiled as C++ when OpenGL ES is enabled.

**Step 1: Filter opengles sources from LVGL_SRCS when ENABLE_OPENGLES=yes**

In `Makefile`, after the `LVGL_SRCS` definition (line 223), add:

```makefile
# When OpenGL ES is enabled, the opengles driver sources contain C++11 raw string
# literals (R"(...)") that can't compile as C. Filter them from C sources and compile
# as C++ separately (like ThorVG).
ifeq ($(ENABLE_OPENGLES),yes)
    LVGL_OPENGLES_SRCS := $(filter $(LVGL_DIR)/src/drivers/opengles/%,$(LVGL_SRCS))
    LVGL_SRCS := $(filter-out $(LVGL_DIR)/src/drivers/opengles/%,$(LVGL_SRCS))
    LVGL_OPENGLES_OBJS := $(patsubst $(LVGL_DIR)/%.c,$(OBJ_DIR)/lvgl/%.o,$(LVGL_OPENGLES_SRCS))
else
    LVGL_OPENGLES_OBJS :=
endif
```

**Step 2: Add opengles objects to the link**

Find where `LVGL_OBJS` is used in the linker command (look for `$(LVGL_OBJS)` in the link target) and add `$(LVGL_OPENGLES_OBJS)` next to it. Also ensure it's in the `ALL_OBJS` or equivalent variable.

Search for how objects are collected. It should be something like:
```makefile
OBJS := $(APP_OBJS) $(LVGL_OBJS) $(THORVG_OBJS) ...
```

Add `$(LVGL_OPENGLES_OBJS)` to this list.

**Step 3: Add C++ compilation rule for opengles sources in mk/rules.mk**

After the ThorVG C++ compilation rule (around line 317), add a new rule. Note: this rule must come BEFORE the generic `$(OBJ_DIR)/lvgl/%.o: $(LVGL_DIR)/%.c` rule since Make uses the first matching pattern rule:

Actually, since these files have `.c` extension but we want C++ compilation, the trick is: the filtered files won't match the C pattern rule (they're removed from `LVGL_SRCS`). We need a new pattern rule for them. But the object paths are the same `$(OBJ_DIR)/lvgl/%.o`. Since they're filtered out of `LVGL_OBJS`, they won't be built by the C rule's prerequisites. We need an explicit rule.

Best approach: use a **static pattern rule** for the opengles objects:

In `mk/rules.mk`, after the ThorVG rule (around line 317), add:

```makefile
# Compile LVGL OpenGL ES sources as C++ (raw string literals require C++11)
# These are .c files compiled with CXX, like ThorVG but for the opengles driver.
# Only built when ENABLE_OPENGLES=yes (LVGL_OPENGLES_OBJS is empty otherwise).
$(LVGL_OPENGLES_OBJS): $(OBJ_DIR)/lvgl/%.o: $(LVGL_DIR)/%.c lv_conf.h
	$(Q)mkdir -p $(dir $@)
	$(ECHO) "$(CYAN)[CXX/GLES]$(RESET) $<"
	$(Q)$(CXX) $(SUBMODULE_CXXFLAGS) $(INCLUDES) $(LV_CONF) -c $< -o $@ || { \
		echo "$(RED)$(BOLD)✗ Compilation failed:$(RESET) $<"; \
		exit 1; \
	}
	$(call emit-compile-command,$(CXX),$(SUBMODULE_CXXFLAGS) $(INCLUDES) $(LV_CONF),$<,$@)
```

**Step 4: Verify build compiles**

Run: `make -j` (native build, won't have ENABLE_OPENGLES — should still work)
Expected: Builds cleanly, no regressions

Run: `make pi-docker` (cross build, will have ENABLE_OPENGLES=yes)
Expected: OpenGL ES sources compile as C++ — no raw string errors
NOTE: This requires Task 1 (Docker toolchain) to be complete first.

**Step 5: Commit**

```bash
git add Makefile mk/rules.mk
git commit -m "build: compile LVGL OpenGL ES sources as C++ for raw string support"
```

---

### Task 4: Update lv_conf.h for OpenGL ES

**Files:**
- Modify: `lv_conf.h` (lines 1169-1172)

**Step 1: Gate LV_USE_OPENGLES on HELIX_ENABLE_OPENGLES**

Replace the hardcoded `#define LV_USE_OPENGLES 0` with:

```c
/* Use OpenGL ES display driver (GLAD-based).
 * Required for GPU-accelerated rendering via DRM+EGL on Pi.
 * Gated by HELIX_ENABLE_OPENGLES (set by build system for Pi DRM builds). */
#ifdef HELIX_ENABLE_OPENGLES
    #define LV_USE_OPENGLES   1
#else
    #define LV_USE_OPENGLES   0
#endif
```

**Step 2: Verify native build still works**

Run: `make -j`
Expected: Builds cleanly (HELIX_ENABLE_OPENGLES not defined, LV_USE_OPENGLES=0)

**Step 3: Commit**

```bash
git add lv_conf.h
git commit -m "build: gate LV_USE_OPENGLES on HELIX_ENABLE_OPENGLES flag"
```

---

### Task 5: Add EGL Fallback to DisplayBackendDRM

**Files:**
- Modify: `src/api/display_backend_drm.cpp`
- Modify: `include/display_backend_drm.h`

**Step 1: Add EGL tracking to header**

In `display_backend_drm.h`, add a member to track which mode was used:

```cpp
  private:
    std::string drm_device_ = "/dev/dri/card0";
    lv_display_t* display_ = nullptr;
    lv_indev_t* pointer_ = nullptr;
    bool using_egl_ = false;  // Track if GPU-accelerated path is active
```

Add a public query method:
```cpp
    bool is_gpu_accelerated() const { return using_egl_; }
```

**Step 2: Refactor create_display() with EGL→dumb buffer fallback**

In `display_backend_drm.cpp`, replace the existing `create_display()` method:

```cpp
lv_display_t* DisplayBackendDRM::create_display(int width, int height) {
    LV_UNUSED(width);
    LV_UNUSED(height);

#ifdef HELIX_ENABLE_OPENGLES
    // Try EGL path first (GPU-accelerated, uses legacy modesetting)
    spdlog::info("[DRM Backend] Attempting GPU-accelerated display via EGL on {}", drm_device_);
    display_ = lv_linux_drm_create();
    if (display_) {
        lv_result_t result = lv_linux_drm_set_file(display_, drm_device_.c_str(), -1);
        if (result == LV_RESULT_OK) {
            using_egl_ = true;
            auto res = detect_resolution();
            spdlog::info("[DRM Backend] GPU-accelerated display active: {}x{} on {}",
                         res.width, res.height, drm_device_);
            return display_;
        }
        // EGL init failed — clean up and try dumb buffers
        spdlog::warn("[DRM Backend] EGL initialization failed, falling back to dumb buffers");
        lv_display_delete(display_);
        display_ = nullptr;
    }
#endif

    // Dumb buffer path (CPU rendering with vsync via atomic modesetting)
    spdlog::info("[DRM Backend] Creating DRM display with dumb buffers on {}", drm_device_);
    display_ = lv_linux_drm_create();
    if (display_ == nullptr) {
        spdlog::error("[DRM Backend] Failed to create DRM display");
        return nullptr;
    }

    lv_result_t result = lv_linux_drm_set_file(display_, drm_device_.c_str(), -1);
    if (result != LV_RESULT_OK) {
        spdlog::error("[DRM Backend] Failed to initialize DRM device {}", drm_device_);
        lv_display_delete(display_);
        display_ = nullptr;
        return nullptr;
    }

    spdlog::info("[DRM Backend] DRM dumb buffer display created on {}", drm_device_);
    return display_;
}
```

Note: `lv_linux_drm_create()` dispatches to the EGL or dumb buffer variant based on compile-time `LV_LINUX_DRM_USE_EGL`. When both are compiled in, we need the EGL path to be tried first. Since `LV_LINUX_DRM_USE_EGL=1` means `lv_linux_drm_create()` goes to the EGL codepath, we may need a different approach:

**IMPORTANT DESIGN CONSIDERATION:** With `LV_LINUX_DRM_USE_EGL=1`, there's only ONE `lv_linux_drm_create()` — the EGL variant. The dumb buffer variant is compiled out. This means we can't easily fall back from EGL to dumb buffers at runtime within the same binary.

**Revised approach:** The fallback chain becomes:
- Try DRM+EGL (`lv_linux_drm_create()` with EGL)
- If that fails → fallback to fbdev (not dumb buffers)

This is actually fine because:
1. The EGL path uses legacy modesetting which is more compatible than atomic
2. If EGL fails, it's likely a driver/Mesa issue where dumb buffers would also be problematic
3. fbdev is the proven reliable fallback

So simplify the `create_display()`:

```cpp
lv_display_t* DisplayBackendDRM::create_display(int width, int height) {
    LV_UNUSED(width);
    LV_UNUSED(height);

    spdlog::info("[DRM Backend] Creating DRM display on {}", drm_device_);

    display_ = lv_linux_drm_create();
    if (display_ == nullptr) {
        spdlog::error("[DRM Backend] Failed to create DRM display");
        return nullptr;
    }

    lv_result_t result = lv_linux_drm_set_file(display_, drm_device_.c_str(), -1);
    if (result != LV_RESULT_OK) {
        spdlog::error("[DRM Backend] Failed to initialize DRM on {}", drm_device_);
        lv_display_delete(display_);
        display_ = nullptr;
        return nullptr;
    }

#ifdef HELIX_ENABLE_OPENGLES
    using_egl_ = true;
    spdlog::info("[DRM Backend] GPU-accelerated display active (EGL/OpenGL ES)");
#else
    spdlog::info("[DRM Backend] DRM display active (dumb buffers, CPU rendering)");
#endif

    return display_;
}
```

The `create_auto()` in `display_backend.cpp` already handles the DRM→fbdev fallback.

**Step 3: Log GPU status at startup**

Add a log line in `display_manager.cpp` after `create_display()` succeeds, so the user knows which rendering path is active. Look for where the backend name is logged and add:

```cpp
#ifdef HELIX_DISPLAY_DRM
if (auto* drm = dynamic_cast<DisplayBackendDRM*>(backend_.get())) {
    if (drm->is_gpu_accelerated()) {
        spdlog::info("[Display] Rendering: GPU-accelerated (OpenGL ES via EGL)");
    } else {
        spdlog::info("[Display] Rendering: CPU (DRM dumb buffers)");
    }
}
#endif
```

**Step 4: Commit**

```bash
git add src/api/display_backend_drm.cpp include/display_backend_drm.h src/application/display_manager.cpp
git commit -m "feat(display): add EGL/GPU rendering path to DRM backend with fbdev fallback"
```

---

### Task 6: Update Service File — Remove Forced fbdev

**Files:**
- Modify: `config/helixscreen.service`

**Step 1: Remove the forced fbdev override**

Change line 50 from:
```ini
Environment="HELIX_DISPLAY_BACKEND=fbdev"
```

To a comment explaining auto-detection:
```ini
# Display backend: auto-detected (DRM+EGL preferred, fbdev fallback)
# Override with: Environment="HELIX_DISPLAY_BACKEND=fbdev" or "drm"
# Environment="HELIX_DISPLAY_BACKEND=fbdev"
```

**Step 2: Commit**

```bash
git add config/helixscreen.service
git commit -m "fix(service): remove forced fbdev override, enable DRM auto-detection"
```

---

### Task 7: Build and Deploy to Pi for Testing

This is a manual testing task — no code changes.

**Step 1: Rebuild Docker toolchain (if not done in Task 1)**

Run: `make docker-toolchain-pi`

**Step 2: Build for Pi**

Run: `make pi-docker`
Expected: Compiles successfully including OpenGL ES sources

**Step 3: Deploy to Pi 3 (Doron Velta)**

Run: `make deploy-pi PI_HOST=192.168.1.39`

**Step 4: Test on Pi 3**

SSH to Pi and run:
```bash
sudo systemctl stop helixscreen
cd ~/helixscreen
./bin/helix-screen --test -vvv 2>&1 | tee /tmp/helix-drm.log
```

Look for:
- `[DRM Backend] Auto-detected DRM device: /dev/dri/card0`
- `[DRM Backend] GPU-accelerated display active (EGL/OpenGL ES)` — SUCCESS
- OR `[DRM Backend] Failed to initialize DRM` → falls back to fbdev — check logs
- UI renders correctly, touch works, no tearing

**Step 5: Test on BTT CB1**

Deploy and test on 192.168.1.112 (biqu user):
```bash
# Build and deploy
make deploy-btt BTT_HOST=192.168.1.112
# Or manually scp + run
```

**Step 6: Test fallback**

Force fbdev fallback to verify it still works:
```bash
HELIX_DISPLAY_BACKEND=fbdev ./bin/helix-screen --test -vv
```

Force failure to test auto-fallback:
```bash
HELIX_DRM_DEVICE=/dev/dri/card99 ./bin/helix-screen --test -vv
```
Expected: Falls through to fbdev, logs the fallback clearly.

---

### Task 8: Install Script — Ensure GPU Runtime Libraries

**Files:**
- Modify: `scripts/install.sh` (or wherever runtime dependencies are checked)

**Step 1: Add GPU library check**

In the dependency check section, add:
```bash
# GPU rendering libraries (required for DRM+EGL backend on Pi)
# These are typically pre-installed on Raspberry Pi OS but may be missing on headless/minimal images
GPU_PACKAGES="libgbm1 libegl1 libgles2"
```

**Step 2: Install if missing**

```bash
for pkg in $GPU_PACKAGES; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        echo "Installing $pkg for GPU rendering..."
        sudo apt-get install -y "$pkg"
    fi
done
```

**Step 3: Commit**

```bash
git add scripts/install.sh
git commit -m "fix(install): ensure GPU runtime libraries are installed on Pi"
```

---

## Dependency Order

```
Task 1 (Docker toolchain) ─┐
Task 2 (Build system)      ├─→ Task 7 (Build & Test)
Task 3 (Compilation fix)   │
Task 4 (lv_conf.h)         │
Task 5 (DRM backend code)  ┘
Task 6 (Service file)      ─→ Task 7 (Test)
Task 8 (Install script)    ─→ independent
```

Tasks 1-6 can be done in order (each builds on prior). Task 7 is the integration test. Task 8 is independent.
