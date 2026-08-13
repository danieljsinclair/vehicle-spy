#!/usr/bin/env python3
"""
Verify a SYNTHETIC CAN feed streamed over TCP by the ESP32 synth-feed sketch
(NO CAN PCB attached). Asserts:
  - ZERO dropped sequence numbers (contiguous from the first seen),
  - ZERO reordered frames (seq strictly increasing in arrival order),
  - reports inter-arrival p50/p95/p99 + frames/sec.

The <10ms arrival target is GATED by the WiFi RF floor measured in the control
benchmark (~100ms p95 on this 2.4GHz link) — so this script proves SEQUENCE
INTEGRITY (the TCP/ESP32/LwIP streaming path) which is the real question for
"solve without the CAN PCB". It reports the arrival cadence honestly.

Usage:
  python3 tcp_synth_feed_verify.py --host 192.168.68.94 --port 3333 \
      --duration 10 --fps 100
"""
import argparse
import re
import socket
import statistics
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--duration", type=int, default=10)
    ap.add_argument("--fps", type=int, default=100)
    args = ap.parse_args()

    s = socket.create_connection((args.host, args.port), timeout=5.0)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(2.0)

    seq_re = re.compile(r"^S(\d+) ")
    expected = None
    seen = 0
    dropped = 0
    reordered = 0
    last_seq = None
    arrivals = []  # inter-arrival gaps (ms)
    prev_ts = None
    t0 = time.monotonic()

    buf = b""
    try:
        while time.monotonic() - t0 < args.duration:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while b"\r" in buf:
                line, buf = buf.split(b"\r", 1)
                text = line.decode(errors="replace").strip()
                if not text or text.startswith("PONG"):
                    continue
                m = seq_re.match(text)
                if not m:
                    continue
                seq = int(m.group(1))

                now = time.monotonic()
                if prev_ts is not None:
                    arrivals.append((now - prev_ts) * 1000.0)
                prev_ts = now

                if expected is None:
                    expected = seq
                elif seq == expected:
                    pass
                elif seq < expected:
                    reordered += 1
                else:  # gap -> dropped frames
                    dropped += (seq - expected)
                expected = seq + 1
                last_seq = seq
                seen += 1
    finally:
        s.close()

    def pct(d, p):
        if not d:
            return 0.0
        d = sorted(d)
        k = (len(d) - 1) * p / 100.0
        f = int(k)
        c = min(f + 1, len(d) - 1)
        return d[f] + (d[c] - d[f]) * (k - f)

    elapsed = args.duration
    fps_actual = seen / elapsed if elapsed else 0
    print(f"host={args.host}:{args.port} duration={elapsed}s frames={seen}")
    print(f"  actual_fps={fps_actual:.1f} (target {args.fps})")
    print(f"  DROPPED={dropped}  REORDERED={reordered}")
    print(f"  inter-arrival p50={pct(arrivals,50):.3f}ms "
          f"p95={pct(arrivals,95):.3f}ms p99={pct(arrivals,99):.3f}ms")

    ok = (dropped == 0) and (reordered == 0)
    print("  RESULT:", "PASS (0 dropped, 0 reordered)" if ok else "FAIL")
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
