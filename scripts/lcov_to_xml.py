#!/usr/bin/env python3
"""Convert an lcov (.info) report to SonarCloud's generic coverage XML format.

SonarCloud's generic coverage format (``sonar.coverageReportPaths``) is defined
by ``sonar-generic-coverage.xsd`` and requires a ``<lineToCover>`` per line:

    <coverage version="1">
      <file path="relative/or/absolute/path">
        <lineToCover lineNumber="6" covered="true"/>
      </file>
    </coverage>

lcov emits absolute ``SF:`` paths. This converter relativises them to the
project root (``--project-root``, default cwd) and, by default, keeps only
files under ``src/`` (the analysed sources). Other roots can be selected via
``--src-root`` (repeatable). Files that cannot be relativised are kept as-is.

This is the lcov analogue of the app's ``scripts/xccov_to_sonar.py``: the app
converts xccov JSON; C++ projects convert lcov. Both write generic XML so the
single ``sonar.coverageReportPaths`` property works for every repo.

Usage:
    lcov_to_xml.py <input.lcov> <output.xml> [--project-root DIR]
                   [--src-root src] [--src-root another/root]
"""

from __future__ import annotations

import fnmatch
import os
import sys
import xml.etree.ElementTree as ET
from typing import Iterable


def _relative_path(path: str, project_root: str) -> str:
    """Best-effort conversion of ``path`` to one relative to ``project_root``.

    Falls back to the original ``path`` if it cannot be made relative or if the
    relative form escapes the project root (avoids confusing ``../`` segments
    for SDK/system headers).
    """
    try:
        rel = os.path.relpath(path, project_root)
    except ValueError:
        return path
    if rel.startswith(".."):
        return path
    return rel


def _parse_lcov(lcov_text: str) -> dict:
    """Parse lcov text into ``{file_path: {line_no: hit_count}}``.

    Only ``SF:`` (file) and ``DA:`` (line data) records are consumed. Lines
    that fail to parse are skipped rather than aborting the whole report.
    """
    files: dict = {}
    current = None
    for line in lcov_text.split("\n"):
        if line.startswith("SF:"):
            current = line[3:].strip()
            files[current] = {}
        elif current and line.startswith("DA:"):
            parts = line[3:].split(",")
            if len(parts) >= 2:
                try:
                    line_no = int(parts[0])
                    count = int(parts[1])
                except ValueError:
                    continue
                files[current][line_no] = count
    return files


def _under_any_root(rel_path: str, src_roots: Iterable[str]) -> bool:
    """True if ``rel_path`` starts with any of the configured source roots."""
    return any(
        rel_path == root or rel_path.startswith(root + "/") for root in src_roots
    )


def _matches_any_glob(rel_path: str, globs: Iterable[str]) -> bool:
    """True if ``rel_path`` matches any fnmatch glob (e.g. ``**/*.mm``).

    The SonarCloud-style ``**/`` prefix is normalized to match at any depth:
    ``fnmatch`` treats ``*`` as a single path segment, so ``**/*.mm`` is
    translated to a check against the basename plus the literal pattern.
    """
    for pat in globs:
        if not pat:
            continue
        if fnmatch.fnmatch(rel_path, pat):
            return True
        # ``**/foo`` — also match ``foo`` at the repo root + by basename.
        if pat.startswith("**/"):
            tail = pat[3:]
            if fnmatch.fnmatch(rel_path, tail):
                return True
            if fnmatch.fnmatch(os.path.basename(rel_path), tail):
                return True
    return False


