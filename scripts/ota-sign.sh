#!/usr/bin/env bash
# ota-sign.sh — sign a firmware binary with your ed25519 private key.
#
#   Produces a detached 64-byte signature alongside the image:
#     firmware.bin -> firmware.bin.sig
#
# Usage:
#   scripts/ota-sign.sh firmware/build/can-bridge.ino.bin
#   scripts/ota-sign.sh image.bin --out image.bin.sig
#   scripts/ota-sign.sh image.bin --keys-dir ~/.vehicle-sim/ota
#
# Requires: openssl 3.x
set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────
KEYS_DIR="${HOME}/.vehicle-sim/ota"
PRIV_KEY="${KEYS_DIR}/ed25519.pem"
OUT=""
IMAGE=""

# ── Args ────────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --keys-dir) KEYS_DIR="$2"; PRIV_KEY="${KEYS_DIR}/ed25519.pem"; shift 2 ;;
        --out)      OUT="$2"; shift 2 ;;
        --help|-h)  sed -n '2,15p' "$0"; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; exit 2 ;;
        *)
            [[ -z "${IMAGE}" ]] || { echo "Error: multiple image args" >&2; exit 2; }
            IMAGE="$1"; shift ;;
    esac
done

# ── Checks ──────────────────────────────────────────────────────────────
command -v openssl >/dev/null 2>&1 || { echo "Error: openssl not found." >&2; exit 1; }
[[ -n "${IMAGE}" ]]       || { echo "Usage: $0 <firmware.bin> [--out x.sig]" >&2; exit 2; }
[[ -f "${IMAGE}" ]]       || { echo "Error: image not found: ${IMAGE}" >&2; exit 1; }
[[ -f "${PRIV_KEY}" ]]    || { echo "Error: private key not found: ${PRIV_KEY}" >&2;
                               echo "       Run: scripts/ota-generate-keys.sh" >&2; exit 1; }
[[ -z "${OUT}" ]] && OUT="${IMAGE}.sig"

# ── Sign (detached ed25519ph — pre-hashed Ed25519, RFC 8032) ─────────────
# The device verifies with crypto_sign_ed25519ph_final_verify, streaming the
# image bytes through libsodium's incremental SHA-512 state. The host signs
# with crypto_sign_ed25519ph_final_create via ctypes + system libsodium.
# Standard libsodium on both sides — no custom crypto.

# Write a temp Python script for Ed25519ph signing (inline to avoid extra files)
PY_SIGN_SCRIPT="$(mktemp -t ota-sign-ph-XXXXXX.py)"
trap 'rm -f "${PY_SIGN_SCRIPT}"' EXIT

cat > "${PY_SIGN_SCRIPT}" << 'PYEOF'
import ctypes, ctypes.util, sys, os

# Load system libsodium
sodium = ctypes.CDLL(ctypes.util.find_library('sodium'))
if sodium.sodium_init() < 0:
    sys.exit("sodium_init failed")

# Ed25519ph constants
_crypto_sign_ed25519ph_statebytes = sodium.crypto_sign_ed25519ph_statebytes
_crypto_sign_ed25519ph_statebytes.restype = ctypes.c_size_t
CRYPTO_SIGN_ED25519PH_STATEBYTES = _crypto_sign_ed25519ph_statebytes()
CRYPTO_SIGN_ED25519_BYTES = 64

priv_key_path = sys.argv[1]
image_path = sys.argv[2]
out_path = sys.argv[3]

# Read private key and extract the raw 32-byte seed from the PKCS#8 DER encoding.
# DER layout: 30 2e 02 01 00 30 05 06 03 2b 65 70 04 22 04 20 <32-byte seed>
# The seed starts at a fixed offset (byte 18) in the 48-byte DER structure.
import subprocess
der = subprocess.check_output(['openssl', 'pkey', '-in', priv_key_path, '-outform', 'DER'])
assert len(der) >= 48, f"private key DER too short ({len(der)} bytes)"
# PKCS#8 Ed25519 DER: ... 04 20 <32-byte-seed>
# The outer OCTET STRING (tag 0x04, length 0x20=32) wraps the raw seed.
# Find the 04 20 pattern after the OID (starts around offset 10).
seed_tag_offset = der.find(b'\x04\x20', 10)
assert seed_tag_offset > 0, "could not find seed OCTET STRING in DER"
seed_start = seed_tag_offset + 2  # skip tag + length
seed = der[seed_start : seed_start + 32]
assert len(seed) == 32, f"seed is {len(seed)} bytes, expected 32"

# Read image
with open(image_path, 'rb') as f:
    image = f.read()

# Derive the 64-byte Ed25519 secret key from the 32-byte seed.
# crypto_sign_seed_keypair(seed) -> (pubkey, privkey) where privkey = seed||pubkey
pub_key = ctypes.create_string_buffer(32)
priv_key = ctypes.create_string_buffer(64)
if sodium.crypto_sign_seed_keypair(pub_key, priv_key, seed) != 0:
    sys.exit("crypto_sign_seed_keypair failed")

# Ed25519ph sign: init -> update (stream image) -> final_create
state = ctypes.create_string_buffer(CRYPTO_SIGN_ED25519PH_STATEBYTES)
if sodium.crypto_sign_ed25519ph_init(state) != 0:
    sys.exit("crypto_sign_ed25519ph_init failed")

CHUNK = 4096
for offset in range(0, len(image), CHUNK):
    chunk = image[offset:offset + CHUNK]
    if sodium.crypto_sign_ed25519ph_update(state, chunk, len(chunk)) != 0:
        sys.exit("crypto_sign_ed25519ph_update failed")

sig = ctypes.create_string_buffer(CRYPTO_SIGN_ED25519_BYTES)
sig_len = ctypes.c_ulonglong(0)
if sodium.crypto_sign_ed25519ph_final_create(state, sig, ctypes.byref(sig_len), priv_key) != 0:
    sys.exit("crypto_sign_ed25519ph_final_create failed")

assert sig_len.value == 64, f"sig is {sig_len.value} bytes, expected 64"

with open(out_path, 'wb') as f:
    f.write(sig.raw[:64])
PYEOF

python3 "${PY_SIGN_SCRIPT}" "${PRIV_KEY}" "${IMAGE}" "${OUT}"

SIG_BYTES=$(wc -c < "${OUT}" | tr -d ' ')
[[ "${SIG_BYTES}" -eq 64 ]] || {
    echo "Error: signature is ${SIG_BYTES} bytes, expected 64 (ed25519ph)." >&2
    rm -f "${OUT}"; exit 1
}

echo "Signed: ${IMAGE}"
echo "   sig: ${OUT}  (64-byte ed25519ph detached, SHA-512 pre-hashed)"
echo "Verify on device: crypto_sign_ed25519ph_final_verify(sig, pubkey)"
