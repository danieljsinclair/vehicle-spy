#!/usr/bin/env python3
"""
serial-send-read.py — single-process serial send + read-back with expect match.

Opens a serial port, sends a string, reads lines until an expected substring
is seen or a timeout expires. Prints everything received. Used by Makefile
targets that need deterministic read-back confirmation (e.g. ATCLEARWIFI,
ATSETWIFI) without the open/write-close-then-open/read race that loses the
device's reply.

Usage:
    scripts/serial-send-read.py --port /dev/cu.usbserial-10 --send 'ATCLEARWIFI\r' --expect 'cleared' --timeout 8

Exit codes:
    0  expect string found in received data
    1  timeout expired before expect string was seen
    2  bad arguments or serial error

Requires: pyserial (brew install pyserial || pip3 install pyserial)
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required. Install with: pip3 install pyserial", file=sys.stderr)
    sys.exit(2)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Serial send + read-back with expect-match (single process, no race)."
    )
    parser.add_argument("--port", required=True, help="Serial port path (e.g. /dev/cu.usbserial-10)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--send", required=True, help="String to write to the port (e.g. 'ATCLEARWIFI\\r')")
    parser.add_argument("--expect", required=True, help="Substring to match in received data (case-sensitive)")
    parser.add_argument("--timeout", type=float, default=8.0, help="Read timeout in seconds (default: 8)")
    return parser.parse_args()


def main():
    args = parse_args()

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baud,
            timeout=0.1,  # non-blocking read timeout per call
            write_timeout=2.0,
        )
    except serial.SerialException as exc:
        print(f"ERROR: cannot open {args.port}: {exc}", file=sys.stderr)
        sys.exit(2)

    ser.reset_input_buffer()
    ser.reset_output_buffer()

    received = []

    try:
        ser.write(args.send.encode("utf-8"))
        ser.flush()
    except serial.SerialException as exc:
        print(f"ERROR: write failed: {exc}", file=sys.stderr)
        ser.close()
        sys.exit(2)

    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        try:
            chunk = ser.read(4096)
        except serial.SerialException as exc:
            print(f"ERROR: read failed: {exc}", file=sys.stderr)
            ser.close()
            sys.exit(2)

        if chunk:
            text = chunk.decode("utf-8", errors="replace")
            received.append(text)
            print(text, end="", flush=True)
            if args.expect in "".join(received):
                ser.close()
                sys.exit(0)

    ser.close()

    print(
        f"\nWARN: no reply within {args.timeout}s (expected '{args.expect}' not seen)",
        file=sys.stderr,
    )
    sys.exit(1)


if __name__ == "__main__":
    main()
