#!/usr/bin/env python3
"""
gen_coverage_config.py - Regenerate the source/exclusion lines of the 3
sonar-project*.properties files from coverage-manifest.toml.

This is the WRITE side of the single-source-of-truth: the manifest is the
authority for each project's `sonar.sources`, `sonar.exclusions`, and
`sonar.coverage.exclusions`; this script rewrites THOSE LINES in each
properties file to match the manifest. Every other line (projectKey, org,
compile-commands, coverageReportPaths, comments) is PRESERVED verbatim —
those are hand-written static config that does not derive from the manifest.

The regenerated lines are flanked by GENERATED markers so humans + the
`make manifest-check` guard know they are derived (hand-editing them is
always overwritten on the next run):

    # @GENERATED@ do not edit — derived from coverage-manifest.toml
    sonar.sources=src,include
    # @END@

Idempotent: running twice yields the same bytes (the manifest truth is
deterministic), so regenerating does not dirty the working tree unless the
manifest actually changed.

Exit codes: 0 on success (writes the files), 1 on manifest/parse error.

Usage:
    python3 scripts/gen_coverage_config.py                       # all 3 projects
    python3 scripts/gen_coverage_config.py --project vehicle-spy # one project
    python3 scripts/gen_coverage_config.py --check               # exit 1 if out of sync
    python3 scripts/gen_coverage_config.py --diff                # print unified diff, write nothing
"""
import argparse
import difflib
import os
import sys
import tomllib

BEGIN = "# @GENERATED@ do not edit below — derived from coverage-manifest.toml"
END = "# @END@"

# The properties keys this script owns (regenerates). Others are preserved.
# `sonar.cpp.file.suffixes` is owned ONLY when the project's manifest declares
# `cfamily_cpp_suffixes` (e.g. to add `.mm` so cfamily analyses Obj-C++ as C++).
# Absent that field, the key is NOT emitted and cfamily uses its built-in
# default (.cpp,.cc,.cxx,.c++,.cppm) — so projects that don't opt in are
# unaffected. This keeps the suffix-model probe tracked + regenerable +
# revertible (remove the field, regen -> gone).
# `sonar.cfamily.compile-commands` is owned ONLY when the project's manifest
# declares `cfamily_compile_commands` (a path to a compile_commands.json).
# Without it cfamily runs in AutoConfig (vehicle-spy-ios historically had no
# cfamily at all — the scraper now provides one so .mm can be indexed).
OWNED_KEYS = ("sonar.sources", "sonar.exclusions", "sonar.coverage.exclusions",
              "sonar.cpp.file.suffixes", "sonar.cfamily.compile-commands")


def load_manifest(path: str) -> dict:
    if not os.path.isfile(path):
        print(f"error: manifest not found: {path}", file=sys.stderr)
        sys.exit(1)
    try:
        with open(path, "rb") as f:
            return tomllib.load(f)
    except tomllib.TOMLDecodeError as exc:
        print(f"error: manifest {path} is invalid TOML: {exc}", file=sys.stderr)
        sys.exit(1)


def _line_value_for(key: str, proj: dict, repo_root: str):
    """Return the comma-joined value for a properties key from the project, or
    None if the manifest has no entry (the line is then left as-is / dropped)."""
    if key == "sonar.sources":
        return ",".join(proj.get("sources", [])) or None
    if key == "sonar.exclusions":
        excl = list(proj.get("exclusions", []))
        # Machine-derived symlink exclusions (the ONE non-wildcard exception).
        # For each dir in symlink_exclude_dirs, run `find <dir> -type l` and add
        # each symlink path. This stops cfamily indexing a symlink facade AS
        # WELL AS its canonical target (source-level double-count).
        for d in proj.get("symlink_exclude_dirs", []):
            abs_dir = os.path.join(repo_root, d)
            if not os.path.isdir(abs_dir):
                continue
            for root, _dirs, files in os.walk(abs_dir):
                for fn in files:
                    fp = os.path.join(root, fn)
                    if os.path.islink(fp):
                        rel = os.path.relpath(fp, repo_root).replace(os.sep, "/")
                        excl.append(rel)
        return ",".join(excl) if excl else None
    if key == "sonar.coverage.exclusions":
        excl = proj.get("coverage_exclusions")
        return (",".join(excl) if excl else None)
    if key == "sonar.cpp.file.suffixes":
        # Opt-in: only emitted when the manifest declares cfamily_cpp_suffixes
        # (a list, e.g. [".cpp",".cc",".cxx",".c++",".cppm",".mm"]). The caller
        # is responsible for including the standard C++ suffixes alongside any
        # additions (.mm) — setting this REPLACES cfamily's default, so omitting
        # .cpp etc. would stop cfamily analysing ordinary C++.
        suffixes = proj.get("cfamily_cpp_suffixes")
        return (",".join(suffixes) if suffixes else None)
    if key == "sonar.cfamily.compile-commands":
        # Opt-in: only emitted when the manifest declares cfamily_compile_commands
        # (a path to a compile_commands.json cfamily reads in Compilation-Database
        # mode). This is what makes cfamily index .mm (the compile DB carries the
        # -x objective-c++ kind + std + -I roots cfamily needs per CU).
        return proj.get("cfamily_compile_commands")
    return None


