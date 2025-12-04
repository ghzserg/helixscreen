#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# helix-launcher.sh - Launch HelixScreen with early splash screen
#
# This script starts the splash screen immediately for instant visual feedback,
# then launches the main application in parallel. The splash automatically exits
# when the main app takes over the display.
#
# Usage:
#   ./helix-launcher.sh [options]
#
# Launcher-specific options:
#   --debug              Enable trace-level logging (-vvv)
#   --log-dest=<dest>    Log destination: auto, journal, syslog, file, console
#   --log-file=<path>    Log file path (when --log-dest=file)
#
# Environment variables:
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

# Debug/verbose mode - pass -vvv to helix-screen for trace-level logging
DEBUG_MODE="${HELIX_DEBUG:-0}"
LOG_DEST="${HELIX_LOG_DEST:-auto}"
LOG_FILE="${HELIX_LOG_FILE:-}"

# Parse launcher-specific arguments
PASSTHROUGH_ARGS=()
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
            PASSTHROUGH_ARGS+=("$arg")
            ;;
    esac
done

# Determine script and binary locations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Support installed, deployed, and development layouts
if [ -x "${SCRIPT_DIR}/helix-splash" ]; then
    # Installed: binaries in same directory as script
    BIN_DIR="${SCRIPT_DIR}"
elif [ -x "${SCRIPT_DIR}/../helix-splash" ]; then
    # Deployed: binaries in parent directory (rsync deployment layout)
    BIN_DIR="${SCRIPT_DIR}/.."
elif [ -x "${SCRIPT_DIR}/../build/bin/helix-splash" ]; then
    # Development: binaries in build/bin relative to config/
    BIN_DIR="${SCRIPT_DIR}/../build/bin"
else
    echo "Error: Cannot find helix-splash binary" >&2
    echo "Looked in: ${SCRIPT_DIR}, ${SCRIPT_DIR}/.., and ${SCRIPT_DIR}/../build/bin" >&2
    exit 1
fi

SPLASH_BIN="${BIN_DIR}/helix-splash"
MAIN_BIN="${BIN_DIR}/helix-screen"

# Verify main binary exists
if [ ! -x "${MAIN_BIN}" ]; then
    echo "Error: Cannot find helix-screen binary at ${MAIN_BIN}" >&2
    exit 1
fi

# Default screen dimensions (can be overridden by environment)
: "${HELIX_SCREEN_WIDTH:=800}"
: "${HELIX_SCREEN_HEIGHT:=480}"

# Log function
log() {
    echo "[helix-launcher] $*"
}

# Cleanup function for signal handling
cleanup() {
    log "Shutting down..."
    if [ -n "${SPLASH_PID}" ] && kill -0 "${SPLASH_PID}" 2>/dev/null; then
        kill "${SPLASH_PID}" 2>/dev/null || true
        wait "${SPLASH_PID}" 2>/dev/null || true
    fi
}

trap cleanup EXIT INT TERM

# Start splash screen in background (if binary exists)
if [ -x "${SPLASH_BIN}" ]; then
    log "Starting splash screen (${HELIX_SCREEN_WIDTH}x${HELIX_SCREEN_HEIGHT})"
    "${SPLASH_BIN}" -w "${HELIX_SCREEN_WIDTH}" -h "${HELIX_SCREEN_HEIGHT}" &
    SPLASH_PID=$!
    log "Splash PID: ${SPLASH_PID}"

    # Pass splash PID to main app so it can signal when ready
    SPLASH_ARGS="--splash-pid=${SPLASH_PID}"
else
    log "Splash binary not found, starting main app directly"
    SPLASH_PID=""
    SPLASH_ARGS=""
fi

# Start main application
# Main app will send SIGUSR1 to splash when it takes over the display
log "Starting main application"

# Build command flags
EXTRA_FLAGS=""

# Debug mode: trace-level logging
if [ "${DEBUG_MODE}" = "1" ]; then
    EXTRA_FLAGS="-vvv"
    log "Debug mode enabled (trace-level logging)"
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

# Run main application
"${MAIN_BIN}" ${SPLASH_ARGS} ${EXTRA_FLAGS} "${PASSTHROUGH_ARGS[@]}"
EXIT_CODE=$?

# Ensure splash is terminated (should have exited when main app took display)
if [ -n "${SPLASH_PID}" ] && kill -0 "${SPLASH_PID}" 2>/dev/null; then
    log "Cleaning up splash process"
    kill "${SPLASH_PID}" 2>/dev/null || true
    wait "${SPLASH_PID}" 2>/dev/null || true
fi

log "Exiting with code ${EXIT_CODE}"
exit ${EXIT_CODE}
