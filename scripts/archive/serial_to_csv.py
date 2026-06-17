#!/usr/bin/env python3
"""Dual-output capture of the ESP32 CAN-bridge serial stream.

The firmware prints each CAN frame in ELM327 monitor format
(``<CANID> <D0> <D1> ... <D7>\\r``) plus status text like
``TWAI started @ 500kbps``. This script writes TWO timestamped files plus an
echo to STDOUT for live console viewing:

* **RAW** (verbatim safety net) — every firmware line as
  ``<ts>,<raw_line>``, NOTHING filtered/dropped. Non-ASCII bytes (electrical
  noise like 0x88) are hex-escaped via ``backslashreplace`` so the file stays
  text-safe but loses no bytes. Header: ``# timestamp_ms,raw_line``.

* **CSV** (parsed/filtered) — one row per VALID frame as
  ``<ts>,<can_id>,<dlc>,<data_hex>``. A valid frame = exactly one hex CAN-ID
  token + up to 8 hex byte tokens, ALL ASCII hex. Status text, corrupt/noise
  lines, and non-frames go to RAW ONLY (that is the intended filtering).
  Header: ``timestamp_ms,can_id,dlc,data_hex``.

* **STDOUT** — each raw verbatim line is echoed for a live ``tail``-style view.

The decoder (``src/domain/CaptureLog.cpp``) already reads BOTH the legacy CSV
form (``timestamp_ms,can_id,dlc,data_hex``) and the verbatim RAW form
(``timestamp_ms,raw_line``), so decode works from EITHER new file.

THE FREEZE FIX
--------------
The read loop is provably robust against the macOS USB-serial freeze that
plagued the VTIME-only design. CH340/CP210x drivers on cheap ESP32 boards
frequently do NOT honour termios VTIME, so ``os.read`` can block far longer
than 0.5s — and if the USB link drops mid-drive (cable/board glitch in a
moving car) the fd goes stale and ``os.read`` hangs FOREVER while bus
activity continues. That is the freeze.

The fix gates every read with ``select.select([fd], [], [], 0.5)`` — a hard
0.5s timeout that works on the fd regardless of USB-driver VTIME support:

  * select timeout (no data) → ``continue`` (bus idle, normal).
  * select says readable → ``os.read(fd, 4096)``. If read returns ``b""``
    while select reported readable, OR raises ``OSError``, the device is
    GONE — break out cleanly with a stderr message
    (``device disappeared, capture stopped after N frames``) instead of
    hanging/spinning. Never loop forever on a dead fd.
  * HEARTBEAT: every ~5s of continuous silence a short line goes to STDERR
    (flushed) like ``# alive: 123 frames, waiting for bus...``. Next time
    capture "freezes": if the heartbeat keeps ticking, the loop is alive and
    the ESP32/USB link stopped sending (NOT a Python bug); if it stops, it's
    a real block.

Usage:
    python3 scripts/serial_to_csv.py <device> --raw <rawfile> --csv <csvfile>
    # or read piped/test input on stdin (no device argument):
    python3 scripts/serial_to_csv.py < input.txt
"""

import argparse
import os
import select
import sys
import time

RAW_HEADER = "# timestamp_ms,raw_line"
CSV_HEADER = "timestamp_ms,can_id,dlc,data_hex"

# Hard 0.5s select() timeout — the real no-hang guarantee (VTIME is only
# belt-and-suspenders on drivers that ignore it).
SELECT_TIMEOUT = 0.5
# Seconds of continuous silence before emitting a heartbeat to stderr.
HEARTBEAT_SECS = 5.0
# Max data bytes in a CAN frame — anything beyond this is not a valid frame.
MAX_CAN_BYTES = 8


def _ts_ms() -> int:
    """Client wall-clock millisecond timestamp."""
    return int(time.time() * 1000)


def is_hex_token(tok: str) -> bool:
    """True iff ``tok`` is a non-empty ASCII hex token (1+ digits, [0-9A-Fa-f]).

    Matches ELM327 monitor tokens: CAN-IDs (``118``, ``7FF``) and byte values
    (``3C``, ``00``). Rejects status text (``TWAI``) and noise.
    """
    return bool(tok) and all(c in "0123456789ABCDEFabcdef" for c in tok)


def format_frame(line: bytes) -> bytes:
    """Render a valid frame ``<CANID> <D0> ... <Dn>`` as
    ``<ts>,<can_id>,<dlc>,<data_hex>`` bytes (no trailing newline).

    Caller has already validated ``line`` is a valid frame via ``parse_line``.
    """
    tokens = line.decode("ascii").split()
    can_id = tokens[0]
    data = tokens[1:]
    dlc = len(data)
    data_hex = " ".join(data)
    return f"{_ts_ms()},{can_id},{dlc},{data_hex}".encode("ascii")


