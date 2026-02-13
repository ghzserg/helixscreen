#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: uninstall
# Uninstall and clean installation functions
#
# Reads: All paths, INIT_SYSTEM, SUDO, AD5M_FIRMWARE, SERVICE_NAME, INSTALL_DIR
# Writes: (none)

# Source guard
[ -n "${_HELIX_UNINSTALL_SOURCED:-}" ] && return 0
_HELIX_UNINSTALL_SOURCED=1

# Re-enable services that were disabled during installation
# Reads the state file and reverses each recorded disable action
reenable_disabled_services() {
    local state_file="${INSTALL_DIR}/config/.disabled_services"
    [ -f "$state_file" ] || return 0

    log_info "Re-enabling previously disabled services..."
    while IFS= read -r entry; do
        # Skip empty lines and comments
        case "$entry" in ""|\#*) continue ;; esac

        local type="${entry%%:*}"
        local target="${entry#*:}"

        case "$type" in
            systemd)
                log_info "Re-enabling systemd service: $target"
                $SUDO systemctl enable "$target" 2>/dev/null || true
                ;;
            sysv-chmod)
                if [ -f "$target" ]; then
                    log_info "Re-enabling init script: $target"
                    $SUDO chmod +x "$target" 2>/dev/null || true
                fi
                ;;
        esac
    done < "$state_file"
}

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
        for init_script in $HELIX_INIT_SCRIPTS; do
            if [ -f "$init_script" ]; then
                log_info "Stopping and removing $init_script..."
                $SUDO "$init_script" stop 2>/dev/null || true
                $SUDO rm -f "$init_script"
            fi
        done
    fi

    # Kill any remaining processes (watchdog first to prevent crash dialog flash)
    # shellcheck disable=SC2086
    kill_process_by_name $HELIX_PROCESSES

    # Clean up PID files and log file
    $SUDO rm -f /var/run/helixscreen.pid 2>/dev/null || true
    $SUDO rm -f /var/run/helix-splash.pid 2>/dev/null || true
    rm -f /tmp/helixscreen.log 2>/dev/null || true

    # Re-enable services from state file (before removing install dir)
    reenable_disabled_services

    # Remove installation (check all possible locations)
    local removed_dir=""
    for install_dir in $HELIX_INSTALL_DIRS; do
        if [ -d "$install_dir" ]; then
            $SUDO rm -rf "$install_dir"
            log_success "Removed ${install_dir}"
            removed_dir="$install_dir"
            # Also remove the updater repo clone if present
            if [ -d "${install_dir}-repo" ]; then
                $SUDO rm -rf "${install_dir}-repo"
                log_success "Removed ${install_dir}-repo"
            fi
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

    # ForgeX - restore GuppyScreen and stock UI settings
    if [ -z "$restored_ui" ] && [ "$AD5M_FIRMWARE" = "forge_x" ]; then
        uninstall_forgex
    fi

    # Clean up helixscreen cache directories
    for cache_dir in /root/.cache/helix /tmp/helix_thumbs /.cache/helix /data/helixscreen/cache /usr/data/helixscreen/cache; do
        if [ -d "$cache_dir" ] 2>/dev/null; then
            log_info "Removing cache: $cache_dir"
            $(file_sudo "$cache_dir") rm -rf "$cache_dir"
        fi
    done
    # Clean up /var/tmp helix files
    for tmp_pattern in /var/tmp/helix_*; do
        if [ -e "$tmp_pattern" ] 2>/dev/null; then
            log_info "Removing cache: $tmp_pattern"
            $(file_sudo "$tmp_pattern") rm -rf "$tmp_pattern"
        fi
    done

    # Clean up active flag file
    rm -f /tmp/helixscreen_active 2>/dev/null || true

    # Clean up macOS resource fork files (created by scp from Mac)
    for pattern in /opt/._helixscreen /root/._helixscreen; do
        $(file_sudo "$pattern") rm -f "$pattern" 2>/dev/null || true
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
    for install_dir in $HELIX_INSTALL_DIRS; do
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
        "/var/tmp/helix_thumbs" \
        "/var/tmp/helix_*" \
        "/data/helixscreen/cache" \
        "/usr/data/helixscreen/cache"
    do
        for cache_dir in $cache_pattern; do
            if [ -d "$cache_dir" ] 2>/dev/null; then
                log_info "Removing cache: $cache_dir"
                $SUDO rm -rf "$cache_dir"
            fi
        done
    done

    # Remove init scripts (check all possible locations)
    for init_script in $HELIX_INIT_SCRIPTS; do
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
