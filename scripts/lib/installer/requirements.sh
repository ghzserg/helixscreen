#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: requirements
# Pre-flight checks: commands, dependencies, disk space, init system
#
# Reads: PLATFORM, SUDO
# Writes: INIT_SYSTEM

# Source guard
[ -n "${_HELIX_REQUIREMENTS_SOURCED:-}" ] && return 0
_HELIX_REQUIREMENTS_SOURCED=1

# Initialize INIT_SYSTEM (will be set by detect_init_system)
INIT_SYSTEM=""

# Check required commands exist
# Requires: curl or wget, tar, gunzip
check_requirements() {
    local missing=""

    # Need either curl or wget
    if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
        missing="curl or wget"
    fi

    # Need tar
    if ! command -v tar >/dev/null 2>&1; then
        if [ -n "$missing" ]; then
            missing="$missing, tar"
        else
            missing="tar"
        fi
    fi

    # Need gunzip (for AD5M BusyBox tar which doesn't support -z)
    if ! command -v gunzip >/dev/null 2>&1; then
        if [ -n "$missing" ]; then
            missing="$missing, gunzip"
        else
            missing="gunzip"
        fi
    fi

    if [ -n "$missing" ]; then
        log_error "Missing required commands: $missing"
        log_error "Please install them and try again."
        exit 1
    fi

    log_success "All required commands available"
}

# Install runtime dependencies for Pi platform
# Required for DRM display and evdev input handling
# AD5M uses framebuffer with static linking, no deps needed
install_runtime_deps() {
    local platform=$1

    # Only needed for Pi (32-bit and 64-bit) - AD5M uses framebuffer with static linking
    if [ "$platform" != "pi" ] && [ "$platform" != "pi32" ]; then
        return 0
    fi

    log_info "Checking runtime dependencies for display/input..."

    # Required libraries for DRM display and libinput
    # Note: GPU libs (libgles2, libegl1, libgbm1) not needed - using software rendering
    # Note: OpenSSL is statically linked for Pi builds, no runtime libssl needed
    local deps="libdrm2 libinput10"
    local missing=""

    for dep in $deps; do
        # Check if package is installed (dpkg-query returns 0 if installed)
        if ! dpkg-query -W -f='${Status}' "$dep" 2>/dev/null | grep -q "install ok installed"; then
            if [ -n "$missing" ]; then
                missing="$missing $dep"
            else
                missing="$dep"
            fi
        fi
    done

    if [ -n "$missing" ]; then
        log_info "Installing missing libraries: $missing"
        $SUDO apt-get update -qq
        # shellcheck disable=SC2086
        $SUDO apt-get install -y --no-install-recommends $missing
        log_success "Runtime libraries installed"
    else
        log_success "All runtime libraries already installed"
    fi
}

# Check available disk space
# Requires at least 50MB free on the install directory's filesystem
# Note: INSTALL_DIR must be set before calling this function
check_disk_space() {
    local platform=$1
    local required_mb=50

    # Get the parent directory of install location (the filesystem to check)
    local check_dir
    check_dir=$(dirname "${INSTALL_DIR:-/opt/helixscreen}")
    # Walk up until we find an existing directory
    while [ ! -d "$check_dir" ] && [ "$check_dir" != "/" ]; do
        check_dir=$(dirname "$check_dir")
    done
    if [ "$check_dir" = "/" ]; then
        check_dir="/"
    fi

    # Get available space in MB
    local available_mb
    if [ "$platform" = "ad5m" ] || [ "$platform" = "k1" ]; then
        # BusyBox df output format: blocks are in KB by default
        available_mb=$(df "$check_dir" 2>/dev/null | tail -1 | awk '{print int($4/1024)}')
    else
        # GNU df with -m flag outputs in MB
        available_mb=$(df -m "$check_dir" 2>/dev/null | tail -1 | awk '{print $4}')
    fi

    if [ -n "$available_mb" ] && [ "$available_mb" -lt "$required_mb" ]; then
        log_error "Insufficient disk space on $check_dir"
        log_error "Required: ${required_mb}MB, Available: ${available_mb}MB"
        exit 1
    fi

    log_info "Disk space check: ${available_mb}MB available on $check_dir"
}

# Detect init system (systemd vs SysV)
# Sets: INIT_SYSTEM to "systemd" or "sysv"
detect_init_system() {
    # Check for systemd
    if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
        INIT_SYSTEM="systemd"
        log_info "Init system: systemd"
        return
    fi

    # Check for SysV init (BusyBox or traditional)
    if [ -d /etc/init.d ]; then
        INIT_SYSTEM="sysv"
        log_info "Init system: SysV (BusyBox/traditional)"
        return
    fi

    log_error "Could not detect init system."
    log_error "Neither systemd nor /etc/init.d found."
    exit 1
}

