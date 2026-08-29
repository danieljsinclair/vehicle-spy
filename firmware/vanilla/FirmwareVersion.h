#pragma once

// FirmwareVersion.h - SINGLE SOURCE OF TRUTH for the firmware version.
//
// FIRMWARE_VERSION is the semantic version — edit it HERE; the can-bridge .ino
// and every consumer read it from this header (the dead per-.ino constant is
// gone). The build-identifying string the device actually reports —
// "<semver>+<short-git-hash>[-dirty] (<build-date>)", e.g.
// "0.3.0+427d556-dirty (2026-08-29)" — is FIRMWARE_BUILD_VERSION, GENERATED at
// build time into FirmwareBuildInfo.h (gitignored) by
// scripts/gen_firmware_build_info.sh, which composes it from FIRMWARE_VERSION
// + `git rev-parse --short HEAD` + the worktree dirty flag + the UTC date, so
// "did I flash THIS exact build?" is answerable from ATI / the [STATE] line.
//
// Macros (not constexpr char[]) so call sites compose one string literal at
// compile time: "ESP32 CAN Bridge v" FIRMWARE_BUILD_VERSION.
//
// 0.3.0 — TCP [STATE] serving + writeLineToClient shipped since 0.2.0.

#define FIRMWARE_VERSION "0.3.0"

// Prefer the generated per-build identity (git hash + date). The __has_include
// fallback keeps generator-less builds compiling (tarball, or a fresh clone
// before its first firmware build): they report "<semver>+unknown (no-vcs)".
#if __has_include("FirmwareBuildInfo.h")
#include "FirmwareBuildInfo.h"
#endif
#ifndef FIRMWARE_BUILD_VERSION
#define FIRMWARE_BUILD_VERSION FIRMWARE_VERSION "+unknown (no-vcs)"
#endif
