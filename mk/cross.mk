# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen - Cross-Compilation Module
# Handles cross-compilation for embedded ARM targets
#
# Usage:
#   make                       # Native build (SDL)
#   make PLATFORM_TARGET=pi    # Cross-compile for Raspberry Pi (aarch64)
#   make PLATFORM_TARGET=ad5m  # Cross-compile for Adventurer 5M (armv7-a)
#   make pi-docker             # Docker-based Pi build
#   make ad5m-docker           # Docker-based AD5M build

# =============================================================================
# Target Platform Definitions
# =============================================================================

# Note: We use PLATFORM_TARGET to avoid collision with Makefile's TARGET (binary path)
PLATFORM_TARGET ?= native

ifeq ($(PLATFORM_TARGET),pi)
    # -------------------------------------------------------------------------
    # Raspberry Pi (Mainsail OS) - aarch64 / ARM64
    # -------------------------------------------------------------------------
    CROSS_COMPILE ?= aarch64-linux-gnu-
    TARGET_ARCH := aarch64
    TARGET_TRIPLE := aarch64-linux-gnu
    # Include paths for cross-compilation:
    # - /usr/aarch64-linux-gnu/include: arm64 sysroot headers
    # - /usr/include/libdrm: drm.h (needed by xf86drmMode.h)
    # -Wno-error=conversion: LVGL headers have int32_t->float conversions that GCC 12 flags
    # -DHELIX_RELEASE_BUILD: Disables debug features like LV_USE_ASSERT_STYLE
    TARGET_CFLAGS := -march=armv8-a -I/usr/aarch64-linux-gnu/include -I/usr/include/libdrm -Wno-error=conversion -Wno-error=sign-conversion -DHELIX_RELEASE_BUILD
    DISPLAY_BACKEND := drm
    ENABLE_SDL := no
    ENABLE_TINYGL_3D := yes
    ENABLE_EVDEV := yes
    BUILD_SUBDIR := pi
    # Strip binary for size - embedded targets don't need debug symbols
    STRIP_BINARY := yes

else ifeq ($(PLATFORM_TARGET),ad5m)
    # -------------------------------------------------------------------------
    # Flashforge Adventurer 5M - Cortex-A7 (armv7-a hard-float)
    # Specs: 800x480 display, 110MB RAM, glibc 2.25
    # -------------------------------------------------------------------------
    # FULLY STATIC BUILD: Link everything statically to avoid glibc version
    # conflicts. The ARM toolchain's sysroot has glibc 2.33 symbols, but AD5M
    # only has glibc 2.25. Static linking sidesteps this entirely.
    # Trade-off: Larger binary (~5-8MB vs ~2MB) but guaranteed compatibility.
    CROSS_COMPILE ?= arm-none-linux-gnueabihf-
    TARGET_ARCH := armv7-a
    TARGET_TRIPLE := arm-none-linux-gnueabihf
    # Memory-optimized build flags:
    # -Os: Optimize for size (vs -O2 for speed)
    # -flto: Link-Time Optimization for dead code elimination
    # -ffunction-sections/-fdata-sections: Allow linker to remove unused sections
    # -Wno-error=conversion: LVGL headers have int32_t->float conversions that GCC flags
    # -DHELIX_RELEASE_BUILD: Disables debug features like LV_USE_ASSERT_STYLE
    # NOTE: AD5M framebuffer is 32bpp (ARGB8888), as is lv_conf.h (LV_COLOR_DEPTH=32)
    TARGET_CFLAGS := -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a7 \
        -Os -flto -ffunction-sections -fdata-sections \
        -Wno-error=conversion -Wno-error=sign-conversion -DHELIX_RELEASE_BUILD
    # -Wl,--gc-sections: Remove unused sections during linking (works with -ffunction-sections)
    # -flto: Must match compiler flag for LTO to work
    # -static: Fully static binary - no runtime dependencies on system libs
    # This avoids glibc version mismatch (binary needs 2.33, system has 2.25)
    # -lstdc++fs: Required for std::experimental::filesystem on GCC 10.x
    TARGET_LDFLAGS := -Wl,--gc-sections -flto -static -lstdc++fs
    # SSL disabled for embedded - Moonraker communication is local/plaintext
    ENABLE_SSL := no
    DISPLAY_BACKEND := fbdev
    ENABLE_SDL := no
    # Disable TinyGL for AD5M - CPU too weak for software 3D (3-4 FPS)
    # Uses 2D layer preview fallback instead
    ENABLE_TINYGL_3D := no
    ENABLE_EVDEV := yes
    BUILD_SUBDIR := ad5m
    # Strip binary for size on memory-constrained device
    STRIP_BINARY := yes

else ifeq ($(PLATFORM_TARGET),native)
    # -------------------------------------------------------------------------
    # Native desktop build (macOS / Linux)
    # -------------------------------------------------------------------------
    CROSS_COMPILE :=
    TARGET_ARCH := $(shell uname -m)
    TARGET_TRIPLE :=
    TARGET_CFLAGS :=
    DISPLAY_BACKEND := sdl
    ENABLE_SDL := yes
    # TinyGL controlled by main Makefile default
    ENABLE_EVDEV := no
    BUILD_SUBDIR :=

else
    $(error Unknown PLATFORM_TARGET: $(PLATFORM_TARGET). Valid options: native, pi, ad5m)
endif

# =============================================================================
# Cross-Compiler Configuration
# =============================================================================

ifneq ($(CROSS_COMPILE),)
    # Override compilers for cross-compilation
    CC := $(CROSS_COMPILE)gcc
    CXX := $(CROSS_COMPILE)g++
    # Use gcc-ar and gcc-ranlib for LTO compatibility (they load the LTO plugin)
    AR := $(CROSS_COMPILE)gcc-ar
    STRIP := $(CROSS_COMPILE)strip
    RANLIB := $(CROSS_COMPILE)gcc-ranlib
    LD := $(CROSS_COMPILE)ld

    # Override build directories for cross-compilation
    ifneq ($(BUILD_SUBDIR),)
        BUILD_DIR := build/$(BUILD_SUBDIR)
        BIN_DIR := $(BUILD_DIR)/bin
        OBJ_DIR := $(BUILD_DIR)/obj
    endif

    # Print cross-compilation info
    $(info )
    $(info ========================================)
    $(info Cross-compiling for: $(PLATFORM_TARGET))
    $(info Architecture: $(TARGET_ARCH))
    $(info Compiler: $(CC))
    $(info Output: $(BUILD_DIR))
    $(info ========================================)
    $(info )
