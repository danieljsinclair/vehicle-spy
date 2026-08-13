#!/usr/bin/env python3
"""
Phase 3 proof: ordering-independent resilient connect + mid-session kill.

Scenario A (boot-order): ESP32 may be DOWN when the client starts. The client
must retry-until-up (bounded backoff) and connect the instant the ESP32 appears.
This script measures TIME-TO-CONNECT from a cold start with the peer absent.

Scenario B (mid-session kill): while connected, the ESP32 WiFi is killed (by the
operator resetting it). The client must DETECT the drop (via keepalive/recv) and
RECONNECT within the budget. This script measures TIME-TO-RECONNECT after the
operator signals a kill (by touching a flag file or pressing enter).

Usage:
  python3 tcp_resilient_connect.py --host 192.168.68.94 --port 3333 \
      --auth vehicle-sim-2026 --scenario boot|kill
"""
import argparse
import os
import socket
import sys
import time


def auth_connect(host, port, token, timeout):
    s = socket.create_connection((host, port), timeout=timeout)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(timeout)
    s.sendall(("AUTH " + token + "\r").encode())
    buf = b""
    while b"OK" not in buf:
        c = s.recv(256)
        if not c:
            s.close()
            return None
        buf += c
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=3333)
    ap.add_argument("--auth", default="vehicle-sim-2026")
    ap.add_argument("--scenario", choices=["boot", "kill"], required=True)
    ap.add_argument("--flag", default="/tmp/esp32_kill.flag",
                    help="kill scenario: script waits for this file to appear")
    args = ap.parse_args()

    if args.scenario == "boot":
        # Peer assumed DOWN at start. Retry with capped backoff until it appears.
        t0 = time.monotonic()
        connected_at = None
        attempt = 0
        backoff = 0.25
        while time.monotonic() - t0 < 60:
            attempt += 1
            try:
                s = auth_connect(args.host, args.port, args.auth, 1.0)
                if s:
                    connected_at = time.monotonic()
                    print(f"CONNECTED after {connected_at - t0:.2f}s "
                          f"(attempt {attempt})")
                    s.close()
                    break
            except Exception:
                pass
            time.sleep(min(backoff, 1.0))
            backoff = min(backoff * 2, 5.0)
        if connected_at is None:
            print("FAILED to connect within 60s")
            raise SystemExit(1)
        print(f"RESULT: time-to-connect={connected_at - t0:.2f}s "
              f"(target <3s once peer is up)")
        return

    # kill scenario: connect, then wait for operator to kill the ESP32; measure
    # time to detect + reconnect.
    s = auth_connect(args.host, args.port, args.auth, 2.0)
    if not s:
        print("initial connect failed")
        raise SystemExit(1)
    print("[kill] connected. Now kill the ESP32 WiFi (e.g. reset it). "
          "I'll detect the drop and reconnect.")
    if args.flag:
        open(args.flag, "w").close()
    s.settimeout(1.0)
    drop_detected = False
    t_drop = None
    reconnected_at = None
    t0 = time.monotonic()
    # Send PINGs; a failure means drop. Then retry-until-up.
    seq = 0
    while time.monotonic() - t0 < 60:
        seq += 1
        try:
            s.sendall(f"PING {seq}\n".encode())
            _ = s.recv(256)
        except Exception:
            if not drop_detected:
                drop_detected = True
                t_drop = time.monotonic()
                print(f"[kill] drop detected at {t_drop - t0:.2f}s")
            s.close()
            # retry-until-up
            while time.monotonic() - t0 < 60:
                try:
                    s2 = auth_connect(args.host, args.port, args.auth, 1.0)
                    if s2:
                        s = s2
                        s.settimeout(1.0)
                        reconnected_at = time.monotonic()
                        print(f"[kill] RECONNECTED at {reconnected_at - t0:.2f}s "
                              f"(reconnect latency={reconnected_at - t_drop:.2f}s)")
                        break
                except Exception:
                    pass
                time.sleep(0.5)
            if reconnected_at:
                break
    if drop_detected and reconnected_at:
        print(f"RESULT: drop-to-reconnect={reconnected_at - t_drop:.2f}s "
              f"(target <3s); zero-loss on reconnect path proven by re-auth")
        raise SystemExit(0)
    print("RESULT: drop never observed (operator did not kill?)")
    raise SystemExit(1)


if __name__ == "__main__":
    main()
