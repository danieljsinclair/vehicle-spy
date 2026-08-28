# Curated usage examples for `vehicle-sim`.
#
# This file is the SINGLE SOURCE OF TRUTH for the `--examples` output. It is
# embedded into the binary at build time via CMake `file(READ ...)` + a
# generated translation unit (`ExamplesContent.cpp`); do not duplicate the
# content into C++ source.
#
# Markers (lines starting with `#` are comments and are stripped at render time):
#   `# section: <NAME>` — sets the current section heading. The block under
#     this heading runs until the next `# section:` line (or end of file).
#   `# topic: <name>[, <name2>...]` — introduces a topic block. The body of
#     the block is everything until the next comment line or end of file.
#     Topic names are bare flag names without leading dashes.
#   `#` (with no marker prefix) — comment, skipped.
#
# Conventions:
#   - Sections are upper-case (e.g. `CONNECTIONS`).
#   - Topics are bare flag names without leading dashes.
#   - Trivial single-flag examples are intentionally NOT shown — the user
#     already knows how `--scan` / `--list` / `--discover` / `--led-help` work
#     from the help text. Only examples that combine two or more flags,
#     show a non-trivial argument pattern, or illustrate a pipe belong here.
#   - The topic filter is hierarchical: `--examples connect` matches
#     `connect` AND every `connect-*` child (e.g. `connect-usb`,
#     `connect-tcp`, `connect-auto`, `connect-file`).

# section: CONNECTIONS

# topic: connect
  vehicle-sim --connect demo --vehicle tesla
  vehicle-sim --connect auto --vehicle tesla
  vehicle-sim --connect tcp:192.168.4.1:3333 --vehicle tesla
  vehicle-sim --connect tcp:192.168.4.1 --vehicle tesla
  vehicle-sim --connect usb:/dev/cu.usbserial-110 --vehicle tesla
  vehicle-sim --connect AA:BB:CC:DD:EE:FF --vehicle tesla
  vehicle-sim --connect AA:BB:CC:DD:EE:FF --vehicle auto
  vehicle-sim --connect file:capture.csv --vehicle tesla

# topic: connect-usb
  vehicle-sim --connect-usb /dev/cu.usbserial-110 --vehicle tesla

# topic: connect-tcp
  vehicle-sim --connect-tcp 192.168.4.1:3333 --vehicle tesla

# topic: connect-auto
  vehicle-sim --connect-auto --vehicle tesla

# topic: connect-file
  vehicle-sim --connect-file capture.csv --vehicle tesla --stdout-csv | head -20

# section: WIFI SETUP
# Provisioning happens over the device's USB-serial AT console. The universal
# `--connect` selects the transport: `auto` (auto-detect the first /dev/cu.*)
# or `usb:/dev/cu.*` (explicit path). All four provisioning operations are
# sugar over the same channel.

# topic: set-wifi-creds
  vehicle-sim --set-wifi-creds MyNet s3cr3tpass --connect auto
  vehicle-sim --set-wifi-creds MyNet s3cr3tpass --connect usb:/dev/cu.usbserial-110

# topic: clear-wifi-creds
  vehicle-sim --clear-wifi-creds --connect usb:/dev/cu.usbserial-110

# topic: reboot
  vehicle-sim --reboot --connect auto

# topic: status
  vehicle-sim --status --connect auto

# section: LOGGING

# topic: log
  vehicle-sim --connect usb:/dev/cu.usbserial-110 --vehicle tesla --log captures/SecondDrive
  vehicle-sim --connect tcp:192.168.4.1:3333 --vehicle tesla --log captures/SecondDrive
  vehicle-sim --connect demo --vehicle tesla --log captures/SecondDrive

# topic: adapter-protocol
  vehicle-sim --connect AA:BB:CC:DD:EE:FF --vehicle tesla --adapter-protocol elm327

# topic: start-from
  vehicle-sim --connect file:capture.csv --vehicle tesla --start-from 12.5

# section: OUTPUT

# topic: interactive
  vehicle-sim --interactive --stdout-csv --vehicle tesla --interval 20

# topic: stdout-csv
  vehicle-sim --connect demo --vehicle tesla --stdout-csv | head -20

# topic: vehicle
  vehicle-sim --connect demo --vehicle tesla
  vehicle-sim --connect demo --vehicle audi_mlb_evo
  vehicle-sim --connect demo --vehicle generic
