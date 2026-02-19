#!/bin/sh
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixScreen Installer (Development/Modular Version)
#
# This script requires the lib/installer/ modules and must be run from a repo checkout.
# For end-user installation via curl, use scripts/install.sh instead.
#
# Usage (from repo root):
#   ./scripts/install-dev.sh [options]
#
# Options:
#   --update    Update existing installation (preserves config)
#   --uninstall Remove HelixScreen
#   --clean     Remove old installation completely before installing (no config backup)
#   --version   Specify version (default: latest)
#

# Fail fast on any error
set -eu

# Configuration
GITHUB_REPO="prestonbrown/helixscreen"
SERVICE_NAME="helixscreen"

# Source modules (if running from repo, not bundled)
if [ -z "${_HELIX_BUNDLED_INSTALLER:-}" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    LIB_DIR="$SCRIPT_DIR/lib/installer"

    . "$LIB_DIR/common.sh"
    . "$LIB_DIR/platform.sh"
    . "$LIB_DIR/permissions.sh"
    . "$LIB_DIR/requirements.sh"
    . "$LIB_DIR/forgex.sh"
    . "$LIB_DIR/competing_uis.sh"
    . "$LIB_DIR/release.sh"
    . "$LIB_DIR/service.sh"
    . "$LIB_DIR/moonraker.sh"
    . "$LIB_DIR/uninstall.sh"  # Must be last - uses functions from other modules
fi

# Set up error trap
trap 'error_handler $LINENO' ERR

# Print usage
usage() {
    echo "HelixScreen Installer"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --update       Update existing installation (preserves config)"
    echo "  --uninstall    Remove HelixScreen"
    echo "  --clean        Clean install: remove old installation completely,"
    echo "                 including config and caches (asks for confirmation)"
    echo "  --version VER  Install specific version (default: latest)"
    echo "  --local FILE   Install from local tarball (skip download)"
    echo "  --help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Fresh install, latest version"
    echo "  $0 --update           # Update existing installation"
    echo "  $0 --clean            # Remove old install completely, then install"
    echo "  $0 --version v1.1.0   # Install specific version"
    echo "  $0 --local /tmp/helixscreen-ad5m.tar.gz  # Install from local file"
}

# Main installation flow
main() {
    update_mode=false
    uninstall_mode=false
    clean_mode=false
    version=""
    local_tarball=""

    # Parse arguments
    while [ $# -gt 0 ]; do
        case $1 in
            --update)
                update_mode=true
                shift
                ;;
            --uninstall)
                uninstall_mode=true
                shift
                ;;
            --clean)
                clean_mode=true
                shift
                ;;
            --version)
                if [ -z "${2:-}" ]; then
                    log_error "--version requires a version argument"
                    exit 1
                fi
                version="$2"
                shift 2
                ;;
            --local)
                if [ -z "${2:-}" ]; then
                    log_error "--local requires a file path argument"
                    exit 1
                fi
                local_tarball="$2"
                shift 2
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done

    printf '\n'
    printf '%b\n' "${BOLD}========================================${NC}"
    printf '%b\n' "${BOLD}       HelixScreen Installer${NC}"
    printf '%b\n' "${BOLD}========================================${NC}"
    printf '\n'

    # Detect platform
    platform=$(detect_platform)
    log_info "Detected platform: ${BOLD}${platform}${NC}"

    if [ "$platform" = "unsupported" ]; then
        log_error "Unsupported platform: $(uname -m)"
        log_error "HelixScreen supports:"
        log_error "  - Raspberry Pi (aarch64/armv7l)"
        log_error "  - FlashForge Adventurer 5M (armv7l)"
        log_error "  - Creality K1 series with Simple AF"
        exit 1
    fi

    # For AD5M, detect firmware variant and set appropriate paths
    local firmware=""
    if [ "$platform" = "ad5m" ]; then
        AD5M_FIRMWARE=$(detect_ad5m_firmware)
        firmware="$AD5M_FIRMWARE"
    elif [ "$platform" = "k1" ]; then
        K1_FIRMWARE=$(detect_k1_firmware)
        firmware="$K1_FIRMWARE"
    fi
    set_install_paths "$platform" "$firmware"

    # Check permissions
    check_permissions "$platform"

    # Handle uninstall (doesn't need all checks)
    if [ "$uninstall_mode" = true ]; then
        uninstall "$platform"
        exit 0
    fi

    # Pre-flight checks
    log_info "Running pre-flight checks..."
    check_requirements
    install_runtime_deps "$platform"
    check_disk_space "$platform"
    detect_init_system

    # Get version (skip if using local tarball)
    if [ -n "$local_tarball" ]; then
        # Validate local file exists
        if [ ! -f "$local_tarball" ]; then
            log_error "Local tarball not found: $local_tarball"
            exit 1
        fi
        # Extract version from filename if possible (helixscreen-platform-v1.2.3.tar.gz)
        version=$(echo "$local_tarball" | sed -n 's/.*helixscreen-[^-]*-\(v[0-9.]*\)\.tar\.gz/\1/p')
        if [ -z "$version" ]; then
            version="local"
        fi
        log_info "Installing from local file: ${BOLD}${local_tarball}${NC}"
    else
        if [ -z "$version" ]; then
            version=$(get_latest_version "$platform")
        fi
    fi
    log_info "Target version: ${BOLD}${version}${NC}"

    # For ForgeX firmware, disable GuppyScreen in variables.cfg before stopping UIs
    # This prevents ForgeX's start.sh from launching GuppyScreen on boot
    if [ "$AD5M_FIRMWARE" = "forge_x" ]; then
        configure_forgex_display || true
        # Also disable stock FlashForge UI in auto_run.sh (runs after init scripts)
        disable_stock_firmware_ui || true
        # Patch screen.sh to skip backlight control when HelixScreen is active
        patch_forgex_screen_sh || true
        # Patch screen.sh to skip drawing (draw_splash, draw_loading, boot_message)
        patch_forgex_screen_drawing || true
        # Wrap logged binary to strip --send-to-screen when HelixScreen is active
        install_forgex_logged_wrapper || true
    fi

    # Stop competing UIs (GuppyScreen, KlipperScreen, FeatherScreen, etc.)
    stop_competing_uis

    # Clean old installation if requested (removes everything including config)
    if [ "$clean_mode" = true ]; then
        clean_old_installation "$platform"
    fi

    # Stop existing service if updating
    if [ "$update_mode" = true ]; then
        if [ ! -d "$INSTALL_DIR" ]; then
            log_warn "No existing installation found. Performing fresh install."
        fi
        stop_service
    fi

    # Download and install (or use local tarball)
    if [ -n "$local_tarball" ]; then
        use_local_tarball "$local_tarball"
    else
        download_release "$version" "$platform"
    fi
    extract_release "$platform"
    install_service "$platform"

    # Install udev/polkit rules for non-root operation (backlight, Wi-Fi)
    install_permission_rules "$platform"

    # Configure Moonraker update_manager (Pi only - enables web UI updates)
    configure_moonraker_updates "$platform"

    # Start service
    start_service

    # Cleanup on success
    cleanup_on_success

    # Clear error trap - installation succeeded
    trap - ERR

    printf '\n'
    printf '%b\n' "${GREEN}${BOLD}========================================${NC}"
    printf '%b\n' "${GREEN}${BOLD}    Installation Complete!${NC}"
    printf '%b\n' "${GREEN}${BOLD}========================================${NC}"
    printf '\n'
    echo "HelixScreen ${version} installed to ${INSTALL_DIR}"
    echo ""
    print_post_install_commands
    echo ""

    if [ "$platform" = "ad5m" ] || [ "$platform" = "k1" ]; then
        echo "Note: You may need to reboot for the display to update."
    fi
}

# Run main
main "$@"
