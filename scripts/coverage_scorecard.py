#!/usr/bin/env python3
"""coverage_scorecard.py - Emit the 3-project coverage scorecard.

Renders a 4-line coverage scorecard. By DEFAULT the numbers come from the LIVE
SonarCloud API (via the shared ``sonar_live`` module — the single source of
truth), so staleness is architecturally impossible: there is no persistent
on-disk cache read. ``--measures PATH=LABEL`` is an explicit OFFLINE fallback
for CI / no-token runs.

PURPOSE (north star): the ``lines_to_cover`` DISTRIBUTION shows WHERE code lives.
The goal is to move code OUT of the ESP32 .ino veneer INTO mockable, unit-tested
vehicle-spy cpp. As extraction proceeds, vehicle-spy's line count RISES (good —
testable cpp grows), esp32's .ino SHRINKS, and total coverage climbs. This makes
extraction progress visible in the headline numbers.

TWO TOTALS, both honest, both emitted with clear labels:
  - ``TOTAL (sum, blended)`` = 100 * covered_lines / total_lines, weighted by size.
    The true blended coverage ("how much of our code is tested").
  - ``TOTAL (avg of 3 %)``   = simple mean of the 3 project percentages, unweighted.
    They diverge when the largest project also has the highest coverage.

TRUST (split state): the PER-PROJECT % is trustworthy — sonar's line-count
matches the coverage XML per project (no hidden denominator gaming; the single-
source coverage-manifest.toml drives all 3 properties + the Makefile). The
TOTAL is directional rather than certified for one reason only: the .mm files
are a DECLARED GAP (decision B) — SonarCloud has no clean Obj-C++ analyzer, so
4 .mm are count-zero-everywhere and excluded from every denominator. They are
surfaced as a named "deferred .mm" line below the table (read from
coverage-manifest.toml's [deferred.mm] block) so the gap is tracked, not buried.
The scorecard prints a banner reflecting this split unless ``--trustworthy`` is
passed (set that flag only if the .mm gap is ever closed via user decision A or C
and re-scanned, so the TOTAL is certified too).

Default data source: LIVE SonarCloud API for the 3 project keys
(danieljsinclair_vehicle-spy, -ios, -esp32). Override to local files via
repeated ``--measures PATH=LABEL`` (offline / CI fallback).
"""
import argparse
import json
import os
import sys
import tomllib
from typing import List, Optional, Tuple


def _load_measures(path: str) -> Optional[dict]:
    """Return {'coverage': float, 'lines_to_cover': int, 'uncovered_lines': int} or None."""
    try:
        with open(path, 'r') as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None
    measures = data.get('component', {}).get('measures', [])
    out = {m['metric']: m['value'] for m in measures}
    if not {'coverage', 'lines_to_cover', 'uncovered_lines'} <= out.keys():
        return None
    return {
        'coverage': float(out['coverage']),
        'lines_to_cover': int(float(out['lines_to_cover'])),
        'uncovered_lines': int(float(out['uncovered_lines'])),
    }


def _load_deferred_mm(manifest_path: str) -> Optional[dict]:
    """Read the [deferred.mm] declared gap from coverage-manifest.toml.

    Returns {'status': str, 'resolution': str, 'files': [ {path, owner, lines,
    measure}... ]} or None if the manifest/section is absent. ``lines`` is the
    int count when known; ``measure`` is 'coverable' (from a coverage XML) or
    'physical' (wc -l upper bound — file never indexed, coverable unmeasured).
    Surfaces the count-ZERO-everywhere .mm so the gap is tracked, not buried.
    """
    try:
        with open(manifest_path, 'rb') as f:
            data = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError):
        return None
    dm = data.get('deferred', {}).get('mm', {})
    if not dm:
        return None
    files = []
    for fi in dm.get('files', []):
        try:
            lines = int(fi.get('lines'))
        except (TypeError, ValueError):
            continue
        files.append({
            'path': fi.get('path', '?'),
            'owner': fi.get('owner', '?'),
            'lines': lines,
            'measure': fi.get('lines_measure', 'unknown'),
        })
    return {
        'status': dm.get('status', 'deferred'),
        'resolution': dm.get('resolution', ''),
        'files': files,
    }


