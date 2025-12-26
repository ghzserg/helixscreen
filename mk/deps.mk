# Copyright (c) 2025 Preston Brown <pbrown@brown-house.net>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen UI Prototype - Dependency Management Module
# Handles dependency checking, installation, and libhv building

# Python virtual environment for build-time dependencies
VENV := .venv
VENV_PYTHON := $(VENV)/bin/python3
VENV_PIP := $(VENV)/bin/pip3

# Dependency checker - calls modular script for maintainability
# Set SKIP_OPTIONAL_DEPS=1 for cross-compilation builds (minimal check)
#
# The script is organized into functions by dependency category:
#   - check_essential: CC, CXX, make, pkg-config
#   - check_submodules: LVGL, spdlog, libhv, wpa_supplicant
#   - check_libraries: fmt, OpenSSL
#   - check_desktop_tools: SDL2, npm, python venv, clang-format (--minimal skips)
#   - check_canvas_libs: cairo, pango, libpng (--minimal skips)
#
check-deps:
ifeq ($(SKIP_OPTIONAL_DEPS),1)
	@CC="$(CC)" CXX="$(CXX)" ENABLE_SSL="$(ENABLE_SSL)" \
		LVGL_DIR="$(LVGL_DIR)" SPDLOG_DIR="$(SPDLOG_DIR)" \
		LIBHV_DIR="$(LIBHV_DIR)" WPA_DIR="$(WPA_DIR)" VENV="$(VENV)" \
		./scripts/check-deps.sh --minimal
else
	@CC="$(CC)" CXX="$(CXX)" ENABLE_SSL="$(ENABLE_SSL)" \
		LVGL_DIR="$(LVGL_DIR)" SPDLOG_DIR="$(SPDLOG_DIR)" \
		LIBHV_DIR="$(LIBHV_DIR)" WPA_DIR="$(WPA_DIR)" VENV="$(VENV)" \
		./scripts/check-deps.sh
endif
	@# Auto-enable git hooks if not already set up
	@if [ -f ".githooks/pre-commit" ] && [ "$$(git config core.hooksPath 2>/dev/null)" != ".githooks" ]; then \
		git config core.hooksPath .githooks; \
		echo "$(GREEN)✓ Git pre-commit hooks enabled (auto-format with clang-format)$(RESET)"; \
	fi

