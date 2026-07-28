#!/usr/bin/env python3
"""
lcov_add_uninstrumented.py - Add synthetic lcov entries for uninstrumented source files.

This script walks the source directories listed on the command line (one or more
``--src-dir`` args, each a path relative to ``--root``), finds every ``.cpp`` and
``.ino`` file under them, and appends a synthetic 0%-coverage lcov entry for any
that are NOT already present in the input lcov report. Declaration-only ``.h``
headers are deliberately NOT injected (a declaration-only header has zero
executable lines, so it contributes 0 to both the numerator and denominator of
coverage — the correct metric definition, not metric-gaming). Headers that DO
contain inline executable code already appear in the input lcov report straight
from llvm-cov and are left untouched.

WHY: ``llvm-cov export`` OMITS source files whose functions no test calls (zero
executed regions) — they get no SF record, even though compiled into the
instrumented binary. The result is a lcov file-list that is a strict subset of
sonar.sources, so sonar's lines_to_cover denominator counts files lcov has no
record for → coverage % is computed against a denominator larger than lcov's
world, and the never-called files are HIDDEN (not shown as 0%, just absent).
Appending synthetic 0% entries makes the file-lists agree: every sonar.sources
file appears in lcov, so absent==0%-not-hidden and sonar line-count == lcov
line-count per project (the single-source-of-truth / "same count" bar).

This is the durable fix for the metric-gaming trap: the alternative (derive
sonar.coverage.exclusions from lcov's SF records) would DROP never-called files
from sonar's denominator entirely, making coverage % rise while hiding untested
code. Adding them as 0% surfaces the gap honestly instead.

Usage:
    # Firmware (backward-compatible default: firmware/can-bridge + firmware/vanilla)
    python3 scripts/lcov_add_uninstrumented.py lcov.info lcov.info.new --root /path

    # vehicle-spy C++ core — explicit src dirs
    python3 scripts/lcov_add_uninstrumented.py lcov.info lcov.info.new \\
        --root /path --src-dir src --src-dir include --src-dir firmware/vanilla
"""

import sys
import os
import re
import argparse
import fnmatch
from pathlib import Path
from typing import Set, Dict, List


def count_code_lines(filepath: str) -> List[int]:
    """
    Return the physical line numbers (1-based) of every non-blank, non-comment
    line in ``filepath``.

    The returned line numbers are the REAL source line numbers — they are
    emitted verbatim as lcov ``DA:<physLine>,0`` entries so the synthetic record
    maps onto the actual source layout (a reader can see which statement lines
    are uncovered) rather than emitting ordinal ``DA:1..N`` that mislabels
    ``#pragma once`` / ``#include`` / ``namespace`` / ``class {`` as executable.

    Simple heuristic: lines that are not empty and not pure comments.
    """
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception:
        return []

    code_line_nums: List[int] = []
    in_block_comment = False

    for idx, line in enumerate(lines, start=1):
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

        # Record as code line
        code_line_nums.append(idx)

    return code_line_nums


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


def matches_exclusion(rel_path: str, excludes: List[str]) -> bool:
    """
    True if rel_path (repo-relative, forward slashes) matches any sonar-style
    exclusion pattern. Handles exact paths (src/main.cpp), basename globs
    (**/OtaPublicKey.h), and path globs (firmware/can-bridge/**, build*/**).
    Sonar's `**` matches any number of path segments; fnmatch treats `*` as
    greedy across `/`, so we normalise `**` -> `*` and also do a basename check.
    """
    if not excludes:
        return False
    base = rel_path.rsplit('/', 1)[-1]
    for pat in excludes:
        # normalise sonar ** to fnmatch *
        p = pat.replace('**/', '').replace('/**', '').replace('**', '*')
        if fnmatch.fnmatch(rel_path, p) or fnmatch.fnmatch(base, p):
            return True
    return False


