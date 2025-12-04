# Build System Documentation

This document describes the HelixScreen prototype build system, including automatic patch application, multi-display support, and development workflows.

**For common development tasks**, see **[DEVELOPMENT.md](../DEVELOPMENT.md)** - this document covers advanced build system internals.

## Cross-Compilation (Embedded Targets)

HelixScreen supports cross-compilation for embedded ARM targets using Docker-based toolchains. This allows building binaries for Raspberry Pi and other embedded displays directly from macOS or Linux development machines.

### Quick Start

```bash
# Build for Raspberry Pi (aarch64/ARM64)
make pi-docker

# Build for Flashforge Adventurer 5M (armv7-a/ARM32)
make ad5m-docker

# Verify the binaries
file build/pi/bin/helix-screen    # ELF 64-bit LSB, ARM aarch64
file build/ad5m/bin/helix-screen  # ELF 32-bit LSB, ARM EABI5
```

Docker images are **automatically built** on first use - no manual setup required!

### Supported Targets

| Target | Command | Architecture | Display | Output Directory |
|--------|---------|--------------|---------|------------------|
| **Raspberry Pi** | `make pi-docker` | aarch64 (ARM64) | DRM/fbdev | `build/pi/` |
| **Adventurer 5M** | `make ad5m-docker` | armv7-a (hard-float) | fbdev | `build/ad5m/` |
| **Native (SDL)** | `make` | Host architecture | SDL2 | `build/` |

### How It Works

1. **Docker Toolchains**: Each target has a Dockerfile (`docker/Dockerfile.pi`, `docker/Dockerfile.ad5m`) that contains the cross-compiler, sysroot libraries, and build tools.

2. **Auto-Build Images**: When you run `make pi-docker` or `make ad5m-docker`, the build system automatically:
   - Checks if the Docker image exists
   - Builds the image if missing (takes 2-5 minutes first time)
   - Runs the cross-compilation inside the container

3. **Volume Mounting**: Your source code is mounted into the container, so compiled binaries appear directly in your `build/` directory.

4. **Display Backend Selection**: Cross-compilation automatically selects the appropriate display backend:
   - **Pi**: DRM (preferred) with fbdev fallback
   - **AD5M**: fbdev (800×480 framebuffer)

### Build Targets

```bash
# Docker-based builds (recommended - no toolchain installation needed)
make pi-docker           # Raspberry Pi via Docker
make ad5m-docker         # Adventurer 5M via Docker
make docker-toolchains   # Pre-build all Docker images

# Direct cross-compilation (requires toolchain installed on host)
make pi                  # Raspberry Pi (needs aarch64-linux-gnu-gcc)
make ad5m                # Adventurer 5M (needs arm-linux-gnueabihf-gcc)

# Information
make cross-info          # Show cross-compilation help
```

### Target Specifications

#### Raspberry Pi (Mainsail OS)
- **CPU**: Cortex-A72/A76 (64-bit ARM)
- **Toolchain**: `aarch64-linux-gnu-gcc` (GCC 12+)
- **Display**: DRM preferred (hardware acceleration), fbdev fallback
- **Input**: libinput for touch
- **Docker Image**: `helixscreen/toolchain-pi` (Debian Bookworm)

#### Flashforge Adventurer 5M
- **CPU**: Cortex-A7 (32-bit ARM, hard-float)
- **Toolchain**: `arm-linux-gnueabihf-gcc` (GCC 8.3)
- **Display**: 800×480 framebuffer (`/dev/fb0`)
- **Input**: evdev for touch (`/dev/input/event4`)
- **C Library**: glibc 2.25 (requires older toolchain for compatibility)
- **RAM**: 110MB total (~36MB available with Klipper running)
- **Docker Image**: `helixscreen/toolchain-ad5m` (Debian Buster)

### Dockerfile Architecture

```
docker/
├── Dockerfile.pi      # Pi toolchain (Debian Bookworm, GCC 12)
└── Dockerfile.ad5m    # AD5M toolchain (Debian Buster, GCC 8)
```

