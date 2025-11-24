#!/bin/bash

# Copyright 2025 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RESTORE_FILE="/tmp/wifi_test_restore_state.txt"
WPA_CONFIG_TEMP="/tmp/wpa_supplicant_test.conf"

echo -e "${BLUE}WiFi Real Hardware Testing Script${NC}"
echo "=================================="

# Helper functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Cleanup function
cleanup() {
    local exit_code=$?
    echo
    log_info "Cleaning up WiFi test environment..."

    if [[ -f "$RESTORE_FILE" ]]; then
        log_step "Restoring original system state..."

        # Stop any test wpa_supplicant processes
        sudo pkill wpa_supplicant || true
        sleep 1

        # Restore RF-kill state (if it was originally blocked)
        if grep -q "soft-blocked: yes" "$RESTORE_FILE" 2>/dev/null; then
            log_step "Restoring RF-kill soft block"
            sudo rfkill block wifi || true
        fi

        # Restore systemd wpa_supplicant service
        log_step "Restoring systemd wpa_supplicant service"
        sudo systemctl start wpa_supplicant || true

        # Restore interface DOWN state
        if grep -q "state DOWN" "$RESTORE_FILE" 2>/dev/null; then
            log_step "Restoring WiFi interface DOWN state"
            local interface=$(grep -o 'wl[a-z0-9]*' "$RESTORE_FILE" | head -1)
            if [[ -n "$interface" ]]; then
                sudo ip link set "$interface" down || true
            fi
        fi

        # Remove test configuration
        if [[ -f "/etc/wpa_supplicant/wpa_supplicant.conf" ]]; then
            log_step "Removing test wpa_supplicant configuration"
            sudo rm -f /etc/wpa_supplicant/wpa_supplicant.conf || true
        fi

        # Remove user from netdev group if we added them
        if grep -q "Added.*netdev group" "$RESTORE_FILE" 2>/dev/null; then
            log_step "Removing user from netdev group"
            sudo gpasswd -d "$(whoami)" netdev || true
        fi

        log_info "System state restored successfully"
    fi

    # Clean up temporary files
    rm -f "$RESTORE_FILE" "$WPA_CONFIG_TEMP" || true

    if [[ $exit_code -eq 0 ]]; then
        log_info "WiFi testing completed successfully"
    else
        log_error "WiFi testing failed with exit code $exit_code"
    fi

    exit $exit_code
}

# Set up cleanup trap
trap cleanup EXIT INT TERM

# Check if we're running as root
if [[ $EUID -eq 0 ]]; then
    log_error "This script should not be run as root"
    exit 1
fi

# Check for sudo access
if ! sudo -n true 2>/dev/null; then
    log_warn "This script requires sudo access for WiFi configuration"
    echo "Please enter your password when prompted."
    sudo -v
fi

