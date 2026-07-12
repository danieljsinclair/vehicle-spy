#!/bin/bash
# Run all test binaries with LLVM coverage profiling, merge, and export.
#
# Discovers test binaries recursively under the build dir so it works for both
# the bridge (binaries directly under build/) and the CLI (binaries nested under
# build-cov/test/{,unit,integration,telemetry,smoke}). A binary is a "test" if
# its path or name contains "test" (case-insensitive) and it is executable.
set -e

BUILD_DIR="$1"
PROFRAW_DIR="$BUILD_DIR/profraw"
LLVM_COV="${LLVM_COV:-$(xcrun --find llvm-cov 2>/dev/null || which llvm-cov 2>/dev/null)}"
LLVM_PROFDATA="${LLVM_PROFDATA:-$(xcrun --find llvm-profdata 2>/dev/null || which llvm-profdata 2>/dev/null)}"

# Change to project root before running tests so relative paths (e.g., resources/dbc/)
# resolve correctly. The CMakeLists.txt sets CTEST_WORKING_DIRECTORY for CTest, but
# this script runs test binaries directly, which inherit the shell's CWD.
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

mkdir -p "$PROFRAW_DIR"

# Discover test binaries: executable regular files whose BASENAME matches
# *test* (so e.g. build/bridge_unit_tests and build-cov/test/unit/unit_tests both
# match). Exclude third-party deps (_deps) and source/script files. Symlinks,
# directories, and non-executable files are skipped by find's predicates.
mapfile -t TEST_BINS < <(find "$BUILD_DIR" \
    -type d -name "_deps" -prune -o \
    -type f -perm +111 -name "*test*" ! -name "*.py" ! -name "*.sh" \
    ! -name "*.cmake" ! -name "*.json" -print 2>/dev/null || true)

echo "=== Running tests with coverage instrumentation ==="
echo "  Found ${#TEST_BINS[@]} test binary candidate(s)"
FAILED_COUNT=0
FAILED_BINS=""
for test_bin in "${TEST_BINS[@]}"; do
    if [ -x "$test_bin" ] && [ -f "$test_bin" ]; then
        echo "  Running: ${test_bin#$BUILD_DIR/}"
        if ! LLVM_PROFILE_FILE="$PROFRAW_DIR/coverage-%p.profraw" "$test_bin"; then
            echo "  FAILED: ${test_bin#$BUILD_DIR/} (exit non-zero) — continuing to collect remaining coverage"
            FAILED_COUNT=$((FAILED_COUNT + 1))
            FAILED_BINS="$FAILED_BINS ${test_bin#$BUILD_DIR/}"
        fi
    fi
done