# Auto-install missing dependencies (interactive, requires confirmation)
install-deps:
	$(ECHO) "$(CYAN)$(BOLD)Dependency Auto-Installer$(RESET)"
	$(ECHO) ""
	@if [ "$(UNAME_S)" = "Darwin" ]; then \
		PLATFORM_TYPE="macOS"; \
		PKG_MGR="brew"; \
	elif [ -f /etc/debian_version ]; then \
		PLATFORM_TYPE="Debian/Ubuntu"; \
		PKG_MGR="apt"; \
	elif [ -f /etc/fedora-release ] || [ -f /etc/redhat-release ]; then \
		PLATFORM_TYPE="Fedora/RHEL"; \
		PKG_MGR="dnf"; \
	else \
		PLATFORM_TYPE="Unknown"; \
		PKG_MGR="unknown"; \
	fi; \
	add_pkg() { \
		case "$$1:$$PKG_MGR" in \
			sdl2:brew) echo "sdl2";; \
			sdl2:apt) echo "libsdl2-dev";; \
			sdl2:dnf) echo "SDL2-devel";; \
			npm:brew) echo "node";; \
			npm:*) echo "npm";; \
			python3-venv:apt) echo "python3-venv";; \
			python3-venv:dnf) echo "python3-libs";; \
			python3-venv:brew) echo "";; \
			clang-format:brew|clang-format:apt) echo "clang-format";; \
			clang-format:dnf) echo "clang-tools-extra";; \
			xmllint:brew) echo "libxml2";; \
			xmllint:apt) echo "libxml2-utils";; \
			xmllint:dnf) echo "libxml2";; \
			pkg-config:brew|pkg-config:apt) echo "pkg-config";; \
			pkg-config:dnf) echo "pkgconfig";; \
			fmt:brew) echo "fmt";; \
			fmt:apt) echo "libfmt-dev";; \
			fmt:dnf) echo "fmt-devel";; \
			cairo:brew) echo "cairo";; \
			cairo:apt) echo "libcairo2-dev";; \
			cairo:dnf) echo "cairo-devel";; \
			pango:brew) echo "pango";; \
			pango:apt) echo "libpango1.0-dev";; \
			pango:dnf) echo "pango-devel";; \
			libpng:brew) echo "libpng";; \
			libpng:apt) echo "libpng-dev";; \
			libpng:dnf) echo "libpng-devel";; \
			libjpeg:brew) echo "jpeg";; \
			libjpeg:apt) echo "libjpeg-dev";; \
			libjpeg:dnf) echo "libjpeg-turbo-devel";; \
			librsvg:brew) echo "librsvg";; \
			librsvg:apt) echo "librsvg2-dev";; \
			librsvg:dnf) echo "librsvg2-devel";; \
			openssl:brew) echo "openssl";; \
			openssl:apt) echo "libssl-dev";; \
			openssl:dnf) echo "openssl-devel";; \
			docker-buildx:brew) echo "docker-buildx";; \
			docker-buildx:apt) echo "";; \
			docker-buildx:dnf) echo "";; \
			*) echo "$$1";; \
		esac; \
	}; \
	echo "$(CYAN)Detected platform:$(RESET) $$PLATFORM_TYPE"; \
	echo ""; \
	INSTALL_NEEDED=0; TO_INSTALL=""; \
	if ! command -v sdl2-config >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg sdl2)"; \
	fi; \
	if ! command -v npm >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg npm)"; \
	fi; \
	if ! command -v python3 >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL python3"; \
	elif ! python3 -c "import venv, ensurepip" 2>/dev/null; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg python3-venv)"; \
	fi; \
	if ! command -v clang >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL clang"; \
	fi; \
	if ! command -v clang-format >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg clang-format)"; \
	fi; \
	if ! command -v xmllint >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg xmllint)"; \
	fi; \
	if ! command -v pkg-config >/dev/null 2>&1; then \
		INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg pkg-config)"; \
	else \
		if ! pkg-config --exists fmt 2>/dev/null; then \
			INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg fmt)"; \
		fi; \
		if ! pkg-config --exists cairo 2>/dev/null; then \
			INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg cairo)"; \
		fi; \
		if ! pkg-config --exists pango 2>/dev/null; then \
			INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg pango)"; \
		fi; \
		if ! pkg-config --exists libpng 2>/dev/null; then \
			INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg libpng)"; \
		fi; \
		if ! pkg-config --exists libjpeg 2>/dev/null; then \
			INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg libjpeg)"; \
		fi; \
		if ! pkg-config --exists librsvg-2.0 2>/dev/null; then \
			INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg librsvg)"; \
		fi; \
		if [ "$(UNAME_S)" != "Darwin" ]; then \
			if ! pkg-config --exists openssl 2>/dev/null && ! pkg-config --exists libssl 2>/dev/null; then \
				if [ ! -f "/usr/include/openssl/ssl.h" ] && [ ! -f "/usr/local/include/openssl/ssl.h" ]; then \
					INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$(add_pkg openssl)"; \
				fi; \
			fi; \
		fi; \
	fi; \
	if command -v docker >/dev/null 2>&1; then \
		if ! docker buildx version >/dev/null 2>&1; then \
			BUILDX_PKG=$$(add_pkg docker-buildx); \
			if [ -n "$$BUILDX_PKG" ]; then \
				INSTALL_NEEDED=1; TO_INSTALL="$$TO_INSTALL $$BUILDX_PKG"; \
			else \
				echo "$(YELLOW)⚠ docker buildx not found (cross-compilation feature)$(RESET)"; \
				echo "  See: https://docs.docker.com/go/buildx/"; \
			fi; \
		fi; \
	fi; \
	if [ $$INSTALL_NEEDED -eq 0 ]; then \
		echo "$(GREEN)✓ No missing system dependencies$(RESET)"; \
	else \
		echo "$(YELLOW)The following packages will be installed:$(RESET)$$TO_INSTALL"; \
		echo ""; \
		if [ "$$PKG_MGR" = "brew" ]; then \
			CMD="brew install$$TO_INSTALL"; \
		elif [ "$$PKG_MGR" = "apt" ]; then \
			CMD="sudo apt update && sudo apt install -y$$TO_INSTALL"; \
		elif [ "$$PKG_MGR" = "dnf" ]; then \
			CMD="sudo dnf install -y$$TO_INSTALL"; \
		else \
			echo "$(RED)✗ Unknown package manager for platform: $$PLATFORM_TYPE$(RESET)"; \
			exit 1; \
		fi; \
		echo "$(CYAN)Command:$(RESET) $$CMD"; \
		echo ""; \
		read -p "$(YELLOW)Continue? [y/N]:$(RESET) " -n 1 -r; \
		echo ""; \
		if [[ $$REPLY =~ ^[Yy]$$ ]]; then \
			echo "$(CYAN)Installing...$(RESET)"; \
			eval $$CMD || { echo "$(RED)✗ Installation failed$(RESET)"; exit 1; }; \
			echo "$(GREEN)✓ System packages installed$(RESET)"; \
		else \
			echo "$(YELLOW)Installation cancelled$(RESET)"; \
			exit 1; \
		fi; \
	fi; \
	echo ""; \
	if [ ! -d "$(LVGL_DIR)/src" ]; then \
		echo "$(CYAN)Initializing git submodules...$(RESET)"; \
		git submodule update --init --recursive && echo "$(GREEN)✓ Submodules initialized$(RESET)" || echo "$(RED)✗ Submodule init failed$(RESET)"; \
	else \
		echo "$(GREEN)✓ Submodules already initialized$(RESET)"; \
	fi; \
	echo ""; \
	if ! command -v npm >/dev/null 2>&1; then \
		echo "$(YELLOW)⚠ npm not available - skipping npm install$(RESET)"; \
	elif [ ! -f "node_modules/.bin/lv_font_conv" ]; then \
		echo "$(CYAN)Installing npm packages (lv_font_conv, lv_img_conv)...$(RESET)"; \
		npm install && echo "$(GREEN)✓ npm packages installed$(RESET)" || echo "$(RED)✗ npm install failed$(RESET)"; \
	else \
		echo "$(GREEN)✓ npm packages already installed$(RESET)"; \
	fi; \
	echo ""; \
	if ! command -v python3 >/dev/null 2>&1; then \
		echo "$(YELLOW)⚠ python3 not available - skipping venv setup$(RESET)"; \
	else \
		NEED_VENV_SETUP=0; \
		if [ ! -f "$(VENV_PYTHON)" ]; then \
			NEED_VENV_SETUP=1; \
		elif ! $(VENV_PYTHON) -c "import png" >/dev/null 2>&1 || ! $(VENV_PYTHON) -c "import lz4" >/dev/null 2>&1; then \
			echo "$(YELLOW)⚠ Python packages missing in venv$(RESET)"; \
			NEED_VENV_SETUP=1; \
		fi; \
		if [ $$NEED_VENV_SETUP -eq 1 ]; then \
			echo "$(CYAN)Setting up Python venv and installing packages...$(RESET)"; \
			$(MAKE) venv-setup && echo "$(GREEN)✓ Python venv set up$(RESET)" || echo "$(RED)✗ venv setup failed$(RESET)"; \
		else \
			echo "$(GREEN)✓ Python venv already set up$(RESET)"; \
		fi; \
	fi; \
	echo ""; \
	if [ ! -f "$(LIBHV_LIB)" ]; then \
		echo "$(CYAN)Building libhv...$(RESET)"; \
		$(MAKE) libhv-build && echo "$(GREEN)✓ libhv built$(RESET)" || echo "$(RED)✗ libhv build failed$(RESET)"; \
	else \
		echo "$(GREEN)✓ libhv already built$(RESET)"; \
	fi; \
	echo ""; \
	echo "$(GREEN)$(BOLD)✓ Dependency installation complete!$(RESET)"; \
	echo "$(CYAN)Run$(RESET) $(YELLOW)make$(RESET) $(CYAN)to build the project$(RESET)"

