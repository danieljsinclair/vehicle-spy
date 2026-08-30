#!/usr/bin/env bash
# gen_firmware_build_info.sh - regenerate firmware/vanilla/FirmwareBuildInfo.h
#
# Composes the build-identifying firmware version:
#     <semver>+<short-git-hash>[-dirty] (<build-date>)
# e.g. "0.3.0+427d556-dirty (2026-08-29)"
#
#   semver   read from firmware/vanilla/FirmwareVersion.h (single source of
#            truth — bump the version THERE, never here)
#   hash     git rev-parse --short HEAD
#   -dirty   appended when the worktree differs from HEAD (staged or unstaged:
#            git diff --quiet HEAD). Untracked files do not count.
#   date     UTC ISO date at generation time
#
# Git-less environments (tarball build, no git on PATH) degrade to
# "<semver>+unknown (no-vcs)" — a missing git must NEVER fail a build.
#
# Wired to run on EVERY firmware build (Makefile firmware targets +
# firmware/CMakeLists.txt host tests), so a new commit or a dirtied tree is
# picked up even when no source file changed. The header is only REWRITTEN
# when its content actually changes (unchanged content keeps its mtime, so
# consumers are not spuriously recompiled).
#
# FirmwareBuildInfo.h is GENERATED and gitignored — do not edit it by hand.
#
# Usage: gen_firmware_build_info.sh [vanilla-dir]
#        (defaults to <repo-root>/firmware/vanilla)

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
vanilla_dir="${1:-$repo_root/firmware/vanilla}"
out="$vanilla_dir/FirmwareBuildInfo.h"
version_h="$vanilla_dir/FirmwareVersion.h"

# BSD-sed-portable (no GNU \+): [[:space:]][[:space:]]* for one-or-more.
semver="$(sed -n 's/^#define[[:space:]][[:space:]]*FIRMWARE_VERSION[[:space:]][[:space:]]*"\([^"]*\)".*/\1/p' "$version_h" | head -n 1)"
if [ -z "$semver" ]; then
    echo "gen_firmware_build_info.sh: cannot read FIRMWARE_VERSION from $version_h" >&2
    exit 1
fi

hash="unknown"
dirty=""
if command -v git >/dev/null 2>&1 && git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    hash="$(git -C "$repo_root" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    if ! git -C "$repo_root" diff --quiet HEAD 2>/dev/null; then
        dirty="-dirty"
    fi
fi

version="$semver+$hash$dirty ($(date -u +%Y-%m-%d))"

content=$(cat <<EOF
#pragma once
// GENERATED FILE — do not edit (gitignored). Regenerated on every firmware
// build by scripts/gen_firmware_build_info.sh; the semver source of truth is
// firmware/vanilla/FirmwareVersion.h. Carries the git hash, a -dirty marker
// when the worktree differed from HEAD, and the UTC build date, so the
// running build is exactly identifiable (ATI banner + [STATE] fw= field).
#define FIRMWARE_BUILD_VERSION "$version"
EOF
)

# Rewrite only on content change so an unchanged value keeps its mtime and
# downstream compilations are not retriggered.
if [ ! -f "$out" ] || [ "$content" != "$(cat "$out")" ]; then
    printf '%s\n' "$content" > "$out"
    echo "FirmwareBuildInfo.h: FIRMWARE_BUILD_VERSION=\"$version\""
fi
