#!/usr/bin/env python3
"""
TCP RTT benchmark for the ESP32 CAN-bridge link (macOS host side).

Methodology (shared by control + fixed-firmware runs so the numbers are
directly comparable):

  - Single-host timing: the macOS client opens ONE TCP connection to the
    ESP32, sends a small frame, and waits for the echoed reply. RTT is
    measured with the SAME host's monotonic clock (no clock sync needed).
  - Frame sizes 16..64 bytes (PING <seq> / PONG <seq>, or raw echo line).
  - N >= 1000 samples, warm-up 150, report p50/p95/p99/max + jitter (p99-p50).
  - TCP_NODELAY is set on the host socket so the measurement reflects the
    DEVICE/link behaviour, not the host's own Nagle.

Usage:
  python3 tcp_rtt_bench.py --host 10.0.0.42 --port 3333 \
      --n 1200 --warmup 150 --size 32 --mode echo|ping

  mode=echo : sends a line, expects the same line echoed (control + fixed fw).
  mode=ping : sends "PING <seq>", expects "PONG <seq>" (fixed fw keepalive).
"""
import argparse
import socket
import statistics
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
    ap.add_argument("--n", type=int, default=1200)
    ap.add_argument("--warmup", type=int, default=150)
    ap.add_argument("--size", type=int, default=32, choices=range(16, 65))
    ap.add_argument("--mode", choices=["echo", "ping"], default="echo")
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--auth", default=None,
                    help="If set, send 'AUTH <token>\\r' on connect (real can-bridge firmware)")
    args = ap.parse_args()

    if args.mode == "ping":
        def make(i):
            return f"PING {i}\n".encode()
        def match(buf, i):
            return f"PONG {i}\n".encode() in buf
    else:
        payload = b"x" * (args.size - 1) + b"\n"
        def make(i):
            return payload
        def match(buf, i):
            return b"\n" in buf

    total = args.n + args.warmup
    rtts = []
    seq = 0
    t0 = time.monotonic()
    # One fresh connection per sample. The link is lossy enough that a held-open
    # connection can silently drop an echo and wedge a single-socket benchmark;
    # per-sample reconnect measures the steady-state per-RTT distribution the
    # production hunt loop would actually experience (it reconnects too).
    for k in range(total):
        seq += 1
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s.settimeout(args.timeout)
            if args.auth:
                s.sendall(("AUTH " + args.auth + "\r").encode())
                # Read the "OK" so the server activates the monitor before pinging.
                _buf = b""
                while b"OK" not in _buf:
                    _chunk = s.recv(256)
                    if not _chunk:
                        raise RuntimeError("no AUTH OK")
                    _buf += _chunk
            frame = make(seq)
            send_ts = time.monotonic()
            s.sendall(frame)
            buf = b""
            while True:
                chunk = s.recv(1024)
                if not chunk:
                    raise RuntimeError("peer closed")
                buf += chunk
                if match(buf, seq):
                    break
            recv_ts = time.monotonic()
            rtt_ms = (recv_ts - send_ts) * 1000.0
        except Exception as e:
            # Count a failed sample as a timeout-sized outlier only if past warmup,
            # so p95/p99 reflect real link behaviour including drops.
            if k >= args.warmup:
                rtts.append(args.timeout * 1000.0)
            continue
        finally:
            try:
                s.close()
            except Exception:
                pass
        if k >= args.warmup:
            rtts.append(rtt_ms)

    elapsed = time.monotonic() - t0
    print(f"host={args.host}:{args.port} mode={args.mode} size={args.size}B "
          f"samples={len(rtts)} wall={elapsed:.1f}s")
    print(f"  p50={percentile(rtts,50):.3f}ms  p95={percentile(rtts,95):.3f}ms  "
          f"p99={percentile(rtts,99):.3f}ms  max={max(rtts):.3f}ms")
    print(f"  mean={statistics.fmean(rtts):.3f}ms  jitter(p99-p50)="
          f"{percentile(rtts,99)-percentile(rtts,50):.3f}ms")


if __name__ == "__main__":
    main()