def build_coverage_xml(
    lcov_text: str, project_root: str, src_roots: Iterable[str],
    exclude_globs: Iterable[str] = (),
) -> ET.Element:
    """Build the ``<coverage>`` XML element from lcov text."""
    root = ET.Element("coverage", {"version": "1"})
    files = _parse_lcov(lcov_text)
    roots = [r for r in src_roots if r]
    globs = list(exclude_globs or [])
    # Track coverage sources that were dropped so the caller can detect a stale
    # or incomplete build (e.g. test binaries missing from the coverage run).
    dropped_no_lines = 0
    dropped_unrelativisable = 0
    dropped_symlink = 0
    dropped_glob = 0
    for abs_path in sorted(files.keys()):
        lines = files[abs_path]
        if not lines:
            dropped_no_lines += 1
            continue
        rel = _relative_path(abs_path, project_root)
        if roots and not _under_any_root(rel, roots):
            continue
        # Two-sided .mm exclusion (pure-fallback policy 2026-07-13): drop .mm
        # from the HEADLINE coverage XML so sonar==lcov matches, but the raw
        # lcov.info still carries them for per-file inspection. The glob is
        # ``**/*.mm`` (auto-catches past/present/future .mm; zero file names).
        # This mirrors sonar.coverage.exclusions on the SonarCloud side — both
        # must drop .mm for the headline counts to agree. cfamily cannot index
        # .mm, so a headline <file> entry for a .mm is dead weight (no component
        # to attach to) — dropping it is the honest two-sided fix.
        if globs and _matches_any_glob(rel, globs):
            dropped_glob += 1
            continue
        # Symlinks are path ALIASES, never canonical source. The esp32 firmware
        # build compiles vanilla C++ via firmware/can-bridge/ symlinks (26 of
        # them point at ../vanilla/), so llvm-cov emits coverage under BOTH
        # paths — emitting both here double-counts every line (the can-bridge
        # symlink entry + the vanilla canonical entry). Dropping the symlink
        # path keeps the canonical entry as the single source of truth, so each
        # production line lands in the denominator exactly once. This is
        # automatic and drift-proof (no hand-maintained exclusion list).
        resolved = abs_path if os.path.isabs(abs_path) else os.path.join(project_root, rel)
        if os.path.islink(resolved):
            dropped_symlink += 1
            continue
        # A file that survives the src filter but still cannot be relativised
        # to the project root would emit an absolute path the scanner cannot
        # match to a module file — flag it instead of silently writing a path
        # that yields 0% coverage on the dashboard.
        if os.path.isabs(rel):
            dropped_unrelativisable += 1
            print(
                f"warning: {abs_path} could not be relativised to "
                f"{project_root!r}; emitting absolute path",
                file=sys.stderr,
            )
        file_el = ET.SubElement(root, "file", {"path": rel})
        for line_no in sorted(lines.keys()):
            covered = "true" if lines[line_no] > 0 else "false"
            ET.SubElement(
                file_el,
                "lineToCover",
                {"lineNumber": str(line_no), "covered": covered},
            )
    if dropped_no_lines:
        print(
            f"note: {dropped_no_lines} lcov file record(s) had no line data "
            f"(excluded from XML)",
            file=sys.stderr,
        )
    if dropped_symlink:
        print(
            f"note: {dropped_symlink} symlinked source(s) dropped "
            f"(canonical path retained; prevents double-count)",
            file=sys.stderr,
        )
    if dropped_glob:
        print(
            f"note: {dropped_glob} source(s) dropped by --exclude-glob "
            f"(two-sided .mm headline exclusion; raw lcov.info retains them)",
            file=sys.stderr,
        )
    return root


def _parse_args(argv) -> tuple:
    if len(argv) < 3:
        print(
            "Usage: lcov_to_xml.py <input.lcov> <output.xml> "
            "[--project-root DIR] [--src-root SRC] "
            "[--exclude-glob PATTERN [--exclude-glob PATTERN ...]]",
            file=sys.stderr,
        )
        sys.exit(2)
    input_path = argv[1]
    output_path = argv[2]
    project_root = os.getcwd()
    src_roots = ["src"]
    exclude_globs: list[str] = []
    rest = argv[3:]
    i = 0
    while i < len(rest):
        if rest[i] == "--project-root" and i + 1 < len(rest):
            project_root = rest[i + 1]
            i += 2
        elif rest[i] == "--src-root" and i + 1 < len(rest):
            src_roots.append(rest[i + 1])
            i += 2
        elif rest[i] == "--no-src-filter":
            src_roots = []
            i += 1
        elif rest[i] == "--exclude-glob" and i + 1 < len(rest):
            exclude_globs.append(rest[i + 1])
            i += 2
        else:
            i += 1
    return input_path, output_path, project_root, src_roots, exclude_globs


def convert(lcov_path: str, xml_path: str, project_root: str, src_roots,
            exclude_globs: Iterable[str] = ()) -> None:
    """Read lcov, write generic coverage XML, print a one-line summary."""
    try:
        with open(lcov_path, "r", encoding="utf-8") as handle:
            lcov_text = handle.read()
    except OSError as exc:
        print(f"error: failed to read {lcov_path!r}: {exc}", file=sys.stderr)
        sys.exit(1)

    root = build_coverage_xml(lcov_text, project_root, src_roots, exclude_globs)
    ET.indent(root, space="  ")
    payload = ET.tostring(root, encoding="utf-8", xml_declaration=True)

    try:
        with open(xml_path, "wb") as handle:
            handle.write(payload)
            handle.write(b"\n")
    except OSError as exc:
        print(f"error: failed to write {xml_path!r}: {exc}", file=sys.stderr)
        sys.exit(1)

    file_count = len(root.findall("file"))
    line_count = len(root.findall("file/lineToCover"))
    covered = len(root.findall('file/lineToCover[@covered="true"]'))
    pct = f"{covered / line_count * 100:.1f}%" if line_count else "n/a"
    print(
        f"wrote {xml_path}: {file_count} files, "
        f"{covered}/{line_count} covered lines ({pct})"
    )


# Backwards-compatible module-level entry point.
def main(argv=None) -> int:
    input_path, output_path, project_root, src_roots, exclude_globs = _parse_args(
        sys.argv if argv is None else argv
    )
    convert(input_path, output_path, project_root, src_roots, exclude_globs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
