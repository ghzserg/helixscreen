#!/bin/sh
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen Uninstaller
# Removes HelixScreen and restores the previous screen UI
#
# Usage:
#   ./uninstall.sh              # Interactive uninstall
#   ./uninstall.sh --force      # Skip confirmation prompt
#
# This script:
#   1. Stops HelixScreen
#   2. Removes init script or systemd service
#   3. Removes installation directory
#   4. Re-enables previous UI (GuppyScreen, FeatherScreen, etc.)

set -e

# Colors (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    CYAN='\033[0;36m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    CYAN=''
    NC=''
fi

log_info() { echo "${CYAN}[INFO]${NC} $1"; }
log_success() { echo "${GREEN}[OK]${NC} $1"; }
log_warn() { echo "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo "${RED}[ERROR]${NC} $1" >&2; }

# Default paths - may be overridden by set_install_paths()
INSTALL_DIR="/opt/helixscreen"
INIT_SCRIPT="/etc/init.d/S90helixscreen"
SYSTEMD_SERVICE="/etc/systemd/system/helixscreen.service"
PREVIOUS_UI_SCRIPT=""
AD5M_FIRMWARE=""

# Previous UIs we may need to re-enable
PREVIOUS_UIS="guppyscreen GuppyScreen featherscreen FeatherScreen klipperscreen KlipperScreen"

# Detect init system
detect_init_system() {
    if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
        echo "systemd"
    elif [ -d /etc/init.d ]; then
        echo "sysv"
    else
        echo "unknown"
    fi
}

# Detect platform (AD5M vs Pi)
detect_platform() {
    local arch=$(uname -m)
    local kernel=$(uname -r)

    # Check for AD5M (armv7l with specific kernel)
    if [ "$arch" = "armv7l" ]; then
        if echo "$kernel" | grep -q "ad5m\|5.4.61"; then
            echo "ad5m"
            return
        fi
    fi

    # Default to pi for other platforms
    echo "pi"
}

# Detect AD5M firmware variant (Klipper Mod vs Forge-X)
# Only called when platform is "ad5m"
detect_ad5m_firmware() {
    # Klipper Mod indicators - check for its specific directory structure
    if [ -d "/root/printer_software" ] || [ -d "/mnt/data/.klipper_mod" ]; then
        echo "klipper_mod"
        return
    fi

    # Forge-X indicators - check for its mod overlay structure
    if [ -d "/opt/config/mod/.root" ]; then
        echo "forge_x"
        return
    fi

    # Default to forge_x (original behavior, most common)
    echo "forge_x"
}

# Set installation paths based on platform and firmware
set_install_paths() {
    local platform=$1
    local firmware=${2:-}

    if [ "$platform" = "ad5m" ]; then
        case "$firmware" in
            klipper_mod)
                INSTALL_DIR="/root/printer_software/helixscreen"
                INIT_SCRIPT="/etc/init.d/S80helixscreen"
                PREVIOUS_UI_SCRIPT="/etc/init.d/S80klipperscreen"
                ;;
            forge_x|*)
                INSTALL_DIR="/opt/helixscreen"
                INIT_SCRIPT="/etc/init.d/S90helixscreen"
                PREVIOUS_UI_SCRIPT="/opt/config/mod/.root/S80guppyscreen"
                ;;
        esac
    else
        # Pi and other platforms - use default paths
        INSTALL_DIR="/opt/helixscreen"
        INIT_SCRIPT="/etc/init.d/S90helixscreen"
        PREVIOUS_UI_SCRIPT=""
    fi
}

# Restore ForgeX display settings to re-enable GuppyScreen
# This reverses what configure_forgex_display() does in install.sh
restore_forgex_display() {
    local var_file="/opt/config/mod_data/variables.cfg"
    if [ -f "$var_file" ]; then
        # Restore display = 'GUPPY' if currently set to STOCK
        if grep -q "display[[:space:]]*=[[:space:]]*'STOCK'" "$var_file"; then
            log_info "Restoring GuppyScreen in ForgeX configuration..."
            sed -i "s/display[[:space:]]*=[[:space:]]*'STOCK'/display = 'GUPPY'/" "$var_file"
            log_success "ForgeX display mode restored to GUPPY"
            return 0
        fi
    fi
    return 1
}

# Check if running as root
check_root() {
    if [ "$(id -u)" != "0" ]; then
        log_error "This script must be run as root."
        log_error "Please run: sudo $0"
        exit 1
    fi
}