endif

# =============================================================================
# Target-Specific Flags
# =============================================================================

ifdef TARGET_CFLAGS
    CFLAGS += $(TARGET_CFLAGS)
    CXXFLAGS += $(TARGET_CFLAGS)
    SUBMODULE_CFLAGS += $(TARGET_CFLAGS)
    SUBMODULE_CXXFLAGS += $(TARGET_CFLAGS)
endif

# For size-optimized targets, override -O2 with -Os
# (GCC uses last optimization flag, but this makes it explicit)
ifeq ($(PLATFORM_TARGET),ad5m)
    CFLAGS := $(subst -O2,-Os,$(CFLAGS))
    CXXFLAGS := $(subst -O2,-Os,$(CXXFLAGS))
    SUBMODULE_CFLAGS := $(subst -O2,-Os,$(SUBMODULE_CFLAGS))
    SUBMODULE_CXXFLAGS := $(subst -O2,-Os,$(SUBMODULE_CXXFLAGS))
endif

ifdef TARGET_LDFLAGS
    LDFLAGS += $(TARGET_LDFLAGS)
endif

# Display backend defines (used by display_backend.cpp and lv_conf.h for conditional compilation)
# Must be added to SUBMODULE_*FLAGS as well for LVGL driver compilation
ifeq ($(DISPLAY_BACKEND),drm)
    CFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    CXXFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    SUBMODULE_CFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    SUBMODULE_CXXFLAGS += -DHELIX_DISPLAY_DRM -DHELIX_DISPLAY_FBDEV
    # DRM backend linker flags are added in Makefile's cross-compile section
else ifeq ($(DISPLAY_BACKEND),fbdev)
    CFLAGS += -DHELIX_DISPLAY_FBDEV
    CXXFLAGS += -DHELIX_DISPLAY_FBDEV
    SUBMODULE_CFLAGS += -DHELIX_DISPLAY_FBDEV
    SUBMODULE_CXXFLAGS += -DHELIX_DISPLAY_FBDEV
else ifeq ($(DISPLAY_BACKEND),sdl)
    CFLAGS += -DHELIX_DISPLAY_SDL
    CXXFLAGS += -DHELIX_DISPLAY_SDL
    SUBMODULE_CFLAGS += -DHELIX_DISPLAY_SDL
    SUBMODULE_CXXFLAGS += -DHELIX_DISPLAY_SDL
endif

# Evdev input support
ifeq ($(ENABLE_EVDEV),yes)
    CFLAGS += -DHELIX_INPUT_EVDEV
    CXXFLAGS += -DHELIX_INPUT_EVDEV
    SUBMODULE_CFLAGS += -DHELIX_INPUT_EVDEV
    SUBMODULE_CXXFLAGS += -DHELIX_INPUT_EVDEV
endif

# NOTE: LV_COLOR_DEPTH is now hardcoded to 32 in lv_conf.h for all platforms.
# This simplifies thumbnail/image handling (always ARGB8888) at negligible memory cost.

# =============================================================================
# Cross-Compilation Build Targets
# =============================================================================

.PHONY: pi ad5m pi-docker ad5m-docker docker-toolchains cross-info ensure-docker ensure-buildx maybe-stop-colima

# Direct cross-compilation (requires toolchain installed)
pi:
	@echo "$(CYAN)$(BOLD)Cross-compiling for Raspberry Pi (aarch64)...$(RESET)"
	$(Q)$(MAKE) PLATFORM_TARGET=pi -j$(NPROC) all

ad5m:
	@echo "$(CYAN)$(BOLD)Cross-compiling for Adventurer 5M (armv7-a)...$(RESET)"
	$(Q)$(MAKE) PLATFORM_TARGET=ad5m -j$(NPROC) all

# Docker-based cross-compilation (recommended)
# SKIP_OPTIONAL_DEPS=1 skips npm, clang-format, python venv, and other development tools

# Helper target to ensure Docker daemon is running
# On macOS with Colima, automatically starts it with resources based on host hardware
# Allocates ~50% of system RAM (min 6GB, max 16GB) and ~50% of CPUs (min 2, max 8)
.PHONY: ensure-docker
ensure-docker:
	@if docker info >/dev/null 2>&1; then \
		exit 0; \
	fi; \
	if [ "$(UNAME_S)" = "Darwin" ]; then \
		if command -v colima >/dev/null 2>&1; then \
			TOTAL_RAM_GB=$$(( $$(sysctl -n hw.memsize) / 1073741824 )); \
			TOTAL_CPUS=$$(sysctl -n hw.ncpu); \
			COLIMA_RAM=$$(( TOTAL_RAM_GB / 2 )); \
			COLIMA_CPUS=$$(( TOTAL_CPUS / 2 )); \
			[ $$COLIMA_RAM -lt 6 ] && COLIMA_RAM=6; \
			[ $$COLIMA_RAM -gt 16 ] && COLIMA_RAM=16; \
			[ $$COLIMA_CPUS -lt 2 ] && COLIMA_CPUS=2; \
			[ $$COLIMA_CPUS -gt 8 ] && COLIMA_CPUS=8; \
			echo "$(YELLOW)Docker not running. Starting Colima ($${COLIMA_RAM}GB RAM, $${COLIMA_CPUS} CPUs)...$(RESET)"; \
			echo "$(CYAN)  (Based on host: $${TOTAL_RAM_GB}GB RAM, $${TOTAL_CPUS} CPUs)$(RESET)"; \
			if colima list 2>/dev/null | grep -q "default"; then \
				CURRENT_RAM=$$(colima list 2>/dev/null | awk '/default/ {gsub(/GiB/,""); print $$5}'); \
				if [ "$$CURRENT_RAM" != "" ] && [ "$$CURRENT_RAM" -lt "$$COLIMA_RAM" ]; then \
					echo "$(YELLOW)⚠ Existing Colima VM has $${CURRENT_RAM}GB RAM (need $${COLIMA_RAM}GB)$(RESET)"; \
					echo "$(YELLOW)  Run 'colima delete' then retry to resize$(RESET)"; \
				fi; \
			fi; \
			colima start --memory $$COLIMA_RAM --cpu $$COLIMA_CPUS && echo "$(GREEN)✓ Colima started$(RESET)"; \
		elif [ -e "/Applications/Docker.app" ]; then \
			echo "$(RED)Docker Desktop is installed but not running.$(RESET)"; \
			echo "$(YELLOW)Please start Docker Desktop from your Applications folder.$(RESET)"; \
			exit 1; \
		else \
			echo "$(RED)Docker is not installed.$(RESET)"; \
			echo "$(YELLOW)Install with: brew install colima docker docker-buildx && colima start$(RESET)"; \
			exit 1; \
		fi; \
	else \
		echo "$(RED)Docker daemon is not running.$(RESET)"; \
		echo "$(YELLOW)Start it with: sudo systemctl start docker$(RESET)"; \
		exit 1; \
	fi

