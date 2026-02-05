#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# helix-launcher.sh - Launch HelixScreen with watchdog supervision
#
# When watchdog is available (embedded targets), it manages the splash screen
# lifecycle and provides crash recovery. Otherwise, launches helix-screen directly.
#
# NOTE: Written for POSIX sh compatibility (no bash arrays) to work on AD5M BusyBox.
#
# Usage:
#   ./helix-launcher.sh [options]
#
# Launcher-specific options:
#   --debug              Enable debug-level logging (-vv)
#   --log-dest=<dest>    Log destination: auto, journal, syslog, file, console
#   --log-file=<path>    Log file path (when --log-dest=file)
#
# Environment variables:
#   HELIX_DATA_DIR=<d>   Override asset directory (ui_xml/, assets/, config/)
#   HELIX_DEBUG=1        Same as --debug
#   HELIX_LOG_DEST=<d>   Same as --log-dest (auto|journal|syslog|file|console)
#   HELIX_LOG_FILE=<f>   Same as --log-file
#
# All other options are passed through to helix-screen.
#
# Logging behavior:
#   - When run via systemd: auto-detects journal (recommended)
#   - When run interactively: auto-detects console
#   - Use --log-dest=file --log-file=/path for explicit file logging
#
# Installation:
#   Copy to /opt/helixscreen/bin/ or similar
#   Make executable: chmod +x helix-launcher.sh
#   Use with systemd service: config/helixscreen.service

set -e

# Debug/verbose mode - pass -vv to helix-screen for debug-level logging
DEBUG_MODE="${HELIX_DEBUG:-0}"
LOG_DEST="${HELIX_LOG_DEST:-auto}"
LOG_FILE="${HELIX_LOG_FILE:-}"

# Parse launcher-specific arguments (POSIX-compatible, no arrays)
# Passthrough args stored as space-separated string
PASSTHROUGH_ARGS=""
for arg in "$@"; do
    case "$arg" in
        --debug)
            DEBUG_MODE=1
            ;;
        --log-dest=*)
            LOG_DEST="${arg#--log-dest=}"
            ;;
        --log-file=*)
            LOG_FILE="${arg#--log-file=}"
            ;;
        *)
            PASSTHROUGH_ARGS="${PASSTHROUGH_ARGS} ${arg}"
            ;;
    esac
done

# Determine script and binary locations
# Use $0 instead of BASH_SOURCE for POSIX compatibility
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Support installed and development layouts
# Installed: launcher is in bin/ alongside binaries
# Development: launcher is in scripts/, binaries in build/bin/
if [ -x "${SCRIPT_DIR}/helix-screen" ]; then
    # Installed: binaries in same directory as launcher (bin/)
    BIN_DIR="${SCRIPT_DIR}"
elif [ -x "${SCRIPT_DIR}/../build/bin/helix-screen" ]; then
    # Development: launcher in scripts/, binaries in build/bin/
    BIN_DIR="${SCRIPT_DIR}/../build/bin"
else
    echo "Error: Cannot find helix-screen binary" >&2
    echo "Looked in: ${SCRIPT_DIR} and ${SCRIPT_DIR}/../build/bin" >&2
    exit 1
fi

SPLASH_BIN="${BIN_DIR}/helix-splash"
MAIN_BIN="${BIN_DIR}/helix-screen"
WATCHDOG_BIN="${BIN_DIR}/helix-watchdog"

# Default screen dimensions (can be overridden by environment)
: "${HELIX_SCREEN_WIDTH:=800}"
: "${HELIX_SCREEN_HEIGHT:=480}"

# Log function (must be defined before first use)
# Uses stderr to avoid polluting stdout which could be captured unexpectedly
log() {
    echo "[helix-launcher] $*" >&2
}

# Verify main binary exists
if [ ! -x "${MAIN_BIN}" ]; then
    echo "Error: Cannot find helix-screen binary at ${MAIN_BIN}" >&2
    exit 1
fi

# Check if watchdog is available (embedded targets only, provides crash recovery)
USE_WATCHDOG=0
if [ -x "${WATCHDOG_BIN}" ]; then
    USE_WATCHDOG=1
    log "Watchdog available: crash recovery enabled"
fi

# Check if splash is already running (started by init script for earlier visibility)
# If so, pass the PID to helix-screen for cleanup, and don't start another
# HELIX_NO_SPLASH=1 disables splash entirely (for debugging)
SPLASH_ARGS=""
if [ "${HELIX_NO_SPLASH:-0}" = "1" ]; then
    log "Splash disabled (HELIX_NO_SPLASH=1)"
elif [ -n "${HELIX_SPLASH_PID}" ]; then
    # Splash was pre-started by init script, pass PID to helix-screen for cleanup
    PASSTHROUGH_ARGS="${PASSTHROUGH_ARGS} --splash-pid=${HELIX_SPLASH_PID}"
    log "Using pre-started splash (PID ${HELIX_SPLASH_PID})"
elif [ -x "${SPLASH_BIN}" ]; then
    # No pre-started splash, let watchdog manage it
    SPLASH_ARGS="--splash-bin=${SPLASH_BIN}"
    log "Splash binary: ${SPLASH_BIN}"
fi

# Cleanup function for signal handling
cleanup() {
    log "Shutting down..."
    # Kill watchdog/helix-screen if we started them
    killall helix-watchdog helix-screen helix-splash 2>/dev/null || true
}

trap cleanup EXIT INT TERM

log "Starting main application"

# Build command flags
EXTRA_FLAGS=""

# Debug mode: debug-level logging
if [ "${DEBUG_MODE}" = "1" ]; then
    EXTRA_FLAGS="-vv"
    log "Debug mode enabled (debug-level logging)"
fi

# Logging destination
if [ "${LOG_DEST}" != "auto" ]; then
    EXTRA_FLAGS="${EXTRA_FLAGS} --log-dest=${LOG_DEST}"
    log "Log destination: ${LOG_DEST}"
fi

# Explicit log file path (only meaningful with --log-dest=file)
if [ -n "${LOG_FILE}" ]; then
    EXTRA_FLAGS="${EXTRA_FLAGS} --log-file=${LOG_FILE}"
    log "Log file: ${LOG_FILE}"
fi

# Run main application (via watchdog if available for crash recovery)
# Note: PASSTHROUGH_ARGS is unquoted to allow word splitting (POSIX compatible)
if [ "${USE_WATCHDOG}" = "1" ]; then
    # Watchdog supervises helix-screen and manages splash lifecycle
    # Pass screen dimensions and splash binary to watchdog, then use -- to separate helix-screen args
    log "Starting via watchdog supervisor"
    # shellcheck disable=SC2086
    "${WATCHDOG_BIN}" -w "${HELIX_SCREEN_WIDTH}" -h "${HELIX_SCREEN_HEIGHT}" \
        ${SPLASH_ARGS} -- \
        "${MAIN_BIN}" ${EXTRA_FLAGS} ${PASSTHROUGH_ARGS}
    EXIT_CODE=$?
else
    # Direct launch (development, or watchdog not built)
    # shellcheck disable=SC2086
    "${MAIN_BIN}" ${EXTRA_FLAGS} ${PASSTHROUGH_ARGS}
    EXIT_CODE=$?
fi

log "Exiting with code ${EXIT_CODE}"
exit ${EXIT_CODE}
