#!/usr/bin/env python3
"""Convert an xccov JSON coverage report to SonarCloud's generic coverage XML.

xccov (``xcrun xccov view --report --json <xcresult>``) emits coverage at
*function* granularity: each function carries a ``lineNumber`` (start line),
``executionCount``, and aggregate ``coveredLines``/``executableLines``. It does
NOT emit a per-line coverage list.

SonarCloud's generic coverage format (``sonar.coverageReportPaths``) is defined
by ``sonar-generic-coverage.xsd`` and requires a ``<lineToCover>`` per line:

    <coverage version="1">
      <file path="relative/or/absolute/path">
        <lineToCover lineNumber="6" covered="true"/>
      </file>
    </coverage>

Because xccov lacks line-level data, this converter emits one ``<lineToCover``
per function, anchored at the function's start line, with ``covered`` derived
from ``executionCount > 0``. This yields function-level line coverage rather
than true statement-level coverage — an inherent limitation of the xccov source
data, documented here so the consumer understands the fidelity.

Paths are written relative to ``--project-root`` (defaults to the current
directory) so the report is portable across machines. Files whose absolute
``path`` cannot be made relative are written as-is (SonarCloud still accepts
absolute paths).

**Shared inclusion truth.** An xccov report for one project can sweep in
sources that belong to a *different* project — e.g. the iOS app build compiles
the C++ core (``src/``, ``include/``) into the app binary, so the iOS xccov
report contains 50+ C++ files whose real coverage lives in the vehicle-spy
lcov. Uploading those to the vehicle-spy-ios SonarCloud project (whose
``sonar.sources`` is iOS-only) means most report files match no indexed source
and the dashboard percentage is distorted by the intersection.

``--include-roots`` (repeatable, mirrors ``lcov_to_xml.py``'s ``--src-root``)
is the single source of truth for which source roots a coverage report may
claim. A file whose relativised path is not under one of the configured roots
is dropped. When no ``--include-roots`` is given, all files are kept (backward
compatibility), but every call site SHOULD pass it so the three projects
(vehicle-spy / -ios / -esp32) never cross-contaminate. The same roots MUST be
mirrored in the corresponding ``sonar-project*.properties``
``sonar.coverage.exclusions`` so the dashboard agrees with the report.

Usage:
    xccov_to_sonar.py [--input coverage.json] [--output coverage-sonar.xml]
                      [--project-root DIR] [--exclude-targets TARGET1,TARGET2]
                      [--include-roots vehicle-sim-ios]

Exit codes:
    0  success (report written, even if empty)
    1  I/O or parse error on the input
    2  invalid command-line usage
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from typing import Iterable, Optional


@dataclass(frozen=True)
class FileCoverage:
    """Coverage for a single source file in the xccov report."""

    path: str
    functions: tuple  # tuple[dict, ...]


def _relative_path(path: str, project_root: str) -> str:
    """Best-effort conversion of ``path`` to one relative to ``project_root``.

    Falls back to the original ``path`` if it cannot be made relative.
    """
    try:
        rel = os.path.relpath(path, project_root)
    except ValueError:
        # On Windows, relpath raises if the paths are on different drives.
        return path
    # Keep absolute paths that escape the project root (e.g. SDK headers) as-is
    # rather than introducing confusing ../ segments.
    if rel.startswith(".."):
        return path
    return rel


def _under_any_root(rel_path: str, include_roots: Iterable[str]) -> bool:
    """True if ``rel_path`` is under one of the configured include roots.

    An empty ``include_roots`` means "no filter" (keep everything) — backward
    compatibility for callers that do not pass ``--include-roots``. Every
    production call site SHOULD pass roots so coverage does not leak across
    SonarCloud projects.
    """
    roots = [r for r in include_roots if r]
    if not roots:
        return True
    return any(
        rel_path == root or rel_path.startswith(root + "/") for root in roots
    )


def _matches_any_glob(rel_path: str, globs: Iterable[str]) -> bool:
    """True if ``rel_path`` matches any fnmatch glob (e.g. ``**/*.mm``).

    SonarCloud-style ``**/`` is normalized to also match at the repo root and
    by basename (fnmatch treats ``*`` as one segment, so ``**/*.mm`` would not
    otherwise match a top-level ``foo.mm`` or a basename check).
    """
    for pat in globs:
        if not pat:
            continue
        if fnmatch.fnmatch(rel_path, pat):
            return True
        if pat.startswith("**/"):
            tail = pat[3:]
            if fnmatch.fnmatch(rel_path, tail):
                return True
            if fnmatch.fnmatch(os.path.basename(rel_path), tail):
                return True
    return False


def iter_file_coverage(
    report: dict,
    exclude_targets: Iterable[str],
    project_root: str,
    include_roots: Iterable[str],
    exclude_globs: Iterable[str] = (),
) -> Iterable[FileCoverage]:
    """Yield :class:`FileCoverage` for each file in each non-excluded target.

    Files whose relativised path is not under one of ``include_roots`` are
    dropped (the shared inclusion truth — see module docstring). Files matching
    any ``exclude_globs`` pattern are dropped (the two-sided .mm headline
    exclusion — mirrors sonar.coverage.exclusions so sonar==lcov matches).
    """
    excluded = {name for name in exclude_targets if name}
    roots = list(include_roots)
    globs = list(exclude_globs or [])
    for target in report.get("targets", []):
        if target.get("name") in excluded:
            continue
        for entry in target.get("files", []):
            path = entry.get("path") or entry.get("name", "")
            if not path:
                continue
            rel = _relative_path(path, project_root)
            if not _under_any_root(rel, roots):
                continue
            if globs and _matches_any_glob(rel, globs):
                continue
            yield FileCoverage(path=path, functions=tuple(entry.get("functions", [])))


def fetch_per_line_coverage(file_path: str, xcresult: str) -> Optional[list]:
    """Fetch true per-line coverage for ``file_path`` from an xcresult bundle.

    Uses ``xcrun xccov view --archive --file <path> --json <bundle>`` which
    emits one entry per source line with ``isExecutable`` and
    ``executionCount``. Returns the list of line entries, or ``None`` if the
    call fails (the caller falls back to function-level coverage for that
    file). The path MUST be absolute — xccov matches against the absolute
    paths recorded in the bundle.
    """
    if not os.path.isabs(file_path):
        file_path = os.path.abspath(file_path)
    try:
        result = subprocess.run(
            ["xcrun", "xccov", "view", "--archive",
             "--file", file_path, "--json", xcresult],
            capture_output=True, text=True, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0 or not result.stdout.strip():
        return None
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    # xccov returns { "<abs path>": [ {line, isExecutable, executionCount}, ... ] }
    if isinstance(data, dict) and data:
        return list(data.values())[0]
    return None


def build_coverage_xml(
    report: dict, project_root: str, exclude_targets: Iterable[str],
    include_roots: Iterable[str], xcresult: Optional[str] = None,
    exclude_globs: Iterable[str] = (),
) -> ET.Element:
    """Build the ``<coverage>`` XML element from an xccov report dict.

    When ``xcresult`` is given, emit TRUE per-line coverage (one
    ``<lineToCover`` per executable source line, ``covered`` from
    ``executionCount > 0``) by calling ``xcrun xccov view --archive --file``.
    This is the same fidelity as the C++ lcov path and makes SonarCloud agree
    with local xccov. When a per-file fetch fails, that file falls back to
    function-level coverage so a single unruly file does not abort the report.

    Without ``xcresult``, fall back to function-level coverage (one
    ``<lineToCover`` per function start line) — backward compatibility.
    """
    root = ET.Element("coverage", {"version": "1"})
    for file_cov in iter_file_coverage(
        report, exclude_targets, project_root, include_roots, exclude_globs
    ):
        rel = _relative_path(file_cov.path, project_root)
        file_el = ET.SubElement(root, "file", {"path": rel})
        emitted = 0
        if xcresult:
            lines = fetch_per_line_coverage(file_cov.path, xcresult)
            if lines:
                seen: set[int] = set()
                for entry in lines:
                    if not entry.get("isExecutable"):
                        continue
                    line = entry.get("line")
                    if line is None or line in seen:
                        continue
                    seen.add(line)
                    count = entry.get("executionCount", 0) or 0
                    covered = "true" if count > 0 else "false"
                    ET.SubElement(
                        file_el, "lineToCover",
                        {"lineNumber": str(line), "covered": covered},
                    )
                    emitted += 1
        if emitted:
            continue
        # Fallback: function-level coverage (or no data for this file).
        if not file_cov.functions:
            root.remove(file_el)
            continue
        seen_lines: set[int] = set()
        for fn in file_cov.functions:
            line = fn.get("lineNumber")
            if line is None or line in seen_lines:
                continue
            seen_lines.add(line)
            covered = "true" if (fn.get("executionCount", 0) or 0) > 0 else "false"
            ET.SubElement(
                file_el, "lineToCover", {"lineNumber": str(line), "covered": covered}
            )
    return root


def serialize(root: ET.Element) -> bytes:
    """Serialize an XML element with an XML declaration and pretty indentation."""
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def _parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert xccov JSON to SonarCloud generic coverage XML."
    )
    parser.add_argument(
        "--input",
        default="coverage.json",
        help="Path to the xccov JSON report (default: coverage.json).",
    )
    parser.add_argument(
        "--output",
        default="coverage-sonar.xml",
        help="Path to write the generic coverage XML (default: coverage-sonar.xml).",
    )
    parser.add_argument(
        "--project-root",
        default=os.getcwd(),
        help="Root used to relativise file paths (default: current directory).",
    )
    parser.add_argument(
        "--exclude-targets",
        default="",
        help="Comma-delimited xccov target names to exclude (e.g. test bundles).",
    )
    parser.add_argument(
        "--include-roots",
        action="append",
        default=[],
        help=(
            "Source root(s) the report may claim, relative to --project-root "
            "(repeatable). A file not under one of these roots is dropped, so "
            "coverage does not leak across SonarCloud projects. Mirrors "
            "lcov_to_xml.py --src-root; SHOULD be passed by every call site."
        ),
    )
    parser.add_argument(
        "--xcresult",
        default=None,
        help=(
            "Path to the .xcresult bundle. When given, emit TRUE per-line "
            "coverage (one <lineToCover> per executable line) via "
            "`xcrun xccov view --archive --file` — same fidelity as the C++ "
            "lcov path. Without it, fall back to function-level coverage."
        ),
    )
    parser.add_argument(
        "--exclude-glob",
        action="append",
        default=[],
        dest="exclude_globs",
        help=(
            "fnmatch glob(s) of files to drop from the HEADLINE XML "
            "(repeatable). The two-sided .mm headline exclusion: pass "
            "`--exclude-glob '**/*.mm'` so sonar==lcov matches (cfamily "
            "cannot index .mm, so a headline <file> entry for a .mm is dead "
            "weight). Mirrors sonar.coverage.exclusions on the SonarCloud "
            "side. The raw xccov report still carries them for inspection."
        ),
    )
    return parser.parse_args(argv)


def main(argv: Optional[list] = None) -> int:
    args = _parse_args(argv)
    exclude_targets = [s.strip() for s in args.exclude_targets.split(",")]
    exclude_globs = list(args.exclude_globs or [])

    try:
        with open(args.input, "r", encoding="utf-8") as handle:
            report = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: failed to read {args.input!r}: {exc}", file=sys.stderr)
        return 1

    root = build_coverage_xml(
        report, args.project_root, exclude_targets, args.include_roots,
        args.xcresult, exclude_globs,
    )
    payload = serialize(root)

    try:
        with open(args.output, "wb") as handle:
            handle.write(payload)
            handle.write(b"\n")
    except OSError as exc:
        print(f"error: failed to write {args.output!r}: {exc}", file=sys.stderr)
        return 1

    file_count = len(root.findall("file"))
    line_count = len(root.findall("file/lineToCover"))
    print(
        f"wrote {args.output}: {file_count} files, {line_count} coverable lines"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