def find_source_files(src_dirs: List[str], excludes: List[str] = None) -> Dict[str, List[int]]:
    """
    Find all source files in source directories and record their code line
    numbers. Skips files matching any exclusion pattern (mirrors sonar.exclusions
    so the lcov completion stays in lock-step with sonar's effective source set
    — no drift in either direction). Returns dict: filepath -> list[line_no].

    Declaration-only ``.h`` headers are NOT injected. Source-based coverage
    (llvm-cov) already attributes header coverage correctly for headers that
    contain inline executable code — those get a real ``SF:`` record from
    llvm-cov and are therefore excluded by ``extract_lcov_files`` below. A
    declaration-only header has zero executable lines, so it must contribute 0
    to BOTH the numerator and denominator of coverage — that is the correct
    metric definition, not hiding untested code (there is no executable logic
    to hide). Injecting a synthetic 0% record for a declaration-only header
    only inflates the denominator with phantom "uncovered executable" lines
    (``#pragma once`` / ``#include`` / ``class{`` marked executable) and
    depressed the headline. Genuinely-uncovered ``.cpp``/``.ino`` files keep
    their honest-uncovered injection (see ``generate_lcov_entry``).
    """
    excludes = excludes or []
    source_files = {}

    for src_dir in src_dirs:
        if not os.path.isdir(src_dir):
            continue

        for root, dirs, files in os.walk(src_dir):
            # Skip build directories
            if 'build' in root or 'verify' in root:
                continue

            for file in files:
                # Inject synthetic 0% only for .cpp / .ino. Declaration-only
                # headers (.h) are skipped — see docstring.
                if file.endswith(('.cpp', '.ino')):
                    filepath = os.path.join(root, file)
                    # Symlinks are path aliases, not canonical source. The esp32
                    # firmware/can-bridge/ tree has 26 symlinks -> ../vanilla/;
                    # walking both would add a synthetic entry for each alias
                    # AND its canonical target, double-counting every line.
                    # Skip the symlink so only the canonical vanilla path counts.
                    if os.path.islink(filepath):
                        continue
                    # Apply sonar-style exclusions (Phase 3 Part 2: keep lcov
                    # completion == sonar.sources effective set).
                    rel = os.path.relpath(filepath).replace(os.sep, '/')
                    if matches_exclusion(rel, excludes):
                        continue
                    line_nums = count_code_lines(filepath)
                    if line_nums:
                        source_files[filepath] = line_nums

    return source_files


def generate_lcov_entry(filepath: str, line_nums: List[int]) -> str:
    """Generate synthetic lcov entry for an uninstrumented file.

    ``line_nums`` are the REAL 1-based source line numbers of the file's
    non-blank, non-comment lines. Each is emitted as ``DA:<physLine>,0`` so the
    synthetic 0% record maps onto the actual source layout. ``LF:`` is the count
    of those lines; ``LH:`` is 0 (nothing covered, by construction).
    """
    entry = [f'SF:{filepath}']

    # Add DA:line,0 for each real source code line (0 coverage)
    for line_num in line_nums:
        entry.append(f'DA:{line_num},0')

    # Summary fields (all zeros for uninstrumented)
    entry.append('LF:{}'.format(len(line_nums)))  # Lines Found
    entry.append('LH:0')  # Lines Hit
    entry.append('end_of_record')

    return '\n'.join(entry)


def main():
    parser = argparse.ArgumentParser(
        description='Add synthetic 0%-coverage lcov entries for source files not already in the lcov report.')
    parser.add_argument('input_lcov', help='input lcov.info path')
    parser.add_argument('output_lcov', help='output lcov.info path')
    parser.add_argument('--root', default=os.getcwd(),
                        help='project root for resolving relative src-dirs (default: cwd)')
    parser.add_argument('--src-dir', action='append', dest='src_dirs',
                        default=None,
                        help='source dir to scan (relative to --root). Repeatable. '
                             'Defaults to firmware/can-bridge + firmware/vanilla for backward compat.')
    parser.add_argument('--exclude', action='append', dest='excludes',
                        default=None,
                        help='sonar-style exclusion pattern to skip (e.g. src/main.cpp, '
                             '**/OtaPublicKey.h, firmware/can-bridge/**). Repeatable. '
                             'Mirrors sonar.exclusions so lcov completion == sonar.sources effective set.')
    args = parser.parse_args()

    input_lcov = args.input_lcov
    output_lcov = args.output_lcov
    project_root = args.root
    # Backward-compatible default: the firmware recipe (Makefile coverage-firmware)
    # invokes with no --src-dir, expecting the firmware can-bridge + vanilla scan.
    src_dirs = args.src_dirs if args.src_dirs else ['firmware/can-bridge', 'firmware/vanilla']
    excludes = args.excludes or []

    # Change to project root for relative path calculations
    original_cwd = os.getcwd()
    os.chdir(project_root)

    try:
        # Extract files already in lcov
        existing_files = extract_lcov_files(input_lcov)

        source_files = find_source_files(src_dirs, excludes)

        # Find uninstrumented files (in source but not in lcov)
        uninstrumented = {}
        for filepath, line_nums in source_files.items():
            # Use relative path for comparison (lcov uses absolute paths)
            abs_path = os.path.abspath(filepath)
            if abs_path not in existing_files:
                uninstrumented[abs_path] = line_nums

        # Read original lcov
        with open(input_lcov, 'r') as f:
            lcov_content = f.read()

        # Append synthetic entries for uninstrumented files
        output = lcov_content
        if uninstrumented:
            output += '\n'
            for filepath, line_nums in sorted(uninstrumented.items()):
                output += generate_lcov_entry(filepath, line_nums) + '\n'

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