The Dockerfiles handle:
- Cross-compiler installation (`crossbuild-essential-*`)
- Target architecture libraries (`:arm64` / `:armhf` packages)
- SSL/crypto libraries for Moonraker WebSocket
- Environment variables for cross-compilation

### Build System Integration

Cross-compilation is handled by `mk/cross.mk`, which defines:

```makefile
# Set target platform (native, pi, ad5m)
PLATFORM_TARGET ?= native

# Cross-compiler configuration
CROSS_COMPILE := arm-linux-gnueabihf-  # For AD5M
CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++

# Target-specific flags
TARGET_CFLAGS := -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
TARGET_LDFLAGS := -lstdc++fs  # GCC 8 requires this for std::filesystem

# Display backend selection
DISPLAY_BACKEND := fbdev  # or drm, sdl
```

### Troubleshooting

**Docker not installed:**
```bash
# macOS - Option 1: Docker Desktop (GUI)
brew install --cask docker

# macOS - Option 2: Colima (lightweight, CLI-only, recommended)
brew install colima docker
colima start --cpu 4 --memory 8  # Start VM with 4 cores, 8GB RAM

# Linux
sudo apt install docker.io
sudo usermod -aG docker $USER  # Logout/login after this
```

**Colima tips (macOS):**
```bash
colima start                    # Start with defaults
colima start --cpu 4 --memory 8 # Custom resources (faster builds)
colima stop                     # Stop VM when not needed
colima status                   # Check if running
```

**Docker image build fails:**
```bash
# Rebuild with no cache
docker build --no-cache -t helixscreen/toolchain-ad5m -f docker/Dockerfile.ad5m docker/
```

**"file format not recognized" linker error:**
This means a library was built for the wrong architecture. Clean and rebuild:
```bash
rm -rf build/ad5m lib/wpa_supplicant/wpa_supplicant/*.a
make ad5m-docker
```

**std::filesystem undefined references (AD5M only):**
GCC 8 requires `-lstdc++fs` for std::filesystem. This is already configured in `mk/cross.mk` for AD5M target.

### Deploying to Target

**Using make targets (recommended for Pi):**
```bash
# Full cycle: build + deploy + run on Pi
make pi-test

# Deploy only (after building)
make deploy-pi                    # Deploy binaries + assets
make deploy-pi-run                # Deploy and run

# Customize target
make deploy-pi PI_HOST=192.168.1.50 PI_USER=pi
```

**Manual deployment:**
```bash
# Raspberry Pi
scp build/pi/bin/helix-screen pi@mainsailos.local:~/

# Adventurer 5M (via SSH or SD card)
scp build/ad5m/bin/helix-screen root@192.168.1.x:/usr/data/
```

### Logging on Target

HelixScreen automatically detects the best logging backend:

| Platform | Default Backend | View Logs |
|----------|----------------|-----------|
| Linux + systemd | journal | `journalctl -t helix -f` |
| Linux (no systemd) | syslog | `tail -f /var/log/syslog \| grep helix` |
| File fallback | rotating file | `tail -f /var/log/helix-screen.log` |

**Override via CLI:**
```bash
./helix-screen --log-dest=journal   # Force systemd journal
./helix-screen --log-dest=file --log-file=/tmp/debug.log
```

**systemd service:** The included `config/helixscreen.service` automatically logs to journal. View with:
```bash
sudo journalctl -u helixscreen -f
```

### Display Backend Selection

The build system automatically selects display backends:

| Backend | Define | Libraries | Use Case |
|---------|--------|-----------|----------|
| **SDL** | `HELIX_DISPLAY_SDL` | SDL2 | Desktop development |
| **DRM** | `HELIX_DISPLAY_DRM` | libdrm, libinput | Pi with KMS |
| **fbdev** | `HELIX_DISPLAY_FBDEV` | (none) | Embedded framebuffer |

