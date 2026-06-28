#!/usr/bin/env python3
"""Build a cfamily-compatible compile_commands.json for the ESP32 firmware.

arduino-cli emits one entry per translation unit, including the ESP32 Arduino
core and library sources. For SonarCloud code-quality analysis we only want
OUR firmware code. arduino-cli preprocessed the sketches into a single amalgam
TU at ``<build>/sketch/can-bridge.ino.cpp`` (concatenating ``can-bridge.ino``
and ``ota_update.ino`` with ``#line`` directives) and captured the full xtensa
cross-compile command (ESP-IDF includes, defines, ``-std=gnu++11``).

We reuse that command but emit one compile-commands entry PER ``.ino`` source
file. Five things stop cfamily analysing the raw arduino-cli output, all fixed
here:

1. ``"arguments"`` array form. cfamily (engine 13.1.x / plugin 6.82) silently
   marks EVERY array-form CU "unsupported" -- it only parses the ``"command"``
   string form CMake emits. We serialise to a shell-quoted ``"command"`` string.

2. ``@response-file`` flags (``@.../build_opt.h``, ``@.../file_opts``) which
   cfamily cannot expand. We inline every ``@path`` argument.

3. Stale ``-o <out>``, ``-MMD`` and the amalgam source path point into the
   build tree; we drop ``-o``/``-MMD`` and replace the amalgam path with the
   real ``.ino`` source being analysed.

4. ``.ino`` is not a C/C++ extension cfamily recognises, so it skips the TU.
   We add ``-x c++`` to force C++ parsing of each ``.ino`` source.

5. Local headers (``OtaPublicKey.h``) are resolved relative to the sketch dir;
   the ``.ino`` sources already live there so they resolve naturally, but we
   add ``-I<sketch-dir>`` defensively for amalgam-relative includes.

Usage:
    sonar_filter_compdb.py <input.json> <output.json> <sketch-dir>
"""
from __future__ import annotations

import json
import os
import shlex
import sys


def _expand_response_files(args: list, base_dir: str) -> list:
    """Inline any ``@path`` GCC response-file arguments."""
    expanded: list = []
    for arg in args:
        if isinstance(arg, str) and arg.startswith("@"):
            rsp = arg[1:]
            if not os.path.isabs(rsp):
                rsp = os.path.join(base_dir, rsp)
            try:
                with open(rsp) as fh:
                    expanded.extend(line.strip() for line in fh if line.strip())
            except OSError:
                expanded.append(arg)
        else:
            expanded.append(arg)
    return expanded


def _shlex_split(command: str) -> list:
    """Parse a shell-style command string into an argument list."""
    try:
        return shlex.split(command)
    except ValueError:
        return command.split()


def _clean_base_args(args: list, amalgam_path: str) -> list:
    """Drop ``-o <out>`` / ``-MMD`` and the amalgam source path.

    The per-source ``.ino`` path and ``-x c++`` are added by the caller.
    """
    cleaned: list = []
    skip_next = False
    for arg in args:
        if skip_next:
            skip_next = False
            continue
        if arg == amalgam_path or (isinstance(arg, str)
                                   and arg.endswith("/can-bridge.ino.cpp")):
            continue
        if arg == "-o":
            skip_next = True
            continue
        if arg == "-MMD":
            continue
        cleaned.append(arg)
    return cleaned


def main(argv: list | None = None) -> int:
    argv = argv if argv is not None else sys.argv[1:]
    if len(argv) != 3:
        print(f"usage: {sys.argv[0]} <input.json> <output.json> <sketch-dir>",
              file=sys.stderr)
        return 2

    in_path, out_path, sketch_dir = argv
    with open(in_path) as fh:
        entries = json.load(fh)

    # Find the arduino-cli-generated sketch amalgam TU and reuse its command.
    base_args: list | None = None
    base_dir = os.getcwd()
    amalgam_path = ""
    for entry in entries:
        f = entry.get("file", "")
        if "/sketch/can-bridge.ino.cpp" in f:
            base_dir = entry.get("directory", base_dir)
            args = entry.get("arguments") or _shlex_split(entry.get("command", ""))
            args = _expand_response_files(args, base_dir)
            amalgam_path = f
            base_args = _clean_base_args(args, f)
            break

    if base_args is None:
        print(f"ERROR: no can-bridge.ino.cpp TU found in {in_path}", file=sys.stderr)
        return 1

    # Ensure local sketch headers (OtaPublicKey.h) resolve.
    if os.path.isdir(sketch_dir):
        base_args.append("-I" + os.path.abspath(sketch_dir))

    # Locate the compiler token to insert "-x c++" right after it.
    compiler_idx = 0
    if base_args and not str(base_args[0]).startswith("-"):
        compiler_idx = 0

    # Emit one CU per .ino source, each forced to C++ via -x c++.
    out = []
    ino_sources = sorted(
        os.path.join(os.path.abspath(sketch_dir), name)
        for name in os.listdir(sketch_dir)
        if name.endswith(".ino")
    )
    for src in ino_sources:
        args = list(base_args)
        # Insert -x c++ immediately after the compiler so cfamily parses the
        # .ino source as C++ (its own suffix list does not include .ino).
        insert_at = compiler_idx + 1
        args[insert_at:insert_at] = ["-x", "c++", src]
        out.append({
            "directory": base_dir,
            "file": src,
            "command": " ".join(shlex.quote(str(a)) for a in args),
        })

    if not out:
        print(f"ERROR: no .ino sources found in {sketch_dir}", file=sys.stderr)
        return 1

    with open(out_path, "w") as fh:
        json.dump(out, fh, indent=2)

    names = ", ".join(os.path.basename(s) for s in ino_sources)
    print(f"  emitted {len(out)} TU(s) for: {names}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
