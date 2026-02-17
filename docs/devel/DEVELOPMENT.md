# Development Guide

Development environment setup, build processes, and workflows for HelixScreen.

## Quick Start

```bash
# Install dependencies (see platform-specific below)
npm install && make venv-setup

# Build and run
make -j
./build/bin/helix-screen --test -vv  # Mock printer + DEBUG logs
```

## Development Environment

### macOS (Homebrew)
```bash
brew install cmake bear imagemagick python3 node shellcheck bats-core
npm install         # lv_font_conv and lv_img_conv
make venv-setup     # Python venv with pypng/lz4
```
**Minimum:** macOS 10.15 (Catalina) for CoreWLAN/CoreLocation WiFi APIs.

### Debian/Ubuntu
```bash
sudo apt install cmake bear imagemagick python3 python3-venv clang make npm \
    shellcheck bats libnl-3-dev libnl-genl-3-dev libssl-dev
npm install && make venv-setup
```

### Fedora/RHEL
```bash
sudo dnf install cmake bear ImageMagick python3 clang make npm \
    ShellCheck bats libnl3-devel openssl-devel
npm install && make venv-setup
```

### Dependencies

| Category | Components | Notes |
|----------|------------|-------|
| **Required** | clang, cmake 3.16+, make, python3, node/npm | Core build tools |
| **Auto-built** | SDL2, spdlog, libhv | Built from submodules if not system-installed |
| **Always submodule** | lvgl, TinyGL | Project-specific patches required |
| **Optional** | bear, imagemagick, shellcheck, bats-core | IDE support, screenshots, shell linting/testing |

```bash
make check-deps      # Check what's missing
make install-deps    # Auto-install (interactive)
```

## Build System

### Core Commands

```bash
make -j              # Parallel incremental build (recommended)
make build           # Clean parallel build with progress/timing
make clean && make -j  # Full rebuild (only when needed)
make V=1             # Verbose mode (shows full commands)
make compile_commands  # Generate compile_commands.json for IDE/LSP
```

### Running the Application

```bash
./build/bin/helix-screen                    # Production mode
./build/bin/helix-screen --test             # Full mock mode (no hardware)
./build/bin/helix-screen --test --real-wifi # Mix real WiFi + mock printer
./build/bin/helix-screen --dark             # Force dark theme
./build/bin/helix-screen --light            # Force light theme
./build/bin/helix-screen -d 1 -s small      # Display 1, small size
```

### Test Mode Flags

| Flag | Effect |
|------|--------|
| `--test` | Enable test mode (required for mocks) |
| `--real-wifi` | Use real WiFi instead of mock |
| `--real-ethernet` | Use real Ethernet instead of mock |
| `--real-moonraker` | Connect to real printer |
| `--real-files` | Use real printer files |

**Test mode keyboard shortcuts:** S=screenshot, P=test prompt, N=test notification, Q/Esc=quit

### Build Options

```bash
make -j ENABLE_TINYGL_3D=no  # Disable 3D rendering (smaller/faster)
```

For cross-compilation, patches, and advanced options, see **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)**.

## Logging

### Verbosity Levels

| Level | Flag | Use For |
|-------|------|---------|
| WARN | (default) | Errors and warnings only |
| INFO | `-v` | User-visible milestones |
| DEBUG | `-vv` | Troubleshooting, summaries |
| TRACE | `-vvv` | Per-item loops, wire protocol |

### Log Destinations

```bash
./build/bin/helix-screen --log-dest=console  # Console (default on macOS)
./build/bin/helix-screen --log-dest=journal  # systemd journal (Linux)
./build/bin/helix-screen --log-dest=file --log-file=/tmp/helix.log
```

**Viewing logs on Linux:**
```bash
journalctl -t helix -f              # systemd
tail -f /var/log/helix-screen.log   # file
```

### Code Usage

**ALWAYS use spdlog** - never printf/cout/LV_LOG_*:
```cpp
spdlog::info("[ComponentName] Message: {}", value);
spdlog::debug("[Theme] Registered {} items", count);
```

**spdlog submodule:** Uses fmt-11.2.0 branch. Initialize with `git submodule update --init --recursive`.

## Configuration

### Config File Pattern

```bash
cp config/helixconfig.json.template config/helixconfig.json  # First-time setup
```

- `config/helixconfig.json` - User settings (git-ignored)
- `config/helixconfig.json.template` - Defaults (versioned)

**Never commit user config.** Legacy root location auto-migrates.

### Config Structure

```json
{
  "printer": {
    "heaters": { "bed": "heater_bed", "hotend": "extruder" },
    "temp_sensors": { "bed": "heater_bed", "hotend": "extruder" },
    "fans": { "part": "fan", "hotend": "heater_fan hotend_fan" }
  }
}
```

**Naming:** Container keys plural (`heaters`), role keys singular (`bed`).

