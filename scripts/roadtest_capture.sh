#!/bin/bash
# ============================================================================
# roadtest_capture.sh — road-test harness
#
#   vehicle-sim --stdout-csv  |  engine-sim-cli --live-telemetry
#
# Captures EVERYTHING into one time-coded run directory (never overwritten):
#   <CAPTURES>/roadtest_<YYYYmmdd_HHMMSS>/
#     input_stream.csv           exact CSV vehicle-sim piped into engine-sim-cli (tee)
#     engine_sim_cli_output.csv  per-frame engine state (--csv-out), incl. latency_ms
#     gearbox_decisions.csv      gearbox shift decisions (--gearbox-log)
#     vehicle_sim_stderr.log     vehicle-sim stderr, each line prefixed epoch-seconds
#     engine_sim_cli_stderr.log  engine-sim-cli stderr, each line prefixed epoch-seconds
#     meta.txt                   exact commands, repo SHAs, start/end, run summary
#
# The engine console readout stays live on your terminal (the same per-frame
# data is machine-parseable in engine_sim_cli_output.csv, so it is not also
# captured as a \r-laden log).
#
# Usage:
#   ./roadtest_capture.sh [extra vehicle-sim flags...] -- [extra engine-sim-cli flags...]
#     e.g. ./roadtest_capture.sh -k                    # keyboard-drive the demo
#          ./roadtest_capture.sh -- --diagnostic-freq  # engine diagnostic line
#
# Env overrides (defaults in parentheses):
#   VSCONNECT (demo)   vehicle-sim --connect target: demo | tcp:IP[:port] | usb:PATH |
#                      file:PATH | auto | BLE address
#   VEHICLE  (tesla)   -v vehicle type (tesla, audi_mlb_evo, ...)
#   INTERVAL (100)     vehicle-sim --interval ms. NOTE: file: replay paces PER ROW,
#                      so use INTERVAL=1 for file replay (else a dense capture takes
#                      minutes). Live transports ignore this nuance (network rate).
#   PIN_TAU  (150)     engine-sim-cli --pin-tau-ms staircase fix (0 = rigid pin)
#   CAPTURES (~/vscode/escli.vehicle-sim/captures)
#   VS_BIN, ESCLI_BIN  binary paths (see defaults below)
#
# Stopping: press 'q' in vehicle-sim (-k mode) for a CLEAN end-of-file shutdown
# (all files close flush-clean), or Ctrl-C (per-frame CSV is flushed every row;
# at most the final partial stderr line is lost).
# No --duration is passed by default — add one with `-- --duration N` if wanted.
# ============================================================================
set -u

VS_BIN="${VS_BIN:-$HOME/vscode/escli.vehicle-sim/build-native/vehicle-sim}"
ESCLI_BIN="${ESCLI_BIN:-$HOME/vscode/engine-sim-app/engine-sim-cli/build/engine-sim-cli}"
CAPTURES="${CAPTURES:-$HOME/vscode/escli.vehicle-sim/captures}"
VSCONNECT="${VSCONNECT:-demo}"
VEHICLE="${VEHICLE:-tesla}"
INTERVAL="${INTERVAL:-100}"
PIN_TAU="${PIN_TAU:-150}"

[ -x "$VS_BIN" ]   || { echo "FATAL: vehicle-sim not found/executable: $VS_BIN" >&2; exit 1; }
[ -x "$ESCLI_BIN" ] || { echo "FATAL: engine-sim-cli not found/executable: $ESCLI_BIN" >&2; exit 1; }
mkdir -p "$CAPTURES" || exit 1

# Split "$@" into vehicle-sim extras (before --) and engine-sim-cli extras (after).
VS_EXTRA=(); ESCLI_EXTRA=(); seen_sep=0
for arg in "$@"; do
    if [ "$arg" = "--" ]; then seen_sep=1
    elif [ $seen_sep -eq 0 ]; then VS_EXTRA+=("$arg")
    else ESCLI_EXTRA+=("$arg"); fi
done

STAMP=$(date +%Y%m%d_%H%M%S)
RUNDIR="$CAPTURES/roadtest_$STAMP"
mkdir "$RUNDIR" || { echo "FATAL: cannot create $RUNDIR" >&2; exit 1; }
ln -sfn "$RUNDIR" "$CAPTURES/roadtest_LATEST"

# Line-timestamping filter: prefixes every line with epoch seconds (millisecond
# precision, e.g. 1788694559.901). Lines are flushed per-line so a hard kill
# loses at most the line in flight. Epoch was chosen (not wall-clock text) so
# log lines correlate EXACTLY with wall_clock_ms / latency_ms in
# engine_sim_cli_output.csv (= value * 1000).
ts() {
    perl -MTime::HiRes=time -ne 'BEGIN{$|=1} printf "%.3f %s", time(), $_'
}

VS_LOG="$RUNDIR/vehicle_sim_stderr.log"
ESCLI_LOG="$RUNDIR/engine_sim_cli_stderr.log"
INPUT_CSV="$RUNDIR/input_stream.csv"
OUTPUT_CSV="$RUNDIR/engine_sim_cli_output.csv"
GEARBOX_CSV="$RUNDIR/gearbox_decisions.csv"
META="$RUNDIR/meta.txt"

START_EPOCH=$(date +%s)
VS_SHA=$(cd "$(dirname "$VS_BIN")/.." && git rev-parse --short HEAD 2>/dev/null || echo unknown)
ESCLI_SHA=$(cd "$(dirname "$ESCLI_BIN")/.." && git rev-parse --short HEAD 2>/dev/null || echo unknown)

