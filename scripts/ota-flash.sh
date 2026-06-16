#!/usr/bin/env bash
# ota-flash.sh — push a signed firmware to an ESP32 over WiFi (OTA, port 3334).
#
# Sends image + detached ed25519 signature per the firmware's OTA protocol:
#   magic("OTA1") | uint32 image_size | uint8 sig_size | 64-byte sig | image
# Expects one ASCII reply: "OK\n" or "ERR:<reason>\n".
#
# Usage:
#   scripts/ota-flash.sh 192.168.1.50 firmware/can-bridge.ino.bin
#   scripts/ota-flash.sh esp32-can.local image.bin --sig image.bin.sig --port 3334
#
# Requires: bash 4+, /dev/tcp (macOS default shell is zsh — run via bash).
set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────
PORT=3334
SIG=""
IMAGE=""
HOST=""
REPLY_TIMEOUT=15  # seconds to wait for the final OK/ERR after upload

# ── Args ────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)  PORT="$2"; shift 2 ;;
        --sig)   SIG="$2"; shift 2 ;;
        --timeout) REPLY_TIMEOUT="$2"; shift 2 ;;
        --help|-h) sed -n '2,13p' "$0"; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            if [[ -z "${HOST}" ]]; then HOST="$1"
            elif [[ -z "${IMAGE}" ]]; then IMAGE="$1"
            else echo "Error: unexpected extra arg: $1" >&2; exit 2; fi
            shift ;;
    esac
done

# ── Checks ──────────────────────────────────────────────────────────────
[[ -n "${HOST}" ]]  || { echo "Usage: $0 <host> <image.bin> [--sig x.sig] [--port N]" >&2; exit 2; }
[[ -n "${IMAGE}" ]] || { echo "Usage: $0 <host> <image.bin>" >&2; exit 2; }
[[ -f "${IMAGE}" ]] || { echo "Error: image not found: ${IMAGE}" >&2; exit 1; }
[[ -z "${SIG}" ]] && SIG="${IMAGE}.sig"
[[ -f "${SIG}" ]]  || { echo "Error: signature not found: ${SIG}" >&2;
                        echo "       Run: scripts/ota-sign.sh ${IMAGE}" >&2; exit 1; }

SIG_BYTES=$(wc -c < "${SIG}" | tr -d ' ')
[[ "${SIG_BYTES}" -eq 64 ]] || { echo "Error: signature must be 64 bytes (got ${SIG_BYTES})." >&2; exit 1; }
IMG_BYTES=$(wc -c < "${IMAGE}" | tr -d ' ')

echo "OTA push -> ${HOST}:${PORT}"
echo "   image: ${IMAGE}  (${IMG_BYTES} bytes)"
echo "   sig:   ${SIG}    (64 bytes ed25519)"

# ── Build the protocol payload ──────────────────────────────────────────
TMP="$(mktemp -t ota-payload)"
trap 'rm -f "${TMP}"' EXIT

# Assemble the binary payload with Python (struct) — robust byte-precise
# output, avoids shell printf/escape pitfalls. Python is already a project
# dependency (scripts/archive/serial_to_csv.py).
python3 -c "
import sys, struct
sig = open('${SIG}', 'rb').read()
img = open('${IMAGE}', 'rb').read()
assert len(sig) == 64, 'sig must be 64 bytes'
out = b'OTA1' + struct.pack('<I', len(img)) + bytes([64]) + sig + img
sys.stdout.buffer.write(out)
" > "${TMP}"

PAYLOAD_BYTES=$(wc -c < "${TMP}" | tr -d ' ')
# header(4) + size(4) + siglen(1) + sig(64) + image
EXPECTED=$(( 9 + 64 + IMG_BYTES ))
[[ "${PAYLOAD_BYTES}" -eq "${EXPECTED}" ]] || {
    echo "Error: payload assembled to ${PAYLOAD_BYTES} bytes, expected ${EXPECTED}." >&2
    exit 1
}

# ── Send + read reply ───────────────────────────────────────────────────
# Uses bash /dev/tcp. The device may reboot immediately on OK, closing the
# socket — so tolerate a dropped connection after a clean "OK".
REPLY_FILE="$(mktemp -t ota-reply)"
trap 'rm -f "${TMP}" "${REPLY_FILE}"' EXIT

# Try to open the TCP connection. Failure here = host unreachable / port closed.
exec 3<>"/dev/tcp/${HOST}/${PORT}" 2>/dev/null || {
    echo "Error: cannot connect to ${HOST}:${PORT} (is the device online? OTA server running?)." >&2
    exit 1
}

set +e
cat "${TMP}" >&3
# Read the reply (small, newline-terminated). Give the device time to verify +
# commit before it reboots. The read may return early if the device reboots
# after sending OK — that's fine, we already have the reply bytes.
timeout "${REPLY_TIMEOUT}" head -c 128 <&3 > "${REPLY_FILE}" 2>/dev/null
RC=$?
set -e
exec 3<&- 2>/dev/null || true
exec 3>&- 2>/dev/null || true

REPLY="$(cat "${REPLY_FILE}" 2>/dev/null | tr -d '\0')"

if [[ "${RC}" -ne 0 && -z "${REPLY}" ]]; then
    echo "Error: failed to connect or send to ${HOST}:${PORT} (rc=${RC})." >&2
    echo "       Is the device online and the OTA server running?" >&2
    exit 1
fi

case "${REPLY}" in
    OK*)
        echo "SUCCESS — device accepted the image and will reboot."
        exit 0 ;;
    ERR:signature*)
        echo "FAILED — device reports signature rejected." >&2
        echo "       Reply: ${REPLY}" >&2
        echo "       The device's baked public key does not match the private key" >&2
        echo "       that signed this image. Re-flash via USB (make flash) with" >&2
        echo "       your public key, or sign with the matching private key." >&2
        exit 2 ;;
    ERR:nopartition*)
        echo "FAILED — device reports no OTA partition." >&2
        echo "       Reply: ${REPLY}" >&2
        echo "       Re-flash via USB with an OTA partition scheme" >&2
        echo "       (e.g. PartitionScheme=min_spiffs)." >&2
        exit 2 ;;
    ERR*)
        echo "FAILED — device rejected the OTA upload." >&2
        echo "       Reply: ${REPLY}" >&2
        echo "       Check the ESP32 serial startup log for the matching OTA error." >&2
        exit 2 ;;
    *)
        echo "FAILED — no valid reply from device." >&2
        echo "       Raw reply: ${REPLY:-<empty>}" >&2
        echo "       It may have rebooted before replying, or the connection dropped." >&2
        echo "       Check the ESP32 serial startup log for OTA server output." >&2
        exit 1 ;;
esac