## DPI & Hardware Profiles

LVGL scales UI based on DPI. Default: 160 (reference, no scaling).

```bash
./build/bin/helix-screen --dpi 170  # 7" @ 1024x600 (BTT Pad 7)
./build/bin/helix-screen --dpi 187  # 5" @ 800x480
./build/bin/helix-screen --dpi 201  # 4.3" @ 720x480 (AD5M)
```

| Hardware | Resolution | DPI |
|----------|------------|-----|
| Reference | — | 160 |
| 7" LCD | 1024×600 | 170 |
| 5" LCD | 800×480 | 187 |
| AD5M | 720×480 | 201 |

## Multi-Display (macOS)

```bash
./build/bin/helix-screen --display 1     # Secondary display
./build/bin/helix-screen -d 1 -s small   # Combined options
```

Uses `SDL_GetDisplayBounds()` for proper positioning on multi-monitor setups.

## Screenshots

```bash
# Interactive: Press 'S' in running UI

# Automated:
./scripts/screenshot.sh helix-screen output-name [panel] [options]
./scripts/screenshot.sh helix-screen home-screen home
./scripts/screenshot.sh helix-screen motion motion -s small

# Environment overrides:
HELIX_SCREENSHOT_DISPLAY=0 ./scripts/screenshot.sh helix-screen test home
HELIX_SCREENSHOT_OPEN=1 ./scripts/screenshot.sh helix-screen review home
```

Output: `/tmp/ui-screenshot-[name].png`

## Icon & Font Workflow

```bash
python3 scripts/generate-icon-consts.py  # After editing include/ui_fonts.h
make icon                                 # Generate platform icons
```

See **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)** for complete font generation details.

## IDE Setup

```bash
make compile_commands  # Generates compile_commands.json (requires bear)
```

**VS Code:** C/C++ extension + clangd extension
**Vim/Neovim:** Configure clangd LSP client
**CLion:** Import as Makefile project

## Daily Workflow

1. **Edit code** in `src/` or `include/`
2. **Edit XML** in `ui_xml/` — **no rebuild needed**, just relaunch the app
3. **Build** with `make -j` (only when C++ changes)
4. **Test** with `./build/bin/helix-screen --test -vv [panel]`
5. **Screenshot** with S key or `./scripts/screenshot.sh`
6. **Commit** working incremental changes

### XML vs C++ Changes

| Change Type | Location | Rebuild? |
|-------------|----------|----------|
| Layout, styling, colors | `ui_xml/*.xml` | **No** — just restart the app |
| Logic, bindings, handlers | `src/*.cpp`, `include/*.h` | Yes (`make -j`) |
| Theme colors | `config/themes/*.json` | No — just restart |
| Translations | `config/strings/*.yaml` | Yes (code generation step) |

This makes UI iteration very fast — edit XML, relaunch, see changes immediately.

---

## UI Development

For layout work, styling fixes, and alternate screen layouts, the **[UI Contributor Guide](UI_CONTRIBUTOR_GUIDE.md)** is the primary reference. It covers breakpoints, design tokens, pre-themed widgets, layout overrides, and what needs work.

Key points for UI contributors:

- **XML layouts load at runtime** — no rebuild needed for layout/styling changes
- **Design tokens are mandatory** — use `#space_md`, `#card_bg`, `<text_body>` instead of hardcoded values
- **5 breakpoint tiers** based on screen height: tiny (≤390px), small (391–460px), medium (461–550px), large (551–700px), xlarge (>700px)
- **Layout overrides** let you provide alternate XML for ultrawide, portrait, or tiny screens without touching the standard layouts
- **Test at multiple sizes** with `-s WIDTHxHEIGHT`:
  ```bash
  ./build/bin/helix-screen --test -vv -s 480x320   # Tiny
  ./build/bin/helix-screen --test -vv -s 800x480   # Standard
  ./build/bin/helix-screen --test -vv -s 1920x480 --layout ultrawide
  ```

### Where UI files live

| Path | Contents |
|------|----------|
| `ui_xml/` | All XML layouts (~170 files) |
| `ui_xml/components/` | Reusable XML components |
| `ui_xml/ultrawide/` | Ultrawide layout overrides |
| `ui_xml/globals.xml` | Design tokens and global variables (shared, never override) |
| `config/themes/` | Theme JSON files (color palettes) |

---

## Worktrees

For major feature work, use git worktrees to isolate your changes:

```bash
scripts/setup-worktree.sh feature/my-branch   # Creates in .worktrees/
```

This creates a worktree with symlinked dependencies and a ready-to-build environment. Worktrees keep `main` clean while you experiment.

---

## macOS WiFi Permission

Real WiFi scanning requires Location Services (network SSIDs reveal location).

**Easiest:** System Settings → Privacy & Security → Location Services → Enable Terminal

