#!/usr/bin/env bash
# ota-roundtrip-test.sh — prove the OTA signing/verify toolchain works.
#
# Mirrors the device's exact verify path (libsodium crypto_sign_ed25519_verify_detached)
# on the host, against an OpenSSL-signed test image. This proves:
#   1. ota-generate-keys.sh produces a valid keypair + header.
#   2. ota-sign.sh produces a 64-byte detached ed25519 signature.
#   3. The OpenSSL signature verifies under libsodium — i.e. the ESP32's
#      baked-in verify (same libsodium call) WILL accept these images.
#
# Exit 0 = pass. Creates only temp files (cleaned up). Does NOT touch the repo.
#
# Requires: openssl, libsodium ( brew install libsodium ), a C compiler.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SODIUM_PREFIX="$(brew --prefix libsodium 2>/dev/null || true)"

command -v openssl >/dev/null 2>&1 || { echo "FAIL: openssl missing"; exit 1; }
command -v cc >/dev/null 2>&1      || { echo "FAIL: cc missing"; exit 1; }
[[ -n "${SODIUM_PREFIX}" && -f "${SODIUM_PREFIX}/lib/libsodium.a" ]] || {
    echo "FAIL: libsodium not found. Run: brew install libsodium"; exit 1; }

WORK="$(mktemp -d -t ota-rt)"
trap 'rm -rf "${WORK}"' EXIT

echo "=== OTA round-trip test ==="
echo "work dir: ${WORK}"
echo

# ── 1. Generate keys into the temp dir (isolated from any real keys) ─────
echo "[1/5] Generate ed25519 keypair..."
bash "${REPO_ROOT}/scripts/ota-generate-keys.sh" \
    --keys-dir "${WORK}/keys" \
    --header "${WORK}/OtaPublicKey.h" >/dev/null
[[ -f "${WORK}/keys/ed25519.pem" ]]         || { echo "FAIL: private key not created"; exit 1; }
[[ -f "${WORK}/OtaPublicKey.h" ]]           || { echo "FAIL: header not created"; exit 1; }

# Idempotency: re-run without --force must keep the same key.
PRIV_HASH_1="$(openssl pkey -in "${WORK}/keys/ed25519.pem" -pubout -outform DER 2>/dev/null | shasum -a 256 | cut -d' ' -f1)"
bash "${REPO_ROOT}/scripts/ota-generate-keys.sh" \
    --keys-dir "${WORK}/keys" \
    --header "${WORK}/OtaPublicKey.h" >/dev/null
PRIV_HASH_2="$(openssl pkey -in "${WORK}/keys/ed25519.pem" -pubout -outform DER 2>/dev/null | shasum -a 256 | cut -d' ' -f1)"
[[ "${PRIV_HASH_1}" == "${PRIV_HASH_2}" ]] || { echo "FAIL: keygen not idempotent"; exit 1; }
echo "   keypair OK (idempotent)"

# ── 2. Build a C verifier that uses the EXACT device call ───────────────
echo "[2/5] Build host libsodium verifier (same call as firmware)..."
cat > "${WORK}/verify.c" <<'EOF'
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reads pubkey (32 raw bytes), signature (64 bytes), message (rest).
 * Returns 0 if libsodium verifies (same as device), non-zero otherwise. */
int main(int argc, char** argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s pub sig msg\n", argv[0]); return 2; }
    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 3; }

    FILE* fp = fopen(argv[1], "rb"); if (!fp) return 4;
    unsigned char pk[32]; if (fread(pk, 1, 32, fp) != 32) return 5; fclose(fp);

    fp = fopen(argv[2], "rb"); if (!fp) return 6;
    unsigned char sig[64]; if (fread(sig, 1, 64, fp) != 64) return 7; fclose(fp);

    fp = fopen(argv[3], "rb"); if (!fp) return 8;
    fseek(fp, 0, SEEK_END); long mlen = ftell(fp); fseek(fp, 0, SEEK_SET);
    unsigned char* m = malloc(mlen);
    if (!m || fread(m, 1, mlen, fp) != (size_t)mlen) return 9; fclose(fp);

    /* The exact call the ESP32 firmware makes: */
    int ok = crypto_sign_ed25519_verify_detached(sig, m, (unsigned long long)mlen, pk);
    free(m);
    if (ok == 0) { printf("VALID\n"); return 0; }
    printf("INVALID\n"); return 1;
}
EOF
cc -O2 -I"${SODIUM_PREFIX}/include" "${WORK}/verify.c" \
   "${SODIUM_PREFIX}/lib/libsodium.a" -o "${WORK}/verify" 2>/dev/null
[[ -x "${WORK}/verify" ]] || { echo "FAIL: could not build verifier"; exit 1; }
echo "   verifier built"

# ── 3. Extract raw 32-byte public key for the C verifier ────────────────
openssl pkey -in "${WORK}/keys/ed25519.pem" -pubout -outform DER 2>/dev/null \
    | tail -c 32 > "${WORK}/pub.bin"
[[ $(wc -c < "${WORK}/pub.bin" | tr -d ' ') -eq 32 ]] || { echo "FAIL: pub extraction"; exit 1; }

# ── 4. Sign a test image and verify it ──────────────────────────────────
echo "[3/5] Create test image + sign it..."
head -c 2048 /dev/urandom > "${WORK}/image.bin"
bash "${REPO_ROOT}/scripts/ota-sign.sh" \
    --keys-dir "${WORK}/keys" "${WORK}/image.bin" --out "${WORK}/image.sig" >/dev/null
[[ $(wc -c < "${WORK}/image.sig" | tr -d ' ') -eq 64 ]] || { echo "FAIL: sig not 64 bytes"; exit 1; }
echo "   signed (64-byte detached sig)"

echo "[4/5] Verify with libsodium (device-equivalent)..."
if "${WORK}/verify" "${WORK}/pub.bin" "${WORK}/image.sig" "${WORK}/image.bin" | grep -q VALID; then
    echo "   VALID — OpenSSL-signed image verifies under libsodium"
else
    echo "FAIL: libsodium rejected a correctly-signed image"; exit 1
fi

# ── 5. Negative test: a tampered image must FAIL ────────────────────────
echo "[5/5] Tamper-resistance: flip a byte, must reject..."
cp "${WORK}/image.bin" "${WORK}/image.bad"
# XOR a byte with 0xFF — guarantees a change regardless of its value.
python3 -c "
import sys
p='${WORK}/image.bad'
d=bytearray(open(p,'rb').read())
d[1024]^=0xFF
open(p,'wb').write(d)
"
set +e
BAD_OUT="$("${WORK}/verify" "${WORK}/pub.bin" "${WORK}/image.sig" "${WORK}/image.bad" 2>&1)"
set -e
echo "   verifier output for tampered image: ${BAD_OUT}"
case "${BAD_OUT}" in
    *INVALID*) echo "   INVALID — tampered image correctly rejected" ;;
    *) echo "FAIL: tampered image was accepted (signature bypass!)"; exit 1 ;;
esac

echo
echo "=== ALL CHECKS PASSED ==="
echo "OpenSSL ed25519 sign  ->  libsodium ed25519 verify : interoperable"
echo "The ESP32 firmware uses crypto_sign_ed25519_verify_detached (same call)."
