#!/usr/bin/env python3
"""Emit a single compact, coloured HEADLINE line per SonarCloud project.

This is the END-OF-MAKE summary (the "russian doll" headline). The
``summary`` Makefile target calls this script ONCE PER PROJECT (three
invocations: vehicle-spy, vehicle-spy-ios, vehicle-spy-esp32). Each
invocation emits one scannable row; the fixed column widths make the three
lines align vertically:

    [vehicle-spy]         tests: PASS <passed>/<total> | cov: <pct>% | sonar: open <O> / total <T> (<reason>)
    [vehicle-spy-ios]     tests: PASS <passed>/<total> | cov: <pct>% | sonar: open <O> / total <T> (<reason>)
    [vehicle-spy-esp32] tests: N/A                  | cov: N/A     | sonar: open <O> / total <T> (<reason>)

Fields for which no data exists are OMITTED gracefully (never fabricated, never
crashed). Colours are EMIT DELIBERATELY here (plain numbers are extracted from
the report files and re-emitted with ANSI) -- we do NOT grep-and-preserve
source ANSI because grep mangles colours. Controlling emission gives one
consistent palette and dodges that entirely.

Colour rules (matching engine-sim-bridge/build_summary.py):

    tests       : GREEN + "PASS" prefix if all passed; RED + "FAIL" prefix if any failed.
    sonar       : RED if a BLOCKER is present, YELLOW if open>0 with no blocker,
                  GREEN if 0 open (clean). A short reason suffix (GREY) names
                  the tier: "(N blocker)" / "(no blocker)" / "(clean)" /
                  "(open-only)" when the total is open-only (no removed-facet).

DATA SOURCES -- plain numbers grepped from existing report files:

    Tests pass/fail
        vehicle-spy's C++ suite runs via ctest over a gtest binary, teed to
        ``build-native/test-report.txt`` (Makefile line ~103). The ctest
        summary line ``N% tests passed, M tests failed out of N`` counts ctest
        CASES (one per gtest binary -- i.e. "out of 1"), NOT gtest tests, so it
        is useless for the headline. This script therefore PREFERS the gtest
        ``[  PASSED  ] N tests`` / ``[  FAILED  ] N tests, M of which ...``
        lines the binary prints, which give the TRUE per-test count. It falls
        back to the ctest summary / per-test markers only when gtest lines are
        absent (e.g. a pure-ctest repo reusing this helper).

        The iOS suite (vehicle-spy-ios) runs via ``xcodebuild test`` (no
        greppable ctest log); its counts come from the .xcresult bundle's
        ResultMetrics via ``--xcresult-glob`` (ported from engine-sim-bridge's
        build_summary). The firmware project has no tests (N/A).

    Coverage
        C++ core coverage (vehicle-spy) from the lcov export
        (``build-cov/lcov.info``); iOS coverage (vehicle-spy-ios) from the
        xccov export (``build-ios/coverage.json``). Each is read via
        ``--local-cov`` + ``--local-type lcov|xccov``. The preferred source is
        the cached SonarCloud ``sonar-measures.json`` (``--cov-measures``); local
        files are the fallback when no token/report exists. The firmware
        project has no coverage (no firmware unit tests).

    Sonar open/total
        The cached ``sonar-report.json`` (the ``/api/issues/search?statuses=OPEN``
        response the sonar-summary target curls). ``open`` is derived from the
        ``impactSeverities`` facet when present (the dashboard's own
        server-side count), else from the per-issue impacts/legacy severity.
        ``total`` is ``open + removed``; ``removed`` comes from a separate
        removed-facet report (``--removed-facet``). When that file is absent
        ``total`` falls back to ``open`` and a GREY ``(open-only)`` marker
        notes the fallback.

The Makefile's ``summary`` target invokes this script three times: once for
``vehicle-spy`` (C++ core: tests + coverage + sonar), once for
``vehicle-spy-ios`` (iOS app: tests + coverage + sonar), and once for
``vehicle-spy-esp32`` (ESP32: sonar only, no tests/coverage). Each
invocation is independent and the fixed column widths make the lines align
vertically.

Usage (Makefile summary target calls this three times, once per project):

    build_summary.py --label "[vehicle-spy]" \\
        [--test-log PATH] \\
        [--sonar-report PATH] [--removed-facet PATH] \\
        [--cov-measures PATH]
        [--local-cov PATH --local-type lcov|xccov]  (repeatable)

    build_summary.py --label "[vehicle-spy-ios]" \\
        [--xcresult-glob GLOB] \\
        [--sonar-report PATH] [--removed-facet PATH] \\
        [--cov-measures PATH]
        [--local-cov PATH --local-type xccov]

    build_summary.py --label "[vehicle-spy-esp32]" \\
        [--sonar-report PATH] [--removed-facet PATH]

Exit codes: 0 always (a missing file / parse failure is reported in-line,
never a crash -- this is a display helper, not a build step).
"""
import argparse
import json
import os
import re
import sys

