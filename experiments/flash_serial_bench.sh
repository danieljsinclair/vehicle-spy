#!/usr/bin/env bash
# Flash SerialBench onto the ESP32 (experiments only — source under experiments/).
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${ESP32_PORT:-/dev/cu.usbserial-210}"
FQBN="esp32:esp32:esp32:PartitionScheme=min_spiffs"
SKETCH="experiments/sketches/SerialBench"
OUT="build-experiments/serial-bench"

ESPTOOL_DIR=$(python3 -c 'import pathlib; roots=sorted(pathlib.Path.home().glob("Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool"), key=lambda p: tuple(int(x) if x.isdigit() else x for x in p.parent.name.split("."))); print(roots[-1].parent if roots else "")')
ESPTOOL="$ESPTOOL_DIR/esptool"
MERGE=$( "$ESPTOOL" --help 2>/dev/null | grep -q 'merge-bin' && echo merge-bin || echo merge_bin )

echo "== Building SerialBench =="
arduino-cli compile --fqbn "$FQBN" "$SKETCH" \
  --output-dir "$OUT" \
  --build-property "compiler.cpp.extra_flags=-std=gnu++14"

echo "== Merging + flashing via $PORT =="
"$ESPTOOL" --chip esp32 $MERGE --output "$OUT/SerialBench.merged.bin" --target-offset 0x0 \
  0x1000 "$OUT/SerialBench.ino.bootloader.bin" \
  0x8000 "$OUT/SerialBench.ino.partitions.bin" \
  0x10000 "$OUT/SerialBench.ino.bin"
"$ESPTOOL" --chip esp32 --port "$PORT" --baud 460800 write_flash 0x0 \
  "$OUT/SerialBench.merged.bin"

echo "== Done. SerialBench streams on $PORT at 115200 baud. =="
