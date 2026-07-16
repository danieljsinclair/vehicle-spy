#!/usr/bin/env python3
"""
manifest_query.py - Read coverage-manifest.toml and emit values for Makefile consumers.

This is the Makefile's single shell-out to the manifest. The coverage recipes
need the per-project source dirs and exclusion patterns in a shell-friendly
format; this script emits them so the Makefile never hardcodes a source-set
truth that can drift from the manifest.

Outputs (mutually exclusive --mode):
    --mode src-dirs KEY            space-separated source dirs (for COVERAGE_SRC_DIRS)
    --mode excludes KEY            space-separated exclusion patterns (for COVERAGE_EXCLUDES)
    --mode src-roots KEY           --src-root a --src-root b (for lcov_to_xml.py)
    --mode exclude-args KEY        --exclude a --exclude b (for lcov_add_uninstrumented.py)

Exit 1 with a message on stderr if the project key is unknown / the manifest is
unreadable. Exit 0 and a single line on stdout on success.

Usage (from Makefile):
    COVERAGE_SRC_DIRS=$$(python3 scripts/manifest_query.py --mode src-dirs vehicle-spy)
    COVERAGE_EXCLUDES=$$(python3 scripts/manifest_query.py --mode excludes vehicle-spy)
"""
import argparse
import os
import sys
import tomllib


def load_manifest(path: str) -> dict:
    """Load and return the manifest, or exit 1 with a clear error."""
    if not os.path.isfile(path):
        print(f"error: manifest not found: {path}", file=sys.stderr)
        sys.exit(1)
    try:
        with open(path, "rb") as f:
            return tomllib.load(f)
    except tomllib.TOMLDecodeError as exc:
        print(f"error: manifest {path} is invalid TOML: {exc}", file=sys.stderr)
        sys.exit(1)


def find_project(manifest: dict, key: str) -> dict:
    """Return the [[project]] table with the given key, or exit 1."""
    for proj in manifest.get("project", []):
        if proj.get("key") == key:
            return proj
    print(f"error: project key {key!r} not in manifest", file=sys.stderr)
    sys.exit(1)


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    p.add_argument("--manifest", default=None,
                   help="path to coverage-manifest.toml (default: <script-dir>/../coverage-manifest.toml)")
    p.add_argument("--mode", required=True,
                   choices=("src-dirs", "excludes", "src-roots", "exclude-args",
                            "src-dir-args"),
                   help="what to emit")
    p.add_argument("key", help="project key (e.g. vehicle-spy)")
    args = p.parse_args(argv)

    manifest_path = args.manifest or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "coverage-manifest.toml"
    )
    manifest = load_manifest(manifest_path)
    proj = find_project(manifest, args.key)
    sources = proj.get("sources", [])
    excludes = proj.get("exclusions", [])

    if args.mode == "src-dirs":
        print(" ".join(sources))
    elif args.mode == "excludes":
        print(" ".join(excludes))
    elif args.mode == "src-roots":
        # Repeatable --src-root for lcov_to_xml.py (shell word-split on caller).
        print(" ".join(f"--src-root {s}" for s in sources))
    elif args.mode == "exclude-args":
        # Repeatable --exclude for lcov_add_uninstrumented.py.
        print(" ".join(f"--exclude {e}" for e in excludes))
    elif args.mode == "src-dir-args":
        # Repeatable --src-dir for lcov_add_uninstrumented.py.
        print(" ".join(f"--src-dir {s}" for s in sources))
    return 0


if __name__ == "__main__":
    sys.exit(main())