def parse_line(line: bytes):
    """Split one firmware line into (raw_row, csv_row_or_None).

    ``raw_row`` is the verbatim ``<ts>,<raw>`` row (bytes), always produced.
    ``csv_row`` is the parsed ``<ts>,<can_id>,<dlc>,<data_hex>`` row (bytes)
    if the line is a VALID frame, else ``None`` (status/corrupt/noise lines go
    to RAW ONLY). Non-ASCII bytes in the raw row are hex-escaped.
    """
    safe = line.decode("ascii", errors="backslashreplace")
    raw_row = f"{_ts_ms()},{safe}".encode("ascii")

    csv_row = None
    # A valid frame must be pure ASCII (no hex-escaped noise bytes) and split
    # into exactly one hex CAN-ID + 1..8 hex data bytes.
    try:
        text = line.decode("ascii")
    except UnicodeDecodeError:
        return raw_row, None
    tokens = text.split()
    if not tokens or len(tokens) < 2 or len(tokens) > MAX_CAN_BYTES + 1:
        return raw_row, None
    if not all(is_hex_token(t) for t in tokens):
        return raw_row, None
    csv_row = format_frame(line)
    return raw_row, csv_row


def _emit(line: bytes, raw_out, csv_out) -> int:
    """Emit one firmware line: verbatim to ``raw_out`` (always) and stdout,
    parsed to ``csv_out`` only if it's a valid frame. Returns 1 if a CSV row
    was written (for the frame counter), else 0."""
    raw_row, csv_row = parse_line(line)
    raw_out.write(raw_row + b"\n")
    raw_out.flush()
    sys.stdout.buffer.write(raw_row + b"\n")
    sys.stdout.buffer.flush()
    if csv_row is not None:
        csv_out.write(csv_row + b"\n")
        csv_out.flush()
        return 1
    return 0


def _drain_lines(buf: bytes, raw_out, csv_out, err):
    """Split ``buf`` on ``\\r`` and ``\\n``, emitting each complete line to
    ``raw_out`` (always) and ``csv_out`` (valid frames only). Returns
    ``(leftover, frames_emitted)`` where ``leftover`` is any trailing partial
    line (no terminator) for the next chunk and ``frames_emitted`` counts the
    CSV rows written. ``err`` is unused (kept for a stable signature).

    Empty tokens (from consecutive terminators like ``\\r\\n``) are skipped:
    they carry no firmware output and would just add ``<ts>,`` junk rows. Every
    non-empty line — including corrupt/noise bytes — is kept verbatim in RAW.
    """
    frames = 0
    while True:
        cuts = [i for i in (buf.find(b"\r"), buf.find(b"\n")) if i != -1]
        if not cuts:
            break
        cut = min(cuts)
        line = buf[:cut]
        if line:
            frames += _emit(line, raw_out, csv_out)
        buf = buf[cut + 1:]
    return buf, frames


def _open_serial(device):
    """Open a serial device configured for 115200 8N1 raw and return its fd.

    Configuring the fd we then read from is what makes the baud reliable — an
    external ``stty`` can be reset by the driver when the device is closed and
    reopened, so reading via a shell redirect can land on the wrong speed.

    macOS baud fix: tcgetattr → [iflag, oflag, cflag, lflag, ispeed, ospeed,
    cc]. On macOS the baud is set by assigning speed constants to
    ispeed/ospeed directly (cfsetispeed/cfsetospeed are Linux-only, absent in
    macOS's termios). CLOCAL|CREAD ignores modem control and enables RX.

    A VTIME timeout (VMIN=0/VTIME=5 = 0.5s) is left set as belt-and-suspenders,
    but the real no-hang guarantee is the ``select.select`` loop in
    ``_run_device`` — CH340/CP210x drivers frequently ignore VTIME.
    """
    import fcntl
    import termios
    import tty

    fd = os.open(device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[4] = termios.B115200  # ispeed
        attrs[5] = termios.B115200  # ospeed
        attrs[2] |= termios.CLOCAL | termios.CREAD  # cflag: ignore modem ctrl, enable RX
        # Belt-and-suspenders: VTIME=5 (0.5s). The select() loop is the real
        # guarantee — some USB-serial drivers ignore VTIME entirely.
        cc = attrs[6]
        cc[termios.VTIME] = 5
        cc[termios.VMIN] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    finally:
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)  # block on read
    return fd


