#!/bin/bash
# Pre-commit validation script for HelixScreen
# Checks for common coding mistakes before allowing commits
#
# Usage:
#   - Warn mode (default): Reports issues but doesn't block commits
#   - Strict mode: Use --strict flag to block commits on errors
#
# Enable:
#   git config core.hooksPath .claude/hooks
#   chmod +x .claude/hooks/pre-commit-check.sh
#
# Disable:
#   git config --unset core.hooksPath
#
# Bypass for one commit:
#   git commit --no-verify

set -e

STRICT_MODE=false
if [[ "$1" == "--strict" ]]; then
    STRICT_MODE=true
fi

ERROR_COUNT=0

echo "🔍 Running pre-commit validation checks..."
echo ""

# ═══════════════════════════════════════════════════════════
# Check 1: Hardcoded colors (lv_color_hex)
# ═══════════════════════════════════════════════════════════
echo "📋 [1/4] Checking for hardcoded colors..."

if git diff --cached --name-only --diff-filter=ACMR | grep -E '\.(cpp|c|h)$' | xargs grep -n "lv_color_hex" 2>/dev/null; then
    echo "❌ Found hardcoded lv_color_hex() usage"
    echo "   → All colors must be defined in globals.xml"
    echo "   → Use: lv_xml_get_const() + ui_theme_parse_color()"
    echo "   → See: CLAUDE.md 'Critical Rules #1'"
    echo ""
    ((ERROR_COUNT++))
else
    echo "✅ No hardcoded colors found"
    echo ""
fi

# ═══════════════════════════════════════════════════════════
# Check 2: printf/cout/LV_LOG usage
# ═══════════════════════════════════════════════════════════
echo "📋 [2/4] Checking for printf/cout/LV_LOG..."

if git diff --cached --name-only --diff-filter=ACMR | grep -E '\.(cpp|c)$' | xargs grep -nE '(printf\(|std::cout|std::cerr|LV_LOG_)' 2>/dev/null; then
    echo "❌ Found printf/cout/LV_LOG_* usage"
    echo "   → Use spdlog instead: spdlog::info(), spdlog::debug(), etc."
    echo "   → See: .claude/quickref/spdlog-patterns.md"
    echo ""
    ((ERROR_COUNT++))
else
    echo "✅ No printf/cout/LV_LOG found"
    echo ""
fi

# ═══════════════════════════════════════════════════════════
# Check 3: Missing GPL copyright headers (new files only)
# ═══════════════════════════════════════════════════════════
echo "📋 [3/4] Checking copyright headers on new files..."

MISSING_HEADERS=()
while IFS= read -r file; do
    if [[ "$file" =~ \.(cpp|c|h|xml)$ ]]; then
        if ! grep -q "GNU General Public License" "$file" 2>/dev/null; then
            MISSING_HEADERS+=("$file")
        fi
    fi
done < <(git diff --cached --name-only --diff-filter=A)

if [[ ${#MISSING_HEADERS[@]} -gt 0 ]]; then
    echo "❌ Missing GPL v3 copyright headers in new files:"
    for file in "${MISSING_HEADERS[@]}"; do
        echo "   - $file"
    done
    echo "   → See: docs/COPYRIGHT_HEADERS.md for templates"
    echo ""
    ((ERROR_COUNT++))
else
    echo "✅ All new files have copyright headers"
    echo ""
fi

# ═══════════════════════════════════════════════════════════
# Check 4: Private LVGL API usage
# ═══════════════════════════════════════════════════════════
echo "📋 [4/4] Checking for private LVGL API usage..."

if git diff --cached --name-only --diff-filter=ACMR | grep -E '\.(cpp|c)$' | xargs grep -nE '(_lv_|->coords\.|lv_obj_mark_dirty)' 2>/dev/null; then
    echo "❌ Found private LVGL API usage"
    echo "   → Never use underscore-prefixed functions (_lv_*)"
    echo "   → Never access object internals (obj->coords)"
    echo "   → Use public APIs: lv_obj_get_x(), lv_obj_update_layout(), etc."
    echo "   → See: CLAUDE.md 'Critical Patterns #7'"
    echo ""
    ((ERROR_COUNT++))
else
    echo "✅ No private LVGL APIs found"
    echo ""
fi

# ═══════════════════════════════════════════════════════════
# Summary and exit
# ═══════════════════════════════════════════════════════════
echo "════════════════════════════════════════════════════════"

if [[ $ERROR_COUNT -eq 0 ]]; then
    echo "✅ All pre-commit checks passed!"
    echo ""
    exit 0
else
    echo "⚠️  Found $ERROR_COUNT issue(s)"
    echo ""

    if [[ "$STRICT_MODE" == "true" ]]; then
        echo "❌ STRICT MODE: Commit blocked"
        echo ""
        echo "Fix the issues above, or bypass with:"
        echo "  git commit --no-verify"
        echo ""
        exit 1
    else
        echo "⚠️  WARN MODE: Commit allowed, but please fix issues"
        echo ""
        echo "Enable strict mode (blocks commits):"
        echo "  .claude/hooks/pre-commit-check.sh --strict"
        echo ""
        echo "Or bypass warnings:"
        echo "  git commit --no-verify"
        echo ""
        exit 0  # Allow commit in warn mode
    fi
fi