# Helper target to ensure Docker BuildKit/buildx is available
# BuildKit is Docker's modern image builder with better caching and parallel builds
# The legacy builder is deprecated and will be removed in a future Docker release
.PHONY: ensure-buildx
ensure-buildx: ensure-docker
	@if docker buildx version >/dev/null 2>&1; then \
		exit 0; \
	fi; \
	echo "$(YELLOW)Docker BuildKit (buildx) not found.$(RESET)"; \
	echo "The legacy Docker builder is deprecated and will be removed."; \
	if [ "$(UNAME_S)" = "Darwin" ]; then \
		echo "$(YELLOW)Install with: brew install docker-buildx$(RESET)"; \
	else \
		echo "$(YELLOW)See: https://docs.docker.com/go/buildx/$(RESET)"; \
	fi; \
	exit 1

pi-docker: ensure-docker
	@echo "$(CYAN)$(BOLD)Cross-compiling for Raspberry Pi via Docker...$(RESET)"
	@if ! docker image inspect helixscreen/toolchain-pi >/dev/null 2>&1; then \
		echo "$(YELLOW)Docker image not found. Building toolchain first...$(RESET)"; \
		$(MAKE) docker-toolchain-pi; \
	fi
	$(Q)docker run --rm --user $$(id -u):$$(id -g) -v "$(PWD)":/src -w /src helixscreen/toolchain-pi \
		make PLATFORM_TARGET=pi SKIP_OPTIONAL_DEPS=1 -j$$(nproc)
	@$(MAKE) --no-print-directory maybe-stop-colima

ad5m-docker: ensure-docker
	@echo "$(CYAN)$(BOLD)Cross-compiling for Adventurer 5M via Docker...$(RESET)"
	@if ! docker image inspect helixscreen/toolchain-ad5m >/dev/null 2>&1; then \
		echo "$(YELLOW)Docker image not found. Building toolchain first...$(RESET)"; \
		$(MAKE) docker-toolchain-ad5m; \
	fi
	$(Q)docker run --rm --user $$(id -u):$$(id -g) -v "$(PWD)":/src -w /src helixscreen/toolchain-ad5m \
		make PLATFORM_TARGET=ad5m SKIP_OPTIONAL_DEPS=1 -j$$(nproc)
	@$(MAKE) --no-print-directory maybe-stop-colima

# Stop Colima after build to free up RAM (macOS only)
# Only stops if Colima is running and we're on macOS
.PHONY: maybe-stop-colima
maybe-stop-colima:
	@if [ "$(UNAME_S)" = "Darwin" ] && command -v colima >/dev/null 2>&1; then \
		if colima status >/dev/null 2>&1; then \
			echo "$(CYAN)Stopping Colima to free up RAM...$(RESET)"; \
			colima stop && echo "$(GREEN)✓ Colima stopped$(RESET)"; \
		fi; \
	fi

# Build Docker toolchain images
docker-toolchains: docker-toolchain-pi docker-toolchain-ad5m
	@echo "$(GREEN)$(BOLD)All Docker toolchains built successfully$(RESET)"

docker-toolchain-pi: ensure-buildx
	@echo "$(CYAN)Building Raspberry Pi toolchain Docker image...$(RESET)"
	$(Q)docker buildx build -t helixscreen/toolchain-pi -f docker/Dockerfile.pi docker/

docker-toolchain-ad5m: ensure-buildx
	@echo "$(CYAN)Building Adventurer 5M toolchain Docker image...$(RESET)"
	$(Q)docker buildx build -t helixscreen/toolchain-ad5m -f docker/Dockerfile.ad5m docker/

# Display cross-compilation info (alias for help-cross)
cross-info: help-cross

