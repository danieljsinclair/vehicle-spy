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

Usage:
    xccov_to_sonar.py [--input coverage.json] [--output coverage-sonar.xml]
                      [--project-root DIR] [--exclude-targets TARGET1,TARGET2]

Exit codes:
    0  success (report written, even if empty)
    1  I/O or parse error on the input
    2  invalid command-line usage
"""

from __future__ import annotations

import argparse
import json
import os
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


def iter_file_coverage(report: dict, exclude_targets: Iterable[str]) -> Iterable[FileCoverage]:
    """Yield :class:`FileCoverage` for each file in each non-excluded target."""
    excluded = {name for name in exclude_targets if name}
    for target in report.get("targets", []):
        if target.get("name") in excluded:
            continue
        for entry in target.get("files", []):
            path = entry.get("path") or entry.get("name", "")
            if not path:
                continue
            yield FileCoverage(path=path, functions=tuple(entry.get("functions", [])))


def build_coverage_xml(
    report: dict, project_root: str, exclude_targets: Iterable[str]
) -> ET.Element:
    """Build the ``<coverage>`` XML element from an xccov report dict."""
    root = ET.Element("coverage", {"version": "1"})
    for file_cov in iter_file_coverage(report, exclude_targets):
        if not file_cov.functions:
            continue
        file_el = ET.SubElement(
            root, "file", {"path": _relative_path(file_cov.path, project_root)}
        )
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
    return parser.parse_args(argv)


def main(argv: Optional[list] = None) -> int:
    args = _parse_args(argv)
    exclude_targets = [s.strip() for s in args.exclude_targets.split(",")]

    try:
        with open(args.input, "r", encoding="utf-8") as handle:
            report = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"error: failed to read {args.input!r}: {exc}", file=sys.stderr)
        return 1

    root = build_coverage_xml(report, args.project_root, exclude_targets)
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
