#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Module: release
# Release download and extraction
#
# Reads: GITHUB_REPO, TMP_DIR, INSTALL_DIR, SUDO
# Writes: CLEANUP_TMP, BACKUP_CONFIG, ORIGINAL_INSTALL_EXISTS

# Source guard
[ -n "${_HELIX_RELEASE_SOURCED:-}" ] && return 0
_HELIX_RELEASE_SOURCED=1

# Validate a tarball is a valid gzip archive and not truncated
# Args: tarball_path, context (e.g., "Downloaded" or "Local")
# Exits on failure
validate_tarball() {
    local tarball=$1
    local context=${2:-""}

    # Verify it's a valid gzip file
    if ! gunzip -t "$tarball" 2>/dev/null; then
        log_error "${context}file is not a valid gzip archive."
        [ -n "$context" ] && log_error "The ${context}may have been corrupted or incomplete."
        exit 1
    fi

    # Verify file isn't truncated (releases should be >1MB)
    local size_kb
    size_kb=$(du -k "$tarball" 2>/dev/null | cut -f1)
    if [ "${size_kb:-0}" -lt 1024 ]; then
        log_error "${context}file too small (${size_kb}KB). File may be incomplete."
        exit 1
    fi
}

# Check if we can download from HTTPS URLs
# BusyBox wget on AD5M doesn't support HTTPS
check_https_capability() {
    # curl with SSL support works
    if command -v curl >/dev/null 2>&1; then
        # Test if curl can reach HTTPS (quick timeout)
        if curl -sSL --connect-timeout 5 -o /dev/null "https://github.com" 2>/dev/null; then
            return 0
        fi
    fi

    # Check if wget supports HTTPS
    if command -v wget >/dev/null 2>&1; then
        # BusyBox wget outputs "not an http or ftp url" for https
        if wget --help 2>&1 | grep -qi "https"; then
            return 0
        fi
        # Try a test fetch - BusyBox wget fails immediately on https URLs
        if wget -q --timeout=5 -O /dev/null "https://github.com" 2>/dev/null; then
            return 0
        fi
    fi

    return 1
}

# Show manual install instructions when HTTPS download isn't available
show_manual_install_instructions() {
    local platform=$1
    local version=${2:-latest}

    echo ""
    log_error "=========================================="
    log_error "  HTTPS Download Not Available"
    log_error "=========================================="
    echo ""
    log_error "This system cannot download from HTTPS URLs."
    log_error "BusyBox wget (common on embedded devices) doesn't support HTTPS."
    echo ""
    log_info "To install HelixScreen, download the release on another computer"
    log_info "and copy it to this device:"
    echo ""
    echo "  1. Download the release:"
    if [ "$version" = "latest" ]; then
        echo "     ${CYAN}https://github.com/${GITHUB_REPO}/releases/latest${NC}"
    else
        echo "     ${CYAN}https://github.com/${GITHUB_REPO}/releases/tag/${version}${NC}"
    fi
    echo ""
    echo "  2. Download: ${BOLD}helixscreen-${platform}-${version}.tar.gz${NC}"
    echo ""
    echo "  3. Copy to this device (note: AD5M needs -O flag):"
    if [ "$platform" = "ad5m" ]; then
        # AD5M /tmp is a tiny tmpfs (~54MB), use /data/ instead
        echo "     ${CYAN}scp -O helixscreen-${platform}.tar.gz root@<this-ip>:/data/${NC}"
        echo ""
        echo "  4. Run the installer with the local file:"
        echo "     ${CYAN}sh /data/install.sh --local /data/helixscreen-${platform}.tar.gz${NC}"
    else
        echo "     ${CYAN}scp helixscreen-${platform}.tar.gz root@<this-ip>:/tmp/${NC}"
        echo ""
        echo "  4. Run the installer with the local file:"
        echo "     ${CYAN}sh /tmp/install.sh --local /tmp/helixscreen-${platform}.tar.gz${NC}"
    fi
    echo ""
    exit 1
}

# Get latest release version from GitHub
# Returns the tag name as-is (e.g., "v0.9.3")
# Args: platform (for error message if HTTPS unavailable)
get_latest_version() {
    local platform=${1:-unknown}
    local url="https://api.github.com/repos/${GITHUB_REPO}/releases/latest"
    local version=""

    # Check HTTPS capability first
    if ! check_https_capability; then
        show_manual_install_instructions "$platform" "latest"
    fi

    log_info "Fetching latest version from GitHub..."

    if command -v curl >/dev/null 2>&1; then
        # Use basic sed regex (no -E flag) for BusyBox compatibility
        version=$(curl -sSL --connect-timeout 10 "$url" 2>/dev/null | grep '"tag_name"' | sed 's/.*"\([^"][^"]*\)".*/\1/')
    elif command -v wget >/dev/null 2>&1; then
        # Use basic sed regex (no -E flag) for BusyBox compatibility
        version=$(wget -qO- --timeout=10 "$url" 2>/dev/null | grep '"tag_name"' | sed 's/.*"\([^"][^"]*\)".*/\1/')
    fi

    if [ -z "$version" ]; then
        log_error "Failed to fetch latest version from GitHub."
        log_error "Check your network connection and try again."
        log_error "URL: $url"
        exit 1
    fi

    echo "$version"
}

