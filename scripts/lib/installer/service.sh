#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: service
# Service installation and management (systemd and SysV)
#
# Reads: INIT_SYSTEM, INSTALL_DIR, INIT_SCRIPT_DEST, SERVICE_NAME, SUDO
# Writes: CLEANUP_SERVICE

# Source guard
[ -n "${_HELIX_SERVICE_SOURCED:-}" ] && return 0
_HELIX_SERVICE_SOURCED=1

# SERVICE_NAME is defined in common.sh

# Install service (dispatcher)
# Calls install_service_systemd or install_service_sysv based on INIT_SYSTEM
install_service() {
    local platform=$1

    if [ "$INIT_SYSTEM" = "systemd" ]; then
        install_service_systemd
    else
        install_service_sysv
    fi
}

# Install systemd service
install_service_systemd() {
    log_info "Installing systemd service..."

    local service_src="${INSTALL_DIR}/config/helixscreen.service"
    local service_dest="/etc/systemd/system/${SERVICE_NAME}.service"

    if [ ! -f "$service_src" ]; then
        log_error "Service file not found: $service_src"
        log_error "The release package may be incomplete."
        exit 1
    fi

    $SUDO cp "$service_src" "$service_dest"

    # Template placeholders (match SysV pattern in install_service_sysv)
    local helix_user="${KLIPPER_USER:-root}"
    local helix_group="${KLIPPER_USER:-root}"
    local install_dir="${INSTALL_DIR:-/opt/helixscreen}"

    $SUDO sed -i "s|@@HELIX_USER@@|${helix_user}|g" "$service_dest" 2>/dev/null || \
    $SUDO sed -i '' "s|@@HELIX_USER@@|${helix_user}|g" "$service_dest" 2>/dev/null || true

    $SUDO sed -i "s|@@HELIX_GROUP@@|${helix_group}|g" "$service_dest" 2>/dev/null || \
    $SUDO sed -i '' "s|@@HELIX_GROUP@@|${helix_group}|g" "$service_dest" 2>/dev/null || true

    $SUDO sed -i "s|@@INSTALL_DIR@@|${install_dir}|g" "$service_dest" 2>/dev/null || \
    $SUDO sed -i '' "s|@@INSTALL_DIR@@|${install_dir}|g" "$service_dest" 2>/dev/null || true

    if ! $SUDO systemctl daemon-reload; then
        log_error "Failed to reload systemd daemon."
        exit 1
    fi

    # Install update watcher (restarts helixscreen after Moonraker web-type update)
    # Workaround for mainsail-crew/mainsail#2444: type: web lacks managed_services
    install_update_watcher_systemd

    CLEANUP_SERVICE=true
    log_success "Installed systemd service"
}

# Install systemd path unit that restarts helixscreen after Moonraker extracts an update
install_update_watcher_systemd() {
    local path_src="${INSTALL_DIR}/config/helixscreen-update.path"
    local svc_src="${INSTALL_DIR}/config/helixscreen-update.service"
    local path_dest="/etc/systemd/system/helixscreen-update.path"
    local svc_dest="/etc/systemd/system/helixscreen-update.service"

    if [ ! -f "$path_src" ] || [ ! -f "$svc_src" ]; then
        log_info "Update watcher units not found, skipping"
        return 0
    fi

    local install_dir="${INSTALL_DIR:-/opt/helixscreen}"

    $SUDO cp "$path_src" "$path_dest"
    $SUDO cp "$svc_src" "$svc_dest"

    # Template the install directory path
    $SUDO sed -i "s|@@INSTALL_DIR@@|${install_dir}|g" "$path_dest" 2>/dev/null || \
    $SUDO sed -i '' "s|@@INSTALL_DIR@@|${install_dir}|g" "$path_dest" 2>/dev/null || true

    $SUDO systemctl daemon-reload
    $SUDO systemctl enable helixscreen-update.path 2>/dev/null || true
    $SUDO systemctl start helixscreen-update.path 2>/dev/null || true

    log_info "Installed update watcher (helixscreen-update.path)"
}

# Install SysV init script
install_service_sysv() {
    log_info "Installing SysV init script..."

    local init_src="${INSTALL_DIR}/config/helixscreen.init"

    if [ ! -f "$init_src" ]; then
        log_error "Init script not found: $init_src"
        log_error "The release package may be incomplete."
        exit 1
    fi

    # Use the dynamically set INIT_SCRIPT_DEST (varies by firmware)
    $SUDO cp "$init_src" "$INIT_SCRIPT_DEST"
    $SUDO chmod +x "$INIT_SCRIPT_DEST"

    # Update the DAEMON_DIR in the init script to match the install location
    # This is important for Klipper Mod which uses a different path
    $SUDO sed -i "s|DAEMON_DIR=.*|DAEMON_DIR=\"${INSTALL_DIR}\"|" "$INIT_SCRIPT_DEST" 2>/dev/null || \
    $SUDO sed -i '' "s|DAEMON_DIR=.*|DAEMON_DIR=\"${INSTALL_DIR}\"|" "$INIT_SCRIPT_DEST" 2>/dev/null || true

    CLEANUP_SERVICE=true
    log_success "Installed SysV init script at $INIT_SCRIPT_DEST"
}