Without permission, app falls back to mock WiFi. Check with:
```bash
./build/bin/helix-screen --wizard -vv 2>&1 | grep -i "location\|wifi"
```

## Troubleshooting

```bash
make check-deps              # Check missing dependencies
make install-deps            # Auto-install
make clean && make V=1       # Verbose rebuild
```

**SDL2 not found:** `brew install sdl2` (macOS) or `sudo apt install libsdl2-dev` (Linux)

For complete troubleshooting, see **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)**.

---

## Contributing

### First-Time Setup

```bash
make setup  # Configures pre-commit hook + commit template
```

The pre-commit hook auto-formats code (clang-format) and runs quality checks.

### Code Standards

**Class-based architecture required** for all new code:

```cpp
// ✅ CORRECT: Class-based panel
class MotionPanel : public PanelBase {
public:
    explicit MotionPanel(lv_obj_t* parent);
    void show() override;
};

// ❌ AVOID: Function-based (legacy)
void ui_panel_motion_init(lv_obj_t* parent);
```

**Naming conventions:**
- Functions/variables: `snake_case` (`ui_panel_home_init`, `temp_target`)
- XML files: `kebab-case` (`nozzle-temp-panel.xml`)
- Constants: `UPPER_SNAKE_CASE` (`MAX_TEMP`)

**Critical patterns:**
```cpp
// Widget lookup: use names, not indices
lv_obj_t* label = lv_obj_find_by_name(panel, "temp_display");  // ✅
lv_obj_t* label = lv_obj_get_child(panel, 3);                   // ❌

// LVGL API: public only
lv_obj_get_x();        // ✅ Public
_lv_obj_mark_dirty();  // ❌ Private (underscore prefix)
```

**Copyright headers** (all new files):
```cpp
// Copyright 2025 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
```

### Commit Messages

```
type(scope): description

Optional detailed explanation.
```

**Types:** `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

**Examples:**
```
feat(ui): add temperature control overlay panel
fix(build): resolve SDL2 linking on macOS Sequoia
docs(readme): update build instructions
```

### Pull Requests

**Before submitting:**
1. Rebase on latest `main`
2. Test build and runtime
3. Update docs if patterns changed
4. Add screenshots for UI changes

**PR description includes:**
- What changed (summary)
- Why (context/problem solved)
- How to test
- Screenshots (if visual)
- Breaking changes (if any)

### Code Review Focus

- Architecture compliance (XML/Subject patterns)
- Error handling (logging, null checks)
- Performance (no blocking in UI thread)
- Documentation updated

---

## Installer Scripts

The installation system is modular for maintainability and BusyBox compatibility.

### Structure

```
scripts/
├── install.sh                    # Auto-generated for curl|sh (user-facing)
├── install-dev.sh                # Modular version (requires lib/installer/)
├── uninstall.sh                  # Standalone uninstaller
├── lib/installer/                # Shared modules
│   ├── common.sh                 # Logging, colors, error handling
│   ├── platform.sh               # Platform/firmware detection
│   ├── permissions.sh            # Root/sudo handling
│   ├── requirements.sh           # Pre-flight checks
│   ├── forgex.sh                 # ForgeX-specific functions
│   ├── competing_uis.sh          # Stop GuppyScreen, KlipperScreen, etc.
│   ├── release.sh                # Download and extract
│   ├── service.sh                # Systemd/SysV service management
│   └── uninstall.sh              # Uninstall/clean functions
└── bundle-installer.sh           # Generate install.sh from modules
```

### BusyBox Compatibility

All modules use POSIX `#!/bin/sh` (not bash) for AD5M's BusyBox environment:
- `[ ]` instead of `[[ ]]`
- `command -v X >/dev/null 2>&1` instead of `&>`
- No arrays (use space-separated strings)
- `ps -ef` instead of `ps aux`

### Generating Bundled Installer

```bash
./scripts/bundle-installer.sh -o ./scripts/install.sh
```

The bundled version inlines all modules for curl|sh usage.

### Testing Installers

```bash
./scripts/install-dev.sh --help       # Test modular version (from repo)
./scripts/install.sh --help           # Test bundled version (for users)
./scripts/uninstall.sh --help         # Test uninstaller
sh -n scripts/install.sh              # Check POSIX syntax
```

---

## Related Documentation

- **[README.md](../README.md)** - Documentation index
- **[UI Contributor Guide](UI_CONTRIBUTOR_GUIDE.md)** - Start here for UI/layout work
- **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)** - Complete build reference
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - System design
- **[LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md)** - XML syntax reference
- **[DEVELOPER_QUICK_REFERENCE.md](DEVELOPER_QUICK_REFERENCE.md)** - Common patterns
- **[TESTING.md](TESTING.md)** - Test infrastructure and Catch2 usage
- **[LOGGING.md](LOGGING.md)** - Log levels and when to use each
