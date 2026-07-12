#!/usr/bin/env python3
"""coverage_scorecard.py - Emit the 3-project coverage scorecard.

Renders a 4-line coverage scorecard from the cached SonarCloud ``sonar-measures.json``
files (one per project — vehicle-spy, vehicle-spy-ios, vehicle-spy-esp32):

    project              lines_to_cover  coverage%  uncovered
    vehicle-spy                    5647      79.9%       1134
    vehicle-spy-ios                1285      31.5%        880
    vehicle-spy-esp32              4176      23.1%       3211
    ------------------------------------------------------------
    TOTAL (sum, blended)          11108      53.0%       5225
    TOTAL (avg of 3 %)                        44.8%

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

TRUST (split state): the PER-PROJECT % is now trustworthy — Phase 3 landed, so
sonar's line-count == lcov's per project (no hidden denominator gaming). The
TOTAL is still directional, not certified: it tightens once the firmware
demarcation inversion config + the 3 vanilla header fixes land and are
re-scanned (firmware/vanilla must be owned by exactly one project, not
double-counted). The scorecard prints a banner reflecting this split unless
``--trustworthy`` is passed (set that flag only once the inversion config has
landed and been re-scanned, so the TOTAL is certified too).

Reads (default paths, override via repeated ``--measures PATH=LABEL``):
  build-sonar/sonar-measures.json     -> vehicle-spy
  build-ios/sonar-measures.json       -> vehicle-spy-ios
  build-firmware/sonar-measures.json  -> vehicle-spy-esp32
"""
import argparse
import json
import os
import sys
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


def render(rows: List[Tuple[str, int, float, int]], trustworthy: bool, use_colour: bool) -> str:
    """Render the scorecard text from (label, lines_to_cover, coverage%, uncovered) rows."""
    out = []
    if not trustworthy:
        out.append('=== COVERAGE SCORECARD (per-project % HONEST [Phase 3 landed]; TOTAL directional [firmware inversion DEFERRED pending user a/b/c]) ===')
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
    return '\n'.join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description='Emit the 3-project coverage scorecard.')
    parser.add_argument('--measures', action='append', default=None,
                        metavar='PATH=LABEL',
                        help='measures JSON path + label (repeatable). '
                             'Defaults to the 3 cached sonar-measures.json files.')
    parser.add_argument('--trustworthy', action='store_true',
                        help='omit the split-state banner (set only after the inversion config lands + re-scan, so the TOTAL is certified too).')
    parser.add_argument('--no-colour', action='store_true',
                        help='disable ANSI colour (for piped/logged output).')
    args = parser.parse_args()

    use_colour = (not args.no_colour) and sys.stdout.isatty()
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    if args.measures:
        specs = []
        for spec in args.measures:
            if '=' not in spec:
                print(f"error: --measures '{spec}' must be PATH=LABEL", file=sys.stderr)
                return 1
            path, label = spec.rsplit('=', 1)
            specs.append((path, label))
    else:
        specs = [
            (os.path.join(repo_root, 'build-sonar', 'sonar-measures.json'), 'vehicle-spy'),
            (os.path.join(repo_root, 'build-ios', 'sonar-measures.json'), 'vehicle-spy-ios'),
            (os.path.join(repo_root, 'build-firmware', 'sonar-measures.json'), 'vehicle-spy-esp32'),
        ]

    rows = []
    missing = []
    for path, label in specs:
        m = _load_measures(path)
        if m is None:
            missing.append(label)
            continue
        rows.append((label, m['lines_to_cover'], m['coverage'], m['uncovered_lines']))

    if missing:
        for label in missing:
            print(f"warning: no measures for {label} (run the corresponding `make sonar-scan-*`)", file=sys.stderr)

    print(render(rows, args.trustworthy, use_colour))
    return 0 if rows else 1


if __name__ == '__main__':
    sys.exit(main())