def _glob_mm_files(repo_root: str) -> List[dict]:
    """Glob the repo for .mm files (the deferred-gap set, glob-derived).

    Returns a list of ``{path, rel, owner, lines}`` for every .mm under the
    repo (excluding build/derived dirs). ``owner`` is the SonarCloud project
    that owns the file (vehicle-spy for src/, vehicle-spy-ios for vehicle-sim-
    ios/) — used to attribute the gap. ``lines`` is the raw physical line count
    (wc -l). Glob-driven so a NEW .mm auto-appears in the warning list with no
    manifest edit (past/present/future .mm all caught by the same `**/*.mm`).
    """
    import subprocess
    try:
        out = subprocess.run(
            ['find', '.', '-name', '*.mm', '-type', 'f',
             '!', '-path', '*/build*/*', '!', '-path', '*/.git/*',
             '!', '-path', '*/DerivedData/*'],
            capture_output=True, text=True, cwd=repo_root, check=False,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    files = []
    for line in sorted(out.splitlines()):
        rel = line.lstrip('./')
        if not rel:
            continue
        abs_path = os.path.join(repo_root, rel)
        try:
            with open(abs_path, 'r', errors='replace') as fh:
                lines = sum(1 for _ in fh)
        except OSError:
            lines = 0
        # Owner attribution by directory scope (mirrors the manifest's source sets).
        if rel.startswith('vehicle-sim-ios/'):
            owner = 'vehicle-spy-ios'
        elif rel.startswith('src/') or rel.startswith('include/'):
            owner = 'vehicle-spy'
        else:
            owner = '?'
        files.append({'path': rel, 'owner': owner, 'lines': lines})
    return files


def _colour_for_pct(pct: float, use_colour: bool) -> str:
    """ANSI colour by coverage band: green>=80, yellow>=50, red<50."""
    if not use_colour:
        return ''
    if pct >= 80.0:
        return '\033[32m'   # green
    if pct >= 50.0:
        return '\033[33m'   # yellow
    return '\033[31m'       # red


RESET = '\033[0m'


def render(rows: List[Tuple[str, int, float, int]], trustworthy: bool, use_colour: bool,
           deferred_mm: Optional[dict] = None, glob_mm: Optional[List[dict]] = None) -> str:
    """Render the scorecard text from (label, lines_to_cover, coverage%, uncovered) rows.

    ``glob_mm`` (preferred) renders the .mm gap as YELLOW ACTIONABLE WARNINGS,
    glob-derived from the filesystem (``**/*.mm`` — auto-catches new .mm, no
    manifest edit). ``deferred_mm`` (legacy fallback) reads the static
    [deferred.mm] list. Both surface the count-zero-everywhere .mm so the gap is
    tracked as a persistent yellow warning, not buried. See [deferred.mm] +
    COVERAGE_MM_POLICY in coverage-manifest.toml.
    """
    yellow = '\033[33m' if use_colour else ''
    reset = '\033[0m' if use_colour else ''
    out = []
    if not trustworthy:
        out.append('=== COVERAGE SCORECARD (per-project % HONEST; TOTAL directional [.mm = declared gap, decision B]) ===')
    else:
        out.append('=== COVERAGE SCORECARD ===')
    out.append('')
    # Column layout: label 22, lines 12 right, pct 9 right, uncovered 10 right.
    header = f"{'project':22s} {'lines_to_cover':>15s} {'coverage%':>10s} {'uncovered':>10s}"
    out.append(header)
    total_lines = 0
    total_uncovered = 0
    pcts = []
    for label, ltc, pct, unc in rows:
        col = _colour_for_pct(pct, use_colour)
        out.append(f"{label:22s} {ltc:>15d} {col}{pct:>9.1f}%{RESET if col else ''} {unc:>10d}")
        total_lines += ltc
        total_uncovered += unc
        pcts.append(pct)
    out.append(f"{'-' * 22} {'-' * 15} {'-' * 10} {'-' * 10}")
    if total_lines:
        covered = total_lines - total_uncovered
        blended = 100.0 * covered / total_lines
        avg = sum(pcts) / len(pcts) if pcts else 0.0
        out.append(f"{'TOTAL (sum, blended)':22s} {total_lines:>15d} {blended:>9.1f}% {total_uncovered:>10d}")
        out.append(f"{'TOTAL (avg of 3 %)':22s} {'':>15s} {avg:>9.1f}% {'' if False else '':>10s}".rstrip())
    else:
        out.append('(no measures data — run `make sonar-scan sonar-scan-ios sonar-scan-esp32`)')
    out.append('')
    # Distribution callout — the north-star signal.
    if rows:
        shares = [(label, ltc) for label, ltc, _, _ in rows]
        callout = '  distribution: ' + ', '.join(f"{lbl} {ltc} ({100*ltc/total_lines:.0f}%)" for lbl, ltc in shares if total_lines)
        out.append(callout)
    # Declared .mm gap — YELLOW ACTIONABLE WARNINGS (glob-derived, persists
    # every build until closed). cfamily cannot index .mm on any tier, so these
    # .mm are count-zero everywhere; they are excluded from the headline
    # (two-sided **/*.mm) for sonar==lcov honesty, NOT hidden — surfaced here as
    # the migration roadmap. Glob-driven: a new .mm auto-appears, no manifest edit.
    mm_block = render_deferred_mm(deferred_mm, glob_mm, use_colour)
    if mm_block:
        out.append(mm_block)
    return '\n'.join(out)


def render_deferred_mm(deferred_mm: Optional[dict], glob_mm: Optional[List[dict]],
                       use_colour: bool) -> str:
    """Render the deferred-.mm YELLOW ACTIONABLE WARNINGS block.

    Shared by the full scorecard (``render``) and ``--mm-only`` (the headline
    summary appends this via ``make summary`` so the .mm gap surfaces in the
    glance-target dashboard, not only under ``make coverage-scorecard``).
    Returns the block (possibly empty) — caller decides where to splice it.
    """
    yellow = '\033[33m' if use_colour else ''
    reset = '\033[0m' if use_colour else ''
    mm_files = glob_mm if glob_mm is not None else (
        deferred_mm.get('files', []) if deferred_mm else []
    )
    if not mm_files:
        return ''
    total_mm_lines = sum(f.get('lines', 0) for f in mm_files)
    lines = []
    lines.append('')
    lines.append(f"{yellow}  ⚠ DEFERRED .mm — {len(mm_files)} file(s), {total_mm_lines} physical lines, "
                 f"count-ZERO in coverage (cfamily cannot index .mm). ACTION: migrate each to thin "
                 f"Obj-C shell + .cpp core (PIMPL) so the cpp counts in vehicle-spy.{reset}")
    for f in mm_files:
        owner = f.get('owner', '?')
        flines = f.get('lines', 0)
        path = f.get('path', '?')
        lines.append(f"{yellow}    [{owner:14s}] {flines:>5d} lines  {path}  -> migrate to .cpp{reset}")
    lines.append(f"{yellow}  (persists every build until each .mm is migrated; tracked, not suppressed){reset}")
    return '\n'.join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description='Emit the 3-project coverage scorecard.')
    parser.add_argument('--measures', action='append', default=None,
                        metavar='PATH=LABEL',
                        help='OFFLINE FALLBACK: measures JSON path + label (repeatable). '
                             'By default the scorecard fetches the LIVE SonarCloud API '
                             '(impossible to be stale); this flag reads local files '
                             'instead for CI / no-token runs.')
    parser.add_argument('--trustworthy', action='store_true',
                        help='omit the split-state banner (set only after the inversion config lands + re-scan, so the TOTAL is certified too).')
    parser.add_argument('--no-colour', action='store_true',
                        help='disable ANSI colour (for piped/logged output).')
    parser.add_argument('--colour', action='store_true',
                        help='FORCE ANSI colour even when stdout is not a TTY. '
                             'Use when the output is written to a file that is '
                             'later shown in a terminal (e.g. make summary '
                             'redirects --mm-only into build-sonar/summary.txt, '
                             'which is then `cat`-ed to the terminal). Without '
                             'this flag colour follows isatty(), so the .mm '
                             'YELLOW warnings would be stripped on the redirect.')
    parser.add_argument('--mm-only', action='store_true',
                        help='emit ONLY the deferred-.mm YELLOW warnings block '
                             '(used by `make summary` to surface the .mm gap in '
                             'the headline dashboard, not just the scorecard).')
    args = parser.parse_args()

    # --colour forces ON (overrides isatty); --no-colour forces OFF. The default
    # follows the TTY so `make coverage-scorecard` in a real terminal is coloured
    # while CI logs (no TTY, or --no-colour) stay plain.
    if args.no_colour:
        use_colour = False
    elif args.colour:
        use_colour = True
    else:
        use_colour = sys.stdout.isatty()
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    # --mm-only: short-circuit — glob the .mm and print just the deferred block.
    if args.mm_only:
        glob_mm = _glob_mm_files(repo_root)
        manifest_path = os.path.join(repo_root, 'coverage-manifest.toml')
        block = render_deferred_mm(_load_deferred_mm(manifest_path), glob_mm, use_colour)
        if block:
            print(block)
        return 0

    # The 3 SonarCloud project keys (fixed identifiers, not cache paths). LIVE
    # by default -> staleness is architecturally impossible (no persistent state
    # is read). --measures PATH=LABEL is the explicit offline escape hatch.
    DEFAULT_PROJECTS = [
        ('danieljsinclair_vehicle-spy', 'vehicle-spy'),
        ('danieljsinclair_vehicle-spy-ios', 'vehicle-spy-ios'),
        ('danieljsinclair_vehicle-spy-esp32', 'vehicle-spy-esp32'),
    ]

    rows = []
    missing = []
    if args.measures:
        # Offline fallback: read local measures files (CI / no token).
        for spec in args.measures:
            if '=' not in spec:
                print(f"error: --measures '{spec}' must be PATH=LABEL", file=sys.stderr)
                return 1
            path, label = spec.rsplit('=', 1)
            m = _load_measures(path)
            if m is None:
                missing.append(label)
                continue
            rows.append((label, m['lines_to_cover'], m['coverage'], m['uncovered_lines']))
    else:
        # Default: LIVE fetch per project (single source of truth).
        from sonar_live import fetch_measures
        for project_key, label in DEFAULT_PROJECTS:
            m = fetch_measures(project_key)
            if m is None:
                missing.append(label)
                continue
            rows.append((label, m['lines_to_cover'], m['coverage'], m['uncovered_lines']))

    if missing:
        for label in missing:
            print(f"warning: no live measures for {label} (set SONAR_TOKEN_ES, "
                  f"or pass --measures PATH=LABEL for offline)", file=sys.stderr)

    manifest_path = os.path.join(repo_root, 'coverage-manifest.toml')
    deferred_mm = _load_deferred_mm(manifest_path)
    glob_mm = _glob_mm_files(repo_root)
    print(render(rows, args.trustworthy, use_colour, deferred_mm, glob_mm))
    return 0 if rows else 1


if __name__ == '__main__':
    sys.exit(main())
