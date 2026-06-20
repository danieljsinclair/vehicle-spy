"""Tests for serial_to_csv capture (run: python3 scripts/serial_to_csv_test.py).

The capture tool writes TWO timestamped files:

* RAW (verbatim safety net) — every firmware line as ``<ts>,<raw_line>``,
  nothing filtered/dropped, non-ASCII bytes hex-escaped.
* CSV (parsed/filtered) — one row per VALID frame as
  ``<ts>,<can_id>,<dlc>,<data_hex>``. Status text / corrupt / noise lines go
  to RAW ONLY.

The device read loop is provably robust against the macOS USB-serial freeze:
``select.select([fd], [], [], 0.5)`` is a hard 0.5s timeout that does NOT
depend on termios VTIME (which CH340/CP210x drivers often ignore). When
``select`` reports readable but ``os.read`` returns ``b""`` (or raises
``OSError``) the device is GONE — the loop breaks cleanly with a stderr
message instead of hanging. A heartbeat line is printed to stderr every ~5s
of continuous silence so a real freeze is distinguishable from a quiet bus.
"""

import io
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from serial_to_csv import (
    RAW_HEADER,
    CSV_HEADER,
    is_hex_token,
    format_frame,
    parse_line,
    _drain_lines,
    _open_serial,
    _run_device,
    _run_stdin,
)

# A captured RAW/CSV line looks like ``<digits>,...``. We assert on shape, not
# the live wall-clock value, so tests stay deterministic and non-fragile.
_TS_LINE = re.compile(rb"^\d+,.*", re.DOTALL)


def _find_line_ending_with(body: bytes, suffix: bytes):
    """Return the first emitted line (bytes) in ``body`` that ends with
    ``suffix``, or None. Each emitted line is ``<ts>,<raw>\\n`` so matching on
    the raw suffix lets us ignore the non-deterministic timestamp prefix."""
    for line in body.split(b"\n"):
        if line.endswith(suffix):
            return line
    return None


class HeaderTest(unittest.TestCase):
    def test_raw_header_lists_raw_line_column(self):
        self.assertEqual(RAW_HEADER, "# timestamp_ms,raw_line")

    def test_csv_header_lists_parsed_columns(self):
        self.assertEqual(CSV_HEADER, "timestamp_ms,can_id,dlc,data_hex")


class HexTokenTest(unittest.TestCase):
    """is_hex_token recognises a single hex CAN-ID / data byte token."""

    def test_lowercase_and_uppercase_hex(self):
        for tok in ("00", "FF", "ff", "3C", "3c", "118", "7FF", "abc"):
            self.assertTrue(is_hex_token(tok), msg=f"{tok!r} should be hex")

    def test_rejects_non_hex(self):
        for tok in ("TWAI", "started", "", "GG", "0x3C", "3C ", " 3C"):
            self.assertFalse(is_hex_token(tok), msg=f"{tok!r} should NOT be hex")


class FormatFrameTest(unittest.TestCase):
    def test_formats_valid_frame_as_ts_id_dlc_hex(self):
        line = format_frame(b"118 3C 00 18")
        # <ts>,118,3,3C 00 18  — assert shape, not the live timestamp.
        self.assertRegex(line, rb"^\d+,118,3,3C 00 18$")

    def test_eight_byte_payload(self):
        line = format_frame(b"201 01 02 03 04 05 06 07 08")
        self.assertRegex(line, rb"^\d+,201,8,01 02 03 04 05 06 07 08$")


