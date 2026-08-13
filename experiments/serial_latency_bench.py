#!/usr/bin/env python3
"""
USB/serial delivery measurement for SerialBench.ino on the ESP32.

Two measurements on the REAL 115200-baud USB CDC link (macOS host side):

  1) BURST ceiling: send 'G', count frames received over a window, compute
     frames/sec (the actual USB/serial throughput ceiling — compares to the
     doc's ~460fps theoretical bound). Also count dropped seq (gaps).

  2) RATE latency: send 'R <hz>' for 20 and 100 Hz, measure per-frame delivery
     cadence (inter-arrival p50/p95/p99) the macOS client sees. Latency here is
     arrival-skew at the client because the only clock is the host's; the
     single-host echo approach used for TCP is unavailable over one-way serial,
     so we report cadence (inter-arrival) which bounds delivery freshness.

Frame text (SerialBench): "1F4 <4 hex bytes> seq=<n>\r"  (~20 bytes incl CR)

Usage:
  python3 serial_latency_bench.py --port /dev/cu.usbserial-210 --baud 115200
"""
import argparse
import re
import serial
import time


def percentile(data, p):
    if not data:
        return 0.0
    s = sorted(data)
    k = (len(s) - 1) * p / 100.0
    f = int(k)
    c = min(f + 1, len(s) - 1)
    if f == c:
        return s[f]
    return s[f] + (s[c] - s[f]) * (k - f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbserial-210")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--burst-duration", type=float, default=5.0)
    ap.add_argument("--rate-duration", type=float, default=5.0)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=2.0)
    time.sleep(0.3)
    ser.reset_input_buffer()

    frame_re = re.compile(rb"^1F4 [0-9A-F]{8} seq=(\d+)$")

    # ---- BURST ceiling ----
    ser.write(b"G\n")
    time.sleep(0.3)
    n = 0
    seqs = []
    t0 = time.monotonic()
    buf = b""
    while time.monotonic() - t0 < args.burst_duration:
        chunk = ser.read(ser.in_waiting or 1)
        if not chunk:
            continue
        buf += chunk
        while b"\r" in buf:
            line, buf = buf.split(b"\r", 1)
            m = frame_re.match(line)
            if m:
                seqs.append(int(m.group(1)))
                n += 1
    elapsed = time.monotonic() - t0
    ser.write(b"S\n")
    dropped = 0
    if seqs:
        sset = set(seqs)
        full = max(seqs) + 1
        dropped = full - len(sset)
    print(f"[USB] BURST: frames={n} over {elapsed:.2f}s "
          f"-> {n/elapsed:.1f} fps  (drop_est={dropped})")

    # ---- RATE cadence at 20 / 100 Hz ----
    for hz in (20, 100):
        ser.write(f"R {hz}\n".encode())
        time.sleep(0.3)
        arrivals = []
        prev = None
        t0 = time.monotonic()
        buf = b""
        while time.monotonic() - t0 < args.rate_duration:
            chunk = ser.read(ser.in_waiting or 1)
            if not chunk:
                continue
            buf += chunk
            while b"\r" in buf:
                line, buf = buf.split(b"\r", 1)
                if not frame_re.match(line):
                    continue
                now = time.monotonic()
                if prev is not None:
                    arrivals.append((now - prev) * 1000.0)
                prev = now
        ser.write(b"S\n")
        if arrivals:
            print(f"[USB] RATE {hz}Hz: frames={len(arrivals)} "
                  f"inter-arrival p50={percentile(arrivals,50):.2f}ms "
                  f"p95={percentile(arrivals,95):.2f}ms "
                  f"p99={percentile(arrivals,99):.2f}ms "
                  f"max={max(arrivals):.2f}ms")
        else:
            print(f"[USB] RATE {hz}Hz: NO FRAMES (check sketch)")
    ser.close()


if __name__ == "__main__":
    main()