# Build libhv (configure + compile)
# Supports native and cross-compilation via CROSS_COMPILE variable
# Output: $(BUILD_DIR)/lib/libhv.a for architecture isolation
# Note: libhv builds in-tree, so we must clean before cross-compilation
# to avoid mixing object files from different architectures

# =============================================================================
# Library Clean Targets
# =============================================================================
# Individual clean targets for forcing rebuilds after flag changes or
# architecture switches. The main 'clean' target calls these automatically,
# but they're useful for targeted rebuilds without full clean.

# Clean libhv build artifacts
libhv-clean:
	$(ECHO) "$(CYAN)Cleaning libhv build artifacts...$(RESET)"
	$(Q)find $(LIBHV_DIR) -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.dylib' \) -delete 2>/dev/null || true
	$(Q)rm -f $(BUILD_DIR)/lib/libhv.a 2>/dev/null || true
	$(ECHO) "$(GREEN)✓ libhv cleaned$(RESET)"

# Clean SDL2 build artifacts (CMake build directory)
# Note: SDL2_BUILD_DIR is only set when building SDL from submodule
sdl2-clean:
	$(ECHO) "$(CYAN)Cleaning SDL2 build artifacts...$(RESET)"
ifdef SDL2_BUILD_DIR
	$(Q)rm -rf $(SDL2_BUILD_DIR) 2>/dev/null || true