# ---------------------------------------------------------------------------
# ANSI palette (deliberate emission -- same codes as engine-sim-bridge so the
# whole report reads as one measurement view).
# ---------------------------------------------------------------------------
RED = '\033[31m'
ORANGE = '\033[38;5;208m'
YELLOW = '\033[33m'
GREEN = '\033[32m'
CYAN = '\033[36m'
GREY = '\033[90m'
BOLD = '\033[1m'
RESET = '\033[0m'

_ANSI_RE = re.compile(r'\x1b\[[0-9;]*m')

# gtest summary lines, e.g. "[  PASSED  ] 836 tests." /
# "[  FAILED  ] 2 tests, listed below:" (with or without ANSI).
_GTEST_PASSED_RE = re.compile(
    r'\[\s*PASSED\s*\]\s*(\d+)\s*tests?', re.IGNORECASE)
_GTEST_FAILED_RE = re.compile(
    r'\[\s*FAILED\s*\]\s*(\d+)\s*tests?', re.IGNORECASE)

# ctest summary line, e.g. "100% tests passed, 0 tests failed out of 1".
_SUMMARY_RE = re.compile(
    r'(\d+)%\s*tests?\s*passed,?\s*(\d+)\s*tests?\s*failed,?\s*out\s*of\s*(\d+)',
    re.IGNORECASE,
)


def _strip_ansi(text):
    """Remove ANSI escape sequences from ``text``."""
    return _ANSI_RE.sub('', text)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
def _parse_gtest_summary(text):
    """Return (passed, total) from gtest's PASSED/FAILED lines, or None.

    gtest prints two summary lines at the end of a run:

        [  PASSED  ] 836 tests.
        [  FAILED  ] 2 tests, listed below:

    These give the TRUE per-test count (unlike the ctest wrapper summary,
    which only sees the single gtest binary as "1 test"). ``total`` is
    ``passed + failed``. Returns None when neither line is present.
    """
    passed_match = _GTEST_PASSED_RE.search(text)
    if not passed_match:
        return None
    passed = int(passed_match.group(1))
    failed_match = _GTEST_FAILED_RE.search(text)
    failed = int(failed_match.group(1)) if failed_match else 0
    return passed, passed + failed


def parse_tests(log_path):
    """Return (passed, total) from a test log, or None if unavailable.

    PREFERS gtest's ``[ PASSED ] N tests`` / ``[ FAILED ] N tests`` lines
    (the true per-test count), falling back to the ctest summary line and
    finally to per-test ctest markers. Returns None when the log is absent
    or carries no test markers so the caller OMITS the tests field rather
    than fabricating a number.
    """
    if not log_path or not os.path.isfile(log_path):
        return None
    try:
        with open(log_path, errors='replace') as handle:
            raw = handle.read()
    except OSError:
        return None
    text = _strip_ansi(raw)

    # Preferred: gtest's own per-test summary lines (true count).
    gtest = _parse_gtest_summary(text)
    if gtest is not None:
        return gtest

    # Fallback A: ctest "N% tests passed, M tests failed out of N".
    passed_total = 0
    failed_total = 0
    saw_summary = False
    for match in _SUMMARY_RE.finditer(text):
        saw_summary = True
        pct, failed, total = (int(g) for g in match.groups())
        passed = total - failed
        passed_total += passed
        failed_total += failed
    if saw_summary:
        return passed_total, passed_total + failed_total

    # Fallback B: per-test markers in LastTest.log.
    passed = len(re.findall(r'^Test Passed\.', text, re.MULTILINE))
    failed = len(re.findall(r'^Test Failed\.', text, re.MULTILINE))
    if passed == 0 and failed == 0:
        return None
    return passed, passed + failed


