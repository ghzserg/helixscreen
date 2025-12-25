#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HelixPrint Moonraker Plugin - Remote Installer
#
# One-liner install:
#   curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/moonraker-plugin/remote-install.sh | bash
#
# This script:
#   1. Clones/updates the helixscreen repo (just the moonraker-plugin folder)
#   2. Creates symlink to Moonraker components
#   3. Adds [helix_print] and [update_manager helix_print] to moonraker.conf
#   4. Restarts Moonraker

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[✓]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[✗]${NC} $1"; exit 1; }
step()  { echo -e "${CYAN}[→]${NC} $1"; }

REPO_URL="https://github.com/prestonbrown/helixscreen.git"
INSTALL_DIR="$HOME/helix_print"
BRANCH="main"

echo ""
echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     HelixPrint Plugin Installer            ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
echo ""

# Step 1: Find Moonraker
step "Finding Moonraker installation..."

MOONRAKER_PATH=""
for loc in "$HOME/moonraker" "/home/pi/moonraker" "/home/klipper/moonraker"; do
    if [[ -d "$loc/moonraker/components" ]]; then
        MOONRAKER_PATH="$loc"
        break
    fi
done

if [[ -z "$MOONRAKER_PATH" ]]; then
    # Try pip-installed moonraker
    MOONRAKER_PATH=$(python3 -c "import moonraker; import os; print(os.path.dirname(os.path.dirname(moonraker.__path__[0])))" 2>/dev/null || true)
fi

[[ -z "$MOONRAKER_PATH" ]] && error "Could not find Moonraker. Is it installed?"
info "Found Moonraker at: $MOONRAKER_PATH"

# Step 2: Find moonraker.conf
step "Finding moonraker.conf..."

MOONRAKER_CONF=""
for loc in "$HOME/printer_data/config/moonraker.conf" \
           "$HOME/klipper_config/moonraker.conf" \
           "/home/pi/printer_data/config/moonraker.conf" \
           "/home/pi/klipper_config/moonraker.conf"; do
    if [[ -f "$loc" ]]; then
        MOONRAKER_CONF="$loc"
        break
    fi
done

[[ -z "$MOONRAKER_CONF" ]] && error "Could not find moonraker.conf"
info "Found config at: $MOONRAKER_CONF"

# Step 3: Clone or update repo
step "Installing plugin files..."

if [[ -d "$INSTALL_DIR/.git" ]]; then
    info "Updating existing installation..."
    cd "$INSTALL_DIR"
    git fetch origin "$BRANCH" --depth=1
    git checkout "$BRANCH"
    git reset --hard "origin/$BRANCH"
else
    info "Cloning repository..."
    # Sparse checkout to only get moonraker-plugin directory
    git clone --depth=1 --filter=blob:none --sparse "$REPO_URL" "$INSTALL_DIR"
    cd "$INSTALL_DIR"
    git sparse-checkout set moonraker-plugin
fi

PLUGIN_FILE="$INSTALL_DIR/moonraker-plugin/helix_print.py"
[[ ! -f "$PLUGIN_FILE" ]] && error "Plugin file not found after clone"
info "Plugin files installed to: $INSTALL_DIR"

# Step 4: Create symlink
step "Creating symlink to Moonraker components..."

COMPONENTS_DIR="$MOONRAKER_PATH/moonraker/components"
TARGET="$COMPONENTS_DIR/helix_print.py"

if [[ -L "$TARGET" ]]; then
    rm "$TARGET"
fi
ln -sf "$PLUGIN_FILE" "$TARGET"
info "Symlink created: $TARGET"

# Step 5: Add config sections if not present
step "Checking moonraker.conf..."

if ! grep -q "^\[helix_print\]" "$MOONRAKER_CONF"; then
    info "Adding [helix_print] section..."
    cat >> "$MOONRAKER_CONF" << 'EOF'

#~# --- HelixPrint Plugin ---
[helix_print]
# enabled: True
# temp_dir: .helix_temp
# symlink_dir: .helix_print
# cleanup_delay: 86400
EOF
else
    info "[helix_print] section already exists"
fi

if ! grep -q "^\[update_manager helix_print\]" "$MOONRAKER_CONF"; then
    info "Adding [update_manager helix_print] section..."
    cat >> "$MOONRAKER_CONF" << EOF

[update_manager helix_print]
type: git_repo
origin: $REPO_URL
path: $INSTALL_DIR
primary_branch: $BRANCH
managed_services: moonraker
EOF
else
    info "[update_manager helix_print] section already exists"
fi

# Step 6: Restart Moonraker
step "Restarting Moonraker..."

if command -v systemctl &> /dev/null && systemctl is-active --quiet moonraker; then
    sudo systemctl restart moonraker
    info "Moonraker restarted"
else
    warn "Could not restart Moonraker automatically"
    echo "    Please run: sudo systemctl restart moonraker"
fi

# Done!
echo ""
echo -e "${GREEN}╔════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     Installation Complete!                 ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════╝${NC}"
echo ""
echo "Verify installation:"
echo "  curl http://localhost:7125/server/helix/status"
echo ""
echo "The plugin will auto-update via Moonraker's update manager."
echo ""