else
	$(Q)rm -rf lib/sdl2/build 2>/dev/null || true
endif
	$(ECHO) "$(GREEN)✓ SDL2 cleaned$(RESET)"

# Clean LVGL compiled objects (forces full recompile)
lvgl-clean:
	$(ECHO) "$(CYAN)Cleaning LVGL build artifacts...$(RESET)"
	$(Q)rm -rf $(OBJ_DIR)/lvgl 2>/dev/null || true
	$(ECHO) "$(GREEN)✓ LVGL cleaned$(RESET)"

# Clean all submodule libraries
libs-clean: libhv-clean sdl2-clean lvgl-clean
	$(ECHO) "$(GREEN)✓ All library artifacts cleaned$(RESET)"

# =============================================================================
# Library Build Targets
# =============================================================================

libhv-build:
	$(ECHO) "$(CYAN)Building libhv...$(RESET)"
	$(Q)mkdir -p $(BUILD_DIR)/lib
ifneq ($(CROSS_COMPILE),)
	# Cross-compilation mode - ALWAYS clean first to avoid architecture mixing and stale artifacts
	# This is critical: mixing native and cross-compiled objects causes subtle runtime bugs
	$(Q)echo "$(YELLOW)→ Cleaning libhv for cross-compilation...$(RESET)"
	$(Q)find $(LIBHV_DIR) -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.dylib' \) -delete 2>/dev/null || true
	# Pass cross-compiler to configure and make
	$(Q)cd $(LIBHV_DIR) && \
		CC=$(CC) CXX=$(CXX) AR=$(AR) \
		CFLAGS="$(TARGET_CFLAGS)" \
		CXXFLAGS="$(TARGET_CFLAGS)" \
		./configure --with-http-client
	$(Q)CC=$(CC) CXX=$(CXX) AR=$(AR) $(MAKE) -C $(LIBHV_DIR) libhv
else ifeq ($(UNAME_S),Darwin)
	$(Q)cd $(LIBHV_DIR) && \
		MACOSX_DEPLOYMENT_TARGET=$(MACOS_MIN_VERSION) \
		CFLAGS="$(MACOS_DEPLOYMENT_TARGET)" \
		CXXFLAGS="$(MACOS_DEPLOYMENT_TARGET)" \
		./configure --with-http-client
	$(Q)MACOSX_DEPLOYMENT_TARGET=$(MACOS_MIN_VERSION) $(MAKE) -C $(LIBHV_DIR) libhv
else
	$(Q)cd $(LIBHV_DIR) && ./configure --with-http-client
	$(Q)$(MAKE) -C $(LIBHV_DIR) libhv
