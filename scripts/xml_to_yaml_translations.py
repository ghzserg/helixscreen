#!/usr/bin/env python3
"""
Convert translations.xml to individual YAML files per language.

Input: ui_xml/translations/translations.xml
Output: translations/{en,de,fr,es,ru}.yml
"""

import xml.etree.ElementTree as ET
from pathlib import Path
import sys

# Languages to extract
LANGUAGES = ['en', 'de', 'fr', 'es', 'ru']

def parse_translations_xml(xml_path: Path) -> dict:
    """Parse translations.xml and return dict of {lang: {tag: translation}}"""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    translations = {lang: {} for lang in LANGUAGES}

    for elem in root.findall('translation'):
        tag = elem.get('tag')
        if not tag:
            continue

        for lang in LANGUAGES:
            value = elem.get(lang)
            if value:
                translations[lang][tag] = value

    return translations

def write_yaml(lang: str, translations: dict, output_path: Path):
    """Write translations to YAML file with proper formatting."""
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f'locale: {lang}\n')
        f.write('translations:\n')

        for tag, value in translations.items():
            # Escape quotes and handle special characters
            # Use double quotes if value contains special chars
            needs_quotes = any(c in value for c in [':', '#', "'", '"', '\n', '{', '}', '[', ']', '&', '*', '!', '|', '>', '%', '@', '`'])

            if needs_quotes:
                # Escape double quotes in value
                escaped = value.replace('\\', '\\\\').replace('"', '\\"')
                f.write(f'  "{tag}": "{escaped}"\n')
            else:
                f.write(f'  "{tag}": {value}\n')

def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent

    xml_path = project_root / 'ui_xml' / 'translations' / 'translations.xml'
    output_dir = project_root / 'translations'

    if not xml_path.exists():
        print(f"Error: {xml_path} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Parsing {xml_path}...")
    translations = parse_translations_xml(xml_path)

    for lang in LANGUAGES:
        output_path = output_dir / f'{lang}.yml'
        print(f"Writing {output_path} ({len(translations[lang])} translations)...")
        write_yaml(lang, translations[lang], output_path)

    print("Done!")

if __name__ == '__main__':
    main()
