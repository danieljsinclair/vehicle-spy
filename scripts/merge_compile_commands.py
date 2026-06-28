#!/usr/bin/env python3
"""Merge multiple ``compile_commands.json`` files into one JSON array.

SonarCloud's cfamily analyser takes a single ``-compile-commands`` path, but
vehicle-spy has TWO independent compile databases produced by different
toolchains:

  * ``build-cov/compile_commands.json``  -- C++ core (CMake, host clang,
                                            coverage-instrumented). Covers
                                            ``src/`` + ``include/`` + ``test/``.
  * ``build-firmware/compile_commands.json`` -- ESP32 firmware (arduino-cli,
                                               xtensa cross-compile, filtered
                                               to per-.ino CUs by
                                               sonar_filter_compdb.py).

This script concatenates the ``[ ... ]`` arrays from each input into a single
array, deduplicating by ``file`` (the LAST input wins on conflict, so callers
can pass overrides in priority order). Missing/non-JSON inputs are skipped
with a warning (not fatal) so a partial build still yields a usable database.

Usage:
    merge_compile_commands.py <output.json> <input1.json> [input2.json ...]
"""
from __future__ import annotations

import json
import os
import sys
from typing import Iterable


def _load(path: str) -> list:
    """Load a compile_commands.json list, or return [] on missing/corrupt."""
    if not path or not os.path.isfile(path):
        print(f"warning: {path!r} does not exist; skipping", file=sys.stderr)
        return []
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as exc:
        print(f"warning: failed to parse {path!r}: {exc}; skipping",
              file=sys.stderr)
        return []
    if not isinstance(data, list):
        print(f"warning: {path!r} is not a JSON array; skipping",
              file=sys.stderr)
        return []
    return data


def merge(inputs: Iterable[str]) -> list:
    """Merge compile databases, deduplicating by ``file`` (last wins)."""
    by_file: dict = {}
    total_seen = 0
    for path in inputs:
        entries = _load(path)
        total_seen += len(entries)
        for entry in entries:
            file = entry.get("file") if isinstance(entry, dict) else None
            if file is not None:
                by_file[file] = entry
            else:
                # Non-file-keyed entry: keep it (append by id to avoid loss).
                by_file.setdefault(f"__nofile_{id(entry)}", entry)
    return list(by_file.values())


def main(argv=None) -> int:
    rest = sys.argv[1:] if argv is None else argv[1:]
    if len(rest) < 2:
        print(
            "Usage: merge_compile_commands.py <output.json> "
            "<input1.json> [input2.json ...]",
            file=sys.stderr,
        )
        return 2
    output_path = rest[0]
    inputs = rest[1:]
    merged = merge(inputs)
    os.makedirs(os.path.dirname(os.path.abspath(output_path)) or ".",
                exist_ok=True)
    try:
        with open(output_path, "w", encoding="utf-8") as handle:
            json.dump(merged, handle, indent=2)
            handle.write("\n")
    except OSError as exc:
        print(f"error: failed to write {output_path!r}: {exc}",
              file=sys.stderr)
        return 1
    print(
        f"wrote {output_path}: {len(merged)} entries "
        f"(from {len(inputs)} input(s))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