Display backend is selected via `DISPLAY_BACKEND` in `mk/cross.mk` and controls:
- LVGL driver compilation (`lv_conf.h` conditionals)
- Display initialization in `display_backend.cpp`
- Input driver selection (SDL mouse, evdev touch, libinput)

---

## Build System Overview

The project uses **GNU Make** with a modular architecture:
- **Modular design**: 1463 lines split across 6 files for maintainability
- **Color-coded output** for easy visual parsing
- **Verbosity control** to show/hide full compiler commands
- **Automatic dependency checking** before builds with smart canvas detection
- **Interactive installation** of missing dependencies (`make install-deps`)
- **Automatic code formatting** for C/C++ and XML files
- **Fail-fast error handling** with clear diagnostics
- **Parallel build support** with output synchronization
- **Build timing** for performance tracking

### Modular Makefile Structure

The build system is organized into focused modules:

- **`Makefile`** (~350 lines) - Configuration, variables, platform detection, module includes
- **`mk/cross.mk`** (~200 lines) - Cross-compilation targets, toolchain setup, display backends
- **`mk/deps.mk`** (~350 lines) - Dependency checking, installation, libhv/wpa_supplicant building
- **`mk/tests.mk`** (~270 lines) - All test targets (unit, integration, specialized)
- **`mk/format.mk`** (~100 lines) - Code and XML formatting (clang-format, xmllint)
- **`mk/fonts.mk`** (~110 lines) - Font/icon generation, Material icons, LVGL patches
- **`mk/patches.mk`** (~30 lines) - LVGL patch application
- **`mk/rules.mk`** (~270 lines) - Compilation rules, linking, main build targets

Each module is self-contained with GPL-3 copyright headers and clear separation of concerns.

### Quick Start

```bash
# Parallel build (auto-detects CPU cores)
make -j

# Clean parallel build with progress/timing
make build

# Verbose mode (shows full commands)
make V=1

# Code formatting (clang-format for C/C++, xmllint for XML)
make format              # Format all files
make format-staged       # Format only staged files

# Dependency checking (comprehensive)
make check-deps

# Auto-install missing dependencies (interactive)
make install-deps

# Help (shows all targets and options)
make help

# Apply patches manually (usually automatic)
make apply-patches

# Generate IDE/LSP support
make compile_commands
```

### Build Configuration Options

The build system supports several configuration flags to customize the build:

**TinyGL 3D Rendering** (default: enabled)
```bash
# Build with TinyGL 3D rendering support (default)
make -j

# Build without TinyGL 3D rendering
make -j ENABLE_TINYGL_3D=no
```

When `ENABLE_TINYGL_3D=yes` (default):
- TinyGL library is built from integrated source (`tinygl/`)
- 3D rendering code is compiled in
- `ENABLE_TINYGL_3D` preprocessor define is set

When `ENABLE_TINYGL_3D=no`:
- TinyGL library is not built
- 3D rendering code is excluded via `#ifdef ENABLE_TINYGL_3D` guards
- Smaller binary size, faster builds

**Verbosity Control** (default: quiet)
```bash
# Quiet mode (default) - shows progress
make -j

# Verbose mode - shows full compiler commands
make -j V=1
```

## Dependency Management

The build system includes comprehensive dependency checking and automatic installation.

### Checking Dependencies

```bash
make check-deps
```

This checks for:
- **System tools**: C/C++ compiler, cmake, make, python3, npm
- **Code formatters**: clang-format (C/C++), xmllint (XML validation/formatting)
- **Libraries**: pkg-config
- **Canvas dependencies**: cairo, pango, libpng, libjpeg, librsvg (for lv_img_conv)
- **npm packages**: lv_font_conv, lv_img_conv
- **Optional libraries**: SDL2, spdlog, libhv (uses system if available, otherwise builds from submodules)
- **Git submodules**: LVGL (always built from submodule)

