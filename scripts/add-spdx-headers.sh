#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Add or update SPDX license identifiers in source files
# Removes verbose copyright boilerplate, replacing with clean SPDX headers

set -euo pipefail

# Configuration
YEAR="2025"
COPYRIGHT="356C LLC"
SPDX_ID="GPL-3.0-or-later"
DRY_RUN=1
VERBOSE=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

usage() {
    cat <<EOF
Usage: $0 [OPTIONS] [FILES...]

Add or update SPDX license identifiers in source files.

OPTIONS:
    -a, --apply         Apply changes (default is dry-run)
    -v, --verbose       Verbose output
    -h, --help          Show this help message

If no files are specified, processes all source files in src/, include/,
scripts/, and ui_xml/ directories.

EXAMPLES:
    $0                              # Dry-run on all files
    $0 --apply                      # Apply to all files
    $0 --apply src/main.cpp         # Apply to specific file
    $0 -v                           # Dry-run with verbose output

SUPPORTED FILE TYPES:
    - C/C++ (.c, .cpp, .h, .hpp)
    - Python (.py)
    - Bash (.sh)
    - XML (.xml)
EOF
    exit 0
}

log_info() {
    echo -e "${BLUE}ℹ️  $*${NC}"
}

log_success() {
    echo -e "${GREEN}✓ $*${NC}"
}

log_warning() {
    echo -e "${YELLOW}⚠️  $*${NC}"
}

log_error() {
    echo -e "${RED}✗ $*${NC}"
}

# Check if file has SPDX identifier
has_spdx() {
    local file="$1"
    head -5 "$file" | grep -q "SPDX-License-Identifier:" && return 0 || return 1
}

# Get SPDX header for file type
get_spdx_header() {
    local file="$1"
    local ext="${file##*.}"

    case "$ext" in
        c|cpp|h|hpp)
            echo "// Copyright $YEAR $COPYRIGHT"
            echo "// SPDX-License-Identifier: $SPDX_ID"
            ;;
        py|sh)
            echo "# Copyright $YEAR $COPYRIGHT"
            echo "# SPDX-License-Identifier: $SPDX_ID"
            ;;
        xml)
            echo "<!-- Copyright $YEAR $COPYRIGHT -->"
            echo "<!-- SPDX-License-Identifier: $SPDX_ID -->"
            ;;
        *)
            log_error "Unknown file type: $ext"
            return 1
            ;;
    esac
}

# Remove old verbose copyright header
remove_old_header() {
    local file="$1"
    local tmpfile="${file}.spdx.tmp"

    # Different strategies based on file type
    local ext="${file##*.}"

    case "$ext" in
        c|cpp|h|hpp)
            # Remove C-style comment blocks at the start
            # Skip shebang if present, then remove /* ... */ blocks
            awk '
                BEGIN { in_comment=0; past_header=0; }
                /^#!/ { print; next; }
                !past_header && /^\/\*/ { in_comment=1; next; }
                in_comment && /\*\// { in_comment=0; past_header=1; next; }
                in_comment { next; }
                !past_header && /^\/\// { next; }
                !past_header && /^$/ { next; }
                { past_header=1; print; }
            ' "$file" > "$tmpfile"
            ;;
        py|sh)
            # Remove leading # comment lines (but preserve shebang)
            awk '
                BEGIN { past_header=0; }
                /^#!/ { print; next; }
                !past_header && /^#/ { next; }
                !past_header && /^$/ { next; }
                { past_header=1; print; }
            ' "$file" > "$tmpfile"
            ;;
        xml)
            # Remove <!-- ... --> comment blocks, preserve <?xml?>
            awk '
                BEGIN { in_comment=0; past_header=0; }
                /<\?xml/ { print; next; }
                !past_header && /<!--/ { in_comment=1; next; }
                in_comment && /-->/ { in_comment=0; past_header=1; next; }
                in_comment { next; }
                !past_header && /^$/ { next; }
                { past_header=1; print; }
            ' "$file" > "$tmpfile"
            ;;
    esac

    echo "$tmpfile"
}

