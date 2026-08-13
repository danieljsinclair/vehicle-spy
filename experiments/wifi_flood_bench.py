#!/usr/bin/env python3
"""
WifiBench FLOOD client — measure the ABSOLUTE TCP throughput of the ESP32 link
(manht2 2.4GHz WiFi). The ESP32 (WifiBench.ino, FLOOD mode) spams 32-byte
datagrams as fast as LwIP+WiFi allow; this client counts bytes over a window.

Output: Mbit/s (TCP), implied frames/sec at 32-byte frames, and a drop estimate
(device's last seq vs frames received — i.e. link/socket loss under saturation).

For per-frame latency at REAL demand (20/100 Hz), use tcp_latency_at_demand.py
(ECHO mode, single-host round-trip — no clock sync needed).

Usage:
  python3 wifi_flood_bench.py --host 192.168.68.87 --port 3333 --duration 5
"""
import argparse
import re
import socket
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--duration", type=float, default=5.0)
    args = ap.parse_args()

    s = socket.create_connection((args.host, args.port), timeout=5.0)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(2.0)
    s.sendall(b"FLOOD\n")

    seq_re = re.compile(rb"^D(\d+) ")
    last_seq = None
    received = 0
    bytes_recv = 0
    t0 = time.monotonic()
    buf = b""
    max_gap = 0
    try:
        while time.monotonic() - t0 < args.duration:
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            bytes_recv += len(chunk)
            buf += chunk
            while b"\r" in buf:
                line, buf = buf.split(b"\r", 1)
                m = seq_re.match(line)
                if not m:
                    continue
                seq = int(m.group(1))
                if last_seq is not None:
                    gap = seq - last_seq
                    if gap > 1:
                        max_gap = max(max_gap, gap - 1)
                last_seq = seq
                received += 1
    finally:
        s.close()

    elapsed = time.monotonic() - t0
    mbits = (bytes_recv * 8.0) / (elapsed * 1_000_000.0) if elapsed else 0.0
    fps = received / elapsed if elapsed else 0.0
    lost = (last_seq + 1 - received) if last_seq is not None else 0

    print(f"host={args.host}:{args.port} duration={elapsed:.2f}s")
    print(f"  bytes_recv={bytes_recv}")
    print(f"  TCP_THROUGHPUT={mbits:.3f} Mbit/s")
    print(f"  frames_recv={received}  implied_fps@32B={fps:.1f}")
    print(f"  device_last_seq={last_seq}  lost_est={lost}  max_contig_gap={max_gap}")
    print(f"  -> Espressif spec 20 Mbit/s TCP ~= "
          f"{20e6/8/32:.0f} frames/sec @32B (theoretical ceiling)")


if __name__ == "__main__":
    main()