endif
	# Copy built library to architecture-specific output directory
	$(Q)cp $(LIBHV_DIR)/lib/libhv.a $(BUILD_DIR)/lib/libhv.a 2>/dev/null || \
		cp $(LIBHV_DIR)/libhv.a $(BUILD_DIR)/lib/libhv.a
	$(ECHO) "$(GREEN)✓ libhv built: $(BUILD_DIR)/lib/libhv.a$(RESET)"

# Build SDL2 from submodule (CMake build)
sdl2-build:
	$(ECHO) "$(CYAN)Building SDL2 from submodule...$(RESET)"
	$(Q)mkdir -p $(SDL2_BUILD_DIR)
ifeq ($(UNAME_S),Darwin)
	$(Q)cd $(SDL2_BUILD_DIR) && \
		MACOSX_DEPLOYMENT_TARGET=$(MACOS_MIN_VERSION) \
		cmake .. \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOS_MIN_VERSION) \
			-DSDL_SHARED=OFF \
			-DSDL_STATIC=ON \
			-DSDL_TEST=OFF \
			-DSDL_TESTS=OFF
	$(Q)MACOSX_DEPLOYMENT_TARGET=$(MACOS_MIN_VERSION) cmake --build $(SDL2_BUILD_DIR) --config Release
else
	$(Q)cd $(SDL2_BUILD_DIR) && \
		cmake .. \
			-DCMAKE_BUILD_TYPE=Release \
			-DSDL_SHARED=OFF \
			-DSDL_STATIC=ON \
			-DSDL_TEST=OFF \
			-DSDL_TESTS=OFF
	$(Q)cmake --build $(SDL2_BUILD_DIR) --config Release
endif
	$(ECHO) "$(GREEN)✓ SDL2 built successfully$(RESET)"

$(SDL2_LIB):
	$(Q)$(MAKE) sdl2-build

# Build wpa_supplicant client library (Linux and cross-compilation targets)
# This is needed for WiFi control on embedded Linux systems
# Output: $(BUILD_DIR)/lib/libwpa_client.a for architecture isolation
#
# NOTE: Make conditionals (ifneq/else/endif) CANNOT be used inside recipes!
# They are processed at parse time, not run time. Use shell conditionals instead.

# Linux native build (not macOS, not cross-compiling)
ifeq ($(UNAME_S),Linux)
ifndef CROSS_COMPILE
$(WPA_CLIENT_LIB): | $(BUILD_DIR)/lib
	$(ECHO) "$(BOLD)$(BLUE)[WPA]$(RESET) Building wpa_supplicant client library..."
	$(Q)if [ ! -f "$(WPA_DIR)/wpa_supplicant/.config" ]; then \
		if [ -f "$(WPA_DIR)/wpa_supplicant/defconfig" ]; then \
			echo "$(CYAN)→ Creating .config from defconfig...$(RESET)"; \
			cp "$(WPA_DIR)/wpa_supplicant/defconfig" "$(WPA_DIR)/wpa_supplicant/.config"; \
		else \
			echo "$(RED)✗ wpa_supplicant/.config not found and no defconfig$(RESET)"; \
			echo "  Expected at: $(WPA_DIR)/wpa_supplicant/.config"; \
			exit 1; \
		fi; \
	fi
	$(Q)$(MAKE) -C $(WPA_DIR)/wpa_supplicant libwpa_client.a
	$(Q)rm -f $(BUILD_DIR)/lib/libwpa_client.a 2>/dev/null || true
	$(Q)cp $(WPA_DIR)/wpa_supplicant/libwpa_client.a $(BUILD_DIR)/lib/libwpa_client.a
	$(Q)$(RANLIB) $(BUILD_DIR)/lib/libwpa_client.a
	$(ECHO) "$(GREEN)✓ libwpa_client.a built: $(BUILD_DIR)/lib/libwpa_client.a$(RESET)"
endif
endif

