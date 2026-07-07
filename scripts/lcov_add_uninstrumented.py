#!/usr/bin/env python3
"""
lcov_add_uninstrumented.py - Add synthetic lcov entries for uninstrumented source files.

This script walks the source directories (firmware/can-bridge, firmware/vanilla),
finds all .cpp/.h/.ino files under sonar.sources, and adds them to the lcov report
with 0% coverage if they're not already present.

Usage:
    python3 scripts/lcov_add_uninstrumented.py lcov.info lcoc.info.new --root /path/to/project
"""

import sys
import os
import re
from pathlib import Path
from typing import Set, Dict, List


def count_code_lines(filepath: str) -> int:
    """
    Count code lines in a file (non-blank, non-comment).
    Simple heuristic: lines that are not empty and not pure comments.
    """
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception:
        return 0

    code_lines = 0
    in_block_comment = False

    for line in lines:
        stripped = line.strip()

        # Track C block comments
        if '/*' in stripped:
            in_block_comment = True
        if '*/' in stripped:
            in_block_comment = False
            continue
        if in_block_comment:
            continue

        # Skip single-line comments
        if stripped.startswith('//') or stripped.startswith('#'):
            continue

        # Skip empty lines
        if not stripped:
            continue

        # Count as code line
        code_lines += 1

    return code_lines


def extract_lcov_files(lcov_path: str) -> Set[str]:
    """Extract set of files already present in lcov."""
    files = set()
    try:
        with open(lcov_path, 'r') as f:
            for line in f:
                if line.startswith('SF:'):
                    filepath = line[3:].strip()
                    files.add(filepath)
    except Exception:
        pass
    return files


def find_source_files(src_dirs: List[str]) -> Dict[str, int]:
    """
    Find all source files in source directories and count their code lines.
    Returns dict: filepath -> line_count
    """
    source_files = {}

    for src_dir in src_dirs:
        if not os.path.isdir(src_dir):
            continue

        for root, dirs, files in os.walk(src_dir):
            # Skip build directories
            if 'build' in root or 'verify' in root:
                continue

            for file in files:
                # Include .cpp, .h, .ino files
                if file.endswith(('.cpp', '.h', '.ino')):
                    filepath = os.path.join(root, file)
                    line_count = count_code_lines(filepath)
                    if line_count > 0:
                        source_files[filepath] = line_count

    return source_files


def generate_lcov_entry(filepath: str, line_count: int) -> str:
    """Generate synthetic lcov entry for an uninstrumented file."""
    entry = [f'SF:{filepath}']

    # Add DA:line,0 for each code line (0 coverage)
    for line_num in range(1, line_count + 1):
        entry.append(f'DA:{line_num},0')

    # Summary fields (all zeros for uninstrumented)
    entry.append('LF:{}'.format(line_count))  # Lines Found
    entry.append('LH:0')  # Lines Hit
    entry.append('end_of_record')

    return '\n'.join(entry)


def main():
    if len(sys.argv) < 3:
        print("Usage: lcov_add_uninstrumented.py <input_lcov.info> <output_lcov.info> --root <project_root>")
        sys.exit(1)

    input_lcov = sys.argv[1]
    output_lcov = sys.argv[2]

    # Parse optional --root argument
    project_root = os.getcwd()
    if len(sys.argv) >= 5 and sys.argv[3] == '--root':
        project_root = sys.argv[4]

    # Change to project root for relative path calculations
    original_cwd = os.getcwd()
    os.chdir(project_root)

    try:
        # Extract files already in lcov
        existing_files = extract_lcov_files(input_lcov)

        # Find all source files under sonar.sources
        src_dirs = [
            'firmware/can-bridge',
            'firmware/vanilla'
        ]

        source_files = find_source_files(src_dirs)

        # Find uninstrumented files (in source but not in lcov)
        uninstrumented = {}
        for filepath, line_count in source_files.items():
            # Use relative path for comparison (lcov uses absolute paths)
            abs_path = os.path.abspath(filepath)
            if abs_path not in existing_files:
                uninstrumented[abs_path] = line_count

        # Read original lcov
        with open(input_lcov, 'r') as f:
            lcov_content = f.read()

        # Append synthetic entries for uninstrumented files
        output = lcov_content
        if uninstrumented:
            output += '\n'
            for filepath, line_count in sorted(uninstrumented.items()):
                output += generate_lcov_entry(filepath, line_count) + '\n'

        # Write output
        with open(output_lcov, 'w') as f:
            f.write(output)

        # Report
        total_files = len(source_files)
        instrumented_files = len(existing_files)
        uninstrumented_count = len(uninstrumented)

        print(f"Coverage statistics:")
        print(f"  Total source files: {total_files}")
        print(f"  Instrumented files: {instrumented_files}")
        print(f"  Uninstrumented files added: {uninstrumented_count}")
        print(f"  Output written to: {output_lcov}")

        if uninstrumented:
            print(f"\nUninstrumented files added:")
            for filepath in sorted(uninstrumented.keys()):
                print(f"  - {filepath}")

        return 0

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    finally:
        os.chdir(original_cwd)


if __name__ == '__main__':
    sys.exit(main())
