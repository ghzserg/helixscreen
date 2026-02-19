#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: permissions
# Permission checks, sudo handling, and system permission rules (udev, polkit)
#
# Reads: (platform passed as argument), INSTALL_DIR, KLIPPER_USER, SUDO
# Writes: SUDO

# Source guard
[ -n "${_HELIX_PERMISSIONS_SOURCED:-}" ] && return 0
_HELIX_PERMISSIONS_SOURCED=1

# Initialize SUDO (will be set by check_permissions)
SUDO=""

# Check if running as root (required for AD5M/K1, optional for Pi)
# Sets: SUDO variable ("sudo" or "")
check_permissions() {
    local platform=$1

    if [ "$platform" = "ad5m" ] || [ "$platform" = "k1" ]; then
        if [ "$(id -u)" != "0" ]; then
            log_error "Installation on $platform requires root privileges."
            log_error "Please run: sudo $0 $*"
            exit 1
        fi
        SUDO=""
    else
        # Pi: warn if not root but allow sudo
        if [ "$(id -u)" != "0" ]; then
            if ! command -v sudo >/dev/null 2>&1; then
                log_error "Not running as root and sudo is not available."
                log_error "Please run as root or install sudo."
                exit 1
            fi
            log_info "Not running as root. Will use sudo for privileged operations."
            SUDO="sudo"
        else
            SUDO=""
        fi
    fi
}

# Install system permission rules for non-root operation
# - udev rule: backlight brightness write access for video group
# - polkit rule: NetworkManager access for service user
# Only installed on platforms that run as non-root (Pi, generic Linux)
install_permission_rules() {
    local platform=$1
    local helix_user="${KLIPPER_USER:-root}"

    # Skip for platforms that run as root (AD5M, K1) or if user is root
    if [ "$platform" = "ad5m" ] || [ "$platform" = "k1" ] || [ "$helix_user" = "root" ]; then
        log_info "Skipping permission rules (running as root)"
        return 0
    fi

    # --- Backlight udev rule ---
    local udev_src="${INSTALL_DIR}/config/99-helixscreen-backlight.rules"
    local udev_dest="/etc/udev/rules.d/99-helixscreen-backlight.rules"

    if [ -f "$udev_src" ] && [ -d /etc/udev/rules.d ]; then
        $SUDO cp "$udev_src" "$udev_dest"
        # Trigger udev to apply immediately for any existing backlight devices
        $SUDO udevadm trigger --subsystem-match=backlight 2>/dev/null || true
        log_info "Installed backlight udev rule"
    fi

    # --- NetworkManager polkit rule ---
    local pkla_src="${INSTALL_DIR}/config/helixscreen-network.pkla"

    if [ -f "$pkla_src" ] && command -v nmcli >/dev/null 2>&1; then
        # polkit JavaScript rules for newer polkit (Debian 12+, polkit >= 0.106)
        local rules_dir="/etc/polkit-1/rules.d"
        # polkit local authority (.pkla) for older polkit (Debian 11 and similar)
        local pkla_dir="/etc/polkit-1/localauthority/50-local.d"

        if [ -d "$rules_dir" ]; then
            # JavaScript rules format (newer polkit, Debian 12+)
            local rules_dest="${rules_dir}/50-helixscreen-network.rules"
            $SUDO tee "$rules_dest" > /dev/null << POLKIT_EOF
// Installed by HelixScreen — allow service user to manage NetworkManager
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.user === "${helix_user}") {
        return polkit.Result.YES;
    }
});
POLKIT_EOF
            log_info "Installed NetworkManager polkit rule (.rules)"
        elif [ -d "$pkla_dir" ]; then
            # .pkla format (pklocalauthority, Debian 11 and older)
            local pkla_dest="${pkla_dir}/helixscreen-network.pkla"
            $SUDO cp "$pkla_src" "$pkla_dest"
            # Template the user
            $SUDO sed -i "s|@@HELIX_USER@@|${helix_user}|g" "$pkla_dest" 2>/dev/null || \
            $SUDO sed -i '' "s|@@HELIX_USER@@|${helix_user}|g" "$pkla_dest" 2>/dev/null || true
            log_info "Installed NetworkManager polkit rule (.pkla)"
        else
            log_warn "polkit rules directory not found — Wi-Fi may not work as non-root"
        fi
    fi
}