The checker is **platform-aware** and shows the correct install commands for:
- **macOS** (Homebrew)
- **Debian/Ubuntu** (apt)
- **Fedora/RHEL** (dnf)

### Installing Dependencies

```bash
make install-deps
```

This **interactively installs** missing dependencies:
1. Detects your platform
2. Lists packages to be installed
3. Shows the command it will run
4. Asks for confirmation before proceeding
5. Installs system packages via brew/apt/dnf
6. Runs `npm install` for lv_font_conv/lv_img_conv
7. Initializes git submodules if needed

**Smart Canvas Detection**: Uses `pkg-config` to detect exactly which canvas libraries are missing and only installs what's needed.

**Automatic Builds**: Git submodules (libhv, wpa_supplicant, spdlog) are built automatically by the main build system when missing - no manual intervention needed.

### Test Harness

The dependency system includes a comprehensive test suite:

```bash
./tests/test_deps.sh
```

Tests 9 scenarios with 22 assertions covering dependency detection, platform-specific commands, and auto-installation workflow.

### Build Options

- **`V=1`** - Verbose mode: shows full compiler commands instead of short `[CC]`/`[CXX]` tags
- **`JOBS=N`** - Set parallel job count (default: auto-detects CPU cores)
- **`NO_COLOR=1`** - Disable colored output (useful for CI/CD)
- **`-j<N>`** - Enable parallel builds with N jobs (NOT auto-enabled by default)

### Build Output

The build system uses color-coded tags:

- **`[CC]`** (cyan) - Compiling C sources (LVGL)
- **`[CXX]`** (blue) - Compiling C++ sources (app code)
- **`[FONT]`** (green) - Compiling font assets
- **`[ICON]`** (green) - Compiling icon assets
- **`[LD]`** (magenta) - Linking binary
- **`✓`** (green) - Success messages
- **`✗`** (red) - Error messages
- **`⚠`** (yellow) - Warning messages

### Error Handling

When compilation fails, the build system:
1. Shows the failed file with a red `✗` marker
2. Displays the full compiler command for debugging
3. Exits immediately (fail-fast behavior)

Example:
```
[CXX] src/ui_panel_home.cpp
✗ Compilation failed: src/ui_panel_home.cpp
Command: clang++ -std=c++17 -Wall -Wextra -O2 -g -I. -Iinclude ...
```

## Code Formatting

The build system includes automatic code formatting for C/C++ and XML files, integrated with pre-commit hooks.

### Formatters

- **clang-format** - Formats C, C++, and Objective-C files according to `.clang-format` config
- **xmllint** - Formats and validates XML layout files with consistent indentation

### Configuration

**`.clang-format`** (LLVM-based with project customizations):
- **Indentation**: 4 spaces, no tabs
- **Line length**: 100 characters
- **Braces**: K&R style (same line)
- **Pointers**: Left-aligned (`int* ptr`)
- **Includes**: Auto-sorted with grouping (project → external → system)

### Formatting Commands

```bash
# Format all C/C++ and XML files
make format

# Format only staged files (useful before commit)
make format-staged

# Check formatting without modifying files
./scripts/quality-checks.sh
```

### Pre-commit Integration

Formatting is automatically checked by the pre-commit hook (`.git/hooks/pre-commit`), which calls `scripts/quality-checks.sh --staged-only`:

1. **Checks staged files** for formatting issues
2. **Reports files** that need formatting
3. **Prevents commit** if formatting issues are found
4. **Suggests fix**: Run `make format-staged` or `clang-format -i <file>`

To bypass (not recommended):
```bash
git commit --no-verify
```

### Quality Checks

The `scripts/quality-checks.sh` script runs multiple checks:
- **Code formatting** (clang-format)
- **XML formatting** (xmllint)
- **XML validation** (xmllint --noout)
- **Copyright headers** (GPL v3 SPDX identifiers)
- **Merge conflict markers**
- **Trailing whitespace**
- **Build verification** (pre-commit only)

Used by both:
- **Pre-commit hook** (staged files only)
- **CI/CD** (all files)