def regenerate_text(properties_path: str, proj: dict, repo_root: str) -> str:
    """Return the NEW text for a properties file, regenerated from the manifest.

    Strategy: walk the existing lines and classify each as either (a) part of a
    @GENERATED@ block (BEGIN..END) or (b) a bare owned-key assignment line or
    (c) verbatim static content. We rebuild by:

      - preserving verbatim content in place,
      - replacing every existing generated block (or bare owned assignment)
        with a FRESH block carrying the current manifest value for that key,
        at the same position,
      - appending a fresh block for any wanted key that had no prior line.

    Marking a key as "placed" when we encounter EITHER form (block or bare
    assignment) keeps the function idempotent: a second run sees only blocks,
    re-emits them with the same values, and produces identical bytes.
    """
    if not os.path.isfile(properties_path):
        print(f"error: properties file not found: {properties_path}", file=sys.stderr)
        sys.exit(1)
    with open(properties_path) as f:
        original = f.readlines()

    # The manifest truth for this project (key -> comma-joined value).
    wanted = {}
    for key in OWNED_KEYS:
        val = _line_value_for(key, proj, repo_root)
        if val is not None:
            wanted[key] = val

    def _block_for(key: str):
        return [f"{BEGIN}\n", f"{key}={wanted[key]}\n", f"{END}\n"]

    out = []
    placed = set()
    i = 0
    while i < len(original):
        line = original[i]
        stripped = line.lstrip()

        # Existing @GENERATED@ block: consume it wholesale (BEGIN..END) and, if
        # its key is still wanted, re-emit a fresh block HERE (same position).
        if stripped.startswith(BEGIN):
            # Collect the assignment line inside (if any) to know which key.
            inner_keys = []
            j = i + 1
            while j < len(original) and not original[j].lstrip().startswith(END):
                for k in OWNED_KEYS:
                    if original[j].lstrip().startswith(k + "="):
                        inner_keys.append(k)
                j += 1
            # j is at END (or past end)
            key = inner_keys[0] if inner_keys else None
            if key in wanted:
                out.extend(_block_for(key))
                placed.add(key)
            # if key not in wanted (or None), the block is dropped (manifest
            # no longer defines it).
            i = j + 1
            continue

        # Bare owned-key assignment line OUTSIDE any @GENERATED@ block.
        # IMPORTANT: a bare owned-key line outside a block is HAND-WRITTEN
        # static config (e.g. esp32's `sonar.cpp.file.suffixes=.ino,...`), NOT
        # generator-managed. The generator's contract is to manage ONLY the
        # content inside its own @GENERATED@/@END@ blocks. So:
        #   - if the key IS wanted by the manifest AND not yet placed: upgrade
        #     the bare line to a fresh @GENERATED@ block in place (one-time
        #     adoption — subsequent runs see only the block).
        #   - if the key is NOT wanted: PRESERVE the bare line verbatim. NEVER
        #     drop a bare owned-key line — it is static config the manifest does
        #     not model. Dropping it would silently delete hand-maintained
        #     settings (regression: the esp32 .ino suffix line was deleted this
        #     way, breaking cfamily with zero CUs).
        replaced_key = None
        for key in OWNED_KEYS:
            if stripped.startswith(key + "="):
                replaced_key = key
                break
        if replaced_key is not None:
            if replaced_key in wanted and replaced_key not in placed:
                out.extend(_block_for(replaced_key))
                placed.add(replaced_key)
            else:
                # Not wanted, or already placed elsewhere: keep the bare line
                # as hand-written static content (do NOT drop, do NOT upgrade).
                out.append(line)
            i += 1
            continue

        # Verbatim static content (comments, static config lines).
        out.append(line)
        i += 1

    # Append wanted keys that had no prior line (block or bare assignment).
    for key in OWNED_KEYS:
        if key in wanted and key not in placed:
            out.append(f"\n{BEGIN}\n")
            out.append(f"{key}={wanted[key]}\n")
            out.append(f"{END}\n")

    return "".join(out)


def _properties_path_for(proj: dict, repo_root: str) -> str:
    return os.path.join(repo_root, proj["properties"])


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Regenerate sonar properties from the coverage manifest.")
    p.add_argument("--manifest", default=None,
                   help="path to coverage-manifest.toml (default: <script-dir>/../coverage-manifest.toml)")
    p.add_argument("--project", default=None,
                   help="only regenerate this project key (default: all)")
    p.add_argument("--check", action="store_true",
                   help="exit 1 if any properties file is out of sync with the manifest (no writes)")
    p.add_argument("--diff", action="store_true",
                   help="print unified diff of what would change (no writes)")
    args = p.parse_args(argv)

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    manifest_path = args.manifest or os.path.join(repo_root, "coverage-manifest.toml")
    manifest = load_manifest(manifest_path)

    projects = manifest.get("project", [])
    if args.project:
        projects = [p for p in projects if p.get("key") == args.project]
        if not projects:
            print(f"error: project key {args.project!r} not in manifest", file=sys.stderr)
            return 1

    out_of_sync = False
    for proj in projects:
        path = _properties_path_for(proj, repo_root)
        with open(path) as f:
            current = f.read()
        regenerated = regenerate_text(path, proj, repo_root)
        if current == regenerated:
            print(f"  {proj['key']}: in sync ({os.path.basename(path)})")
            continue
        out_of_sync = True
        if args.diff:
            print(f"--- {os.path.basename(path)} (current)")
            print(f"+++ {os.path.basename(path)} (from manifest)")
            diff = difflib.unified_diff(
                current.splitlines(keepends=True),
                regenerated.splitlines(keepends=True),
                fromfile=f"{os.path.basename(path)} (current)",
                tofile=f"{os.path.basename(path)} (from manifest)",
                n=1,
            )
            sys.stdout.writelines(diff)
        elif args.check:
            print(f"  {proj['key']}: OUT OF SYNC ({os.path.basename(path)})")
        else:
            with open(path, "w") as f:
                f.write(regenerated)
            print(f"  {proj['key']}: regenerated ({os.path.basename(path)})")

    if args.check and out_of_sync:
        print("\nerror: properties out of sync with manifest — run "
              "'python3 scripts/gen_coverage_config.py' or 'make manifest-regen'",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
