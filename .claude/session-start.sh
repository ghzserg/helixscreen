#!/bin/bash
# Session startup script for HelixScreen development
# Shows context and status at the beginning of each Claude Code session
#
# Enable: Add to .claude/settings.local.json:
# {
#   "startup": {
#     "command": ".claude/session-start.sh"
#   }
# }
#
# Or run manually: .claude/session-start.sh

set -e

# Color definitions
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${GREEN}🚀 HelixScreen Development Session${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ═══════════════════════════════════════════════════════════
# Show recent commits (last 5)
# ═══════════════════════════════════════════════════════════
echo -e "${BLUE}📊 Recent commits:${NC}"
git log --oneline --color=always -5 2>/dev/null || echo "  (git log not available)"
echo ""

# ═══════════════════════════════════════════════════════════
# Show current branch and status
# ═══════════════════════════════════════════════════════════
BRANCH=$(git branch --show-current 2>/dev/null || echo "unknown")
echo -e "${BLUE}📂 Current branch:${NC} $BRANCH"

# Show status summary
MODIFIED=$(git status --porcelain 2>/dev/null | grep -c "^ M" || echo "0")
STAGED=$(git status --porcelain 2>/dev/null | grep -c "^M " || echo "0")
UNTRACKED=$(git status --porcelain 2>/dev/null | grep -c "^??" || echo "0")

if [[ $MODIFIED -gt 0 ]] || [[ $STAGED -gt 0 ]] || [[ $UNTRACKED -gt 0 ]]; then
    echo -e "${YELLOW}📝 Working tree:${NC} $MODIFIED modified, $STAGED staged, $UNTRACKED untracked"
else
    echo -e "${GREEN}✨ Working tree:${NC} clean"
fi
echo ""

# ═══════════════════════════════════════════════════════════
# Show current focus from HANDOFF.md
# ═══════════════════════════════════════════════════════════
if [[ -f "HANDOFF.md" ]]; then
    echo -e "${BLUE}🎯 Current focus (from HANDOFF.md):${NC}"
    FOCUS=$(head -5 HANDOFF.md | tail -1 | sed 's/\*\*Current Focus:\*\* //')
    echo "   $FOCUS"
    echo ""

    # ═══════════════════════════════════════════════════════════
    # Check HANDOFF.md size
    # ═══════════════════════════════════════════════════════════
    HANDOFF_LINES=$(wc -l < HANDOFF.md)
    if [[ $HANDOFF_LINES -gt 200 ]]; then
        echo -e "${RED}⚠️  WARNING:${NC} HANDOFF.md is $HANDOFF_LINES lines (max recommended: 150)"
        echo "   Consider pruning completed work to keep it lean."
        echo ""
    elif [[ $HANDOFF_LINES -gt 150 ]]; then
        echo -e "${YELLOW}⚠️  Note:${NC} HANDOFF.md is $HANDOFF_LINES lines (approaching max of 150)"
        echo ""
    fi
else
    echo -e "${YELLOW}⚠️  HANDOFF.md not found${NC}"
    echo ""
fi

# ═══════════════════════════════════════════════════════════
# Quick help reminder
# ═══════════════════════════════════════════════════════════
echo -e "${BLUE}💡 Quick tips:${NC}"
echo "   • Pre-flight checklist: .claude/checklist.md"
echo "   • Quick reference: .claude/quickref/*.md"
echo "   • Build: make -j"
echo "   • Test: ./build/bin/helix-ui-proto [panel_name]"
echo ""

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${GREEN}✅ Ready to work!${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