## Automatic Patch Application

The build system automatically applies patches to git submodules before compilation.

### How It Works

1. **Patch Storage**: All submodule patches are stored in `patches/` (in the repository root)
2. **Auto-Detection**: Makefile checks if patches are already applied before each build
3. **Idempotent**: Safe to run multiple times - patches are only applied once
4. **Transparent**: No manual intervention needed for normal development

### Patch: LVGL SDL Window Position

**File**: `patches/lvgl_sdl_window_position.patch`

**Purpose**: Adds multi-display support to LVGL 9's SDL driver by reading environment variables.

**Environment Variables**:
- `HELIX_SDL_DISPLAY` - Display number (0, 1, 2...) to center window on
- `HELIX_SDL_XPOS` - X coordinate for exact window position
- `HELIX_SDL_YPOS` - Y coordinate for exact window position

**Application Logic** (in `Makefile`):
```makefile
apply-patches:
	@echo "Checking LVGL patches..."
	@if git -C $(LVGL_DIR) diff --quiet src/drivers/sdl/lv_sdl_window.c; then \
		# File is clean, apply patch
		git -C $(LVGL_DIR) apply ../patches/lvgl_sdl_window_position.patch
	else \
		# File already modified (patch applied)
		echo "✓ LVGL SDL window position patch already applied"
	fi
```

**Status Messages**:
- `✓ Patch applied successfully` - Patch was applied during this build
- `✓ LVGL SDL window position patch already applied` - Patch was already present
- `⚠ Cannot apply patch (already applied or conflicts)` - Manual intervention needed

### Adding New Patches

To add a new submodule patch:

1. **Make changes** in the submodule directory
2. **Generate patch**:
   ```bash
   cd lib/lvgl
   git diff > ../../patches/my-new-patch.patch
   ```
3. **Update Makefile** to apply the patch in the `apply-patches` target
4. **Document** in `patches/README.md`

## Multi-Display Support (macOS)

The prototype supports multi-monitor development workflows with automatic window positioning.

### Command Line Arguments

```bash
# Display-based positioning (centered)
./build/bin/helix-screen --display 0    # Main display
./build/bin/helix-screen --display 1    # Secondary display
./build/bin/helix-screen -d 2           # Third display (short form)

# Exact pixel coordinates
./build/bin/helix-screen --x-pos 100 --y-pos 200
./build/bin/helix-screen -x 1500 -y -500  # Works with negative Y (display above)

# Combined with other options
./build/bin/helix-screen -d 1 -s small --panel home
```

### Implementation Details

**Flow**:
1. `main.cpp` parses command line arguments
2. Sets environment variables before LVGL initialization:
   ```cpp
   setenv("HELIX_SDL_DISPLAY", "1", 1);  // For --display 1
   // or
   setenv("HELIX_SDL_XPOS", "100", 1);   // For --x-pos 100
   setenv("HELIX_SDL_YPOS", "200", 1);   // For --y-pos 200
   ```
3. LVGL SDL driver reads environment variables during window creation
4. Uses `SDL_GetDisplayBounds()` to query display geometry
5. Calculates center position: `display_x + (display_w - window_w) / 2`
6. Calls `SDL_SetWindowPosition()` after window creation (fixes macOS quirks)

**Source Files**:
- `src/main.cpp` - Argument parsing and environment setup (lines 218-220, 385-401)
- `lvgl/src/drivers/sdl/lv_sdl_window.c` - Window positioning logic (patch)

### Screenshot Script Integration

The `scripts/screenshot.sh` script automatically uses display positioning:

```bash
# Default: opens on display 1 (keeps terminal visible on display 0)
./scripts/screenshot.sh helix-screen output-name panel

# Override display
HELIX_SCREENSHOT_DISPLAY=0 ./scripts/screenshot.sh helix-screen output panel
```