# Cross-compilation help
.PHONY: help-cross
help-cross:
	@if [ -t 1 ] && [ -n "$(TERM)" ] && [ "$(TERM)" != "dumb" ]; then \
		B='$(BOLD)'; G='$(GREEN)'; Y='$(YELLOW)'; C='$(CYAN)'; X='$(RESET)'; \
	else \
		B=''; G=''; Y=''; C=''; X=''; \
	fi; \
	echo "$${B}Cross-Compilation & Deployment$${X}"; \
	echo ""; \
	echo "$${C}Docker Cross-Compilation (recommended):$${X}"; \
	echo "  $${G}pi-docker$${X}            - Build for Raspberry Pi (aarch64) via Docker"; \
	echo "  $${G}ad5m-docker$${X}          - Build for Adventurer 5M (armv7-a) via Docker"; \
	echo "  $${G}docker-toolchains$${X}    - Build all Docker toolchain images"; \
	echo "  $${G}docker-toolchain-pi$${X}  - Build Pi toolchain image only"; \
	echo "  $${G}docker-toolchain-ad5m$${X} - Build AD5M toolchain image only"; \
	echo ""; \
	echo "$${C}Direct Cross-Compilation (requires local toolchain):$${X}"; \
	echo "  $${G}pi$${X}                   - Cross-compile for Raspberry Pi"; \
	echo "  $${G}ad5m$${X}                 - Cross-compile for Adventurer 5M"; \
	echo ""; \
	echo "$${C}Pi Deployment:$${X}"; \
	echo "  $${G}deploy-pi$${X}            - Deploy and restart in background (default)"; \
	echo "  $${G}deploy-pi-fg$${X}         - Deploy and run in foreground (debug)"; \
	echo "  $${G}pi-test$${X}              - Full cycle: build + deploy + run (fg)"; \
	echo "  $${G}pi-ssh$${X}               - SSH into the Pi"; \
	echo ""; \
	echo "$${C}AD5M Deployment:$${X}"; \
	echo "  $${G}deploy-ad5m$${X}          - Deploy and restart in background"; \
	echo "  $${G}deploy-ad5m-fg$${X}       - Deploy and run in foreground (debug)"; \
	echo "  $${G}deploy-ad5m-bin$${X}      - Deploy binaries only (fast iteration)"; \
	echo "  $${G}ad5m-test$${X}            - Full cycle: remote build + deploy + run (fg)"; \
	echo "  $${G}ad5m-ssh$${X}             - SSH into the AD5M"; \
	echo ""; \
	echo "$${C}Deployment Options:$${X}"; \
	echo "  $${Y}PI_HOST$${X}=hostname     - Pi hostname (default: helixpi.local)"; \
	echo "  $${Y}PI_USER$${X}=user         - Pi username (default: from SSH config)"; \
	echo "  $${Y}PI_DEPLOY_DIR$${X}=path   - Deployment directory (default: ~/helixscreen)"; \
	echo "  $${Y}AD5M_HOST$${X}=hostname   - AD5M hostname/IP (default: ad5m.local)"; \
	echo "  $${Y}AD5M_USER$${X}=user       - AD5M username (default: root)"; \
	echo "  $${Y}AD5M_DEPLOY_DIR$${X}=path - AD5M deploy directory (default: /opt/helixscreen)"; \
	echo ""; \
	echo "$${C}Current Configuration:$${X}"; \
	echo "  Platform target: $(PLATFORM_TARGET)"; \
	echo "  Display backend: $(DISPLAY_BACKEND)"; \
	echo "  SDL enabled: $(ENABLE_SDL)"

# =============================================================================
# Pi Deployment Configuration
# =============================================================================

# Pi deployment settings (can override via environment or command line)
# Example: make deploy-pi PI_HOST=192.168.1.50 PI_USER=pi
# PI_USER defaults to empty (uses SSH config or current user)
# PI_DEPLOY_DIR defaults to ~/helixscreen (full app directory)
PI_HOST ?= helixpi.local
PI_USER ?=
PI_DEPLOY_DIR ?= ~/helixscreen

# Build SSH target: user@host or just host if no user specified
ifdef PI_USER
    PI_SSH_TARGET := $(PI_USER)@$(PI_HOST)
else
    PI_SSH_TARGET := $(PI_HOST)
endif

# =============================================================================
# Pi Deployment Targets
# =============================================================================

.PHONY: deploy-pi deploy-pi-fg deploy-pi-quiet pi-ssh pi-test

# Deploy full application to Pi and restart in background
# Uses rsync for efficient delta transfers - only changed files are sent
# Kills any existing instance and restarts automatically
# Includes pre-rendered .bin assets from build/ if they exist (for splash performance)
deploy-pi:
	@test -f build/pi/bin/helix-screen || { echo "$(RED)Error: build/pi/bin/helix-screen not found. Run 'make pi-docker' first.$(RESET)"; exit 1; }
	@test -f build/pi/bin/helix-splash || { echo "$(RED)Error: build/pi/bin/helix-splash not found. Run 'make pi-docker' first.$(RESET)"; exit 1; }
	@echo "$(CYAN)Deploying HelixScreen to $(PI_SSH_TARGET):$(PI_DEPLOY_DIR)...$(RESET)"
	@echo "  Binaries: helix-screen, helix-splash, helix-watchdog"
	@echo "  Assets: ui_xml/, assets/, config/, moonraker-plugin/"
	ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)"
	rsync -avz --progress \
		build/pi/bin/helix-screen \
		build/pi/bin/helix-splash \
		$(if $(wildcard build/pi/bin/helix-watchdog),build/pi/bin/helix-watchdog) \
		ui_xml \
		assets \
		config \
		moonraker-plugin \
		$(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/
	@# Use --checksum for pre-rendered images (regenerated with new timestamps but same content)
	@if [ -d build/assets/images/prerendered ]; then \
		echo "$(DIM)Transferring pre-rendered splash images...$(RESET)"; \
		ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)/assets/images/prerendered"; \
		rsync -avz --checksum build/assets/images/prerendered/ $(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/assets/images/prerendered/; \
	fi
	@if [ -d build/assets/images/printers/prerendered ]; then \
		echo "$(DIM)Transferring pre-rendered printer images...$(RESET)"; \
		ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)/assets/images/printers/prerendered"; \
		rsync -avz --checksum build/assets/images/printers/prerendered/ $(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/assets/images/printers/prerendered/; \
	fi
	@echo "$(GREEN)✓ Deployed to $(PI_HOST):$(PI_DEPLOY_DIR)$(RESET)"
	@echo "$(CYAN)Restarting helix-screen on $(PI_HOST)...$(RESET)"
	ssh $(PI_SSH_TARGET) "cd $(PI_DEPLOY_DIR) && killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 0.5; setsid ./config/helix-launcher.sh </dev/null >/dev/null 2>&1 &"
	@echo "$(GREEN)✓ helix-screen restarted in background$(RESET)"
	@echo "$(DIM)Logs: ssh $(PI_SSH_TARGET) 'journalctl -t helix-screen -f'$(RESET)"

