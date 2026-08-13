#!/usr/bin/env bash
# Flash WifiBench onto the ESP32 (experiments only — source under experiments/).
# Creds come from env ESP32_WIFI_SSID / ESP32_WIFI_PASS (like make firmware).
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${ESP32_PORT:-/dev/cu.usbserial-210}"
FQBN="esp32:esp32:esp32:PartitionScheme=min_spiffs"
SSID="${ESP32_WIFI_SSID:-manht2}"
PASS="${ESP32_WIFI_PASS:-}"
SKETCH="experiments/sketches/WifiBench"
OUT="build-experiments/wifi-bench"

ESPTOOL_DIR=$(python3 -c 'import pathlib; roots=sorted(pathlib.Path.home().glob("Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool"), key=lambda p: tuple(int(x) if x.isdigit() else x for x in p.parent.name.split("."))); print(roots[-1].parent if roots else "")')
ESPTOOL="$ESPTOOL_DIR/esptool"
MERGE=$( "$ESPTOOL" --help 2>/dev/null | grep -q 'merge-bin' && echo merge-bin || echo merge_bin )

echo "== Building WifiBench =="
arduino-cli compile --fqbn "$FQBN" "$SKETCH" \
  --output-dir "$OUT" \
  --build-property "compiler.cpp.extra_flags=-DESP32_WIFI_SSID=$SSID -DESP32_WIFI_PASS=$PASS -std=gnu++14"

echo "== Merging + flashing via $PORT =="
"$ESPTOOL" --chip esp32 $MERGE --output "$OUT/WifiBench.merged.bin" --target-offset 0x0 \
  0x1000 "$OUT/WifiBench.ino.bootloader.bin" \
  0x8000 "$OUT/WifiBench.ino.partitions.bin" \
  0x10000 "$OUT/WifiBench.ino.bin"
"$ESPTOOL" --chip esp32 --port "$PORT" --baud 460800 write_flash 0x0 \
  "$OUT/WifiBench.merged.bin"

echo "== Done. Watch startup log: =="
echo "   screen $PORT 115200   (Ctrl-A k to quit)"