# Bail out gracefully if no coverage was produced (no test binaries ran).
if ! ls "$PROFRAW_DIR"/*.profraw >/dev/null 2>&1; then
    echo "=== No coverage profiles generated (no test binaries ran) ==="
    exit 1
fi

echo "=== Merging coverage profiles ==="
# nullglob-style safety: the ls check above guarantees a match here.
$LLVM_PROFDATA merge -sparse "$PROFRAW_DIR"/*.profraw -o "$BUILD_DIR/coverage.profdata"

# Build -object args for all test binaries; the first is the main object.
OBJECT_ARGS=""
MAIN_BIN=""
for test_bin in "${TEST_BINS[@]}"; do
    if [ -x "$test_bin" ] && [ -f "$test_bin" ]; then
        if [ -z "$MAIN_BIN" ]; then
            MAIN_BIN="$test_bin"
        else
            OBJECT_ARGS="$OBJECT_ARGS -object $test_bin"
        fi
    fi
done

echo "=== Generating llvm-cov text report ==="
$LLVM_COV show --show-branches=count \
    -instr-profile "$BUILD_DIR/coverage.profdata" \
    "$MAIN_BIN" \
    $OBJECT_ARGS \
    > "$BUILD_DIR/coverage.txt" 2>/dev/null || true

echo "=== Generating lcov report (for coverage-gutters + SonarCloud) ==="
$LLVM_COV export -format=lcov \
    -instr-profile "$BUILD_DIR/coverage.profdata" \
    "$MAIN_BIN" \
    $OBJECT_ARGS \
    > "$BUILD_DIR/lcov.info" 2>/dev/null || true

# SINGLE SOURCE OF TRUTH — complete the lcov file-list.
# llvm-cov export OMITS source files whose functions no test calls (zero
# executed regions), so lcov.info is a strict subset of sonar.sources. That
# makes sonar's lines_to_cover denominator count files lcov has no record for,
# and the never-called files are HIDDEN (absent, not 0%). Adding synthetic 0%
# entries for every sonar.sources file not already present makes the file-lists
# agree → absent==0%-not-hidden → sonar line-count == lcov line-count per
# project (the user's non-negotiable "same count" bar), AND the coverage %
# stays honest instead of being flattered by a too-small denominator.
#
# Opt-in via COVERAGE_SRC_DIRS (space-separated, relative to project root).
# When set, run lcov_add_uninstrumented.py over those dirs; when unset, skip
# (the bridge build/ path is unaffected). The vehicle-spy Makefile recipe sets
# COVERAGE_SRC_DIRS="src include firmware/vanilla" to mirror its sonar.sources.
#
# COVERAGE_EXCLUDES (also space-separated) mirrors sonar.exclusions so the lcov
# completion step SKIPS files sonar excludes (Phase 3 Part 2: lcov file-list ==
# sonar.sources EFFECTIVE set, not just the raw dir walk). Without this, a
# sonar.exclusions entry (e.g. src/main.cpp Path A) would leak into lcov as a
# 0% entry, re-introducing drift in the opposite direction from Part 1.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$BUILD_DIR/.." && pwd)"
if [ -n "${COVERAGE_SRC_DIRS:-}" ]; then
    echo "=== Completing lcov file-list (adding uninstrumented sources as 0%) ==="
    add_args=()
    for d in $COVERAGE_SRC_DIRS; do add_args+=(--src-dir "$d"); done
    # COVERAGE_EXCLUDES patterns may contain `**`, so word-splitting is safe
    # (no glob chars reach the shell — they are passed verbatim to python's
    # fnmatch via --exclude). Quote each token to preserve any inner chars.
    for p in ${COVERAGE_EXCLUDES:-}; do add_args+=(--exclude "$p"); done
    python3 "$SCRIPT_DIR/lcov_add_uninstrumented.py" \
        "$BUILD_DIR/lcov.info" "$BUILD_DIR/lcov.info.completed" \
        --root "$PROJECT_ROOT" "${add_args[@]}" \
        && mv "$BUILD_DIR/lcov.info.completed" "$BUILD_DIR/lcov.info"
fi

echo "=== Generating SonarCloud generic coverage XML (lcov -> XML) ==="
# lcov.info is NOT accepted by sonar-scanner; it needs generic XML per
# sonar-generic-coverage.xsd (same format the app produces via xccov_to_sonar).
# Convert here so both bridge (build/) and CLI (build-cov/) get coverage-sonar.xml.
# Errors are NOT suppressed: a silent converter failure or a stale build dir
# (missing test binaries) would otherwise drop covered files from the XML and
# the SonarCloud dashboard would show them as 0% with no warning.
SRC_ROOT_ARGS=()
for d in ${COVERAGE_SRC_DIRS:-src include}; do SRC_ROOT_ARGS+=(--src-root "$d"); done
python3 "$SCRIPT_DIR/lcov_to_xml.py" \
    "$BUILD_DIR/lcov.info" \
    "$BUILD_DIR/coverage-sonar.xml" \
    --project-root "$PROJECT_ROOT" \
    "${SRC_ROOT_ARGS[@]}"

# Guard: every src/ file that has DA line data in lcov.info MUST appear in the
# XML, otherwise coverage is silently lost. This catches a stale build dir that
# was not rebuilt after new source/test targets were added (the lcov then lacks
# the new test binaries' coverage, so files the scanner should see are absent).
LCOV_SRC_FILES=$(grep -E "^SF:" "$BUILD_DIR/lcov.info" \
    | grep -c "${PROJECT_ROOT}/src/")
XML_SRC_FILES=$(grep -c '<file path="src/' "$BUILD_DIR/coverage-sonar.xml" || true)
echo "  lcov src/ file records: ${LCOV_SRC_FILES}"
echo "  coverage-sonar.xml src/ files: ${XML_SRC_FILES}"
if [ "${XML_SRC_FILES}" -lt "${LCOV_SRC_FILES}" ]; then
    echo "  WARNING: coverage-sonar.xml has fewer src/ files (${XML_SRC_FILES})" \
         "than lcov.info (${LCOV_SRC_FILES}). The coverage build dir may be" \
         "stale — rebuild it (make coverage-clean && make coverage-run) so all" \
         "test binaries are present and every covered source is exported." >&2
fi

echo "=== Coverage report generated ==="
wc -l "$BUILD_DIR/coverage.txt" | awk '{print "Lines:", $1}'

# Honesty gate: if any test binary failed during the run loop above, surface it
# now (after coverage artefacts have been collected) and exit non-zero. The
# merge/export steps above completed successfully under `set -e`.
if [ "$FAILED_COUNT" -gt 0 ]; then
    echo "=== FAILED: $FAILED_COUNT test binary(ies) during coverage run:$FAILED_BINS ===" >&2
    exit 1
fi