class ParseLineTest(unittest.TestCase):
    """parse_line returns (raw_row, csv_row_or_None)."""

    def test_valid_frame_returns_both_raw_and_csv(self):
        raw_row, csv_row = parse_line(b"118 3C 00 18")
        self.assertIsNotNone(raw_row)
        self.assertRegex(raw_row, rb"^\d+,118 3C 00 18$")
        self.assertIsNotNone(csv_row)
        self.assertRegex(csv_row, rb"^\d+,118,3,3C 00 18$")

    def test_status_text_raw_only_no_csv(self):
        raw_row, csv_row = parse_line(b"TWAI started @ 500kbps")
        self.assertIsNotNone(raw_row)
        self.assertRegex(raw_row, rb"^\d+,TWAI started @ 500kbps$")
        self.assertIsNone(csv_row)

    def test_corrupt_non_ascii_bytes_raw_only_hex_escaped(self):
        raw_row, csv_row = parse_line(b"\x88\x88 junk")
        self.assertIsNotNone(raw_row)
        self.assertIn(b"\\x88\\x88 junk", raw_row)
        self.assertNotIn(b"\x88", raw_row)
        self.assertIsNone(csv_row)

    def test_too_many_bytes_rejected_from_csv(self):
        # 9 data bytes is invalid (max 8) — RAW only.
        raw_row, csv_row = parse_line(b"201 01 02 03 04 05 06 07 08 09")
        self.assertIsNotNone(raw_row)
        self.assertIsNone(csv_row)


class DrainLinesTest(unittest.TestCase):
    """_drain_lines splits on \\r and \\n, emitting to raw_out (always) and
    csv_out (valid frames only)."""

    def test_emits_one_raw_line_per_complete_line(self):
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        leftover, frames = _drain_lines(b"118 3C 00 18\nTWAI started\r", raw, csv_out, err)
        self.assertEqual(leftover, b"")
        self.assertEqual(frames, 1)  # only the 118 frame emitted to CSV
        self.assertEqual(err.getvalue(), b"")
        for raw_line in (b"118 3C 00 18", b"TWAI started"):
            line = _find_line_ending_with(raw.getvalue(), raw_line)
            self.assertIsNotNone(line, msg=f"no raw line ends with {raw_line!r}")
            self.assertRegex(line, _TS_LINE)

    def test_csv_only_has_valid_frame(self):
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        _drain_lines(b"118 3C 00 18\nTWAI started\r", raw, csv_out, err)
        # CSV: header-less body should contain ONLY the 118 frame row.
        csv_body = csv_out.getvalue()
        self.assertIn(b"118,3,3C 00 18", csv_body)
        self.assertNotIn(b"TWAI", csv_body)

    def test_keeps_partial_chunk_as_leftover_without_emitting(self):
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        leftover, frames = _drain_lines(b"partial with no terminator", raw, csv_out, err)
        self.assertEqual(leftover, b"partial with no terminator")
        self.assertEqual(frames, 0)
        self.assertEqual(raw.getvalue(), b"")
        self.assertEqual(csv_out.getvalue(), b"")

    def test_corrupt_non_ascii_kept_in_raw_only(self):
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        _drain_lines(b"\x88\x88 junk\r", raw, csv_out, err)
        self.assertIn(b"\\x88\\x88 junk", raw.getvalue())
        self.assertEqual(csv_out.getvalue(), b"")