{
    echo "roadtest $STAMP"
    echo "started_epoch=$START_EPOCH  ($(date))"
    echo "host=$(hostname)  user=$(whoami)"
    echo
    echo "[vehicle-sim]"
    echo "  binary=$VS_BIN  (mtime $(stat -f '%Sm' "$VS_BIN"))"
    echo "  repo_sha=$VS_SHA"
    echo "  cmd=$VS_BIN --connect $VSCONNECT -v $VEHICLE --interval $INTERVAL --stdout-csv ${VS_EXTRA[*]:-}"
    echo
    echo "[engine-sim-cli]"
    echo "  binary=$ESCLI_BIN  (mtime $(stat -f '%Sm' "$ESCLI_BIN"))"
    echo "  repo_sha=$ESCLI_SHA"
    echo "  cmd=$ESCLI_BIN --live-telemetry --pin-tau-ms $PIN_TAU --csv-out $OUTPUT_CSV --gearbox-log $GEARBOX_CSV ${ESCLI_EXTRA[*]:-}"
    echo
    echo "captures:"
    echo "  $INPUT_CSV              (exact piped stream)"
    echo "  $OUTPUT_CSV        (per-frame engine state, latency_ms col 4)"
    echo "  $GEARBOX_CSV     (gearbox shift decisions)"
    echo "  $VS_LOG      (stderr, epoch-prefixed)"
    echo "  $ESCLI_LOG   (stderr, epoch-prefixed)"
    echo "  $RUNDIR/engine_sim_cli_console.log (terminal readout, \\r-normalized)"
    echo
    echo "note: engine-sim-cli logs [INFO] lines to stdout, so its stderr file"
    echo "may be empty on a clean run; those lines are in the console log."
    echo "note: latency_ms = wall_clock_ms - recorded timestamp_ms. Meaningful"
    echo "for live sources (tcp/usb/demo); file: replay carries recorded"
    echo "(days-old) timestamps, excluded from the summary average."
} > "$META"

echo "==> capture dir: $RUNDIR"
echo "==> vehicle-sim : $VSCONNECT (vehicle $VEHICLE, interval ${INTERVAL}ms)"
echo "==> engine-sim-cli: --live-telemetry --pin-tau-ms $PIN_TAU"
echo "==> stop with 'q' (vehicle-sim -k mode) or Ctrl-C"

# ---- run summary -----------------------------------------------------------
# Installed BEFORE the pipeline so Ctrl-C (SIGINT) still writes the summary:
# the INT trap exits 130, which fires this EXIT trap.
summarize() {
    local END_EPOCH; END_EPOCH=$(date +%s)
    local in_rows=0 out_rows=0 gb_rows=0
    [ -f "$INPUT_CSV" ]  && in_rows=$(( $(wc -l < "$INPUT_CSV") - 1 ))
    [ -f "$OUTPUT_CSV" ] && out_rows=$(( $(wc -l < "$OUTPUT_CSV") - 1 ))
    [ -f "$GEARBOX_CSV" ] && gb_rows=$(( $(wc -l < "$GEARBOX_CSV") - 1 ))
    # latency stats from engine_sim_cli_output.csv col 4, sane live range only
    # (<60s): file-replay rows carry days-old recorded timestamps and would
    # skew the average; they are excluded but still present in the raw CSV.
    local lat
    lat=$(awk -F, 'NR>1 && $4>=0 && $4<60000 {n++; s+=$4; if(min==""||$4<min)min=$4; if($4>max)max=$4}
                  END {
                      if (n==0) { print "latency_ms rows=0 (none in live range)"; exit }
                      print "latency_ms rows=" n " avg=" sprintf("%.1f",s/n) \
                            " min=" min " max=" max
                  }' "$OUTPUT_CSV" 2>/dev/null)
    {
        echo
        echo "---- summary ----"
        echo "ended_epoch=$END_EPOCH  ($(date))"
        echo "wall_duration=$((END_EPOCH - START_EPOCH))s"
        echo "exit_statuses: vehicle-sim=${PIPE_STATUS[0]:-NA} tee=${PIPE_STATUS[1]:-NA} engine-sim-cli=${PIPE_STATUS[2]:-NA}"
        echo "input_stream_rows=$in_rows"
        echo "engine_output_rows=$out_rows"
        echo "gearbox_rows=$gb_rows"
        echo "$lat"
    } >> "$META"
    echo
    echo "==> done. capture dir: $RUNDIR  (also at $CAPTURES/roadtest_LATEST)"
    echo "==> $in_rows input rows | $out_rows engine frames | $gb_rows gearbox rows | $lat"
}
trap summarize EXIT
trap 'echo; echo "==> interrupted (Ctrl-C)"; exit 130' INT TERM

"$VS_BIN" --connect "$VSCONNECT" -v "$VEHICLE" --interval "$INTERVAL" \
    --stdout-csv ${VS_EXTRA[@]+"${VS_EXTRA[@]}"} \
    2> >(ts > "$VS_LOG") \
    | tee "$INPUT_CSV" \
    | "$ESCLI_BIN" --live-telemetry --pin-tau-ms "$PIN_TAU" \
        --csv-out "$OUTPUT_CSV" \
        --gearbox-log "$GEARBOX_CSV" \
        ${ESCLI_EXTRA[@]+"${ESCLI_EXTRA[@]}"} \
        2> >(ts > "$ESCLI_LOG") \
    | tee >(tr '\r' '\n' | ts > "$RUNDIR/engine_sim_cli_console.log")
PIPE_STATUS=("${PIPESTATUS[@]}")