# ---------------------------------------------------------------------------
# iOS tests (xcresult ResultMetrics -- ported from engine-sim-bridge)
# ---------------------------------------------------------------------------
def _shell_quote(value):
    """Single-quote ``value`` for safe shell interpolation (sh style)."""
    if not value:
        return "''"
    return "'" + value.replace("'", "'\\''") + "'"


def _xcresult_metric(metrics, key):
    """Read an int metric from an xcresult metrics dict, or None.

    xcresult JSON wraps each value as ``{"_type": {"_name": "Int"},
    "_value": "<n>"}``. Returns the int value or None when absent/unparseable.
    """
    if not isinstance(metrics, dict):
        return None
    node = metrics.get(key)
    if not isinstance(node, dict):
        return None
    try:
        return int(node.get('_value'))
    except (TypeError, ValueError):
        return None


def parse_xcresult_tests(glob_pattern):
    """Return (passed, total) from the NEWEST xcresult matching ``glob_pattern``.

    iOS app tests run via ``xcodebuild test`` (no greppable ctest log). The
    cleanest count source is the action's ResultMetrics in the .xcresult bundle
    xcodebuild writes: it carries ``testsCount`` (total) and ``testsFailedCount``
    (failures; absent or 0 when all pass). Returns None when no bundle exists,
    xcresulttool is unavailable, or the metrics are absent so the caller OMITS
    the tests field gracefully.
    """
    if not glob_pattern:
        return None
    import glob as _glob
    import tempfile
    bundles = sorted(_glob.glob(glob_pattern), key=os.path.getmtime)
    if not bundles:
        return None
    xcresult = bundles[-1]
    fd, dump_path = tempfile.mkstemp(prefix='build_summary_xcresult_', suffix='.json')
    os.close(fd)
    try:
        # --legacy gives the stable JSON shape with metrics at the top level.
        rc = os.system(
            'xcrun xcresulttool get --legacy --format json --path {} >{}'.format(
                _shell_quote(xcresult), _shell_quote(dump_path)))
        if rc != 0:
            return None
        with open(dump_path, errors='replace') as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    finally:
        try:
            os.unlink(dump_path)
        except OSError:
            pass
    metrics = data.get('metrics') or {}
    total = _xcresult_metric(metrics, 'testsCount')
    if total is None:
        for action in (data.get('actions', {}) or {}).get('_values', []) or []:
            am = (action.get('actionResult', {}) or {}).get('metrics', {})
            total = _xcresult_metric(am, 'testsCount')
            if total is not None:
                metrics = am
                break
    if total is None:
        return None
    failed = _xcresult_metric(metrics, 'testsFailedCount') or 0
    return total - failed, total


# ---------------------------------------------------------------------------
# Coverage (plumbing retained; vehicle-spy has no coverage source yet so the
# field omits gracefully until --cov-measures / --local-cov is wired in)
# ---------------------------------------------------------------------------
def _coverage_colour(pct):
    """Return the ANSI colour for a coverage percentage (matches coverage_block)."""
    if pct >= 80:
        return GREEN
    if pct >= 60:
        return CYAN
    if pct >= 40:
        return YELLOW
    if pct > 0:
        return ORANGE
    return RED


def _measures_coverage(path):
    """Read (covered, total, pct) from a cached sonar-measures.json, or None."""
    if not path or not os.path.isfile(path):
        return None
    try:
        with open(path) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    measures = {}
    for m in (data.get('component', {}) or {}).get('measures', []) or []:
        if m.get('metric'):
            measures[m['metric']] = m.get('value')
    if 'coverage' not in measures:
        return None
    try:
        pct = float(measures['coverage'])
    except (TypeError, ValueError):
        return None
    try:
        total = int(float(measures.get('lines_to_cover', 0) or 0))
        uncovered = int(float(measures.get('uncovered_lines', 0) or 0))
        covered = total - uncovered
    except (TypeError, ValueError):
        covered, total = None, None
    return covered, total, pct