# Cross-compilation build (from any host to Linux target)
ifdef CROSS_COMPILE
# wpa_supplicant CFLAGS: Strip LTO and section flags since wpa_supplicant is a separate
# build system that doesn't participate in our LTO linking. LTO-compiled .a files contain
# GIMPLE IR instead of machine code, which breaks linking when mixed with our LTO objects.
# Use EXTRA_CFLAGS since wpa_supplicant's Makefile appends EXTRA_CFLAGS to its internal flags.
WPA_CFLAGS := $(filter-out -flto -ffunction-sections -fdata-sections,$(TARGET_CFLAGS))

$(WPA_CLIENT_LIB): | $(BUILD_DIR)/lib
	$(ECHO) "$(BOLD)$(BLUE)[WPA]$(RESET) Building wpa_supplicant client library (cross-compile)..."
	$(Q)if [ ! -f "$(WPA_DIR)/wpa_supplicant/.config" ]; then \
		if [ -f "$(WPA_DIR)/wpa_supplicant/defconfig" ]; then \
			echo "$(CYAN)→ Creating .config from defconfig...$(RESET)"; \
			cp "$(WPA_DIR)/wpa_supplicant/defconfig" "$(WPA_DIR)/wpa_supplicant/.config"; \
		else \
			echo "$(RED)✗ wpa_supplicant/.config not found and no defconfig$(RESET)"; \
			exit 1; \
		fi; \
	fi
	$(Q)if [ -f "$(WPA_DIR)/wpa_supplicant/libwpa_client.a" ] || [ -d "$(WPA_DIR)/build" ]; then \
		echo "$(YELLOW)→ Cleaning wpa_supplicant in-tree artifacts for cross-compilation...$(RESET)"; \
		$(MAKE) -C $(WPA_DIR)/wpa_supplicant clean; \
		rm -rf $(WPA_DIR)/build; \
	fi
	@# Use env -u to unset inherited CFLAGS from parent make, then set clean values
	@# wpa_supplicant uses EXTRA_CFLAGS for additional flags
	$(Q)env -u CFLAGS CC="$(CC)" EXTRA_CFLAGS="$(WPA_CFLAGS)" $(MAKE) -C $(WPA_DIR)/wpa_supplicant libwpa_client.a
	$(Q)rm -f $(BUILD_DIR)/lib/libwpa_client.a 2>/dev/null || true
	$(Q)cp $(WPA_DIR)/wpa_supplicant/libwpa_client.a $(BUILD_DIR)/lib/libwpa_client.a
	$(Q)$(RANLIB) $(BUILD_DIR)/lib/libwpa_client.a
	$(ECHO) "$(GREEN)✓ libwpa_client.a built: $(BUILD_DIR)/lib/libwpa_client.a$(RESET)"
endif

# Python virtual environment setup
venv-setup:
	$(ECHO) "$(CYAN)Setting up Python virtual environment...$(RESET)"
	$(Q)if [ ! -f "$(VENV_PIP)" ]; then \
		rm -rf $(VENV) 2>/dev/null; \
		python3 -m venv $(VENV) || { \
			echo "$(RED)✗ Failed to create venv$(RESET)"; \
			echo "$(YELLOW)On Debian/Ubuntu, install: sudo apt install python3-venv$(RESET)"; \
			exit 1; \
		}; \
		if [ ! -f "$(VENV_PIP)" ]; then \
			echo "$(RED)✗ venv created but pip missing - python3-venv may not be fully installed$(RESET)"; \
			echo "$(YELLOW)On Debian/Ubuntu, install: sudo apt install python3-venv$(RESET)"; \
			rm -rf $(VENV); \
			exit 1; \
		fi; \
		echo "$(GREEN)✓ Virtual environment created$(RESET)"; \
	else \
		echo "$(GREEN)✓ Virtual environment exists$(RESET)"; \
	fi
	$(Q)if [ -f "requirements.txt" ]; then \
		echo "$(CYAN)Installing Python packages from requirements.txt...$(RESET)"; \
		$(VENV_PIP) install -r requirements.txt || { echo "$(RED)✗ Failed to install requirements$(RESET)"; exit 1; }; \
		echo "$(GREEN)✓ Python packages installed$(RESET)"; \
	else \
		echo "$(YELLOW)⚠ requirements.txt not found$(RESET)"; \
	fi
	$(ECHO) "$(GREEN)✓ Python venv setup complete$(RESET)"

