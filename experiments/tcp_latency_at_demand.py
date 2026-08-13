#!/usr/bin/env python3
"""
Measure per-frame DELIVERY LATENCY of the ESP32 CAN link at REAL demand rates
(20Hz and 100Hz), over TCP (WifiBench.ino ECHO mode).

Method (pipelined, async-style recv so the measured rate is NOT collapsed to
1/RTT): the macOS client keeps up to WINDOW in-flight tagged frames
("P <seq> <client_send_us>"). It sends at the requested Hz from a single
monotonic clock; a background recv loop timestamps each echoed frame and
computes RTT = host_recv_ts - client_send_ts (same monotonic clock, no sync).
This separates SEND RATE (the demand) from LATENCY (the link), which is the
real question for "does TCP at 20-100Hz beat USB latency".

Also reports inter-arrival jitter and a 0-drop / reorder check on the echo seq.

Usage:
  python3 tcp_latency_at_demand.py --host 192.168.68.87 --port 3333 \
      --hz 100 --duration 10
"""
import argparse
import re
import socket
import statistics
import threading
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
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--hz", type=int, required=True)
    ap.add_argument("--duration", type=float, default=10.0)
    ap.add_argument("--window", type=int, default=64,
                    help="max in-flight frames (decouples send rate from RTT)")
    args = ap.parse_args()

    interval = 1.0 / args.hz
    s = socket.create_connection((args.host, args.port), timeout=5.0)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(2.0)
    s.sendall(b"ECHO\n")

    # Shared state between sender thread and recv thread.
    send_ts = {}          # seq -> send monotonic ts (s)
    recv_lock = threading.Lock()
    lat = []
    arrivals = []
    seen_seqs = []
    last_recv_ts = [None]
    stop = threading.Event()

    def recv_loop():
        s.settimeout(1.0)
        buf = b""
        while not stop.is_set():
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while b"\r" in buf:
                line, buf = buf.split(b"\r", 1)
                line = line.rstrip(b"\n")
                m = re.match(rb"^P (\d+) (\d+)$", line)
                if not m:
                    continue
                seq = int(m.group(1))
                st = int(m.group(2)) / 1e6
                rt = time.monotonic()
                with recv_lock:
                    send_ts.pop(seq, None)
                    rtt_ms = (rt - st) * 1000.0
                    lat.append(rtt_ms)
                    seen_seqs.append(seq)
                    if last_recv_ts[0] is not None:
                        arrivals.append((rt - last_recv_ts[0]) * 1000.0)
                    last_recv_ts[0] = rt

    t0 = time.monotonic()
    next_send = t0
    seq = 0
    rthread = threading.Thread(target=recv_loop, daemon=True)
    rthread.start()
    try:
        while time.monotonic() - t0 < args.duration:
            now = time.monotonic()
            if now >= next_send and len(send_ts) < args.window:
                seq += 1
                with recv_lock:
                    send_ts[seq] = now
                s.sendall(f"P {seq} {int(now*1e6)}\n".encode())
                next_send = now + interval
            else:
                time.sleep(0.0005)
    finally:
        stop.set()
        s.close()
        rthread.join(timeout=2.0)

    # Drop/reorder analysis on seen seq (may be unordered due to parallelism).
    seen_seqs_sorted = sorted(seen_seqs)
    dropped = 0
    reordered = 0
    if seen_seqs_sorted:
        # contiguous from min..max?
        full = set(range(seen_seqs_sorted[0], seen_seqs_sorted[-1] + 1))
        dropped = len(full - set(seen_seqs_sorted))
        # reorder = any earlier seq arriving after a later one (seen order vs seq)
        last = -1
        for sq in seen_seqs:
            if sq < last:
                reordered += 1
            last = sq

    elapsed = args.duration
    fps = len(seen_seqs) / elapsed if elapsed else 0
    print(f"host={args.host}:{args.port} mode=TCP-ECHO hz={args.hz} "
          f"duration={elapsed:.1f}s")
    print(f"  frames_recv={len(seen_seqs)} achieved_fps={fps:.1f} "
          f"(target {args.hz})")
    print(f"  DROPPED={dropped}  REORDERED={reordered}")
    if lat:
        print(f"  latency p50={percentile(lat,50):.3f}ms "
              f"p95={percentile(lat,95):.3f}ms "
              f"p99={percentile(lat,99):.3f}ms max={max(lat):.3f}ms "
              f"mean={statistics.fmean(lat):.3f}ms")
    if arrivals:
        print(f"  inter-arrival p50={percentile(arrivals,50):.3f}ms "
              f"p95={percentile(arrivals,95):.3f}ms "
              f"p99={percentile(arrivals,99):.3f}ms "
              f"jitter(p99-p50)={percentile(arrivals,99)-percentile(arrivals,50):.3f}ms")


if __name__ == "__main__":
    main()