def _lcov_coverage(path):
    """Aggregate (covered, total, pct) over /src/ lcov records (fallback: all)."""
    if not path or not os.path.isfile(path):
        return None
    records = []
    current = None
    lf = 0
    lh = 0
    try:
        with open(path) as handle:
            for line in handle:
                line = line.rstrip('\n')
                if line.startswith('SF:'):
                    current = line[3:]
                    lf = 0
                    lh = 0
                elif line.startswith('LF:'):
                    try:
                        lf = int(line[3:])
                    except ValueError:
                        lf = 0
                elif line.startswith('LH:'):
                    try:
                        lh = int(line[3:])
                    except ValueError:
                        lh = 0
                elif line.startswith('end_of_record') and current is not None:
                    records.append((current, lf, lh))
                    current = None
    except OSError:
        return None
    if not records:
        return None
    src = [r for r in records if '/src/' in r[0]]
    scope = src if src else records
    total = sum(r[1] for r in scope)
    hit = sum(r[2] for r in scope)
    pct = (100.0 * hit / total) if total else 0.0
    return hit, total, pct


def _xccov_coverage(path):
    """Aggregate (covered, total, pct) from an xccov JSON report.

    Sums ``coveredLines``/``executableLines`` across all targets (the xccov
    ``--report --json`` top-level aggregates + per-target). Returns None when
    the file is absent/unparseable or has no executable lines.
    """
    if not path or not os.path.isfile(path):
        return None
    try:
        with open(path) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None
    covered = 0
    total = 0
    # Per-target sums (the most reliable: top-level aggregates can be absent).
    for target in data.get('targets', []) or []:
        try:
            total += int(target.get('executableLines', 0) or 0)
            covered += int(target.get('coveredLines', 0) or 0)
        except (TypeError, ValueError):
            continue
    if total == 0:
        # Fall back to top-level aggregates if present.
        try:
            total = int(data.get('executableLines', 0) or 0)
            covered = int(data.get('coveredLines', 0) or 0)
        except (TypeError, ValueError):
            return None
    if total == 0:
        return None
    pct = 100.0 * covered / total
    return covered, total, pct


def local_coverage(path, local_type):
    """Return (covered, total, pct) for the local source, or None if unavailable."""
    if not path or local_type == 'none':
        return None
    if local_type == 'lcov':
        return _lcov_coverage(path)
    if local_type == 'xccov':
        return _xccov_coverage(path)
    return None


def coverage_for(cov_measures_path, local_cov_pairs):
    """Coverage from cached SonarCloud measures, else summed local sources.

    ``local_cov_pairs`` is a list of (path, type). When multiple local sources
    are given (C++ lcov + iOS xccov) their covered/total are SUMMED and the pct
    recomputed over the union. None when no source yields data.
    """
    cov = _measures_coverage(cov_measures_path)
    if cov is not None:
        return cov
    covered = 0
    total = 0
    found = False
    for path, local_type in local_cov_pairs:
        one = local_coverage(path, local_type)
        if one is None:
            continue
        found = True
        c, t, _ = one
        covered += c or 0
        total += t or 0
    if not found or total == 0:
        return None
    pct = 100.0 * covered / total
    return covered, total, pct


# ---------------------------------------------------------------------------
# Sonar issues
# ---------------------------------------------------------------------------
def _sonar_colour(blocker_count, open_count):
    """Sonar colour by a 3-tier rule.

    RED    = a BLOCKER severity is present.
    YELLOW = open issues but NO blocker.
    GREEN  = 0 open issues (clean).
    """
    if blocker_count:
        return RED
    if open_count > 0:
        return YELLOW
    return GREEN


def _sonar_reason(blocker_count, open_count):
    """Short reason suffix explaining the sonar tier."""
    if blocker_count:
        return '({} blocker)'.format(blocker_count)
    if open_count > 0:
        return '(no blocker)'
    return '(clean)'