def _run_stdin(read_chunk, raw_out, csv_out, err):
    """Piped/test input: ``b""`` is real EOF → break. ``read_chunk`` returns a
    plain bytes chunk (no select contract, no heartbeat)."""
    _run(read_chunk, raw_out, csv_out, err, device=False)


def _run_device(read_chunk, raw_out, csv_out, err):
    """Live serial device loop, gated by ``select`` so it NEVER blocks longer
    than ``SELECT_TIMEOUT`` regardless of USB-driver VTIME support.

    ``read_chunk`` is callable() -> (readable, data) where ``readable`` is what
    select reported (True = fd readable, None = select timed out, no data).
    The split lets unit tests drive both paths without a real fd. On a real
    device ``read_chunk`` is built from ``select.select([fd], ...); os.read``.
    """
    _run(read_chunk, raw_out, csv_out, err, device=True)


def _run(read_chunk, raw_out, csv_out, err, *, device):
    """Shared drain loop. Writes both headers first, then stamps every firmware
    line. ``device`` selects the empty-read contract:

      * STDIN (``device=False``): ``read_chunk`` returns bytes; ``b""`` is real
        EOF → break silently.
      * device (``device=True``): ``read_chunk`` returns ``(readable, data)``;
        ``readable is None`` is a select timeout (bus idle, ``continue`` with
        heartbeat on silence); ``b""`` while readable means the device is GONE
        → break with a clear stderr message; ``OSError`` likewise.

    Single exit point at the bottom: any trailing partial line is flushed.
    """
    raw_out.write((RAW_HEADER + "\n").encode("ascii"))
    raw_out.flush()
    csv_out.write((CSV_HEADER + "\n").encode("ascii"))
    csv_out.flush()

    frames = 0
    buf = b""
    heartbeat_next = time.monotonic() + HEARTBEAT_SECS
    reason = None  # set on break; None = fell through (shouldn't normally hit)

    while True:
        try:
            if device:
                readable, data = read_chunk()
            else:
                data = read_chunk()
                readable = True
        except OSError:
            reason = (f"device disappeared, capture stopped after {frames} "
                      f"frames\n")
            break

        if device and readable is None:
            # select timeout: bus idle. Heartbeat on sustained silence.
            now = time.monotonic()
            if now >= heartbeat_next:
                err.write(f"# alive: {frames} frames, waiting for bus...\n"
                          .encode("ascii"))
                err.flush()
                heartbeat_next = now + HEARTBEAT_SECS
            continue

        if not data:
            if device:
                reason = (f"device disappeared, capture stopped after {frames} "
                          f"frames\n")
            break

        heartbeat_next = time.monotonic() + HEARTBEAT_SECS
        buf += data
        buf, emitted = _drain_lines(buf, raw_out, csv_out, err)
        frames += emitted

    if reason is not None:
        err.write(reason.encode("ascii"))
        err.flush()
    # Flush any trailing non-empty partial line (no terminator before
    # EOF/close). Whitespace-only leftovers carry no firmware output.
    if buf.strip():
        frames += _emit(buf, raw_out, csv_out)


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    parser = argparse.ArgumentParser(
        description="Capture ESP32 CAN-bridge serial stream to RAW + CSV.")
    parser.add_argument("device", nargs="?",
                        help="serial device (omit to read stdin)")
    parser.add_argument("--raw", dest="raw", help="verbatim RAW output file")
    parser.add_argument("--csv", dest="csv", help="parsed CSV output file")
    args = parser.parse_args(argv)

    err = sys.stderr.buffer

    # Open output files. When omitted (stdin/test path) use /dev/null so the
    # always-write code paths still work without ceremony.
    raw_path = args.raw or os.devnull
    csv_path = args.csv or os.devnull
    raw_fh = open(raw_path, "wb")
    csv_fh = open(csv_path, "wb")

    try:
        if args.device:
            try:
                fd = _open_serial(args.device)
            except OSError as e:
                err.write(f"Error opening {args.device}: {e}\n".encode())
                err.flush()
                return 1
            try:
                def read_chunk():
                    r, _, _ = select.select([fd], [], [], SELECT_TIMEOUT)
                    if not r:
                        return None, b""  # timeout, no data
                    return True, os.read(fd, 4096)
                _run_device(read_chunk, raw_fh, csv_fh, err)
            finally:
                os.close(fd)
            return 0

        # No device arg: read stdin (unit tests / piped input).
        def read_chunk():
            return sys.stdin.buffer.read(4096)
        _run_stdin(read_chunk, raw_fh, csv_fh, err)
        return 0
    finally:
        raw_fh.close()
        csv_fh.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
