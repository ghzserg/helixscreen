#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
XML Translation Migration Script for HelixScreen.

This script migrates XML files to use LVGL's translation_tag attribute
by scanning for text= attributes and adding translation_tag= when
the string exists in translations.xml.

Usage:
    python migrate_xml_translations.py [--dry-run] [--verbose]

Options:
    --dry-run   Show what would change without modifying files
    --verbose   Show detailed output for each file
    --add-missing   Add missing strings to en.yml (requires manual translation after)
"""

import argparse
import re
import sys
from pathlib import Path
from typing import NamedTuple
import xml.etree.ElementTree as ET


class Migration(NamedTuple):
    """A single migration change."""
    file: Path
    line: int
    old_text: str
    new_text: str
    translation_key: str


def load_translation_tags(translations_xml: Path) -> set[str]:
    """Load all translation tags from translations.xml."""
    tags = set()

    tree = ET.parse(translations_xml)
    root = tree.getroot()

    for translation in root.findall('translation'):
        tag = translation.get('tag')
        if tag:
            tags.add(tag)

    return tags


def should_skip_text(text: str) -> bool:
    """Check if a text value should be skipped from translation."""
    # Skip empty strings
    if not text or text.strip() == '':
        return True

    # Skip prop references like $label, $title
    if text.startswith('$'):
        return True

    # Skip const references like #space_lg
    if text.startswith('#'):
        return True

    # Skip pure numeric values
    if re.match(r'^-?\d+(\.\d+)?$', text):
        return True

    # Skip things that look like CSS/hex colors
    if re.match(r'^0x[0-9a-fA-F]+$', text):
        return True

    # Skip percentage values
    if re.match(r'^-?\d+%$', text):
        return True

    # Skip obviously technical strings (file paths, etc)
    if '/' in text and not any(c.isalpha() for c in text.replace('/', '')):
        return True

    # Skip UTF-8 icon codepoints (MDI icons start with high bytes \xF0+)
    # These are single-character icon glyphs, not user-visible text
    if len(text) <= 4 and ord(text[0]) >= 0xF0:
        return True

    # Skip single characters that are likely icons or special chars
    if len(text) == 1 and not text.isalnum():
        return True

    return False


def find_text_attributes(content: str) -> list[tuple[int, int, str, str, str]]:
    """
    Find all text="..." attributes in XML content.

    Returns list of (start_pos, end_pos, attr_name, text_value, tag_attr_name) tuples.
    Handles both text= and other props like label=, title=, description=.
    """
    results = []

    # Map of attribute names to their corresponding tag attribute names
    # Format: attr_name -> tag_attr_name
    attr_map = {
        'text': 'translation_tag',
        'placeholder_text': 'placeholder_tag',
        'label': 'label_tag',
        'title': 'title_tag',
        'description': 'description_tag',
        'message': 'message_tag',
        'value': 'value_tag',  # for setting_info_row
    }

    # Build regex pattern for all attribute names
    attr_names = '|'.join(attr_map.keys())
    pattern = rf'\b({attr_names})="([^"]*)"'

    for match in re.finditer(pattern, content):
        attr_name = match.group(1)
        text_value = match.group(2)
        start = match.start()
        end = match.end()
        tag_attr = attr_map[attr_name]
        results.append((start, end, attr_name, text_value, tag_attr))

    return results


def already_has_tag_attr(content: str, text_attr_end: int, tag_attr_name: str) -> bool:
    """Check if the element already has the corresponding tag attribute."""
    # Look ahead in the same element (up to the closing > or />)
    remaining = content[text_attr_end:]

    # Find the end of this element/tag
    close_match = re.search(r'/?>', remaining)
    if not close_match:
        return False

    element_rest = remaining[:close_match.start()]
    return f'{tag_attr_name}=' in element_rest


def migrate_file(
    file_path: Path,
    known_tags: set[str],
    dry_run: bool = False,
    verbose: bool = False,
) -> list[Migration]:
    """
    Migrate a single XML file to use translation_tag.

    Returns list of migrations applied.
    """
    migrations = []

    content = file_path.read_text(encoding='utf-8')
    original_content = content

    # Find all text attributes
    text_attrs = find_text_attributes(content)

    # Process in reverse order so positions stay valid
    for start, end, attr_name, text_value, tag_attr_name in reversed(text_attrs):
        # Skip values that shouldn't be translated
        if should_skip_text(text_value):
            if verbose:
                print(f"  SKIP (technical): {text_value[:50]}")
            continue

        # Check if already has the tag attribute
        if already_has_tag_attr(content, end, tag_attr_name):
            if verbose:
                print(f"  SKIP (has tag): {text_value[:50]}")
            continue

        # Check if this text exists in translations
        if text_value not in known_tags:
            if verbose:
                print(f"  SKIP (not in translations): {text_value[:50]}")
            continue

        # Calculate line number for reporting
        line_num = content[:start].count('\n') + 1

        # Add tag attribute after the text attribute
        tag_attr = f'{tag_attr_name}="{text_value}"'
        new_attr = f'{attr_name}="{text_value}" {tag_attr}'

        # Replace the text attribute with text + tag
        content = content[:start] + new_attr + content[end:]

        migrations.append(Migration(
            file=file_path,
            line=line_num,
            old_text=f'{attr_name}="{text_value}"',
            new_text=new_attr,
            translation_key=text_value,
        ))

    # Write changes if not dry run and content changed
    if not dry_run and content != original_content:
        file_path.write_text(content, encoding='utf-8')

    return migrations


def collect_missing_strings(
    file_path: Path,
    known_tags: set[str],
) -> list[str]:
    """
    Collect text strings that are not in translations.

    Returns list of missing translation keys.
    """
    missing = []

    content = file_path.read_text(encoding='utf-8')
    text_attrs = find_text_attributes(content)

    for start, end, attr_name, text_value, tag_attr_name in text_attrs:
        if should_skip_text(text_value):
            continue

        if already_has_tag_attr(content, end, tag_attr_name):
            continue

        if text_value not in known_tags:
            missing.append(text_value)

    return missing


def add_to_en_yml(yml_path: Path, new_keys: list[str]) -> int:
    """
    Add new translation keys to en.yml.

    Returns count of keys added.
    """
    content = yml_path.read_text(encoding='utf-8')

    added = 0
    for key in sorted(set(new_keys)):
        # Escape quotes in key for YAML
        escaped_key = key.replace('"', '\\"')

        # Check if already exists
        if f'"{escaped_key}":' in content or f"'{escaped_key}':" in content:
            continue

        # Add at end of file
        yaml_line = f'  "{escaped_key}": "{escaped_key}"\n'
        content += yaml_line
        added += 1

    if added > 0:
        yml_path.write_text(content, encoding='utf-8')

    return added


def main():
    parser = argparse.ArgumentParser(
        description="Migrate XML files to use translation_tag"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would change without modifying files",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show detailed output for each file",
    )
    parser.add_argument(
        "--add-missing",
        action="store_true",
        help="Add missing strings to en.yml",
    )
    parser.add_argument(
        "--xml-dir",
        type=Path,
        default=Path("ui_xml"),
        help="Directory containing XML files to migrate",
    )
    parser.add_argument(
        "--translations-xml",
        type=Path,
        default=Path("ui_xml/translations/translations.xml"),
        help="Path to translations.xml",
    )
    parser.add_argument(
        "--en-yml",
        type=Path,
        default=Path("translations/en.yml"),
        help="Path to en.yml for adding missing strings",
    )

    args = parser.parse_args()

    # Load known translation tags
    if not args.translations_xml.exists():
        print(f"ERROR: translations.xml not found at {args.translations_xml}")
        return 1

    known_tags = load_translation_tags(args.translations_xml)
    print(f"Loaded {len(known_tags)} translation tags from {args.translations_xml}")

    # Find all XML files
    xml_files = list(args.xml_dir.glob("**/*.xml"))

    # Exclude translations.xml and globals.xml (no user-facing strings)
    xml_files = [f for f in xml_files if 'translations.xml' not in str(f)]
    xml_files = [f for f in xml_files if f.name != 'globals.xml']

    print(f"Found {len(xml_files)} XML files to scan")

    all_migrations = []
    all_missing = []

    for xml_file in sorted(xml_files):
        if args.verbose:
            print(f"\nProcessing: {xml_file}")

        # Collect missing strings
        missing = collect_missing_strings(xml_file, known_tags)
        if missing:
            all_missing.extend(missing)
            if args.verbose:
                for m in missing:
                    print(f"  MISSING: {m[:60]}")

        # Run migrations
        migrations = migrate_file(
            xml_file,
            known_tags,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
        all_migrations.extend(migrations)

    # Report results
    print(f"\n{'='*60}")
    print(f"Migration Summary")
    print(f"{'='*60}")

    if all_migrations:
        print(f"\n{len(all_migrations)} translations added:")
        for m in all_migrations[:20]:
            print(f"  {m.file.name}:{m.line} - {m.translation_key[:50]}")
        if len(all_migrations) > 20:
            print(f"  ... and {len(all_migrations) - 20} more")
    else:
        print("\nNo migrations needed.")

    unique_missing = sorted(set(all_missing))
    if unique_missing:
        print(f"\n{len(unique_missing)} strings missing from translations:")
        for m in unique_missing[:20]:
            print(f"  - {m[:60]}")
        if len(unique_missing) > 20:
            print(f"  ... and {len(unique_missing) - 20} more")

        if args.add_missing:
            if not args.dry_run:
                added = add_to_en_yml(args.en_yml, unique_missing)
                print(f"\nAdded {added} new keys to {args.en_yml}")
                print("Run generate_translations.py to update translations.xml")
            else:
                print(f"\n[DRY RUN] Would add {len(unique_missing)} keys to {args.en_yml}")

    if args.dry_run:
        print("\n[DRY RUN] No files were modified")

    return 0


if __name__ == "__main__":
    sys.exit(main())