# Deploy and run in foreground with debug logging (for interactive debugging)
# Uses --debug for debug-level logging and --log-dest=console for immediate output
deploy-pi-fg:
	@test -f build/pi/bin/helix-screen || { echo "$(RED)Error: build/pi/bin/helix-screen not found. Run 'make pi-docker' first.$(RESET)"; exit 1; }
	@test -f build/pi/bin/helix-splash || { echo "$(RED)Error: build/pi/bin/helix-splash not found. Run 'make pi-docker' first.$(RESET)"; exit 1; }
	@echo "$(CYAN)Deploying HelixScreen to $(PI_SSH_TARGET):$(PI_DEPLOY_DIR)...$(RESET)"
	ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)"
	rsync -avz --progress \
		build/pi/bin/helix-screen \
		build/pi/bin/helix-splash \
		$(if $(wildcard build/pi/bin/helix-watchdog),build/pi/bin/helix-watchdog) \
		ui_xml \
		assets \
		config \
		$(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/
	@if [ -d build/assets/images/prerendered ]; then \
		echo "$(DIM)Transferring pre-rendered images...$(RESET)"; \
		ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)/assets/images/prerendered"; \
		rsync -avz build/assets/images/prerendered/ $(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/assets/images/prerendered/; \
	fi
	@echo "$(CYAN)Starting helix-screen on $(PI_HOST) (foreground, debug mode)...$(RESET)"
	ssh -t $(PI_SSH_TARGET) "cd $(PI_DEPLOY_DIR) && killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 0.5; ./config/helix-launcher.sh --debug --log-dest=console"