**How it works**:
```bash
# In screenshot.sh
HELIX_SCREENSHOT_DISPLAY=${HELIX_SCREENSHOT_DISPLAY:-1}  # Default to display 1
EXTRA_ARGS="--display $HELIX_SCREENSHOT_DISPLAY $EXTRA_ARGS"
```

This ensures the UI window appears on a different display from the terminal, making it easier to monitor build output and screenshots simultaneously.

## Parallel Compilation

**Important**: Parallel builds are **NOT** enabled by default. Use `-j` flag explicitly.

### Platform Detection

```makefile
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)
    PLATFORM := macOS
else
    # Linux
    NPROC := $(shell nproc 2>/dev/null || echo 4)
    PLATFORM := Linux
endif
```

### Usage

```bash
make -j          # Auto-detect CPU cores and parallelize (recommended)
make -j16        # Explicit job count (current system has 16 cores)
make JOBS=16     # Set job count via variable
make build       # Clean parallel build (auto-detects cores)
```

The build system uses `--output-sync=target` to prevent interleaved output during parallel builds.

## Font Generation

The build system includes automated font generation using `lv_font_conv` to convert TrueType fonts into LVGL-compatible C arrays.

### How It Works

Fonts are automatically regenerated when `package.json` is modified (which contains the glyph ranges). The build system uses Make's dependency tracking to only regenerate when needed.

**Automatic regeneration:**
```bash
make          # Checks fonts and regenerates if package.json is newer
```

**Manual regeneration:**
```bash
make generate-fonts    # Explicitly regenerate fonts
npm run convert-all-fonts  # Direct npm script (bypasses Make)
```

### Adding New Font Glyphs

To add new FontAwesome icons or glyphs:

1. **Find the Unicode codepoint** for the icon (e.g., `circle-question` = `0xF059`)
2. **Edit `package.json`** and add the codepoint to the appropriate `convert-font-*` script's `--range` parameter:
   ```json
   "convert-font-24": "lv_font_conv ... --range 0xf008,0xf011,0xf059,..."
   ```
3. **Regenerate fonts:**
   ```bash
   make generate-fonts
   ```
   Or let the build system handle it automatically on next `make`.

### Font Files

**FontAwesome icons:**
- `fa_icons_16.c` - 16px icons (small UI elements)
- `fa_icons_24.c` - 24px icons (standard buttons/labels)
- `fa_icons_32.c` - 32px icons (medium-sized elements)
- `fa_icons_48.c` - 48px icons (large buttons/cards)
- `fa_icons_64.c` - 64px icons (very large/hero elements)

**Arrow glyphs (from Arial Unicode):**
- `arrows_32.c`, `arrows_48.c`, `arrows_64.c` - Unicode directional arrows (U+2190-2193, U+2196-2199)

### Requirements

- **Node.js and npm** - Required for font generation
  - macOS: `brew install node`
  - Ubuntu/Debian: `sudo apt install npm`
  - Fedora/RHEL: `sudo dnf install npm`
- **lv_font_conv** - Installed automatically via `npm install` (see `package.json` devDependencies)

### Troubleshooting

**npm not found:**
```bash
# macOS
brew install node

# Linux
sudo apt install npm   # Debian/Ubuntu
sudo dnf install npm   # Fedora/RHEL

# Verify
npm --version
```

**Fonts not regenerating:**
```bash
# Force regeneration by touching package.json
touch package.json
make generate-fonts
```

**Manual font generation:**
```bash
# Generate specific size
npm run convert-font-24

# Generate all fonts
npm run convert-all-fonts
```

## Icon Generation

The build system includes automated icon generation with platform-specific output formats.

### Quick Start

```bash
# Generate/regenerate icon from source logo
make icon
```

**Output:**
- **macOS**: `helix-icon.icns` (multi-resolution bundle) + `helix-icon.png` (650x650)
- **Linux**: `helix-icon.png` (650x650 for application use)

### Requirements