# Stop HelixScreen
stop_helixscreen() {
    log_info "Stopping HelixScreen..."

    INIT_SYSTEM=$(detect_init_system)

    if [ "$INIT_SYSTEM" = "systemd" ]; then
        systemctl stop helixscreen 2>/dev/null || true
        systemctl disable helixscreen 2>/dev/null || true
    fi

    # Stop via configured init script
    if [ -x "$INIT_SCRIPT" ]; then
        "$INIT_SCRIPT" stop 2>/dev/null || true
    fi

    # Also check both possible init script locations (for cross-firmware compatibility)
    for init_script in /etc/init.d/S80helixscreen /etc/init.d/S90helixscreen; do
        if [ -x "$init_script" ] && [ "$init_script" != "$INIT_SCRIPT" ]; then
            "$init_script" stop 2>/dev/null || true
        fi
    done

    # Kill any remaining processes (watchdog first to prevent crash dialog flash)
    if command -v killall >/dev/null 2>&1; then
        killall helix-watchdog 2>/dev/null || true
        killall helix-screen 2>/dev/null || true
        killall helix-splash 2>/dev/null || true
    elif command -v pidof >/dev/null 2>&1; then
        for proc in helix-watchdog helix-screen helix-splash; do
            for pid in $(pidof "$proc" 2>/dev/null); do
                kill "$pid" 2>/dev/null || true
            done
        done
    fi

    log_success "HelixScreen stopped"
}

# Remove init script or systemd service
remove_service() {
    log_info "Removing service configuration..."

    INIT_SYSTEM=$(detect_init_system)

    if [ "$INIT_SYSTEM" = "systemd" ]; then
        if [ -f "$SYSTEMD_SERVICE" ]; then
            rm -f "$SYSTEMD_SERVICE"
            systemctl daemon-reload
            log_success "Removed systemd service"
        fi
    fi

    # Remove configured init script
    if [ -f "$INIT_SCRIPT" ]; then
        rm -f "$INIT_SCRIPT"
        log_success "Removed SysV init script: $INIT_SCRIPT"
    fi

    # Also check and remove from both possible locations (for cross-firmware compatibility)
    for init_script in /etc/init.d/S80helixscreen /etc/init.d/S90helixscreen; do
        if [ -f "$init_script" ] && [ "$init_script" != "$INIT_SCRIPT" ]; then
            rm -f "$init_script"
            log_success "Removed SysV init script: $init_script"
        fi
    done
}

# Remove installation directory
remove_installation() {
    log_info "Removing installation..."

    local removed_any=false

    # Remove from configured location
    if [ -d "$INSTALL_DIR" ]; then
        rm -rf "$INSTALL_DIR"
        log_success "Removed $INSTALL_DIR"
        removed_any=true
    fi

    # Also check and remove from both possible locations (for cross-firmware compatibility)
    for install_dir in /root/printer_software/helixscreen /opt/helixscreen; do
        if [ -d "$install_dir" ] && [ "$install_dir" != "$INSTALL_DIR" ]; then
            rm -rf "$install_dir"
            log_success "Removed $install_dir"
            removed_any=true
        fi
    done

    if [ "$removed_any" = false ]; then
        log_warn "No HelixScreen installation found (already removed?)"
    fi

    # Clean up PID files
    rm -f /var/run/helixscreen.pid 2>/dev/null || true
    rm -f /var/run/helix-splash.pid 2>/dev/null || true

    # Clean up log file
    rm -f /tmp/helixscreen.log 2>/dev/null || true
}