def _impact_severity_facet(data):
    """Return the ``impactSeverities`` facet as a {severity: count} dict, or None."""
    for facet in data.get('facets') or []:
        if facet.get('property') == 'impactSeverities':
            return {v.get('val'): v.get('count', 0)
                    for v in facet.get('values') or []}
    return None


def _removed_count(removed_facet_path):
    """Return the REMOVED issue count from a cached removed-facet report, or 0."""
    if not removed_facet_path or not os.path.isfile(removed_facet_path):
        return 0
    try:
        with open(removed_facet_path) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return 0
    facet = _impact_severity_facet(data)
    return sum(facet.values()) if facet else 0


def parse_sonar(report_path, removed_facet_path=None):
    """Return (open, total, blocker_count, removed) from cached reports, or None.

    ``open`` + ``blocker_count`` come from the OPEN report's
    ``impactSeverities`` facet when present (the dashboard's own server-side
    count), else from per-issue impacts/legacy severity. ``total`` is
    ``open + removed``. ``removed`` is None when the removed-facet file is
    absent/unparseable (open-only fallback) so the emitter can annotate it.
    """
    if not report_path or not os.path.isfile(report_path):
        return None
    try:
        with open(report_path) as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return None

    facet = _impact_severity_facet(data)
    if facet is not None:
        open_count = sum(facet.values())
        blocker_count = facet.get('BLOCKER', 0) or 0
    else:
        issues = data.get('issues') or []
        open_count = len(issues)
        blocker_count = 0
        for issue in issues:
            impacts = issue.get('impacts') or []
            issue_is_blocker = any(
                isinstance(imp, dict) and imp.get('severity') == 'BLOCKER'
                for imp in impacts)
            if not issue_is_blocker and issue.get('severity') == 'BLOCKER':
                issue_is_blocker = True
            if issue_is_blocker:
                blocker_count += 1

    removed_present = bool(removed_facet_path) and os.path.isfile(
        removed_facet_path or '')
    removed = _removed_count(removed_facet_path) if removed_present else None
    effective_removed = removed if removed is not None else 0
    total = open_count + effective_removed
    return open_count, total, blocker_count, removed


# ---------------------------------------------------------------------------
# Line emission
# ---------------------------------------------------------------------------
# Fixed column widths so multi-component lines align vertically (ANSI codes
# are ZERO display-width, so padding is computed on the VISIBLE text via
# pad_visible). Sized for the widest realistic value per column.
COL_WIDTH_LABEL = 19
COL_WIDTH_TESTS = 18
COL_WIDTH_COV = 20
COL_WIDTH_SONAR = 39


def _visible_len(text):
    """Return the display length of ``text`` (ANSI escape codes count as 0)."""
    return len(_strip_ansi(text))


def pad_visible(text, width):
    """Right-pad ``text`` with plain spaces so its VISIBLE length is ``width``.

    The pad is appended AFTER the text (outside any ANSI span) as plain spaces,
    so it never inherits a colour and never throws off alignment. When the text
    is already wider than ``width`` it is returned unchanged (no truncation).
    """
    pad = width - _visible_len(text)
    return text + (' ' * pad if pad > 0 else '')


def _separator():
    """Return the plain `` | `` separator (no ANSI, visible width 3)."""
    return ' {}|{} '.format(GREY, RESET)