**Required:**
- `imagemagick` - Image processing (`magick` command)
  - macOS: `brew install imagemagick`
  - Ubuntu/Debian: `sudo apt install imagemagick`
  - Fedora/RHEL: `sudo dnf install ImageMagick`

**macOS only:**
- `iconutil` - macOS icon bundle creator (built-in on macOS)

### What It Does

The `make icon` target performs the following steps:

**All platforms:**
1. **Crops source logo** (`assets/images/helixscreen-logo.png`) to just the circular helix
2. **Creates square icon** at 650x650px with transparent background → `helix-icon.png`

**macOS only (additional steps):**
3. **Generates 12 resolutions**:
   - Standard: 16x16, 32x32, 64x64, 128x128, 256x256, 512x512
   - Retina (@2x): 32x32, 64x64, 128x128, 256x256, 512x512, 1024x1024
4. **Bundles into .icns** file using `iconutil` → `helix-icon.icns`
5. **Cleans up** temporary iconset directory

### Generated Files

**All platforms:**
- **`assets/images/helix-icon.png`** - Cropped square logo (650x650px, ~245KB)

**macOS only:**
- **`assets/images/helix-icon.icns`** - macOS icon bundle (~1.3MB with all resolutions)
- **`assets/images/icon.iconset/`** - Temporary directory (auto-deleted after .icns creation)

### Usage

**Cross-platform:**
- SDL window icons (programmatically via `SDL_SetWindowIcon()` with PNG)
- Linux `.desktop` files (Icon= field pointing to PNG)

**macOS specific:**
- macOS `.app` bundles with `Info.plist` (CFBundleIconFile pointing to .icns)
- Dock/Finder display when bundled as application

### Troubleshooting

**ImageMagick not found (macOS):**
```bash
brew install imagemagick
make icon
```

**ImageMagick not found (Linux):**
```bash
# Ubuntu/Debian
sudo apt install imagemagick

# Fedora/RHEL
sudo dnf install ImageMagick
```

**Linux output:**
- Linux builds generate PNG only (not .icns)
- This is expected - `.icns` is macOS-specific
- PNG icons work with SDL and Linux desktop environments

**Regenerating after logo changes:**
```bash
# Update assets/images/helixscreen-logo.png
make icon  # Regenerates all icon files (platform-specific)
```

## Build Targets

### Primary Targets

- **`all`** (default) - Build the main binary with dependency checks
- **`build`** - Clean parallel build with progress and timing
- **`clean`** - Remove all build artifacts
- **`run`** - Build and run the prototype
- **`help`** - Show comprehensive help with all targets and options

### Development Targets

- **`compile_commands`** - Generate `compile_commands.json` for IDE/LSP (requires `bear`)
- **`check-deps`** - Verify all build dependencies are installed
- **`apply-patches`** - Manually apply submodule patches (usually automatic)
- **`icon`** - Generate macOS .icns icon from logo (requires `imagemagick`, `iconutil`)

### Test Targets

- **`test`** - Run unit tests
- **`test-cards`** - Test dynamic card instantiation
- **`test-print-select`** - Test print select panel with mock data

### Demo Target

- **`demo`** - Build LVGL demo widgets (for LVGL API testing)

## Dependency Checking

Before building, the system automatically checks for required dependencies:

**Required:**
- `clang` / `clang++` - C/C++ compiler with C++17 support
- `cmake` - Build system for SDL2 when building from submodule (version 3.16+)
- Git submodules: `lvgl`, `wpa_supplicant` (auto-built by build system)

**Optional (uses system if available, otherwise builds from submodules):**
- `sdl2`, `spdlog`, `libhv` - Auto-detected and built only if not system-installed

**Optional:**
- `bear` - For generating `compile_commands.json`
- `imagemagick` - For screenshot conversion and icon generation
- `iconutil` - For macOS .icns icon generation (macOS only, built-in)

### Manual Dependency Check

```bash
make check-deps
```