# Start service (dispatcher)
# Calls start_service_systemd or start_service_sysv based on INIT_SYSTEM
start_service() {
    if [ "$INIT_SYSTEM" = "systemd" ]; then
        start_service_systemd
    else
        start_service_sysv
    fi
}

# Start service (systemd)
start_service_systemd() {
    log_info "Enabling and starting HelixScreen (systemd)..."

    if ! $SUDO systemctl enable "$SERVICE_NAME"; then
        log_error "Failed to enable ${SERVICE_NAME} service."
        exit 1
    fi

    if ! $SUDO systemctl start "$SERVICE_NAME"; then
        log_error "Failed to start ${SERVICE_NAME} service."
        log_error "Check logs with: journalctl -u ${SERVICE_NAME} -n 50"
        exit 1
    fi

    # Wait for service to start (may be slow on embedded hardware)
    local i
    for i in 1 2 3 4 5; do
        sleep 1
        if $SUDO systemctl is-active --quiet "$SERVICE_NAME"; then
            log_success "HelixScreen is running!"
            return
        fi
    done
    log_warn "Service may still be starting..."
    log_warn "Check status with: systemctl status $SERVICE_NAME"
}

# Start service (SysV init)
start_service_sysv() {
    log_info "Starting HelixScreen (SysV init)..."

    if [ ! -x "$INIT_SCRIPT_DEST" ]; then
        log_error "Init script not executable: $INIT_SCRIPT_DEST"
        exit 1
    fi

    if ! $SUDO "$INIT_SCRIPT_DEST" start; then
        log_error "Failed to start HelixScreen."
        log_error "Check logs in: /tmp/helixscreen.log"
        exit 1
    fi

    # Wait for service to start (may be slow on embedded hardware)
    local i
    for i in 1 2 3 4 5; do
        sleep 1
        if $SUDO "$INIT_SCRIPT_DEST" status >/dev/null 2>&1; then
            log_success "HelixScreen is running!"
            return
        fi
    done
    log_warn "Service may still be starting..."
    log_warn "Check: $INIT_SCRIPT_DEST status"
}

# Deploy platform-specific hook file
# Copies the correct hook file to $INSTALL_DIR/platform/hooks.sh so the
# init script can source it at runtime.
deploy_platform_hooks() {
    local install_dir="$1"
    local platform="$2"  # "ad5m-forgex", "ad5m-kmod", "pi", "k1"
    local hooks_src="${install_dir}/config/platform/hooks-${platform}.sh"

    if [ ! -f "$hooks_src" ]; then
        log_warn "No platform hooks for: $platform"
        return 0
    fi

    $SUDO mkdir -p "${install_dir}/platform"
    $SUDO cp "$hooks_src" "${install_dir}/platform/hooks.sh"
    $SUDO chmod +x "${install_dir}/platform/hooks.sh"
    log_info "Deployed platform hooks: $platform"
}

# Fix ownership of config directory for non-root Klipper users
# Binaries stay root-owned for security; only config needs user write access
fix_install_ownership() {
    local user="${KLIPPER_USER:-}"
    if [ -n "$user" ] && [ "$user" != "root" ] && [ -d "$INSTALL_DIR" ]; then
        log_info "Setting ownership to ${user}..."
        if [ -d "${INSTALL_DIR}/config" ]; then
            $SUDO chown -R "${user}:${user}" "${INSTALL_DIR}/config"
        fi
    fi
}

# Stop service for update
stop_service() {
    if [ "$INIT_SYSTEM" = "systemd" ]; then
        if $SUDO systemctl is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
            log_info "Stopping existing HelixScreen service (systemd)..."
            $SUDO systemctl stop "$SERVICE_NAME" || true
        fi
    else
        # Try the configured init script location first
        if [ -n "$INIT_SCRIPT_DEST" ] && [ -x "$INIT_SCRIPT_DEST" ]; then
            log_info "Stopping existing HelixScreen service (SysV)..."
            $SUDO "$INIT_SCRIPT_DEST" stop 2>/dev/null || true
        fi
        # Also check all possible locations (for updates/uninstalls)
        for init_script in $HELIX_INIT_SCRIPTS; do
            if [ -x "$init_script" ]; then
                log_info "Stopping HelixScreen at $init_script..."
                $SUDO "$init_script" stop 2>/dev/null || true
            fi
        done
        # Also try to kill by name (watchdog first to prevent crash dialog flash)
        # shellcheck disable=SC2086
        kill_process_by_name $HELIX_PROCESSES
    fi
}