def emit_line(label, tests, cov, sonar):
    """Print the single coloured headline line for a SonarCloud project.

    ``tests`` is (passed, total) or None; ``cov`` is (covered, total, pct) or
    None; ``sonar`` is (open, total, blocker_count, removed) or None. Missing
    fields are OMITTED so the line never prints garbage.

    Columns are positioned ABSOLUTELY: each field is padded to its column
    width, so the sonar column starts at the same character position whether
    or not tests/cov are present. This keeps the three (or N) headline lines
    vertically aligned regardless of which fields each project has.
    """
    sep = _separator()
    segments = []

    # tests column (always reserve width so subsequent columns align)
    if tests is not None:
        passed, total = tests
        failed = total - passed
        if failed > 0:
            tests_str = '{}tests: FAIL ({}/{} passed, {} failed){}'.format(
                RED, passed, total, failed, RESET)
        else:
            tests_str = '{}tests: PASS {}/{}{}'.format(
                GREEN, passed, total, RESET)
        segments.append(pad_visible(tests_str, COL_WIDTH_TESTS))
    else:
        segments.append(' ' * COL_WIDTH_TESTS)

    # cov column (always reserve width so sonar column aligns)
    if cov is not None:
        covered, total, pct = cov
        colour = _coverage_colour(pct)
        if covered is not None and total:
            cov_str = '{}cov: {:.1f}%{} {}{}/{}{}'.format(
                colour, pct, RESET, GREY, covered, total, RESET)
        else:
            cov_str = '{}cov: {:.1f}%{}'.format(colour, pct, RESET)
        segments.append(pad_visible(cov_str, COL_WIDTH_COV))
    else:
        segments.append(' ' * COL_WIDTH_COV)

    # sonar column (always present for a scanned project)
    if sonar is not None:
        open_count, total, blocker_count, removed = sonar
        colour = _sonar_colour(blocker_count, open_count)
        reason = _sonar_reason(blocker_count, open_count)
        if removed is None:
            note = '(open-only)'
        else:
            note = reason
        sonar_str = '{}sonar: open {} / total {}{} {}{}{}'.format(
            colour, open_count, total, RESET, GREY, note, RESET)
        segments.append(pad_visible(sonar_str, COL_WIDTH_SONAR))

    body = sep.join(segments) if segments else \
        '{}(no summary data){}'.format(GREY, RESET)
    label_str = pad_visible('{}{}{}'.format(BOLD, label, RESET), COL_WIDTH_LABEL)
    print('{} {}'.format(label_str, body))


def main(argv=None):
    """Parse args and emit one headline line. Always exits 0 (display helper)."""
    p = argparse.ArgumentParser(
        description='Emit a compact end-of-make headline line for vehicle-spy / '
                    'vehicle-spy-ios / vehicle-spy-esp32.')
    p.add_argument('--label', required=True,
                   help='Repo label, e.g. "[vehicle-spy]"')
    p.add_argument('--test-log',
                   help='Test log (gtest PASSED/FAILED lines preferred, '
                        'ctest summary / per-test markers as fallback)')
    p.add_argument('--xcresult-glob',
                   help='Glob for the NEWEST .xcresult bundle (iOS tests); '
                        'testsCount/testsFailedCount are read via xcresulttool. '
                        'When given alongside --test-log the counts are SUMMED.')
    p.add_argument('--cov-measures',
                   help='Cached sonar-measures.json (preferred coverage source)')
    # Repeatable --local-cov / --local-type pairs (C++ lcov + iOS xccov).
    p.add_argument('--local-cov', action='append', default=[],
                   help='Local coverage file (repeatable). Pairs positionally '
                        'with --local-type.')
    p.add_argument('--local-type', action='append', default=[],
                   choices=('lcov', 'xccov', 'none'),
                   help='Local coverage source type for the Nth --local-cov '
                        '(repeatable; default lcov).')
    p.add_argument('--sonar-report',
                   help='Cached sonar-report.json (issues/search response)')
    p.add_argument('--removed-facet',
                   help='Cached resolutions=REMOVED&facets=impactSeverities report '
                        '(makes total = open + removed)')
    args = p.parse_args(argv)

    # Zip --local-cov/--local-type into (path, type) pairs. If fewer types than
    # paths were given, default the remainder to 'lcov' (the common case).
    types = list(args.local_type)
    while len(types) < len(args.local_cov):
        types.append('lcov')
    local_pairs = list(zip(args.local_cov, types))

    try:
        tests = parse_tests(args.test_log)
        xc_tests = parse_xcresult_tests(args.xcresult_glob)
        if tests is not None and xc_tests is not None:
            # Sum C++ (gtest log) + iOS (xcresult) into one row.
            tests = (tests[0] + xc_tests[0], tests[1] + xc_tests[1])
        elif xc_tests is not None:
            tests = xc_tests
        cov = coverage_for(args.cov_measures, local_pairs)
        sonar = parse_sonar(args.sonar_report, args.removed_facet)
        emit_line(args.label, tests, cov, sonar)
    except Exception as exc:  # never crash a display target
        print('{}{} summary failed: {}{}'.format(RED, args.label, exc, RESET),
              file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
