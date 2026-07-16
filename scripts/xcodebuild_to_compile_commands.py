#!/usr/bin/env python3
"""Scrape a clang compile_commands.json for SonarCloud CFamily from xcodebuild.

Usage:
    python3 xcodebuild_to_compile_commands.py <xcodeproj> <scheme> <config> \
        <sdk> <destination> <derived_data> <output_json> [<source_glob> ...]

WHY THIS EXISTS
---------------
SonarCloud's CFamily sensor analyses Objective-C++ (.mm) ONLY when a
compilation database (compile_commands.json) tells it the language kind
(``-x objective-c++``), the std, the defines, and the include roots.
Without one, CFamily runs in AutoConfig, indexes plain ``.cpp``/``.h``
units it can infer, and silently logs ``ObjC: 0`` — leaving every ``.mm``
file as an "unknown file" with no ncloc, so coverage cannot attach.

xcodebuild has no ``-generate-export-compile-commands`` flag (that is a
SwiftPM/swiftc capability, not the Xcode app build system). The other
CFamily option — Sonar's ``build-wrapper`` — requires downloading a
binary and a full instrumented rebuild. This scraper instead captures the
REAL clang invocation xcodebuild emits during a normal verbose build and
synthesizes a minimal compile_commands.json from it: the same flags the
file is actually compiled with, stripped of build-session-specific junk
(``.hmap`` header maps, ``@resp`` files, DerivedData index paths) that
CFamily cannot consume and that would otherwise pin the database to a
single temp directory.

The result is what the CLI/bridge/vehicle-sim get for free from CMake's
``-DCMAKE_EXPORT_COMPILE_COMMANDS`` — DRY in intent, just sourced from
xcodebuild for the iOS app.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path


# Flags CFamily neither needs nor can resolve (they reference per-build
# DerivedData temp paths or Xcode index artefacts). Dropped from the
# synthesized command so the database is stable across builds/machines.
#
# -ivfsstatcache / -index-store-path / -index-unit-output-path : Xcode
#   index-store artefacts (DerivedData-specific).
# -MF / --serialize-diagnostics / -MMD / -MT : dependency tracking for
#   the build system; CFamily does its own dependency walk.
# @<resp> : Xcode response file (expanded below so includes are visible).
# *.hmap -I / -iquote : binary header maps (DerivedData-specific); the
#   stable project-relative -I paths are kept separately.
# -o : object output path (DerivedData-specific).
DROP_FLAG_PREFIXES = (
    "-ivfsstatcache",
    "-index-store-path",
    "-index-unit-output-path",
    "-MF",
    "-MT",
    "--serialize-diagnostics",
    "-o",
)
DROP_IF_VALUE_GLOB = (
    "*.hmap",   # -I / -iquote <DerivedData>/....hmap  (CFamily can't read hmaps)
    "*.resp",   # @<DerivedData>/....resp  (expanded inline below)
)


def _is_dropped_flag(arg: str) -> bool:
    """True if arg is a build-session-specific flag CFamily should not see."""
    if arg.startswith("@"):
        return True
    for prefix in DROP_FLAG_PREFIXES:
        if arg == prefix or arg.startswith(prefix + " "):
            return True
        # xcodebuild escapes ``=`` as ``\=`` for some flags; accept both forms.
        if arg.startswith(prefix + "\\=") or arg.startswith(prefix + "="):
            return True
    return False


def _expand_resp(arg: str) -> list:
    """Expand a ``@file.resp`` response file into its argument list.

    Returns [arg] untouched if it isn't a response file or can't be read
    (so the caller still sees the original token). xcodebuild writes resp
    files as a single space-separated line.
    """
    if not arg.startswith("@"):
        return [arg]
    resp_path = arg[1:]
    try:
        text = Path(resp_path).read_text()
    except OSError:
        return [arg]
    return text.split()


def _is_derived_data_path(value: str) -> bool:
    """True if value looks like a DerivedData temp path (not portable)."""
    return "/var/folders/" in value or "/Build/Intermediates" in value


def _strip_quotes(token: str) -> str:
    """Strip a single layer of surrounding single/double quotes."""
    if len(token) >= 2 and token[0] in ("'", '"') and token[-1] == token[0]:
        return token[1:-1]
    return token


def _clean_arguments(argv: list) -> list:
    """Drop build-session-specific flags and DerivedData -I paths.

    Walks argv left-to-right. ``@resp`` files are expanded in place so
    their -I/-D tokens are visible to the filter (keeps stable project
    paths, drops DerivedData hmaps). Handles both joined (``-Ifoo``) and
    split (``-I foo``) include forms, and strips the quote layer xcodebuild
    writes into resp files (``'-std=gnu++17'`` -> ``-std=gnu++17``).
    """
    # First expand @resp so we filter their contents too.
    expanded: list = []
    for arg in argv:
        expanded.extend(_expand_resp(arg))

    # Pass 1: strip quote layer + unescape xcodebuild's ``\=`` -> ``=`` so
    # tokens are real flags CFamily can match against.
    expanded = [_strip_quotes(a).replace("\\=", "=") for a in expanded]

    # Pass 2: walk and drop. Prefix flags consume their following value.
    cleaned: list = []
    skip_next = False
    for arg in expanded:
        if skip_next:
            skip_next = False
            continue
        # Dropped prefix-flag (value is the NEXT token) -> skip both.
        if arg in DROP_FLAG_PREFIXES:
            skip_next = True
            continue
        # Dropped "=" form (value inline) -> drop just this token.
        if any(arg.startswith(p + "=") for p in DROP_FLAG_PREFIXES):
            continue
        if arg.startswith("@"):
            continue
        # Include flags are normalized by _repair_split_includes below;
        # here we only need to drop DerivedData/hmap paths that appear as
        # bare tokens (the value half of a split include). Joined forms
        # (``-Ipath``) are handled there too.
        if _is_derived_data_path(arg) or arg.endswith(".hmap"):
            continue
        cleaned.append(arg)

    # The split-include skip above unconditionally drops the next token;
    # repair the case where that value was a KEEP-worthy project path by
    # re-walking with a join instead. (Simpler: rebuild includes joined.)
    return _repair_split_includes(cleaned)


def _repair_split_includes(argv: list) -> list:
    """Re-join ``-I``/``-iquote``/``-F`` that xcodebuild split from their value.

    CFamily's compile_commands expects include flags as a single token
    (``-Ipath``) or a clean two-token pair. xcodebuild emits them split;
    joining makes the database deterministic and the filter's job easy.
    """
    out: list = []
    i = 0
    includes = ("-I", "-iquote", "-F")
    while i < len(argv):
        tok = argv[i]
        if tok in includes and i + 1 < len(argv) and not argv[i + 1].startswith("-"):
            # Next token is a real path — join only if it's not DerivedData/hmap.
            if not (_is_derived_data_path(argv[i + 1])
                    or argv[i + 1].endswith(".hmap")):
                out.append(tok + argv[i + 1])
            i += 2
            continue
        out.append(tok)
        i += 1
    return out


# Regex for the xcodebuild "CompileC <obj> <src> ..." header line. The
# source path is the second whitespace-delimited token after CompileC.
_COMPILEC_RE = re.compile(r"^\s*CompileC\s+\S+\s+(\S+\.(?:mm|m|cpp|cc|c))\s")


def scrape(xcodeproj: str, scheme: str, config: str, sdk: str,
           destination: str, derived_data: str, output_json: str,
           source_globs: list) -> int:
    """Run a verbose xcodebuild and emit compile_commands.json. Returns count."""
    # Fresh build dir each run so xcodebuild actually emits compile lines
    # (a no-op build prints nothing). We only need the compile phase.
    cmd = [
        "xcodebuild",
        "-project", xcodeproj,
        "-scheme", scheme,
        "-configuration", config,
        "-sdk", sdk,
        "-destination", destination,
        "-derivedDataPath", derived_data,
        "build",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    # xcodebuild exit code is non-zero if any link/sign step fails on a
    # simulator-only compile, but compile lines are still emitted. We only
    # need the compile invocations, so parse stdout regardless of exit.
    log = proc.stdout + "\n" + proc.stderr

    project_root = os.path.dirname(os.path.abspath(xcodeproj))
    entries = {}  # keyed by abs source path (dedup arm64/x86_64 duplicates)

    lines = log.splitlines()
    # The clang invocation follows the CompileC header but NOT immediately —
    # xcodebuild interleaves ``cd``, ``Using response file:`` and blank lines
    # between them. Scan forward from each header to the next clang line.
    for i, line in enumerate(lines):
        m = _COMPILEC_RE.match(line)
        if not m:
            continue
        src = m.group(1)
        clang_line = None
        for j in range(i + 1, min(i + 8, len(lines))):
            cand = lines[j].strip()
            # A clang invocation: starts with a path containing "clang" and
            # carries the -x <lang> kind. Stop at the next CompileC header.
            if cand.startswith("CompileC"):
                break
            if "clang" in cand and " -x " in cand:
                clang_line = cand
                break
        if not clang_line:
            continue
        argv = clang_line.split()
        if not argv:
            continue
        # Keep argv[0] (the clang binary) — CFamily's compilation database
        # treats arguments[0] as the compiler; dropping it makes every entry
        # an "unknown compiler" and the analysis comes back empty.
        cleaned = _clean_arguments(argv)

        # Optional source-glob filter: only emit entries for sources we name.
        if source_globs:
            base = os.path.basename(src)
            if not any(re.match(pat.replace("*", ".*").replace("?", "."), base)
                       for pat in source_globs):
                continue

        entries[os.path.abspath(src)] = {
            "directory": project_root,
            "file": os.path.abspath(src),
            "arguments": cleaned,
        }

    db = list(entries.values())
    Path(output_json).parent.mkdir(parents=True, exist_ok=True)
    Path(output_json).write_text(json.dumps(db, indent=2))
    print(f"Wrote {len(db)} compile units to {output_json}")
    return len(db)


if __name__ == "__main__":
    # argv: xcodeproj scheme config sdk destination derived_data out_json [globs...]
    if len(sys.argv) < 8:
        print(
            "Usage: xcodebuild_to_compile_commands.py <xcodeproj> <scheme> "
            "<config> <sdk> <destination> <derived_data> <output_json> "
            "[<source_glob> ...]"
        )
        sys.exit(1)
    count = scrape(
        sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4],
        sys.argv[5], sys.argv[6], sys.argv[7], sys.argv[8:],
    )
    sys.exit(0 if count > 0 else 1)
