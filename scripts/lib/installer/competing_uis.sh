#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: competing_uis
# Stop competing screen UIs (GuppyScreen, KlipperScreen, Xorg, stock FlashForge UI)
#
# Reads: AD5M_FIRMWARE, INIT_SYSTEM, PREVIOUS_UI_SCRIPT, SUDO, INSTALL_DIR

# Source guard
[ -n "${_HELIX_COMPETING_UIS_SOURCED:-}" ] && return 0
_HELIX_COMPETING_UIS_SOURCED=1

# Known competing screen UIs to stop
# Includes: GuppyScreen (AD5M/K1), Grumpyscreen (K1/Simple AF), KlipperScreen, FeatherScreen
COMPETING_UIS="guppyscreen GuppyScreen grumpyscreen Grumpyscreen KlipperScreen klipperscreen featherscreen FeatherScreen"

# Record a disabled service for later re-enablement
# Args: $1 = type ("systemd" or "sysv-chmod"), $2 = target (service name or script path)
record_disabled_service() {
    local type="$1"
    local target="$2"
    local entry="${type}:${target}"
    local state_file="${INSTALL_DIR}/config/.disabled_services"

    # Ensure config directory exists
    if [ -n "${INSTALL_DIR:-}" ] && [ ! -d "${INSTALL_DIR}/config" ]; then
        $(file_sudo "${INSTALL_DIR}") mkdir -p "${INSTALL_DIR}/config"
    fi

    # Don't duplicate entries
    if [ -f "$state_file" ] && grep -qF "$entry" "$state_file" 2>/dev/null; then
        return 0
    fi

    echo "$entry" | $(file_sudo "${INSTALL_DIR}/config") tee -a "$state_file" >/dev/null
}

# Stop ForgeX-specific competing UIs (stock FlashForge firmware UI)
stop_forgex_competing_uis() {
    # Stop stock FlashForge firmware UI (AD5M/Adventurer 5M)
    # ffstartup-arm is the startup manager that launches firmwareExe (the stock Qt UI)
    if [ -f /opt/PROGRAM/ffstartup-arm ]; then
        log_info "Stopping stock FlashForge UI..."
        kill_process_by_name firmwareExe ffstartup-arm || true
        found_any=true
    fi
}

# Stop Klipper Mod-specific competing UIs (Xorg, KlipperScreen)
stop_kmod_competing_uis() {
    # Stop Xorg first (required for framebuffer access)
    # Xorg takes over /dev/fb0 layer, preventing direct framebuffer rendering
    if [ -x "/etc/init.d/S40xorg" ]; then
        log_info "Stopping Xorg (Klipper Mod display server)..."
        $SUDO /etc/init.d/S40xorg stop 2>/dev/null || true
        # Disable Xorg init script (non-destructive, reversible)
        $SUDO chmod -x /etc/init.d/S40xorg 2>/dev/null || true
        record_disabled_service "sysv-chmod" "/etc/init.d/S40xorg"
        # Kill any remaining Xorg processes
        kill_process_by_name Xorg X || true
        found_any=true
    fi

    # Kill python processes running KlipperScreen (common on Klipper Mod)
    # BusyBox ps doesn't support 'aux', use portable approach
    # shellcheck disable=SC2009
    for pid in $(ps -ef 2>/dev/null | grep -E 'KlipperScreen.*screen\.py' | grep -v grep | awk '{print $2}'); do
        log_info "Killing KlipperScreen python process (PID $pid)..."
        $SUDO kill "$pid" 2>/dev/null || true
        found_any=true
    done
}

# Stop competing screen UIs (GuppyScreen, KlipperScreen, Xorg, etc.)
# Dispatches platform-specific logic, then runs generic UI stopping
stop_competing_uis() {
    log_info "Checking for competing screen UIs..."

    found_any=false

    # Platform-specific competing UI handling
    case "$AD5M_FIRMWARE" in
        forge_x)    stop_forgex_competing_uis ;;
        klipper_mod) stop_kmod_competing_uis ;;
    esac

    # Handle the specific previous UI if we know it (for clean reversibility)
    if [ -n "$PREVIOUS_UI_SCRIPT" ] && [ -x "$PREVIOUS_UI_SCRIPT" ] 2>/dev/null; then
        log_info "Stopping previous UI: $PREVIOUS_UI_SCRIPT"
        $SUDO "$PREVIOUS_UI_SCRIPT" stop 2>/dev/null || true
        # Disable by removing execute permission (non-destructive, reversible)
        $SUDO chmod -x "$PREVIOUS_UI_SCRIPT" 2>/dev/null || true
        record_disabled_service "sysv-chmod" "$PREVIOUS_UI_SCRIPT"
        found_any=true
    fi

    for ui in $COMPETING_UIS; do
        # Check systemd services
        if [ "$INIT_SYSTEM" = "systemd" ]; then
            if $SUDO systemctl is-active --quiet "$ui" 2>/dev/null; then
                log_info "Stopping $ui (systemd service)..."
                $SUDO systemctl stop "$ui" 2>/dev/null || true
                $SUDO systemctl disable "$ui" 2>/dev/null || true
                record_disabled_service "systemd" "$ui"
                found_any=true
            fi
        fi

        # Check SysV init scripts (various locations)
        for initscript in /etc/init.d/S*${ui}* /etc/init.d/${ui}* /opt/config/mod/.root/S*${ui}*; do
            # Skip if glob didn't match any files (literal pattern returned)
            [ -e "$initscript" ] || continue
            # Skip if this is the PREVIOUS_UI_SCRIPT we already handled
            if [ "$initscript" = "$PREVIOUS_UI_SCRIPT" ]; then
                continue
            fi
            if [ -x "$initscript" ]; then
                log_info "Stopping $ui ($initscript)..."
                $SUDO "$initscript" stop 2>/dev/null || true
                # Disable by removing execute permission (non-destructive)
                $SUDO chmod -x "$initscript" 2>/dev/null || true
                record_disabled_service "sysv-chmod" "$initscript"
                found_any=true
            fi
        done

        # Kill any remaining processes by name
        if kill_process_by_name "$ui"; then
            log_info "Killed remaining $ui processes"
            found_any=true
        fi
    done

    if [ "$found_any" = true ]; then
        log_info "Waiting for competing UIs to stop..."
        sleep 2
    else
        log_info "No competing UIs found"
    fi
}