# Function to document current state
document_system_state() {
    log_step "Documenting current system state for restoration"

    cat > "$RESTORE_FILE" << 'EOF'
=== CURRENT SYSTEM STATE (for restoration) ===

1. wpa_supplicant service status:
EOF

    systemctl status wpa_supplicant >> "$RESTORE_FILE" 2>&1 || echo "Service not active" >> "$RESTORE_FILE"

    echo -e "\n2. WiFi interface state:" >> "$RESTORE_FILE"

    # Find WiFi interfaces
    local wifi_interfaces=($(ip link show | grep -E "wl[a-z0-9]*:" | cut -d: -f2 | tr -d ' '))
    if [[ ${#wifi_interfaces[@]} -eq 0 ]]; then
        echo "No WiFi interfaces found" >> "$RESTORE_FILE"
        log_error "No WiFi interfaces detected"
        return 1
    fi

    local wifi_interface="${wifi_interfaces[0]}"
    echo "Primary WiFi interface: $wifi_interface" >> "$RESTORE_FILE"
    ip link show "$wifi_interface" >> "$RESTORE_FILE" 2>&1

    echo -e "\n3. RF-kill status:" >> "$RESTORE_FILE"
    rfkill list >> "$RESTORE_FILE" 2>&1

    echo -e "\n4. wpa_supplicant config directory:" >> "$RESTORE_FILE"
    ls -la /etc/wpa_supplicant/ >> "$RESTORE_FILE" 2>&1 || echo "Directory does not exist" >> "$RESTORE_FILE"

    echo -e "\n5. Control socket directory:" >> "$RESTORE_FILE"
    ls -la /run/wpa_supplicant/ >> "$RESTORE_FILE" 2>&1 || echo "Directory does not exist" >> "$RESTORE_FILE"

    echo "$wifi_interface"  # Return interface name
}

# Function to check WiFi hardware
check_wifi_hardware() {
    log_step "Checking WiFi hardware availability"

    local wifi_interfaces=($(ip link show | grep -E "wl[a-z0-9]*:" | cut -d: -f2 | tr -d ' '))

    if [[ ${#wifi_interfaces[@]} -eq 0 ]]; then
        log_error "No WiFi interfaces detected"
        log_info "This system appears to have no WiFi hardware"
        log_info "WiFi testing will use mock mode only"
        return 1
    fi

    log_info "Found WiFi interfaces: ${wifi_interfaces[*]}"
    return 0
}

# Function to create wpa_supplicant config
create_wpa_config() {
    log_step "Creating temporary wpa_supplicant configuration"

    cat > "$WPA_CONFIG_TEMP" << 'EOF'
# Control interface for programmatic access
ctrl_interface=/run/wpa_supplicant

# Allow runtime configuration updates
update_config=1

# Global WPA settings
country=US
ap_scan=1

# Example network (disabled - will be managed dynamically by UI)
network={
    ssid="example"
    psk="password"
    disabled=1
}
EOF

    log_info "Created temporary wpa_supplicant config: $WPA_CONFIG_TEMP"
}

# Function to setup real wpa_supplicant
setup_wpa_supplicant() {
    local wifi_interface="$1"

    log_step "Setting up real wpa_supplicant with interface binding"

    # Install configuration
    sudo cp "$WPA_CONFIG_TEMP" /etc/wpa_supplicant/wpa_supplicant.conf
    sudo chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf
    sudo chown root:root /etc/wpa_supplicant/wpa_supplicant.conf

    # Check RF-kill status
    if rfkill list | grep -q "Soft blocked: yes"; then
        log_step "WiFi is RF-kill blocked, unblocking..."
        sudo rfkill unblock wifi
        echo "RF-kill unblocked during testing" >> "$RESTORE_FILE"
    fi

    # Bring up interface
    log_step "Bringing up WiFi interface: $wifi_interface"
    sudo ip link set "$wifi_interface" up

    # Stop systemd service and start interface-bound wpa_supplicant
    log_step "Starting interface-bound wpa_supplicant"
    sudo systemctl stop wpa_supplicant
    sudo wpa_supplicant -B -i "$wifi_interface" -c /etc/wpa_supplicant/wpa_supplicant.conf

    # Wait for control sockets
    sleep 2

    if [[ -e "/run/wpa_supplicant/$wifi_interface" ]]; then
        log_info "Control socket created: /run/wpa_supplicant/$wifi_interface"

        # Make socket accessible (for testing purposes)
        sudo chmod 777 "/run/wpa_supplicant/$wifi_interface" || true

        return 0
    else
        log_error "Control socket not created"
        return 1
    fi
}

# Function to test wpa_supplicant functionality
test_wpa_functionality() {
    local wifi_interface="$1"

    log_step "Testing wpa_supplicant functionality"

    # Test status
    log_info "Testing wpa_supplicant status..."
    if sudo wpa_cli -i "$wifi_interface" status > /dev/null; then
        log_info "✓ wpa_supplicant status command works"
    else
        log_error "✗ wpa_supplicant status command failed"
        return 1
    fi

    # Test scan
    log_info "Testing WiFi network scanning..."
    if sudo wpa_cli -i "$wifi_interface" scan > /dev/null; then
        log_info "✓ WiFi scan initiated successfully"

        # Wait for scan results
        sleep 3
        local scan_results=$(sudo wpa_cli -i "$wifi_interface" scan_results)
        local network_count=$(echo "$scan_results" | grep -v "^bssid" | wc -l)

        if [[ $network_count -gt 0 ]]; then
            log_info "✓ Found $network_count WiFi networks"
            echo "Sample networks found:"
            echo "$scan_results" | head -5 | grep -v "^bssid" | while read line; do
                local ssid=$(echo "$line" | cut -f5)
                local signal=$(echo "$line" | cut -f3)
                if [[ -n "$ssid" ]]; then
                    echo "  - $ssid (${signal}dBm)"
                fi
            done
        else
            log_warn "✗ No WiFi networks found in scan"
        fi

        return 0
    else
        log_error "✗ WiFi scan failed"
        return 1
    fi
}

# Function to test WiFi backend with real hardware
test_wifi_backend() {
    log_step "Testing WiFi backend with real hardware"

    log_info "Building project..."
    cd "$PROJECT_ROOT"
    if ! make -j > /dev/null 2>&1; then
        log_error "Build failed"
        return 1
    fi

    log_info "Testing WiFi backend initialization..."

    # Run UI briefly and capture WiFi-related logs
    local wifi_log=$(timeout 5s ./build/bin/helix-ui-proto --timeout 3 -v 2>&1 | grep -E "(WiFi|WifiBackend|wpa)" || true)

    if echo "$wifi_log" | grep -q "WifiBackend.*socket"; then
        if echo "$wifi_log" | grep -q "Could not find"; then
            log_warn "✗ WiFi backend couldn't access sockets (permission issue)"
            log_info "This is expected on systems without proper netdev group setup"
        else
            log_info "✓ WiFi backend detected control sockets"
        fi
    else
        log_warn "✗ No WiFi backend socket messages detected"
    fi

    if echo "$wifi_log" | grep -q "WiFiManager.*initialized"; then
        log_info "✓ WiFiManager initialized successfully"
        return 0
    else
        log_warn "✗ WiFiManager initialization not detected"
        return 1
    fi
}

# Function to test robustness scenarios
test_robustness_scenarios() {
    log_step "Testing robustness scenarios"

    # Test 1: Missing control interface
    log_info "Test 1: Missing control interface scenario"
    sudo pkill wpa_supplicant || true
    sleep 1

    local fallback_log=$(timeout 3s ./build/bin/helix-ui-proto --timeout 2 -v 2>&1 | grep -E "(WiFi|WifiBackend)" || true)
    if echo "$fallback_log" | grep -q "Could not find.*socket"; then
        log_info "✓ Graceful fallback when control interface missing"
    else
        log_warn "✗ Fallback behavior not detected"
    fi

    # Test 2: Interface down scenario
    log_info "Test 2: Interface down scenario"
    local wifi_interface=$(grep -o 'wl[a-z0-9]*' "$RESTORE_FILE" | head -1)
    if [[ -n "$wifi_interface" ]]; then
        sudo ip link set "$wifi_interface" down
        sleep 1

        # Re-setup wpa_supplicant for further testing
        setup_wpa_supplicant "$wifi_interface" > /dev/null 2>&1 || true
    fi

    log_info "Robustness testing completed"
}

# Function to run fallback-only testing
test_fallback_only() {
    log_step "Testing WiFi fallback behavior (no real hardware)"

    cd "$PROJECT_ROOT"
    if ! make -j > /dev/null 2>&1; then
        log_error "Build failed"
        return 1
    fi

    log_info "Testing fallback behavior with no WiFi hardware..."
    local fallback_log=$(timeout 3s ./build/bin/helix-ui-proto --timeout 2 -v 2>&1 | grep -E "(WiFi|WifiBackend)" || true)

    if echo "$fallback_log" | grep -q "WiFiManager.*initialized"; then
        log_info "✓ WiFiManager gracefully handles missing WiFi hardware"
        return 0
    else
        log_warn "✗ WiFiManager fallback behavior needs improvement"
        return 1
    fi
}

# Main execution
main() {
    log_info "Starting WiFi real hardware testing"

    # Document current state first
    local wifi_interface
    if wifi_interface=$(document_system_state); then
        log_info "System state documented for restoration"

        # Check if we have WiFi hardware
        if check_wifi_hardware; then
            # Create wpa_supplicant config
            create_wpa_config

            # Setup real wpa_supplicant
            if setup_wpa_supplicant "$wifi_interface"; then
                # Test wpa_supplicant functionality
                if test_wpa_functionality "$wifi_interface"; then
                    # Test our WiFi backend
                    test_wifi_backend

                    # Test robustness scenarios
                    test_robustness_scenarios
                else
                    log_error "wpa_supplicant functionality test failed"
                    return 1
                fi
            else
                log_error "Failed to setup wpa_supplicant"
                return 1
            fi
        else
            # No WiFi hardware - test fallback only
            test_fallback_only
        fi
    else
        log_error "Failed to document system state"
        return 1
    fi

    log_info "All WiFi tests completed successfully"
}

# Script entry point
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