# Download release tarball
download_release() {
    local version=$1
    local platform=$2

    local filename="helixscreen-${platform}-${version}.tar.gz"
    local url="https://github.com/${GITHUB_REPO}/releases/download/${version}/${filename}"
    local dest="${TMP_DIR}/helixscreen.tar.gz"

    log_info "Downloading HelixScreen ${version} for ${platform}..."
    log_info "URL: $url"

    mkdir -p "$TMP_DIR"
    CLEANUP_TMP=true

    local http_code=""
    if command -v curl >/dev/null 2>&1; then
        http_code=$(curl -sSL --connect-timeout 30 -w "%{http_code}" -o "$dest" "$url")
    elif command -v wget >/dev/null 2>&1; then
        if wget -q --timeout=30 -O "$dest" "$url"; then
            http_code="200"
        else
            http_code="failed"
        fi
    fi

    if [ ! -f "$dest" ] || [ ! -s "$dest" ]; then
        log_error "Failed to download release."
        log_error "URL: $url"
        if [ -n "$http_code" ] && [ "$http_code" != "200" ]; then
            log_error "HTTP status: $http_code"
        fi
        log_error ""
        log_error "Possible causes:"
        log_error "  - Version ${version} may not exist for platform ${platform}"
        log_error "  - Network connectivity issues"
        log_error "  - GitHub may be unavailable"
        exit 1
    fi

    validate_tarball "$dest" "Downloaded "

    local size
    size=$(ls -lh "$dest" | awk '{print $5}')
    log_success "Downloaded ${filename} (${size})"
}

# Use a local tarball instead of downloading
use_local_tarball() {
    local src=$1

    log_info "Using local tarball: $src"

    validate_tarball "$src" "Local "

    # Point TMP_DIR tarball location to the source file directly
    # This avoids copying large files on space-constrained systems
    mkdir -p "$TMP_DIR"
    CLEANUP_TMP=true

    # Create symlink or use directly based on what the extraction expects
    # The extract_release function looks for ${TMP_DIR}/helixscreen.tar.gz
    local dest="${TMP_DIR}/helixscreen.tar.gz"
    if [ "$src" != "$dest" ]; then
        # Use symlink if possible, otherwise copy
        ln -sf "$src" "$dest" 2>/dev/null || cp "$src" "$dest"
    fi

    local size
    size=$(ls -lh "$src" | awk '{print $5}')
    log_success "Using local tarball (${size})"
}

# Extract tarball (handles BusyBox tar on AD5M)
extract_release() {
    local platform=$1
    local tarball="${TMP_DIR}/helixscreen.tar.gz"

    log_info "Extracting release to ${INSTALL_DIR}..."

    # Check if install dir already exists
    if [ -d "${INSTALL_DIR}" ]; then
        ORIGINAL_INSTALL_EXISTS=true

        # Backup existing config (check new location first, then legacy)
        if [ -f "${INSTALL_DIR}/config/helixconfig.json" ]; then
            BACKUP_CONFIG="${TMP_DIR}/helixconfig.json.backup"
            cp "${INSTALL_DIR}/config/helixconfig.json" "$BACKUP_CONFIG"
            log_info "Backed up existing configuration (from config/)"
        elif [ -f "${INSTALL_DIR}/helixconfig.json" ]; then
            BACKUP_CONFIG="${TMP_DIR}/helixconfig.json.backup"
            cp "${INSTALL_DIR}/helixconfig.json" "$BACKUP_CONFIG"
            log_info "Backed up existing configuration (legacy location)"
        fi
    fi

    # Remove old installation
    $SUDO rm -rf "${INSTALL_DIR}"

    # Create parent directory
    $SUDO mkdir -p "$(dirname "${INSTALL_DIR}")"

    # Extract - AD5M and K1 use BusyBox tar which doesn't support -z
    cd "$(dirname "${INSTALL_DIR}")" || exit 1
    if [ "$platform" = "ad5m" ] || [ "$platform" = "k1" ]; then
        if ! gunzip -c "$tarball" | $SUDO tar xf -; then
            log_error "Failed to extract tarball."
            log_error "The archive may be corrupted."
            exit 1
        fi
    else
        if ! $SUDO tar -xzf "$tarball"; then
            log_error "Failed to extract tarball."
            log_error "The archive may be corrupted."
            exit 1
        fi
    fi

    # Verify extraction succeeded
    if [ ! -f "${INSTALL_DIR}/bin/helix-screen" ]; then
        log_error "Extraction failed - helix-screen binary not found."
        log_error "Expected: ${INSTALL_DIR}/bin/helix-screen"
        exit 1
    fi

    # Restore config to new location (config/helixconfig.json)
    if [ -n "$BACKUP_CONFIG" ] && [ -f "$BACKUP_CONFIG" ]; then
        $SUDO mkdir -p "${INSTALL_DIR}/config"
        $SUDO cp "$BACKUP_CONFIG" "${INSTALL_DIR}/config/helixconfig.json"
        log_info "Restored existing configuration to config/"
    fi

    log_success "Extracted to ${INSTALL_DIR}"
}