$(VENV_PYTHON):
	$(Q)$(MAKE) venv-setup

# ============================================================================
# Git Hooks Setup
# ============================================================================

.PHONY: setup-hooks
setup-hooks:
	$(ECHO) "$(CYAN)Setting up git hooks...$(RESET)"
	$(Q)if [ -f ".githooks/pre-commit" ]; then \
		git config core.hooksPath .githooks; \
		echo "$(GREEN)✓ Git hooks enabled (.githooks/pre-commit)$(RESET)"; \
		echo "$(CYAN)  Pre-commit will auto-format C/C++ files with clang-format$(RESET)"; \
	else \
		echo "$(RED)✗ .githooks/pre-commit not found$(RESET)"; \
		exit 1; \
	fi

# ============================================================================
# Build/Dependency Help
# ============================================================================

.PHONY: libhv-clean sdl2-clean lvgl-clean libs-clean help-build
help-build:
	@if [ -t 1 ] && [ -n "$(TERM)" ] && [ "$(TERM)" != "dumb" ]; then \
		B='$(BOLD)'; G='$(GREEN)'; Y='$(YELLOW)'; C='$(CYAN)'; X='$(RESET)'; \
	else \
		B=''; G=''; Y=''; C=''; X=''; \
	fi; \
	echo "$${B}Build & Dependency Targets$${X}"; \
	echo ""; \
	echo "$${C}Core Build:$${X}"; \
	echo "  $${G}all$${X}                 - Build main binary (default)"; \
	echo "  $${G}build$${X}               - Clean build with progress"; \
	echo "  $${G}clean$${X}               - Remove all build artifacts"; \
	echo "  $${G}run$${X}                 - Build and run the UI"; \
	echo ""; \
	echo "$${C}Dependency Management:$${X}"; \
	echo "  $${G}check-deps$${X}          - Verify all dependencies installed"; \
	echo "  $${G}install-deps$${X}        - Auto-install missing dependencies"; \
	echo "  $${G}libhv-build$${X}         - Build libhv WebSocket library"; \
	echo "  $${G}sdl2-build$${X}          - Build SDL2 from submodule"; \
	echo "  $${G}libhv-clean$${X}         - Clean libhv artifacts (force rebuild)"; \
	echo "  $${G}sdl2-clean$${X}          - Clean SDL2 build directory"; \
	echo "  $${G}lvgl-clean$${X}          - Clean LVGL compiled objects"; \
	echo "  $${G}libs-clean$${X}          - Clean all library artifacts"; \
	echo "  $${G}venv-setup$${X}          - Set up Python virtual environment"; \
	echo "  $${G}setup-hooks$${X}         - Enable git pre-commit hooks (clang-format)"; \
	echo ""; \
	echo "$${C}Patches:$${X}"; \
	echo "  $${G}apply-patches$${X}       - Apply LVGL/libhv patches (idempotent)"; \
	echo "  $${G}reset-patches$${X}       - Reset patched files to upstream"; \
	echo "  $${G}reapply-patches$${X}     - Force reapply all patches"; \
	echo ""; \
	echo "$${C}Code Quality:$${X}"; \
	echo "  $${G}format$${X}              - Auto-format all C/C++ and XML"; \
	echo "  $${G}format-staged$${X}       - Format only staged files"; \
	echo "  $${G}compile_commands$${X}    - Generate compile_commands.json"; \
	echo ""; \
	echo "$${C}Libraries (embedded targets):$${X}"; \
	echo "  $${G}display-lib$${X}         - Build display backend library"; \
	echo "  $${G}splash$${X}              - Build splash screen binary"; \
	echo ""; \
	echo "$${C}Build Options:$${X}"; \
	echo "  $${Y}V=1$${X}                 - Verbose (show compiler commands)"; \
	echo "  $${Y}JOBS=N$${X}              - Parallel job count"; \
	echo "  $${Y}NO_COLOR=1$${X}          - Disable colored output"; \
	echo "  $${Y}ENABLE_TINYGL_3D$${X}=no - Disable 3D rendering"