class RunLoopTest(unittest.TestCase):
    def test_run_emits_both_headers_first(self):
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        chunks = [b"118 3C 00 18\r", b""]
        _run_stdin(lambda: chunks.pop(0), raw, csv_out, err)
        first_raw = raw.getvalue().split(b"\n", 1)[0]
        first_csv = csv_out.getvalue().split(b"\n", 1)[0]
        self.assertEqual(first_raw, b"# timestamp_ms,raw_line")
        self.assertEqual(first_csv, b"timestamp_ms,can_id,dlc,data_hex")

    def test_run_breaks_on_empty_chunk_eof(self):
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        chunks = [b"118 3C 00 18\r", b"", b"SHOULD NEVER READ\r"]
        _run_stdin(lambda: chunks.pop(0), raw, csv_out, err)
        self.assertNotIn(b"SHOULD NEVER READ", raw.getvalue())

    def test_device_loop_continues_on_select_timeout(self):
        # On a live serial device, a select() timeout with no readable data is
        # bus idle (NOT EOF) — the loop must continue and read the next chunk.
        # read_chunk returns (readable, data): None=timeout, True=fd readable.
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        chunks = [(None, b""), (None, b""), (True, b"118 3C 00 18\r"), (True, b"")]

        def read_chunk():
            if chunks:
                return chunks.pop(0)
            return (True, b"")  # device gone after final empty read

        _run_device(read_chunk, raw, csv_out, err)
        self.assertIn(b",118 3C 00 18\n", raw.getvalue())
        self.assertIn(b"118,3,3C 00 18", csv_out.getvalue())

    def test_device_loop_breaks_on_device_gone_empty_after_readable(self):
        # select says readable (True) but os.read returns b"" → device gone.
        # The loop MUST break cleanly and emit a stderr message, never hang.
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()

        def read_chunk():
            return (True, b"")  # readable + empty = device gone

        _run_device(read_chunk, raw, csv_out, err)
        self.assertIn(b"device disappeared", err.getvalue())
        # Only the header line was written to RAW.
        self.assertEqual(raw.getvalue(), b"# timestamp_ms,raw_line\n")

    def test_device_loop_breaks_on_oserror(self):
        # An OSError from os.read (device yanked) ends the loop cleanly with a
        # stderr message, after emitting any frame already read.
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        calls = [0]

        def seq():
            calls[0] += 1
            if calls[0] == 1:
                return (True, b"118 3C 00 18\r")
            raise OSError("device gone")

        _run_device(seq, raw, csv_out, err)
        self.assertIn(b",118 3C 00 18\n", raw.getvalue())
        self.assertIn(b"device disappeared", err.getvalue())

    def test_heartbeat_emitted_on_sustained_silence(self):
        # ~5s of continuous silence (no data) should produce a heartbeat line
        # on stderr. We monkeypatch HEARTBEAT_SECS to 0 so it fires immediately.
        import serial_to_csv
        raw, csv_out, err = io.BytesIO(), io.BytesIO(), io.BytesIO()
        original = serial_to_csv.HEARTBEAT_SECS
        serial_to_csv.HEARTBEAT_SECS = 0
        try:
            calls = [0]

            def read_chunk():
                calls[0] += 1
                if calls[0] > 3:
                    return (True, b"")  # device gone → end loop
                return (None, b"")  # select timeout, no data

            _run_device(read_chunk, raw, csv_out, err)
        finally:
            serial_to_csv.HEARTBEAT_SECS = original
        self.assertIn(b"alive:", err.getvalue())


class MainDeviceArgTest(unittest.TestCase):
    def test_bogus_device_path_returns_error(self):
        from serial_to_csv import main
        self.assertEqual(main(["/nonexistent/path/xyz.csv"]), 1)


class OpenSerialTest(unittest.TestCase):
    def test_opens_device_and_applies_115200_baud_with_vtimeout(self):
        # Regression: macOS's termios module has no cfsetispeed/cfsetospeed —
        # baud must be set via the ispeed/ospeed list elements. VTIME is kept as
        # belt-and-suspenders; the real no-hang guarantee is the select() loop.
        import termios
        import serial_to_csv
        master, slave_fd = os.openpty()
        try:
            name = os.ttyname(slave_fd)
            os.close(slave_fd)
            fd = serial_to_csv._open_serial(name)
            try:
                attrs = termios.tcgetattr(fd)
                self.assertEqual(attrs[4], termios.B115200)  # ispeed
                self.assertEqual(attrs[5], termios.B115200)  # ospeed
                cc = attrs[6]
                self.assertEqual(cc[termios.VTIME], 5)  # belt-and-suspenders
                self.assertEqual(cc[termios.VMIN], 0)
            finally:
                os.close(fd)
        finally:
            os.close(master)


if __name__ == "__main__":
    unittest.main()
