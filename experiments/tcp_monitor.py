#!/usr/bin/env python3
"""Bare TCP monitor for the ESP32 CAN bridge — the TCP equivalent of `make monitor`.

The firmware streams CAN frames to the *serial* port unconditionally, but only
streams to *TCP* after the client sends the ELM327 monitor-start command (ATMA),
which sets monitorActive_ in CanBridge (see firmware/vanilla/CanBridge.cpp:48).

A plain `nc <ip> 3333` connects but sends nothing, so monitorActive_ stays false
and you see no frames. This script sends the same init sequence vehicle-sim
sends (ATZ/ATE0/ATSP6/ATH1/ATMA) and then prints the live frame stream, so you
can confirm the hardware is delivering data before running vehicle-sim.

Usage:
    python3 experiments/tcp_monitor.py <ESP32_HOST> [port]
Defaults to port 3333 (DiscoveryConfig::TCP_PORT).
"""
import socket
import sys
import time

PORT = 3333
# ELM327 CAN-monitor init (matches boundary::ELM327Transport::buildCANMonitorInitSequence)
INIT = ["ATZ", "ATE0", "ATSP6", "ATH1", "ATMA"]


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: tcp_monitor.py <ESP32_HOST> [port]", file=sys.stderr)
        return 2
    host = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else PORT

    print(f"[tcp_monitor] connecting to {host}:{port} ...", file=sys.stderr)
    sock = socket.create_connection((host, port), timeout=5)
    sock.settimeout(1.0)
    print("[tcp_monitor] connected. Sending ELM327 init (ATZ/ATE0/ATSP6/ATH1/ATMA)...",
          file=sys.stderr)

    for cmd in INIT:
        sock.sendall((cmd + "\r").encode())
        time.sleep(0.1)  # pace like the production client (perCommandDelayMs)

    print("[tcp_monitor] monitor active — printing CAN stream (Ctrl+C to stop):",
          file=sys.stderr)
    try:
        while True:
            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                print("[tcp_monitor] connection closed by peer", file=sys.stderr)
                return 0
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        print("\n[tcp_monitor] stopped", file=sys.stderr)
        return 0


if __name__ == "__main__":
    sys.exit(main())