Example output:
```
Checking build dependencies...
✓ clang found: Apple clang version 17.0.0
✓ clang++ found: Apple clang version 17.0.0
✓ SDL2: Using system version 2.32.10
✓ cmake found: cmake version 3.30.5
✓ libhv: Using submodule version
✓ spdlog: Using submodule version (header-only)
✓ LVGL found: lvgl

All dependencies satisfied!
```

If dependencies are missing, the check provides installation instructions.

## Dependency Management

### Git Submodules

The project uses git submodules for external dependencies:

- `lvgl` - LVGL 9.4 graphics library (with automatic patches)
- `libhv` - HTTP/WebSocket client library (auto-built)
- `spdlog` - Logging library
- `wpa_supplicant` - WiFi control (Linux only, auto-built)

**Automatic handling**: Submodule dependencies are built automatically when missing. Patches are applied automatically before builds. Never commit changes directly to submodules - always create patches instead.

### SDL2

SDL2 is a system dependency installed via package manager:

```bash
# macOS
brew install sdl2

# Debian/Ubuntu
sudo apt install libsdl2-dev

# Fedora/RHEL
sudo dnf install SDL2-devel
```

The Makefile uses `sdl2-config` to auto-detect paths:
```makefile
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS := $(shell sdl2-config --libs)
```

## Troubleshooting

### Patch Application Fails

**Symptom**: `⚠ Cannot apply patch (already applied or conflicts)`

**Causes**:
1. Submodule was manually modified (expected if patch is working)
2. Patch conflicts with newer LVGL version
3. Patch file is corrupted

**Solutions**:
```bash
# Check if file is modified (expected)
git -C lvgl diff src/drivers/sdl/lv_sdl_window.c

# Revert to original (re-applies patch on next build)
git -C lvgl checkout src/drivers/sdl/lv_sdl_window.c
make apply-patches

# Force re-apply
git -C lvgl checkout src/drivers/sdl/lv_sdl_window.c
git -C lvgl apply ../patches/lvgl_sdl_window_position.patch
```

### Build Performance

**Symptom**: Slow compilation

**Solutions**:
- Use parallel builds: `make -j` (auto-detects all cores)
- Use incremental builds: `make -j` instead of `make clean && make`
- Check CPU usage during build (should be near 100% with parallel builds)
- Use `make build` for optimized clean builds with timing

### SDL2 Not Found

**Symptom**: `sdl2-config: command not found`

**Solutions**:
```bash
# macOS
brew install sdl2

# Debian/Ubuntu
sudo apt install libsdl2-dev

# Verify installation
which sdl2-config
sdl2-config --version
```

## Best Practices

### Development Workflow

1. **Edit code** in `src/` or `include/`
2. **Run `make -j`** - parallel incremental build with auto-patching
3. **Test** with `./build/bin/helix-screen`
4. **Screenshot** with `./scripts/screenshot.sh` (auto-opens on display 1)
5. **Commit** with working incremental changes

For debugging build issues:
```bash
make clean
make V=1   # Verbose sequential build
```

### Clean Builds

Only use `make clean && make` when:
- Switching branches with significant changes
- Build artifacts are corrupted
- Troubleshooting mysterious build errors

**Avoid** clean rebuilds for normal development (wastes time).

### Submodule Management

**Never**:
- Commit changes directly to submodules
- Update submodule commits without testing
- Modify submodule files without creating patches

**Always**:
- Create patches for submodule changes
- Document patches in `patches/README.md`
- Test patch application on clean checkouts

## See Also

- **[README.md](../README.md)** - Project overview and quick start
- **[DEVELOPMENT.md](../DEVELOPMENT.md)** - Development environment setup and daily workflow
- **[ARCHITECTURE.md](../ARCHITECTURE.md)** - System design and technical patterns
- **[CONTRIBUTING.md](../CONTRIBUTING.md)** - Code standards and submission guidelines
- **[CLAUDE.md](../CLAUDE.md)** - Development context and AI assistant guidelines
- **[patches/README.md](../patches/README.md)** - Patch documentation