# Deploy and run in foreground without debug logging (production mode)
deploy-pi-quiet:
	@test -f build/pi/bin/helix-screen || { echo "$(RED)Error: build/pi/bin/helix-screen not found. Run 'make pi-docker' first.$(RESET)"; exit 1; }
	@test -f build/pi/bin/helix-splash || { echo "$(RED)Error: build/pi/bin/helix-splash not found. Run 'make pi-docker' first.$(RESET)"; exit 1; }
	ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)"
	rsync -avz --progress \
		build/pi/bin/helix-screen \
		build/pi/bin/helix-splash \
		$(if $(wildcard build/pi/bin/helix-watchdog),build/pi/bin/helix-watchdog) \
		ui_xml \
		assets \
		config \
		$(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/
	@if [ -d build/assets/images/prerendered ]; then \
		echo "$(DIM)Transferring pre-rendered images...$(RESET)"; \
		ssh $(PI_SSH_TARGET) "mkdir -p $(PI_DEPLOY_DIR)/assets/images/prerendered"; \
		rsync -avz build/assets/images/prerendered/ $(PI_SSH_TARGET):$(PI_DEPLOY_DIR)/assets/images/prerendered/; \
	fi
	@echo "$(CYAN)Starting helix-screen on $(PI_HOST) (foreground)...$(RESET)"
	ssh -t $(PI_SSH_TARGET) "cd $(PI_DEPLOY_DIR) && killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 0.5; ./config/helix-launcher.sh"

# Convenience: SSH into the Pi
pi-ssh:
	ssh $(PI_SSH_TARGET)

# Full cycle: build + deploy + run in foreground
pi-test: pi-docker deploy-pi-fg

# =============================================================================
# AD5M Deployment Configuration
# =============================================================================

# AD5M deployment settings (can override via environment or command line)
# Example: make deploy-ad5m AD5M_HOST=192.168.1.100
# Note: AD5M uses BusyBox and only has scp (no rsync), so we use scp -O for compatibility
#
# Deploy directory is auto-detected:
#   - KlipperMod: /root/printer_software/helixscreen (if /root/printer_software exists)
#   - Forge-X/Stock: /opt/helixscreen
# Override with AD5M_DEPLOY_DIR if needed.
AD5M_HOST ?= ad5m.local
AD5M_USER ?= root

# Build SSH target for AD5M
AD5M_SSH_TARGET := $(AD5M_USER)@$(AD5M_HOST)

# Auto-detect deploy directory (KlipperMod vs Forge-X/Stock)
# Can be overridden: make deploy-ad5m AD5M_DEPLOY_DIR=/custom/path
AD5M_DEPLOY_DIR ?= $(shell ssh -o ConnectTimeout=5 $(AD5M_SSH_TARGET) \
	"if [ -d /root/printer_software ]; then echo /root/printer_software/helixscreen; else echo /opt/helixscreen; fi" 2>/dev/null || echo /opt/helixscreen)

# =============================================================================
# AD5M Deployment Targets
# =============================================================================

.PHONY: deploy-ad5m deploy-ad5m-fg deploy-ad5m-bin ad5m-ssh ad5m-test

# Deploy full application to AD5M using rsync (fast incremental sync)
# Only transfers changed files, much faster than tar/scp for iterative development
deploy-ad5m:
	@test -f build/ad5m/bin/helix-screen || { echo "$(RED)Error: build/ad5m/bin/helix-screen not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@test -f build/ad5m/bin/helix-splash || { echo "$(RED)Error: build/ad5m/bin/helix-splash not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@# Generate pre-rendered images if missing (requires Python/PIL)
	@if [ ! -f build/assets/images/prerendered/splash-logo-small.bin ]; then \
		echo "$(CYAN)Generating pre-rendered splash images for AD5M...$(RESET)"; \
		$(MAKE) gen-images-ad5m; \
	fi
	@if [ ! -d build/assets/images/printers/prerendered ]; then \
		echo "$(CYAN)Generating pre-rendered printer images...$(RESET)"; \
		$(MAKE) gen-printer-images; \
	fi
	@echo "$(CYAN)Deploying HelixScreen to $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR) (rsync)...$(RESET)"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; mkdir -p $(AD5M_DEPLOY_DIR)"
	@# Clean up stale XML files from root (should be in ui_xml/)
	ssh $(AD5M_SSH_TARGET) "rm -f $(AD5M_DEPLOY_DIR)/*.xml 2>/dev/null || true"
	@# Sync binaries
	rsync -avz --progress build/ad5m/bin/helix-screen build/ad5m/bin/helix-splash $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/
	@if [ -f build/ad5m/bin/helix-watchdog ]; then rsync -avz build/ad5m/bin/helix-watchdog $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/; fi
	@# Sync assets (excluding test files and macOS junk)
	@# Use --checksum to skip files with same content (avoids re-transferring regenerated assets)
	rsync -avz --checksum --exclude='test_gcodes' --exclude='gcode' --exclude='.DS_Store' --exclude='*.pyc' --exclude='helixconfig.json' \
		ui_xml assets config moonraker-plugin $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/
	@# Sync pre-rendered images if they exist
	@if [ -d build/assets/images/prerendered ]; then \
		rsync -avz --checksum build/assets/images/prerendered/ $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/assets/images/prerendered/; \
	fi
	@if [ -d build/assets/images/printers/prerendered ]; then \
		rsync -avz --checksum build/assets/images/printers/prerendered/ $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/assets/images/printers/prerendered/; \
	fi
	@# Update init script in /etc/init.d/ if it differs from deployed version
	@echo "$(DIM)Checking init script...$(RESET)"
	@ssh $(AD5M_SSH_TARGET) '\
		INIT_SCRIPT=""; \
		if [ -f /etc/init.d/S80helixscreen ]; then INIT_SCRIPT="/etc/init.d/S80helixscreen"; \
		elif [ -f /etc/init.d/S90helixscreen ]; then INIT_SCRIPT="/etc/init.d/S90helixscreen"; fi; \
		if [ -n "$$INIT_SCRIPT" ]; then \
			if ! cmp -s "$$INIT_SCRIPT" "$(AD5M_DEPLOY_DIR)/config/helixscreen.init" 2>/dev/null; then \
				echo "Updating $$INIT_SCRIPT..."; \
				cp "$(AD5M_DEPLOY_DIR)/config/helixscreen.init" "$$INIT_SCRIPT"; \
				sed -i "s|DAEMON_DIR=\"/opt/helixscreen\"|DAEMON_DIR=\"$(AD5M_DEPLOY_DIR)\"|" "$$INIT_SCRIPT"; \
				chmod +x "$$INIT_SCRIPT"; \
				echo "Init script updated"; \
			else \
				echo "Init script unchanged"; \
			fi; \
		fi'
	@echo "$(GREEN)✓ Deployed to $(AD5M_HOST):$(AD5M_DEPLOY_DIR)$(RESET)"
	@echo "$(CYAN)Restarting helix-screen on $(AD5M_HOST)...$(RESET)"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 1; cd $(AD5M_DEPLOY_DIR) && ./config/helix-launcher.sh >/dev/null 2>&1 &"
	@echo "$(GREEN)✓ helix-screen restarted in background$(RESET)"
	@echo "$(DIM)Logs: ssh $(AD5M_SSH_TARGET) 'tail -f /var/log/messages | grep helix'$(RESET)"

# Legacy deploy using tar/scp (for systems without rsync)
deploy-ad5m-legacy:
	@test -f build/ad5m/bin/helix-screen || { echo "$(RED)Error: build/ad5m/bin/helix-screen not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@test -f build/ad5m/bin/helix-splash || { echo "$(RED)Error: build/ad5m/bin/helix-splash not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@# Generate pre-rendered images if missing (requires Python/PIL)
	@if [ ! -f build/assets/images/prerendered/splash-logo-small.bin ]; then \
		echo "$(CYAN)Generating pre-rendered splash images for AD5M...$(RESET)"; \
		$(MAKE) gen-images-ad5m; \
	fi
	@if [ ! -d build/assets/images/printers/prerendered ]; then \
		echo "$(CYAN)Generating pre-rendered printer images...$(RESET)"; \
		$(MAKE) gen-printer-images; \
	fi
	@echo "$(CYAN)Deploying HelixScreen to $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)...$(RESET)"
	@echo "  Binaries: helix-screen, helix-splash, helix-watchdog"
	@echo "  Assets: ui_xml/, assets/ (excl. test files), config/"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; mkdir -p $(AD5M_DEPLOY_DIR)"
	scp -O build/ad5m/bin/helix-screen build/ad5m/bin/helix-splash $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/
	@if [ -f build/ad5m/bin/helix-watchdog ]; then scp -O build/ad5m/bin/helix-watchdog $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/; fi
	@echo "$(DIM)Transferring assets (excluding test files)...$(RESET)"
	tar -cf - --exclude='test_gcodes' --exclude='gcode' --exclude='.DS_Store' ui_xml assets config | ssh $(AD5M_SSH_TARGET) "cd $(AD5M_DEPLOY_DIR) && tar -xf -"
	@if [ -d build/assets/images/prerendered ] && ls build/assets/images/prerendered/*.bin >/dev/null 2>&1; then \
		echo "$(DIM)Transferring pre-rendered splash images...$(RESET)"; \
		ssh $(AD5M_SSH_TARGET) "mkdir -p $(AD5M_DEPLOY_DIR)/assets/images/prerendered"; \
		scp -O build/assets/images/prerendered/*.bin $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/assets/images/prerendered/; \
	fi
	@if [ -d build/assets/images/printers/prerendered ] && ls build/assets/images/printers/prerendered/*.bin >/dev/null 2>&1; then \
		echo "$(DIM)Transferring pre-rendered printer images...$(RESET)"; \
		ssh $(AD5M_SSH_TARGET) "mkdir -p $(AD5M_DEPLOY_DIR)/assets/images/printers/prerendered"; \
		scp -O build/assets/images/printers/prerendered/*.bin $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/assets/images/printers/prerendered/; \
	fi
	@# Update init script in /etc/init.d/ if it differs from deployed version
	@echo "$(DIM)Checking init script...$(RESET)"
	@ssh $(AD5M_SSH_TARGET) '\
		INIT_SCRIPT=""; \
		if [ -f /etc/init.d/S80helixscreen ]; then INIT_SCRIPT="/etc/init.d/S80helixscreen"; \
		elif [ -f /etc/init.d/S90helixscreen ]; then INIT_SCRIPT="/etc/init.d/S90helixscreen"; fi; \
		if [ -n "$$INIT_SCRIPT" ]; then \
			if ! cmp -s "$$INIT_SCRIPT" "$(AD5M_DEPLOY_DIR)/config/helixscreen.init" 2>/dev/null; then \
				echo "Updating $$INIT_SCRIPT..."; \
				cp "$(AD5M_DEPLOY_DIR)/config/helixscreen.init" "$$INIT_SCRIPT"; \
				sed -i "s|DAEMON_DIR=\"/opt/helixscreen\"|DAEMON_DIR=\"$(AD5M_DEPLOY_DIR)\"|" "$$INIT_SCRIPT"; \
				chmod +x "$$INIT_SCRIPT"; \
				echo "Init script updated"; \
			else \
				echo "Init script unchanged"; \
			fi; \
		fi'
	@echo "$(GREEN)✓ Deployed to $(AD5M_HOST):$(AD5M_DEPLOY_DIR)$(RESET)"
	@echo "$(CYAN)Restarting helix-screen on $(AD5M_HOST)...$(RESET)"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 1; cd $(AD5M_DEPLOY_DIR) && ./config/helix-launcher.sh >/dev/null 2>&1 &"
	@echo "$(GREEN)✓ helix-screen restarted in background$(RESET)"
	@echo "$(DIM)Logs: ssh $(AD5M_SSH_TARGET) 'tail -f /var/log/messages | grep helix'$(RESET)"

# Deploy and run in foreground with verbose logging (for interactive debugging)
deploy-ad5m-fg:
	@test -f build/ad5m/bin/helix-screen || { echo "$(RED)Error: build/ad5m/bin/helix-screen not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@test -f build/ad5m/bin/helix-splash || { echo "$(RED)Error: build/ad5m/bin/helix-splash not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@# Generate pre-rendered images if missing (requires Python/PIL)
	@if [ ! -f build/assets/images/prerendered/splash-logo-small.bin ]; then \
		echo "$(CYAN)Generating pre-rendered splash images for AD5M...$(RESET)"; \
		$(MAKE) gen-images-ad5m; \
	fi
	@if [ ! -d build/assets/images/printers/prerendered ]; then \
		echo "$(CYAN)Generating pre-rendered printer images...$(RESET)"; \
		$(MAKE) gen-printer-images; \
	fi
	@echo "$(CYAN)Deploying HelixScreen to $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)...$(RESET)"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; mkdir -p $(AD5M_DEPLOY_DIR)"
	scp -O build/ad5m/bin/helix-screen build/ad5m/bin/helix-splash $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/
	@if [ -f build/ad5m/bin/helix-watchdog ]; then scp -O build/ad5m/bin/helix-watchdog $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/; fi
	@echo "$(DIM)Transferring assets (excluding test files)...$(RESET)"
	tar -cf - --exclude='test_gcodes' --exclude='gcode' --exclude='.DS_Store' ui_xml assets config | ssh $(AD5M_SSH_TARGET) "cd $(AD5M_DEPLOY_DIR) && tar -xf -"
	@if [ -d build/assets/images/prerendered ] && ls build/assets/images/prerendered/*.bin >/dev/null 2>&1; then \
		echo "$(DIM)Transferring pre-rendered splash images...$(RESET)"; \
		ssh $(AD5M_SSH_TARGET) "mkdir -p $(AD5M_DEPLOY_DIR)/assets/images/prerendered"; \
		scp -O build/assets/images/prerendered/*.bin $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/assets/images/prerendered/; \
	fi
	@if [ -d build/assets/images/printers/prerendered ] && ls build/assets/images/printers/prerendered/*.bin >/dev/null 2>&1; then \
		echo "$(DIM)Transferring pre-rendered printer images...$(RESET)"; \
		ssh $(AD5M_SSH_TARGET) "mkdir -p $(AD5M_DEPLOY_DIR)/assets/images/printers/prerendered"; \
		scp -O build/assets/images/printers/prerendered/*.bin $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/assets/images/printers/prerendered/; \
	fi
	@echo "$(CYAN)Starting helix-screen on $(AD5M_HOST) (foreground, verbose)...$(RESET)"
	ssh -t $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 1; cd $(AD5M_DEPLOY_DIR) && ./config/helix-launcher.sh --debug"

# Deploy binaries only (fast, for quick iteration)
deploy-ad5m-bin:
	@test -f build/ad5m/bin/helix-screen || { echo "$(RED)Error: build/ad5m/bin/helix-screen not found. Run 'make remote-ad5m' first.$(RESET)"; exit 1; }
	@echo "$(CYAN)Deploying binaries only to $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)...$(RESET)"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true"
	scp -O build/ad5m/bin/helix-screen build/ad5m/bin/helix-splash $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/
	@if [ -f build/ad5m/bin/helix-watchdog ]; then scp -O build/ad5m/bin/helix-watchdog $(AD5M_SSH_TARGET):$(AD5M_DEPLOY_DIR)/; fi
	@echo "$(GREEN)✓ Binaries deployed$(RESET)"
	@echo "$(CYAN)Restarting helix-screen on $(AD5M_HOST)...$(RESET)"
	ssh $(AD5M_SSH_TARGET) "killall helix-watchdog helix-screen helix-splash 2>/dev/null || true; sleep 1; cd $(AD5M_DEPLOY_DIR) && ./config/helix-launcher.sh >/dev/null 2>&1 &"
	@echo "$(GREEN)✓ helix-screen restarted$(RESET)"

# Convenience: SSH into the AD5M
ad5m-ssh:
	ssh $(AD5M_SSH_TARGET)

# Full cycle: remote build + deploy + run in foreground
ad5m-test: remote-ad5m deploy-ad5m-fg

# =============================================================================
# Release Packaging
# =============================================================================
# Creates distributable tar.gz archives for each platform
# Includes: binaries, ui_xml, config, assets (fonts/images only, no test files)

RELEASE_DIR := releases
VERSION := $(shell cat VERSION.txt 2>/dev/null || echo "dev")

# Assets to include (exclude test_gcodes, gcode test files)
RELEASE_ASSETS := assets/fonts assets/images

.PHONY: release-pi release-ad5m release-all release-clean

# Package Pi release
release-pi: | build/pi/bin/helix-screen build/pi/bin/helix-splash
	@echo "$(CYAN)$(BOLD)Packaging Pi release v$(VERSION)...$(RESET)"
	@mkdir -p $(RELEASE_DIR)/helixscreen
	@cp build/pi/bin/helix-screen build/pi/bin/helix-splash $(RELEASE_DIR)/helixscreen/
	@cp -r ui_xml config $(RELEASE_DIR)/helixscreen/
	@mkdir -p $(RELEASE_DIR)/helixscreen/scripts
	@cp scripts/uninstall.sh $(RELEASE_DIR)/helixscreen/scripts/
	@mkdir -p $(RELEASE_DIR)/helixscreen/assets
	@for asset in $(RELEASE_ASSETS); do \
		if [ -d "$$asset" ]; then cp -r "$$asset" $(RELEASE_DIR)/helixscreen/assets/; fi; \
	done
	@# Copy pre-rendered images from build directory (splash + printer images)
	@if [ -d "build/assets/images/prerendered" ]; then \
		mkdir -p $(RELEASE_DIR)/helixscreen/assets/images/prerendered; \
		cp -r build/assets/images/prerendered/* $(RELEASE_DIR)/helixscreen/assets/images/prerendered/; \
	fi
	@if [ -d "build/assets/images/printers/prerendered" ]; then \
		mkdir -p $(RELEASE_DIR)/helixscreen/assets/images/printers/prerendered; \
		cp -r build/assets/images/printers/prerendered/* $(RELEASE_DIR)/helixscreen/assets/images/printers/prerendered/; \
	fi
	@find $(RELEASE_DIR)/helixscreen -name '.DS_Store' -delete 2>/dev/null || true
	@cd $(RELEASE_DIR) && COPYFILE_DISABLE=1 tar -czvf helixscreen-pi-$(VERSION).tar.gz helixscreen
	@rm -rf $(RELEASE_DIR)/helixscreen
	@echo "$(GREEN)✓ Created $(RELEASE_DIR)/helixscreen-pi-$(VERSION).tar.gz$(RESET)"
	@ls -lh $(RELEASE_DIR)/helixscreen-pi-$(VERSION).tar.gz

# Package AD5M release
# Note: AD5M uses BusyBox which doesn't support tar -z, so we create uncompressed tar + gzip separately
# Includes pre-configured helixconfig.json for Adventurer 5M Pro (skips setup wizard)
release-ad5m: | build/ad5m/bin/helix-screen build/ad5m/bin/helix-splash
	@echo "$(CYAN)$(BOLD)Packaging AD5M release v$(VERSION)...$(RESET)"
	@mkdir -p $(RELEASE_DIR)/helixscreen
	@cp build/ad5m/bin/helix-screen build/ad5m/bin/helix-splash $(RELEASE_DIR)/helixscreen/
	@cp -r ui_xml config $(RELEASE_DIR)/helixscreen/
	@# Copy AD5M Pro default config as config/helixconfig.json (skips wizard on first run)
	@cp config/printers/adventurer-5m-pro.json $(RELEASE_DIR)/helixscreen/config/helixconfig.json
	@echo "  $(DIM)Included pre-configured config/helixconfig.json for AD5M Pro$(RESET)"
	@mkdir -p $(RELEASE_DIR)/helixscreen/scripts
	@cp scripts/uninstall.sh $(RELEASE_DIR)/helixscreen/scripts/
	@mkdir -p $(RELEASE_DIR)/helixscreen/assets
	@for asset in $(RELEASE_ASSETS); do \
		if [ -d "$$asset" ]; then cp -r "$$asset" $(RELEASE_DIR)/helixscreen/assets/; fi; \
	done
	@# Copy pre-rendered images from build directory (splash + printer images)
	@if [ -d "build/assets/images/prerendered" ]; then \
		mkdir -p $(RELEASE_DIR)/helixscreen/assets/images/prerendered; \
		cp -r build/assets/images/prerendered/* $(RELEASE_DIR)/helixscreen/assets/images/prerendered/; \
	fi
	@if [ -d "build/assets/images/printers/prerendered" ]; then \
		mkdir -p $(RELEASE_DIR)/helixscreen/assets/images/printers/prerendered; \
		cp -r build/assets/images/printers/prerendered/* $(RELEASE_DIR)/helixscreen/assets/images/printers/prerendered/; \
	fi
	@find $(RELEASE_DIR)/helixscreen -name '.DS_Store' -delete 2>/dev/null || true
	@cd $(RELEASE_DIR) && COPYFILE_DISABLE=1 tar -czvf helixscreen-ad5m-$(VERSION).tar.gz helixscreen
	@rm -rf $(RELEASE_DIR)/helixscreen
	@echo "$(GREEN)✓ Created $(RELEASE_DIR)/helixscreen-ad5m-$(VERSION).tar.gz$(RESET)"
	@ls -lh $(RELEASE_DIR)/helixscreen-ad5m-$(VERSION).tar.gz

# Package all releases
release-all: release-pi release-ad5m
	@echo "$(GREEN)$(BOLD)✓ All releases packaged in $(RELEASE_DIR)/$(RESET)"
	@ls -lh $(RELEASE_DIR)/*.tar.gz

# Clean release artifacts
release-clean:
	@rm -rf $(RELEASE_DIR)
	@echo "$(GREEN)✓ Release directory cleaned$(RESET)"

# Aliases for package-* (matches scripts/package.sh naming)
# These trigger the full build + package workflow
.PHONY: package-ad5m package-pi package-all package-clean
package-ad5m: ad5m-docker gen-images-ad5m gen-printer-images release-ad5m
package-pi: pi-docker gen-images gen-printer-images release-pi
package-all: package-ad5m package-pi
package-clean: release-clean