# Process a single file
process_file() {
    local file="$1"
    local ext="${file##*.}"

    # Skip if not a supported type
    case "$ext" in
        c|cpp|h|hpp|py|sh|xml) ;;
        *)
            [[ $VERBOSE -eq 1 ]] && log_warning "Skipping unsupported file type: $file"
            return 0
            ;;
    esac

    # Check if already has SPDX
    if has_spdx "$file"; then
        [[ $VERBOSE -eq 1 ]] && log_success "Already has SPDX: $file"
        return 0
    fi

    log_info "Processing: $file"

    # Get SPDX header for this file type
    local spdx_header
    spdx_header=$(get_spdx_header "$file")

    # Remove old header and get temp file
    local tmpfile
    tmpfile=$(remove_old_header "$file")

    # Create final file with SPDX header
    local finalfile="${file}.spdx.final"

    # Handle special cases (shebang, XML declaration)
    if [[ "$ext" == "sh" ]] || [[ "$ext" == "py" ]]; then
        # Check if first line is shebang
        if head -1 "$file" | grep -q '^#!'; then
            head -1 "$file" > "$finalfile"
            echo "" >> "$finalfile"
            echo "$spdx_header" >> "$finalfile"
            echo "" >> "$finalfile"
            tail -n +2 "$tmpfile" >> "$finalfile"
        else
            echo "$spdx_header" > "$finalfile"
            echo "" >> "$finalfile"
            cat "$tmpfile" >> "$finalfile"
        fi
    elif [[ "$ext" == "xml" ]]; then
        # Check if first line is <?xml?>
        if head -1 "$file" | grep -q '^<?xml'; then
            head -1 "$file" > "$finalfile"
            echo "$spdx_header" >> "$finalfile"
            echo "" >> "$finalfile"
            tail -n +2 "$tmpfile" >> "$finalfile"
        else
            echo "$spdx_header" > "$finalfile"
            echo "" >> "$finalfile"
            cat "$tmpfile" >> "$finalfile"
        fi
    else
        # C/C++ files - just prepend header
        echo "$spdx_header" > "$finalfile"
        echo "" >> "$finalfile"
        cat "$tmpfile" >> "$finalfile"
    fi

    # Show diff if verbose
    if [[ $VERBOSE -eq 1 ]]; then
        echo "--- Diff for $file ---"
        diff -u "$file" "$finalfile" || true
        echo ""
    fi

    # Apply or report
    if [[ $DRY_RUN -eq 0 ]]; then
        mv "$finalfile" "$file"
        log_success "Updated: $file"
    else
        log_info "Would update: $file"
    fi

    # Cleanup
    rm -f "$tmpfile" "$finalfile"
}

# Main
main() {
    local files=()

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -a|--apply)
                DRY_RUN=0
                shift
                ;;
            -v|--verbose)
                VERBOSE=1
                shift
                ;;
            -h|--help)
                usage
                ;;
            -*)
                log_error "Unknown option: $1"
                usage
                ;;
            *)
                files+=("$1")
                shift
                ;;
        esac
    done

    # If no files specified, find all source files
    if [[ ${#files[@]} -eq 0 ]]; then
        log_info "Finding source files..."

        # C/C++ files
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find src include -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 2>/dev/null || true)

        # Python files
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find scripts -type f -name "*.py" -print0 2>/dev/null || true)

        # Bash files
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find scripts -type f -name "*.sh" -print0 2>/dev/null || true)

        # XML files
        while IFS= read -r -d '' file; do
            files+=("$file")
        done < <(find ui_xml -type f -name "*.xml" -print0 2>/dev/null || true)

        log_info "Found ${#files[@]} files"
    fi

    # Process files
    local processed=0
    local skipped=0

    if [[ $DRY_RUN -eq 1 ]]; then
        log_warning "DRY RUN - No files will be modified"
        log_info "Use --apply to apply changes"
        echo ""
    fi

    for file in "${files[@]}"; do
        if [[ ! -f "$file" ]]; then
            log_warning "File not found: $file"
            continue
        fi

        # Skip third-party code
        if [[ "$file" == *"/lib/"* ]] || [[ "$file" == *"/node_modules/"* ]]; then
            [[ $VERBOSE -eq 1 ]] && log_warning "Skipping third-party: $file"
            ((skipped++))
            continue
        fi

        process_file "$file"
        ((processed++))
    done

    echo ""
    log_success "Processed: $processed files"
    [[ $skipped -gt 0 ]] && log_info "Skipped: $skipped files"

    if [[ $DRY_RUN -eq 1 ]]; then
        echo ""
        log_warning "DRY RUN - Run with --apply to make changes"
    fi
}

main "$@"
