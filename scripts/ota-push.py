#!/usr/bin/env python3
"""
ota-push.py — sign a firmware .bin with Ed25519ph and push to ESP32 over HTTP.

Usage:
    scripts/ota-push.py firmware.bin --host 192.168.1.50
    scripts/ota-push.py firmware.bin --host esp32-can.local --port 80
    scripts/ota-push.py firmware.bin --host 192.168.68.69 --keys-dir ~/.vehicle-sim/ota

Requirements: libsodium (via ctypes, uses system libsodium)
"""
import argparse
import base64
import ctypes
import ctypes.util
import http.client
import os
import socket
import sys

def load_sodium():
    sodium = ctypes.CDLL(ctypes.util.find_library('sodium'))
    if sodium.sodium_init() < 0:
        sys.exit("sodium_init failed")
    return sodium

def sign_firmware(sodium, image_path, priv_key_path):
    """Sign a firmware image with Ed25519ph (RFC 8032 pre-hashed)."""
    # Read private key and extract the raw 32-byte seed from PKCS#8 DER.
    import subprocess
    der = subprocess.check_output(['openssl', 'pkey', '-in', priv_key_path, '-outform', 'DER'])
    assert len(der) >= 48, f"private key DER too short ({len(der)} bytes)"
    seed_tag_offset = der.find(b'\x04\x20', 10)
    assert seed_tag_offset > 0, "could not find seed OCTET STRING in DER"
    seed_start = seed_tag_offset + 2
    seed = der[seed_start : seed_start + 32]
    assert len(seed) == 32

    # Derive keypair from seed
    pub_key = ctypes.create_string_buffer(32)
    priv_key = ctypes.create_string_buffer(64)
    if sodium.crypto_sign_seed_keypair(pub_key, priv_key, seed) != 0:
        sys.exit("crypto_sign_seed_keypair failed")

    # Read firmware
    with open(image_path, 'rb') as f:
        image = f.read()
    print(f"Image: {len(image)} bytes", file=sys.stderr)

    # Ed25519ph sign: init -> update (stream image) -> final_create
    state_size = sodium.crypto_sign_ed25519ph_statebytes()
    state = ctypes.create_string_buffer(state_size)
    if sodium.crypto_sign_ed25519ph_init(state) != 0:
        sys.exit("crypto_sign_ed25519ph_init failed")

    CHUNK = 4096
    for offset in range(0, len(image), CHUNK):
        chunk = image[offset:offset + CHUNK]
        if sodium.crypto_sign_ed25519ph_update(state, chunk, len(chunk)) != 0:
            sys.exit("crypto_sign_ed25519ph_update failed")

    sig = ctypes.create_string_buffer(64)
    sig_len = ctypes.c_ulonglong(0)
    if sodium.crypto_sign_ed25519ph_final_create(state, sig, ctypes.byref(sig_len), priv_key) != 0:
        sys.exit("crypto_sign_ed25519ph_final_create failed")

    assert sig_len.value == 64
    return bytes(sig.raw[:64])

def derive_public_key(priv_key_path):
    """Derive the public key from the private key for OtaPublicKey.h generation."""
    import subprocess
    pub_pem = subprocess.check_output(['openssl', 'pkey', '-in', priv_key_path, '-pubout', '-outform', 'DER'])
    # DER: 12-byte header + 32 raw bytes
    raw_pub = pub_pem[-32:]
    return bytes(raw_pub)

def push_ota(host, port, username, password, firmware_bytes, sig_bytes):
    """Push signed firmware to ESP32 via HTTP multipart POST."""
    boundary = "----OTABoundary7d2c4a9e1f"

    body = b''
    body += f'--{boundary}\r\n'.encode()
    body += b'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
    body += b'Content-Type: application/octet-stream\r\n\r\n'
    body += firmware_bytes + b'\r\n'
    body += f'--{boundary}--\r\n'.encode()

    auth = base64.b64encode(f'{username}:{password}'.encode()).decode()
    signature_hex = sig_bytes.hex()

    conn = http.client.HTTPConnection(host, port, timeout=600)
    print(f"OTA: uploading to {host}:{port}...", file=sys.stderr)
    headers = {
        'Content-Type': f'multipart/form-data; boundary={boundary}',
        'Content-Length': str(len(body)),
        'Authorization': f'Basic {auth}',
        'X-Firmware-Signature': signature_hex,
        'Connection': 'close',
    }

    import time as _time
    size_mb = len(firmware_bytes) / (1024 * 1024)
    print(f"OTA: pushing {size_mb:.1f} MiB to {host}:{port}...", file=sys.stderr)

    try:
        conn.request('POST', '/update', body, headers)
        # Read response with progress indication
        print("OTA: waiting for device to receive and verify...", file=sys.stderr)
        resp = conn.getresponse()
        resp_body = resp.read().decode('utf-8', errors='replace')
        conn.close()
    except (socket.timeout, TimeoutError) as exc:
        print(f"OTA: FAILED — timed out waiting for {host}:{port}", file=sys.stderr)
        print(f"     The upload may have succeeded but the device took too long to", file=sys.stderr)
        print(f"     verify the signature and respond. Check if the device rebooted.", file=sys.stderr)
        return False
    except (ConnectionRefusedError, ConnectionResetError, OSError) as exc:
        print(f"OTA: FAILED — {exc}", file=sys.stderr)
        return False

    if resp.status == 200:
        print(f"OTA: SUCCESS — device accepted update ({resp_body.strip()})", file=sys.stderr)
        print("OTA: device will reboot to apply new firmware.", file=sys.stderr)
        return True
    else:
        print(f"OTA: FAILED — HTTP {resp.status}: {resp_body}", file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description='Sign and push firmware to ESP32 over HTTP OTA')
    parser.add_argument('firmware', help='Path to firmware .bin')
    parser.add_argument('--host', required=True, help='ESP32 IP/hostname')
    parser.add_argument('--port', type=int, default=80, help='HTTP port (default: 80)')
    parser.add_argument('--username', default='ota', help='HTTP Basic Auth username')
    parser.add_argument('--password', default='vehicle-sim', help='HTTP Basic Auth password')
    parser.add_argument('--keys-dir', default=os.path.expanduser('~/.vehicle-sim/ota'),
                        help='Directory containing ed25519.pem')
    args = parser.parse_args()

    if not os.path.isfile(args.firmware):
        sys.exit(f"Error: firmware not found: {args.firmware}")

    priv_key = os.path.join(args.keys_dir, 'ed25519.pem')
    if not os.path.isfile(priv_key):
        sys.exit(f"Error: private key not found: {priv_key}\n"
                 f"Run: scripts/ota-generate-keys.sh --keys-dir {args.keys_dir}")

    sodium = load_sodium()
    print("Signing firmware...", file=sys.stderr)
    sig = sign_firmware(sodium, args.firmware, priv_key)
    print(f"Signature: 64 bytes (Ed25519ph)", file=sys.stderr)

    with open(args.firmware, 'rb') as f:
        firmware_bytes = f.read()

    success = push_ota(args.host, args.port, args.username, args.password, firmware_bytes, sig)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
