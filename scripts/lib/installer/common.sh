#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: common
# Core utilities: logging, colors, error handling, cleanup
#
# Reads: -
# Writes: RED, GREEN, YELLOW, CYAN, BOLD, NC, CLEANUP_TMP, BACKUP_CONFIG, INSTALL_DIR, GITHUB_REPO

# Source guard
[ -n "${_HELIX_COMMON_SOURCED:-}" ] && return 0
_HELIX_COMMON_SOURCED=1

# Default configuration (can be overridden before sourcing)
: "${GITHUB_REPO:=prestonbrown/helixscreen}"
: "${INSTALL_DIR:=/opt/helixscreen}"
: "${SERVICE_NAME:=helixscreen}"

# Well-known paths (used by uninstall, clean, stop_service)
# AD5M: /opt/helixscreen or /root/printer_software/helixscreen
# K1: /usr/data/helixscreen
# Pi: /opt/helixscreen
HELIX_INSTALL_DIRS="/root/printer_software/helixscreen /opt/helixscreen /usr/data/helixscreen"

# Init script locations vary by platform/firmware
# AD5M Klipper Mod: S80, AD5M Forge-X: S90, K1: S99
HELIX_INIT_SCRIPTS="/etc/init.d/S80helixscreen /etc/init.d/S90helixscreen /etc/init.d/S99helixscreen"

# HelixScreen process names (order matters: watchdog first to prevent crash dialog)
HELIX_PROCESSES="helix-watchdog helix-screen helix-splash"

# Get sudo prefix needed for a file operation.
# Returns empty string if current user has write access, $SUDO otherwise.
# For existing files, checks file writability. For new files, checks parent dir.
# This avoids creating root-owned files in user-writable directories.
file_sudo() {
    local path="$1"
    if [ -e "$path" ]; then
        [ -w "$path" ] && echo "" || echo "$SUDO"
    else
        local dir
        dir="$(dirname "$path")"
        [ -w "$dir" ] && echo "" || echo "$SUDO"
    fi
}

# Track what we've done for cleanup
CLEANUP_TMP=false
CLEANUP_SERVICE=false
BACKUP_CONFIG=""
ORIGINAL_INSTALL_EXISTS=false

# Colors (if terminal supports it)
setup_colors() {
    if [ -t 1 ]; then
        RED='\033[0;31m'
        GREEN='\033[0;32m'
        YELLOW='\033[1;33m'
        CYAN='\033[0;36m'
        BOLD='\033[1m'
        NC='\033[0m'
    else
        RED=''
        GREEN=''
        YELLOW=''
        CYAN=''
        BOLD=''
        NC=''
    fi
}

# Initialize colors immediately
setup_colors

# Logging functions (printf %b interprets \033 escapes; BusyBox echo does not)
log_info() { printf '%b\n' "${CYAN}[INFO]${NC} $1" >&2; }
log_success() { printf '%b\n' "${GREEN}[OK]${NC} $1" >&2; }
log_warn() { printf '%b\n' "${YELLOW}[WARN]${NC} $1" >&2; }
log_error() { printf '%b\n' "${RED}[ERROR]${NC} $1" >&2; }

# Error handler - cleanup and report what went wrong
# Usage: trap 'error_handler $LINENO' ERR
error_handler() {
    local exit_code=$?
    local line_no=$1

    echo ""
    log_error "=========================================="
    log_error "Installation FAILED at line $line_no"
    log_error "Exit code: $exit_code"
    log_error "=========================================="
    echo ""

    # Cleanup temporary files
    if [ "$CLEANUP_TMP" = true ] && [ -d "$TMP_DIR" ]; then
        log_info "Cleaning up temporary files..."
        rm -rf "$TMP_DIR"
    fi

    # If we backed up config and install failed, try to restore state
    if [ -n "$BACKUP_CONFIG" ] && [ -f "$BACKUP_CONFIG" ]; then
        log_info "Restoring backed up configuration..."
        if $SUDO mkdir -p "${INSTALL_DIR}/config" 2>/dev/null; then
            if $SUDO cp "$BACKUP_CONFIG" "${INSTALL_DIR}/config/helixconfig.json" 2>/dev/null; then
                log_success "Configuration restored"
            else
                log_warn "Could not restore config. Backup saved at: $BACKUP_CONFIG"
            fi
        else
            log_warn "Could not create config directory. Backup saved at: $BACKUP_CONFIG"
        fi
    fi

    echo ""
    log_error "Installation was NOT completed."
    log_error "Your system should be in its original state."
    echo ""
    log_info "For help, please:"
    log_info "  1. Check the error message above"
    log_info "  2. Verify network connectivity"
    log_info "  3. Report issues at: https://github.com/${GITHUB_REPO}/issues"
    echo ""

    exit $exit_code
}

# Cleanup function for normal exit
cleanup_on_success() {
    if [ -d "$TMP_DIR" ]; then
        rm -rf "$TMP_DIR"
    fi
}

# Kill process(es) by name, using killall or pidof fallback
# Works on both GNU systems and BusyBox (AD5M/K1)
# Args: process_name [process_name2 ...]
# Returns: 0 if any process was killed, 1 if none found
kill_process_by_name() {
    local killed_any=false

    for proc in "$@"; do
        if command -v killall >/dev/null 2>&1; then
            if killall -0 "$proc" 2>/dev/null; then
                $SUDO killall "$proc" 2>/dev/null || true
                killed_any=true
            fi
        elif command -v pidof >/dev/null 2>&1; then
            local pids
            pids=$(pidof "$proc" 2>/dev/null)
            if [ -n "$pids" ]; then
                for pid in $pids; do
                    $SUDO kill "$pid" 2>/dev/null || true
                done
                killed_any=true
            fi
        fi
    done

    [ "$killed_any" = true ]
}

# Print post-install commands for the user
# Reads: INIT_SYSTEM, SERVICE_NAME, INIT_SCRIPT_DEST
print_post_install_commands() {
    echo "Useful commands:"
    if [ "$INIT_SYSTEM" = "systemd" ]; then
        echo "  systemctl status ${SERVICE_NAME}    # Check status"
        echo "  journalctl -u ${SERVICE_NAME} -f    # View logs"
        echo "  systemctl restart ${SERVICE_NAME}   # Restart"
    else
        echo "  ${INIT_SCRIPT_DEST} status   # Check status"
        echo "  cat /tmp/helixscreen.log            # View logs"
        echo "  ${INIT_SCRIPT_DEST} restart  # Restart"
    fi
}