# Re-enable previous UI
reenable_previous_ui() {
    log_info "Looking for previous screen UI to re-enable..."

    local found_ui=false
    local restored_xorg=false

    # For ForgeX firmware, restore display setting in variables.cfg first
    # This must happen before re-enabling GuppyScreen init script
    if [ "$AD5M_FIRMWARE" = "forge_x" ]; then
        restore_forgex_display || true
    fi

    # For Klipper Mod, re-enable Xorg first (required for KlipperScreen)
    if [ "$AD5M_FIRMWARE" = "klipper_mod" ] || [ -f "/etc/init.d/S40xorg" ]; then
        if [ -f "/etc/init.d/S40xorg" ]; then
            log_info "Re-enabling Xorg display server..."
            chmod +x "/etc/init.d/S40xorg" 2>/dev/null || true
            restored_xorg=true
        fi
    fi

    # First, try the specific previous UI script for this firmware
    if [ -n "$PREVIOUS_UI_SCRIPT" ] && [ -f "$PREVIOUS_UI_SCRIPT" ]; then
        log_info "Found previous UI: $PREVIOUS_UI_SCRIPT"
        chmod +x "$PREVIOUS_UI_SCRIPT" 2>/dev/null || true
        if "$PREVIOUS_UI_SCRIPT" start 2>/dev/null; then
            log_success "Re-enabled and started: $PREVIOUS_UI_SCRIPT"
            found_ui=true
        else
            log_warn "Re-enabled but failed to start: $PREVIOUS_UI_SCRIPT"
            log_warn "You may need to reboot"
            found_ui=true
        fi
    fi

    # Also scan for other UIs we might have disabled
    for ui in $PREVIOUS_UIS; do
        # Check for init.d scripts
        for initscript in /etc/init.d/S*${ui}* /opt/config/mod/.root/S*${ui}*; do
            # Skip if this is the PREVIOUS_UI_SCRIPT we already handled
            if [ "$initscript" = "$PREVIOUS_UI_SCRIPT" ]; then
                continue
            fi
            if [ -f "$initscript" ] 2>/dev/null; then
                log_info "Found previous UI: $initscript"
                # Re-enable by making executable
                chmod +x "$initscript" 2>/dev/null || true
                # Start it (only if we haven't already started one)
                if [ "$found_ui" = false ]; then
                    if "$initscript" start 2>/dev/null; then
                        log_success "Re-enabled and started: $initscript"
                        found_ui=true
                    else
                        log_warn "Re-enabled but failed to start: $initscript"
                        log_warn "You may need to reboot"
                        found_ui=true
                    fi
                else
                    log_info "Re-enabled: $initscript (not started, another UI already running)"
                fi
            fi
        done

        # Check for systemd services
        INIT_SYSTEM=$(detect_init_system)
        if [ "$INIT_SYSTEM" = "systemd" ]; then
            if systemctl list-unit-files "${ui}.service" >/dev/null 2>&1; then
                log_info "Found previous UI (systemd): $ui"
                systemctl enable "$ui" 2>/dev/null || true
                if systemctl start "$ui" 2>/dev/null; then
                    log_success "Re-enabled and started: $ui"
                    found_ui=true
                else
                    log_warn "Re-enabled but failed to start: $ui"
                    found_ui=true
                fi
            fi
        fi
    done

    if [ "$found_ui" = false ]; then
        log_info "No previous screen UI found to re-enable"
        log_info "If you had a stock UI, a reboot may restore it"
    fi

    if [ "$restored_xorg" = true ]; then
        log_info "Re-enabled: Xorg (/etc/init.d/S40xorg)"
    fi
}

# Main uninstall
main() {
    local force=false

    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            --force|-f)
                force=true
                shift
                ;;
            --help|-h)
                echo "HelixScreen Uninstaller"
                echo ""
                echo "Usage: $0 [options]"
                echo ""
                echo "Options:"
                echo "  --force, -f   Skip confirmation prompt"
                echo "  --help, -h    Show this help message"
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    echo ""
    echo "${CYAN}========================================${NC}"
    echo "${CYAN}     HelixScreen Uninstaller${NC}"
    echo "${CYAN}========================================${NC}"
    echo ""

    # Check for root
    check_root

    # Detect platform and firmware to set correct paths
    local platform=$(detect_platform)
    if [ "$platform" = "ad5m" ]; then
        AD5M_FIRMWARE=$(detect_ad5m_firmware)
        log_info "Detected AD5M firmware: $AD5M_FIRMWARE"
    fi
    set_install_paths "$platform" "$AD5M_FIRMWARE"

    # Confirm unless --force
    if [ "$force" = false ]; then
        echo "This will:"
        echo "  - Stop HelixScreen"
        echo "  - Remove $INSTALL_DIR"
        echo "  - Remove service configuration"
        echo "  - Re-enable previous screen UI (if found)"
        if [ "$AD5M_FIRMWARE" = "forge_x" ]; then
            echo "  - Restore ForgeX display configuration (GuppyScreen)"
        fi
        echo ""
        printf "Are you sure you want to continue? [y/N] "
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                ;;
            *)
                log_info "Uninstall cancelled"
                exit 0
                ;;
        esac
        echo ""
    fi

    # Perform uninstall
    stop_helixscreen
    remove_service
    remove_installation
    reenable_previous_ui

    echo ""
    echo "${GREEN}========================================${NC}"
    echo "${GREEN}    Uninstall Complete!${NC}"
    echo "${GREEN}========================================${NC}"
    echo ""
    log_info "HelixScreen has been removed."
    log_info "A reboot is recommended to ensure clean state."
    echo ""
}

main "$@"
