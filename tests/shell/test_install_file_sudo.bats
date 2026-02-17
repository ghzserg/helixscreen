#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests that extract_and_install_release() uses file_sudo instead of bare $SUDO
# for the atomic swap/restore/cleanup operations. Without file_sudo, a user-owned
# INSTALL_DIR requires sudo (which fails under NoNewPrivileges=true in systemd),
# causing the running process's CWD to be renamed and all relative paths to break.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
RELEASE_SH="$WORKTREE_ROOT/scripts/lib/installer/release.sh"
INSTALL_SH="$WORKTREE_ROOT/scripts/install.sh"

# =============================================================================
# Static analysis: modular release.sh must use file_sudo in swap section
# =============================================================================

@test "release.sh: atomic swap uses file_sudo, not bare SUDO for mv to .old" {
    # The mv that renames INSTALL_DIR to INSTALL_DIR.old must use file_sudo
    # Old pattern: $SUDO mv "${INSTALL_DIR}" "${INSTALL_DIR}.old"
    ! grep -E '\$SUDO mv "\$\{INSTALL_DIR\}" "\$\{INSTALL_DIR\}\.old"' "$RELEASE_SH"
}

@test "release.sh: move new install into place uses file_sudo" {
    # The mv that moves new_install into INSTALL_DIR must use file_sudo
    # Old pattern: $SUDO mv "${new_install}" "${INSTALL_DIR}"
    ! grep -E '\$SUDO mv "\$\{new_install\}" "\$\{INSTALL_DIR\}"' "$RELEASE_SH"
}

@test "release.sh: rollback mv uses file_sudo" {
    # Old pattern: $SUDO mv "${INSTALL_DIR}.old" "${INSTALL_DIR}"
    ! grep -E '\$SUDO mv "\$\{INSTALL_DIR\}\.old" "\$\{INSTALL_DIR\}"' "$RELEASE_SH"
}

@test "release.sh: config restore uses file_sudo" {
    # Old pattern: $SUDO mkdir -p "${INSTALL_DIR}/config"
    ! grep -E '\$SUDO mkdir -p "\$\{INSTALL_DIR\}/config"' "$RELEASE_SH"
    # Old pattern: $SUDO cp "$BACKUP_CONFIG"
    ! grep -E '\$SUDO cp "\$BACKUP_CONFIG"' "$RELEASE_SH"
}

@test "release.sh: cleanup_old_install uses file_sudo" {
    # Old pattern: $SUDO rm -rf "${INSTALL_DIR}.old"
    ! grep -E '\$SUDO rm -rf "\$\{INSTALL_DIR\}\.old"' "$RELEASE_SH"
}

# =============================================================================
# Static analysis: monolithic install.sh must match
# =============================================================================

@test "install.sh: atomic swap uses file_sudo, not bare SUDO for mv to .old" {
    ! grep -E '\$SUDO mv "\$\{INSTALL_DIR\}" "\$\{INSTALL_DIR\}\.old"' "$INSTALL_SH"
}

@test "install.sh: move new install into place uses file_sudo" {
    ! grep -E '\$SUDO mv "\$\{new_install\}" "\$\{INSTALL_DIR\}"' "$INSTALL_SH"
}

@test "install.sh: rollback mv uses file_sudo" {
    ! grep -E '\$SUDO mv "\$\{INSTALL_DIR\}\.old" "\$\{INSTALL_DIR\}"' "$INSTALL_SH"
}

@test "install.sh: config restore uses file_sudo" {
    ! grep -E '\$SUDO mkdir -p "\$\{INSTALL_DIR\}/config"' "$INSTALL_SH"
    ! grep -E '\$SUDO cp "\$BACKUP_CONFIG"' "$INSTALL_SH"
}

@test "install.sh: cleanup_old_install uses file_sudo" {
    ! grep -E '\$SUDO rm -rf "\$\{INSTALL_DIR\}\.old"' "$INSTALL_SH"
}
