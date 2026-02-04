#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: uninstall
# Uninstall and clean installation functions
#
# Reads: All paths, INIT_SYSTEM, SUDO, AD5M_FIRMWARE, SERVICE_NAME
# Writes: (none)

# Source guard
[ -n "${_HELIX_UNINSTALL_SOURCED:-}" ] && return 0
_HELIX_UNINSTALL_SOURCED=1

# Uninstall HelixScreen
# Args: platform (optional)
uninstall() {
    local platform=${1:-}

    log_info "Uninstalling HelixScreen..."

    # Detect init system first
    detect_init_system

    if [ "$INIT_SYSTEM" = "systemd" ]; then
        # Stop and disable systemd service
        $SUDO systemctl stop "$SERVICE_NAME" 2>/dev/null || true
        $SUDO systemctl disable "$SERVICE_NAME" 2>/dev/null || true
        $SUDO rm -f "/etc/systemd/system/${SERVICE_NAME}.service"
        $SUDO systemctl daemon-reload
    else
        # Stop and remove SysV init scripts (check all possible locations)
        # AD5M: S80/S90, K1: S99
        for init_script in /etc/init.d/S80helixscreen /etc/init.d/S90helixscreen /etc/init.d/S99helixscreen; do
            if [ -f "$init_script" ]; then
                log_info "Stopping and removing $init_script..."
                $SUDO "$init_script" stop 2>/dev/null || true
                $SUDO rm -f "$init_script"
            fi
        done
    fi

    # Kill any remaining processes (watchdog first to prevent crash dialog flash)
    if command -v killall >/dev/null 2>&1; then
        $SUDO killall helix-watchdog 2>/dev/null || true
        $SUDO killall helix-screen 2>/dev/null || true
        $SUDO killall helix-splash 2>/dev/null || true
    elif command -v pidof >/dev/null 2>&1; then
        for proc in helix-watchdog helix-screen helix-splash; do
            for pid in $(pidof "$proc" 2>/dev/null); do
                $SUDO kill "$pid" 2>/dev/null || true
            done
        done
    fi

    # Clean up PID files and log file
    $SUDO rm -f /var/run/helixscreen.pid 2>/dev/null || true
    $SUDO rm -f /var/run/helix-splash.pid 2>/dev/null || true
    $SUDO rm -f /tmp/helixscreen.log 2>/dev/null || true

    # Remove installation (check all possible locations)
    # AD5M: /opt/helixscreen, /root/printer_software/helixscreen
    # K1: /usr/data/helixscreen
    local removed_dir=""
    for install_dir in "/root/printer_software/helixscreen" "/opt/helixscreen" "/usr/data/helixscreen"; do
        if [ -d "$install_dir" ]; then
            $SUDO rm -rf "$install_dir"
            log_success "Removed ${install_dir}"
            removed_dir="$install_dir"
        fi
    done

    if [ -z "$removed_dir" ]; then
        log_warn "No HelixScreen installation found"
    fi

    # Re-enable the previous UI based on firmware
    log_info "Re-enabling previous screen UI..."
    local restored_ui=""
    local restored_xorg=""

    if [ "$AD5M_FIRMWARE" = "klipper_mod" ] || [ -f "/etc/init.d/S80klipperscreen" ]; then
        # Klipper Mod - restore Xorg and KlipperScreen
        if [ -f "/etc/init.d/S40xorg" ]; then
            $SUDO chmod +x "/etc/init.d/S40xorg" 2>/dev/null || true
            restored_xorg="Xorg (/etc/init.d/S40xorg)"
        fi
        if [ -f "/etc/init.d/S80klipperscreen" ]; then
            $SUDO chmod +x "/etc/init.d/S80klipperscreen" 2>/dev/null || true
            restored_ui="KlipperScreen (/etc/init.d/S80klipperscreen)"
        fi
    fi

    # Check for K1/Simple AF GuppyScreen
    if [ -z "$restored_ui" ] && [ -f "/etc/init.d/S99guppyscreen" ]; then
        $SUDO chmod +x "/etc/init.d/S99guppyscreen" 2>/dev/null || true
        restored_ui="GuppyScreen (/etc/init.d/S99guppyscreen)"
    fi

    if [ -z "$restored_ui" ]; then
        # Forge-X - restore GuppyScreen and stock UI settings
        # Restore ForgeX display mode to GUPPY (from HEADLESS or STOCK)
        if [ -f "/opt/config/mod_data/variables.cfg" ]; then
            if grep -q "display[[:space:]]*=[[:space:]]*'HEADLESS'" "/opt/config/mod_data/variables.cfg"; then
                log_info "Restoring ForgeX display mode to GUPPY..."
                $SUDO sed -i "s/display[[:space:]]*=[[:space:]]*'HEADLESS'/display = 'GUPPY'/" "/opt/config/mod_data/variables.cfg"
            elif grep -q "display[[:space:]]*=[[:space:]]*'STOCK'" "/opt/config/mod_data/variables.cfg"; then
                log_info "Restoring ForgeX display mode to GUPPY..."
                $SUDO sed -i "s/display[[:space:]]*=[[:space:]]*'STOCK'/display = 'GUPPY'/" "/opt/config/mod_data/variables.cfg"
            fi
        fi
        # Restore stock FlashForge UI in auto_run.sh
        restore_stock_firmware_ui || true
        # Remove HelixScreen patch from screen.sh
        unpatch_forgex_screen_sh || true
        # Re-enable GuppyScreen and tslib init scripts
        if [ -f "/opt/config/mod/.root/S80guppyscreen" ]; then
            $SUDO chmod +x "/opt/config/mod/.root/S80guppyscreen" 2>/dev/null || true
            restored_ui="GuppyScreen (/opt/config/mod/.root/S80guppyscreen)"
        fi
        if [ -f "/opt/config/mod/.root/S35tslib" ]; then
            $SUDO chmod +x "/opt/config/mod/.root/S35tslib" 2>/dev/null || true
        fi
        # Clean up any leftover backup files from manual patches
        for backup_file in /opt/config/mod/.shell/*.helix-backup /opt/config/mod/.shell/*.bak; do
            if [ -f "$backup_file" ] 2>/dev/null; then
                log_info "Removing leftover backup: $backup_file"
                $SUDO rm -f "$backup_file"
            fi
        done
    fi

    # Clean up helixscreen cache directories
    for cache_dir in /root/.cache/helix /tmp/helix_thumbs /.cache/helix; do
        if [ -d "$cache_dir" ] 2>/dev/null; then
            log_info "Removing cache: $cache_dir"
            $SUDO rm -rf "$cache_dir"
        fi
    done

    # Clean up active flag file
    $SUDO rm -f /tmp/helixscreen_active 2>/dev/null || true

    # Clean up macOS resource fork files (created by scp from Mac)
    for pattern in /opt/._helixscreen /root/._helixscreen; do
        $SUDO rm -f "$pattern" 2>/dev/null || true
    done

    # Remove update_manager section from moonraker.conf (if present)
    if type remove_update_manager_section >/dev/null 2>&1; then
        remove_update_manager_section || true
    fi

    log_success "HelixScreen uninstalled"
    if [ -n "$restored_xorg" ]; then
        log_info "Re-enabled: $restored_xorg"
    fi
    if [ -n "$restored_ui" ]; then
        log_info "Re-enabled: $restored_ui"
        log_info "Reboot to start the previous UI"
    else
        log_info "Note: No previous UI found to restore"
    fi
}

# Clean up old installation completely (for --clean flag)
# Removes all files, config, and caches without backup
# Args: platform
clean_old_installation() {
    local platform=$1

    log_warn "=========================================="
    log_warn "  CLEAN INSTALL MODE"
    log_warn "=========================================="
    log_warn ""
    log_warn "This will PERMANENTLY DELETE:"
    log_warn "  - All HelixScreen files in ${INSTALL_DIR}"
    log_warn "  - Your configuration (helixconfig.json)"
    log_warn "  - Thumbnail cache files"
    log_warn ""

    # Interactive confirmation if stdin is a terminal
    if [ -t 0 ]; then
        printf "Are you sure? [y/N] "
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                ;;
            *)
                log_info "Clean install cancelled."
                exit 0
                ;;
        esac
    fi

    log_info "Cleaning old installation..."

    # Stop any running services
    stop_service

    # Remove installation directories (check all possible locations)
    # AD5M: /opt/helixscreen, /root/printer_software/helixscreen
    # K1: /usr/data/helixscreen
    for install_dir in "/root/printer_software/helixscreen" "/opt/helixscreen" "/usr/data/helixscreen"; do
        if [ -d "$install_dir" ]; then
            log_info "Removing $install_dir..."
            $SUDO rm -rf "$install_dir"
        fi
    done

    # Remove thumbnail caches (POSIX-compatible: no arrays)
    for cache_pattern in \
        "/root/.cache/helix/helix_thumbs" \
        "/home/*/.cache/helix/helix_thumbs" \
        "/tmp/helix_thumbs" \
        "/var/tmp/helix_thumbs"
    do
        for cache_dir in $cache_pattern; do
            if [ -d "$cache_dir" ] 2>/dev/null; then
                log_info "Removing cache: $cache_dir"
                $SUDO rm -rf "$cache_dir"
            fi
        done
    done

    # Remove init scripts (check all possible locations)
    # AD5M: S80/S90, K1: S99
    for init_script in /etc/init.d/S80helixscreen /etc/init.d/S90helixscreen /etc/init.d/S99helixscreen; do
        if [ -f "$init_script" ]; then
            log_info "Removing init script: $init_script"
            $SUDO rm -f "$init_script"
        fi
    done

    # Remove systemd service if present
    if [ -f "/etc/systemd/system/${SERVICE_NAME}.service" ]; then
        log_info "Removing systemd service..."
        $SUDO systemctl disable "$SERVICE_NAME" 2>/dev/null || true
        $SUDO rm -f "/etc/systemd/system/${SERVICE_NAME}.service"
        $SUDO systemctl daemon-reload 2>/dev/null || true
    fi

    log_success "Old installation cleaned"
    echo ""
}