# Check that Klipper and Moonraker are running (AD5M + K1 only)
# These platforms require local Klipper/Moonraker; without them HelixScreen
# has nothing to connect to. Warns and prompts for confirmation.
check_klipper_ecosystem() {
    local platform=$1

    # Only relevant for embedded platforms with local Klipper
    case "$platform" in
        ad5m|k1) ;;
        *) return 0 ;;
    esac

    local warnings=""

    # Check Klipper process
    # Klipper runs as "klippy" (python module) or "klipper" (service name)
    if ! ps | grep -v grep | grep -q -e '[Kk]lipper' -e '[Kk]lippy'; then
        warnings="Klipper does not appear to be running."
    fi

    # Check Moonraker process
    if ! ps | grep -v grep | grep -q '[Mm]oonraker'; then
        if [ -n "$warnings" ]; then
            warnings="$warnings
Moonraker does not appear to be running."
        else
            warnings="Moonraker does not appear to be running."
        fi
    fi

    # If Moonraker process is up, verify it responds on the expected port
    if ps | grep -v grep | grep -q '[Mm]oonraker'; then
        if command -v wget >/dev/null 2>&1; then
            if ! wget -q -O /dev/null --timeout=5 "http://127.0.0.1:7125/server/info" 2>/dev/null; then
                if [ -n "$warnings" ]; then
                    warnings="$warnings
Moonraker is running but not responding on http://127.0.0.1:7125."
                else
                    warnings="Moonraker is running but not responding on http://127.0.0.1:7125."
                fi
            fi
        elif command -v curl >/dev/null 2>&1; then
            if ! curl -sf --connect-timeout 5 "http://127.0.0.1:7125/server/info" >/dev/null 2>&1; then
                if [ -n "$warnings" ]; then
                    warnings="$warnings
Moonraker is running but not responding on http://127.0.0.1:7125."
                else
                    warnings="Moonraker is running but not responding on http://127.0.0.1:7125."
                fi
            fi
        fi
    fi

    # Everything looks good
    if [ -z "$warnings" ]; then
        log_success "Klipper and Moonraker are running"
        return 0
    fi

    # Show warnings and prompt
    log_warn ""
    log_warn "WARNING: Klipper ecosystem check failed:"
    # Print each warning line separately
    echo "$warnings" | while IFS= read -r line; do
        log_warn "  - $line"
    done
    log_warn ""
    log_warn "HelixScreen requires Klipper and Moonraker to function."
    log_warn "It will install but won't work until these services are available."

    # Non-interactive mode: just warn and continue
    if [ ! -t 0 ]; then
        log_warn "Non-interactive mode: continuing anyway."
        return 0
    fi

    printf "Continue anyway? [y/N] "
    read -r answer
    case "$answer" in
        [Yy]|[Yy][Ee][Ss])
            log_info "Continuing installation..."
            return 0
            ;;
        *)
            log_error "Installation cancelled."
            exit 1
            ;;
    esac
}

# Verify the installed binary can find all shared libraries
# Runs ldd on the binary and checks for "not found" entries.
# If libssl.so.1.1 is missing (Bullseye→Bookworm upgrade), tries to install compat package.
# Called after extraction, before starting the service.
# Requires: INSTALL_DIR
verify_binary_deps() {
    local platform=$1
    local binary="${INSTALL_DIR}/bin/helix-screen"

    # Only relevant for platforms with dynamic linking and ldd
    if ! command -v ldd >/dev/null 2>&1; then
        return 0
    fi

    # Binary must exist
    if [ ! -f "$binary" ]; then
        log_warn "Binary not found at $binary, skipping dependency check"
        return 0
    fi

    # Check for missing shared libraries
    local missing_libs
    missing_libs=$(ldd "$binary" 2>/dev/null | grep "not found" || true)

    if [ -z "$missing_libs" ]; then
        log_success "All shared library dependencies satisfied"
        return 0
    fi

    log_warn "Missing shared libraries detected:"
    echo "$missing_libs" | while IFS= read -r line; do
        log_warn "  $line"
    done

    # Try to fix known issues on Pi
    case "$platform" in
        pi|pi32)
            # libssl.so.1.1 missing = Bookworm system with Bullseye-era binary
            if echo "$missing_libs" | grep -q "libssl.so.1.1"; then
                log_info "libssl.so.1.1 not found (common after Debian Bullseye→Bookworm upgrade)"
                # Try installing the compat package if available
                if apt-cache show libssl1.1 >/dev/null 2>&1; then
                    log_info "Installing libssl1.1 compatibility package..."
                    $SUDO apt-get install -y --no-install-recommends libssl1.1
                else
                    log_error "libssl1.1 package not available in your repositories."
                    log_error "This binary was built against OpenSSL 1.1 but your system has OpenSSL 3."
                    log_error "Please update HelixScreen to the latest version which includes OpenSSL statically."
                    exit 1
                fi
            fi

            # Re-check after attempted fixes
            missing_libs=$(ldd "$binary" 2>/dev/null | grep "not found" || true)
            if [ -n "$missing_libs" ]; then
                log_error "Could not resolve all missing libraries:"
                echo "$missing_libs" | while IFS= read -r line; do
                    log_error "  $line"
                done
                log_error "The binary may not start correctly."
                log_error "Please report this issue at https://github.com/prestonbrown/helixscreen/issues"
                exit 1
            fi
            log_success "All shared library dependencies resolved"
            ;;
        *)
            # Non-Pi platforms: just warn, don't block
            log_warn "Some libraries are missing. The binary may not start correctly."
            ;;
    esac
}
