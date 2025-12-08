#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Initialize a git worktree for HelixScreen development
#
# Usage: ./scripts/init-worktree.sh <worktree-path>
#
# This script handles the git worktree + submodule dance:
# 1. Initializes all required submodules (skips SDL2 - uses system)
# 2. Copies generated libhv headers (they're built, not in repo)
#
# Example:
#   ./scripts/init-worktree.sh ../helixscreen-feature-parity

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN_REPO="$(dirname "$SCRIPT_DIR")"
WORKTREE_PATH="$1"

if [ -z "$WORKTREE_PATH" ]; then
    echo "Usage: $0 <worktree-path>"
    echo "Example: $0 ../helixscreen-my-feature"
    exit 1
fi

# Resolve to absolute path
WORKTREE_PATH="$(cd "$(dirname "$WORKTREE_PATH")" 2>/dev/null && pwd)/$(basename "$WORKTREE_PATH")"

if [ ! -d "$WORKTREE_PATH" ]; then
    echo "Error: Worktree path does not exist: $WORKTREE_PATH"
    echo "Create it first with: git worktree add -b <branch> $WORKTREE_PATH main"
    exit 1
fi

echo "Initializing worktree: $WORKTREE_PATH"
echo "Main repo: $MAIN_REPO"
echo ""

cd "$WORKTREE_PATH"

# Step 1: Deinit SDL2 (we use system SDL2 on macOS, and the commit is stale)
echo "→ Skipping SDL2 submodule (using system SDL2)..."
git submodule deinit lib/sdl2 2>/dev/null || true
rm -rf lib/sdl2 2>/dev/null || true
mkdir -p lib/sdl2

# Step 2: Initialize required submodules
SUBMODULES="lib/lvgl lib/spdlog lib/libhv lib/glm lib/cpp-terminal lib/wpa_supplicant"
echo "→ Initializing submodules..."
for sub in $SUBMODULES; do
    echo "  - $sub"
    rm -rf "$sub" 2>/dev/null || true
    git submodule update --init --force "$sub"
done

# Step 3: Copy generated libhv headers (they're created during build, not in git)
echo "→ Copying libhv generated headers from main repo..."
if [ -d "$MAIN_REPO/lib/libhv/include/hv" ]; then
    mkdir -p lib/libhv/include
    cp -r "$MAIN_REPO/lib/libhv/include/hv" lib/libhv/include/
    echo "  ✓ Copied $(ls lib/libhv/include/hv | wc -l | tr -d ' ') headers"
else
    echo "  ⚠ Warning: Main repo libhv headers not found. Run 'make' in main repo first."
fi

# Step 4: Copy pre-built libhv.a if it exists (saves build time)
if [ -f "$MAIN_REPO/build/lib/libhv.a" ]; then
    echo "→ Copying pre-built libhv.a..."
    mkdir -p build/lib
    cp "$MAIN_REPO/build/lib/libhv.a" build/lib/
    echo "  ✓ Copied libhv.a"
fi

echo ""
echo "✓ Worktree initialized!"
echo ""
echo "Next steps:"
echo "  cd $WORKTREE_PATH"
echo "  make -j"
echo "  ./build/bin/helix-screen --test -vv"
