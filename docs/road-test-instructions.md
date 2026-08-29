# STARTER PROMPT — ESP32 rewire + live Tesla road test (make an EV sound like an ICE)

You are starting fresh with no prior context. Read this whole brief, then work through it.

## GOAL

Make a Tesla Model 3/Y (silent EV) sound like a combustion car in real time. A newly-built
ESP32 CAN bridge listens (read-only) to the car's CAN bus, streams frames over WiFi TCP to a
MacBook, `vehicle-sim` decodes them to a telemetry CSV, and that CSV is piped live into
`engine-sim-cli`, whose virtual-ICE twin drives an engine-physics simulation and plays the
engine note through speakers. Two jobs today:

1. Wire and flash NEW ESP32 boards (the wiring is unforgiving — see the pin-13 trap).
2. Run the live road test and capture evidence.

Hardware: MacBook M4 Pro (Apple Silicon, macOS). Tesla Model 3/Y post-Jan-2019 with a 26-pin
diagnostic harness (behind the rear centre console) breaking out to a standard 16-pin OBD2 socket.

## REPOS AND PATHS (all verified to exist)

Paths below use two shell variables, exported where first used:
`$VSIM_ROOT` is **this repo** — the vehicle-sim checkout containing
`docs/road-test-instructions.md` (set in PART 2); `$ESC_ROOT` is your
engine-sim-cli checkout (set at the binary trap below).

| What | Path |
|---|---|
| vehicle-sim (CAN → telemetry CSV) | `$VSIM_ROOT` (this repo) |
| vehicle-sim binary | `$VSIM_ROOT/build-native/vehicle-sim` |
| ESP32 firmware sketch | `$VSIM_ROOT/firmware/can-bridge/can-bridge.ino` |
| Portable firmware C++ (CanDriver, WiFiManager, CanBridge, TcpServerManager) | `$VSIM_ROOT/firmware/vanilla/` |
| Hardware/wiring guide | `$VSIM_ROOT/docs/hardware-adapter-guide.md` |
| Tesla CAN reference | `$VSIM_ROOT/docs/tesla-model3-can-reference.md` |
| Tesla DBC | `$VSIM_ROOT/resources/dbc/Model3CAN.dbc` |
| Recorded captures (fallback) | `$VSIM_ROOT/captures/` |
| engine-sim-cli (CANONICAL checkout) | `$ESC_ROOT` (see binary trap below) |
| engine-sim-cli binary | `$ESC_ROOT/build/engine-sim-cli` |
| Engine presets | `$ESC_ROOT/engine-sim-bridge/preset/*.json` |

**Binary trap:** there are several engine-sim-cli checkouts. Only the canonical
checkout's `build/engine-sim-cli` supports `--live-telemetry`.
The one under `engine-sim-app/engine-sim-cli/build/` is older and rejects the flag with
"The following argument was not expected: --live-telemetry". Point `ESC_ROOT` at the
canonical checkout and verify before you start:

```bash
export ESC_ROOT="$HOME/vscode/engine-sim-cli"   # your --live-telemetry-capable checkout
"$ESC_ROOT/build/engine-sim-cli" --help | grep live-telemetry
```

There is **no** pre-existing road-test document — this brief is the procedure. If you produce a
better one, write it to `$VSIM_ROOT/docs/road-test-instructions.md`.

## PART 1 — WIRING

Parts per board: ESP32-WROOM-32 dev board, SN65HVD230 (VP230) CAN transceiver, OBD2 breakout cable.

Dupont wire colours deliberately follow the **OBD2 harness** colours, not the usual
black=GND convention. **Go by the table, not by colour instinct.**

| Wire colour | Signal | SN65HVD230 pin | ESP32 pin | OBD2 pin (harness wire colour) |
|---|---|---|---|---|
| Red | 3.3V power | 3.3V | 3V3 | — |
| Orange | GND | GND | GND | 4 (orange) |
| Yellow | GND | — | — | 5 (yellow) — tie to the same common GND |
| Green | CAN-H | CANH | — | 6 (green) |
| Black | CAN-L | CANL | — | 14 (brown/white) |
| Brown | TX | TX | D22 (GPIO22) | — |
| Blue | RX | RX | D21 (GPIO21) | — |

GPIO22 = TWAI TX and GPIO21 = TWAI RX are hard-coded in `can-bridge.ino`
(`Constants::TWAI_TX = GPIO_NUM_22`, `TWAI_RX = GPIO_NUM_21`). Bus config is
**500 kbps, LISTEN-ONLY, accept-all filter** (`TWAI_MODE_LISTEN_ONLY`,
`TWAI_TIMING_CONFIG_500KBITS`, `TWAI_FILTER_CONFIG_ACCEPT_ALL`) — the ESP32 never transmits and
puts no ACK bits on the vehicle bus.

**PIN 13 TRAP — read this twice.** OBD2 pin 13 carries a **black/white** wire that is
vendor-specific and is **NOT CAN-L**. CAN-L is **pin 14 (brown/white)**. The two look nearly
identical in the loom. Wiring CAN-L to pin 13 gives you a board that powers up, joins WiFi, and
serves TCP — but delivers **zero CAN frames**. If you see 0 frames, suspect pin 13 before
anything else.

Other wiring gotchas:
- Tie **both** OBD2 GND pins (4 orange and 5 yellow) to the common GND net. Do not leave pin 5 floating.
- If wiring is correct but no data flows, **swap TX/RX (D22/D21)** — transceiver breakout silkscreens vary.
- Many cheap USB-C ESP32 boards have USB data pins unconnected. If macOS doesn't enumerate the
  board, use a **USB-A → USB-C adapter**; the USB-C-to-USB-C handshake is often broken on these boards.

## PART 2 — FLASH THE FIRMWARE

Everything below runs from this repo's root — the vehicle-sim checkout containing this
document.

```bash
cd /path/to/this/vehicle-sim-checkout   # the repo this doc lives in
export VSIM_ROOT="$PWD"
make install-deps          # first time only: cmake, arduino-cli, imagemagick
```

**1. Plug the board into the Mac by USB and confirm the port:**
```bash
make firmware-port         # prints e.g. /dev/cu.usbserial-210; empty = cable/adapter problem
```

**2. Provision WiFi credentials** (stored in NVS, survives re-flash):
   Flash first — the ESP32 boots AP-first (`ESP32-CAN` / `cancan12`).
   For a deterministic clean start, clear any existing credentials first:
   ```bash
   make clear-wifi-creds
   ```
   Then provision your network:
   ```bash
   make set-wifi-creds ESP32_WIFI_SSID="YourNetworkName" ESP32_WIFI_PASS="YourPassword"
   ```
   For the road test the phone hotspot the Mac is also on is usually the right SSID.

**3. TCP auth token** (firmware default baked, overridable on-device):
```bash
make ota-creds             # random token, offers to persist to ~/.zshrc
source ~/.zshrc
```
The firmware default token (`vehicle-sim-2026`) is compiled in as a first-boot fallback, but
you can override it on-device via `ATSETTOKEN <token>` (stored in NVS, survives re-flash).
The host binary is baked at build time, so **if you change the token you must rebuild BOTH
sides** — firmware (`make flash`) and host (`make native`/`make ios`) — or the host's
`AUTH <token>` line is rejected and you get zero frames.

**4. Optional, for later WiFi pushes — OTA signing keys:**
```bash
make ota-keys              # bakes the public key into firmware/can-bridge/OtaPublicKey.h
```
The first flash after generating a keypair MUST be over USB so the device trusts it.

**5. Build, test and flash:**
```bash
make flash                 # runs tests, builds firmware, esptool write_flash @ 460800
```
`make flash` prints the startup log automatically (via `scripts/serial-startup-log.pl`, 115200,
30 s window) — read the ESP32's IP address out of it. It does **not** open an interactive monitor.

**6. Serial monitor (live view) when you want it:**
```bash
make monitor               # screen <port> 115200  — exit with Ctrl-A then k, then y
```

**Monitor auto-activate — the fix and what it means.** "Monitor" here has two meanings, and the
important one is the *CAN monitor flag*, not the serial console. `CanBridge::monitorActive_`
gates whether frames are written to the TCP client. It used to be set **only** by the ELM327
`ATMA` command — but the host CLI resolves a `tcp:` target to the **raw** protocol and never
sends `ATMA`. Result: the client reported "HELO acknowledged, Streaming" and received nothing.
Commit **290dd35** ("fix(firmware): stream CAN on authenticated connect") makes
`TcpServerManager` set `setMonitorActive(true)` immediately after a successful `AUTH`, on the
principle that an authenticated client is by contract asking for the stream. `ATMA`/`ATPC` still
drive the same flag for ELM327-mode clients. **Verify your firmware contains 290dd35** — if you
flash anything older you will get an authenticated connection with zero frames.

```bash
git -C "$VSIM_ROOT" log --oneline | grep -E "290dd35|600af31"
```

**7. Confirm the board is reachable and streaming:**
```bash
make check-esp32 ESP32_HOST=<esp32-ip>     # ping + TCP 3333 + TCP 80
nc <esp32-ip> 3333
# First line you type MUST be:  AUTH <your-ESP32_TCP_TOKEN>
# Expect: OK   then, with the car awake, CAN frames should start flowing.
# Typing ATZ should answer: ELM327 v2.3  with a '>' prompt.
```

Ports: **3333** = CAN/command TCP, **3335** = UDP discovery broadcast, **80** = OTA push.

**Later firmware updates over WiFi** (no cable, once the device trusts your key):
```bash
make flash-over-tcp ESP32_HOST=<esp32-ip>
make reboot-over-tcp ESP32_HOST=<esp32-ip>
```

## PART 3 — THE LIVE ROAD TEST

Pre-flight, in the car, engine/car awake (Tesla CAN goes quiet when fully asleep):

```bash
cd "$VSIM_ROOT"
make native                                  # ensure host binary matches your token
./build-native/vehicle-sim --discover        # finds the ESP32 via UDP 3335, prints tcp:<ip>:3333
./build-native/vehicle-sim --connect tcp:<esp32-ip> --vehicle tesla   # sanity: watch decoded rows
```

### THE LIVE PIPELINE (the headline command)

`vehicle-sim` decodes CAN to CSV on stdout; `engine-sim-cli --live-telemetry` reads that CSV on
stdin and drives the virtual-ICE twin. `--stdout-csv` moves all progress output to stderr so
stdout pipes cleanly.

```bash
cd "$ESC_ROOT"

"$VSIM_ROOT/build-native/vehicle-sim" \
    --connect tcp:<esp32-ip>:3333 \
    --vehicle tesla \
    --stdout-csv \
    --log "$VSIM_ROOT/captures/RoadTest_$(date +%Y-%m-%d-%H%M%S)" \
  2>/tmp/vehicle-sim.err \
  | ./build/engine-sim-cli \
      --live-telemetry \
      --threaded \
      --script engine-sim-bridge/preset/ferrari_f136.json \
      --play \
      --interactive
```

Notes on each part, all verified:
- `--log <base>` writes `<base>.csv` (decoded) **and** `<base>.raw.txt` (verbatim raw capture)
  while still streaming to stdout. Always record the road test — a re-runnable capture is worth
  far more than a memory of the drive.
- `--threaded` matters. Default sync-pull runs engine physics on the CoreAudio sample clock,
  which creates a two-clock feedback loop and makes engine-start non-deterministic.
  `--threaded` uses a single nominal-dt clock. Use it unless you are deliberately testing sync-pull.
- `--script engine-sim-bridge/preset/ferrari_f136.json` — the Ferrari F136 is the designated
  reliable engine (starts consistently). Pass the **path**; a bare `ferrari_f136.json` fails with
  "Cannot open preset file". Other presets: `C63_M156_V2b.json`, `v8_gm_ls.json`, `lfa_v10.json`,
  `subaru_ej25.json`, `2jz.json`, `11_merlin_v12.json`.
- `--play` for real audio. Swap for `--silent` (full pipeline at zero volume) when benching
  headlessly. `--duration <s>` bounds a non-interactive run; `--interactive` runs open-ended.
- Add `--gearbox-log /tmp/gearbox.csv` to record shift decisions — **confirmed working on the
  live-telemetry path** on the canonical checkout (columns: frame, dt, speedKmh, throttleRaw,
  throttleSmoothed, vehicleSpeedFeedbackKmh, engineRpmFeedback, currentGear, targetGear,
  requestsShift, timeSinceLastShiftS, kickdownActive, engineRpm, upshiftSpeed, downshiftSpeed,
  twinState, clutchPressure).

### What good looks like

The engine-sim-cli status line should show RPM climbing off idle, `Running`, a gear label
(`DA1`/`DA2`… = Drive-Auto-gear-N, `DAN` = coasting, `NMN` = neutral), and mph tracking the car.
`[UR: 0]` = zero audio underruns. Example from a verified replay run:

```
[ 3240 RPM] [S:0 I:1] ferrari_f136  Running [Gas: 0% B:0.0] [Gear:DA2] [ 26 mph] [UR: 0] [THREADED]
```

### Known twin defects — expect these, do not chase them as new bugs

- **Gear hunting**: the auto gearbox flip-flops between adjacent gears at steady cruise
  (measured 51 shifts at a constant 50 km/h). Torque-driven downshift fighting speed-upshift.
- **Engine over-rev / speed decoupling**: engine RPM can exceed the gearbox redline (9531 vs
  6500) because the redline-upshift uses the RPM *implied by road speed*, not the engine's actual
  RPM. The engine free-revs semi-independently of road speed by design of the current twin.
- **Gearbox is hard-coded** to `IceVehicleProfile::zf8hp45()` (8-speed). The preset's own
  transmission ratios are ignored; `--auto`/`--manual` do not change shift behaviour.
- **`--output <file>.wav` is broken** — it is accepted and reports the path but writes no file.
  Do not rely on it for evidence; record audio externally or listen live.

## PART 4 — FALLBACK: DRIVE THE TWIN FROM A RECORDING

If the car, the board, or the weather is uncooperative, the identical pipeline runs off a
recorded capture — same CSV schema, same twin, no hardware. Do this **first**, indoors, to prove
the software half before you touch the car.

```bash
cd "$ESC_ROOT"

"$VSIM_ROOT/build-native/vehicle-sim" \
    --connect "file:$VSIM_ROOT/captures/StationHomeward_2026-06-17-213250.raw.txt" \
    --vehicle tesla --stdout-csv 2>/dev/null \
  | ./build/engine-sim-cli --live-telemetry --threaded \
      --script engine-sim-bridge/preset/ferrari_f136.json \
      --play --duration 20 --start-from 120
```

**Which capture file to use — this bites people.** In `captures/` you will find `<name>.csv` and
`<name>.raw.txt` / `<name>.raw` pairs.

- The **`.raw.txt` / `.raw` file is the raw CAN log** and is what `--connect file:` expects.
  Replaying `StationHomeward_2026-06-17-213250.raw.txt` decodes 122k+ rows with real speed.
- The **`.csv` file is already-decoded telemetry output**. Feeding it back to
  `vehicle-sim --connect file:` yields `frames decoded=0 ... malformed=3227` and an empty stream.
  (You *can* pipe a decoded `.csv` straight into `engine-sim-cli --live-telemetry` — skip
  vehicle-sim entirely — since it is the same schema the twin consumes.)

Useful captures:

| File | Content |
|---|---|
| `StationHomeward_2026-06-17-213250.raw.txt` | Long real drive, good speed range — best gearbox exercise |
| `FirstDrive_2026-06-14-222831.raw.txt` | Early drive |
| `MoorendRoad3ptPark_2026-06-17-213923.raw.txt` | Manoeuvring, reverse |
| `ParkedRevving_2026-06-16-002040.raw.txt` | Stationary throttle only |

`--start-from <seconds|mm:ss|hh:mm:ss>` skips the stationary lead-in. Without it you will spend
the whole run parked at 0 mph and conclude, wrongly, that nothing works.

## TROUBLESHOOTING

**0 CAN frames over TCP** — work down this list in order:
1. **Pin 13 vs pin 14.** CAN-L must be on pin 14 (brown/white), not pin 13 (black/white). This is
   the single most likely cause of a healthy-looking board with no data.
2. Firmware predates **290dd35** — the CAN monitor flag never activates on the raw path, so an
   authenticated client gets silence. `make flash` from a checkout containing that commit.
3. **Token mismatch.** Firmware bakes `TCP_AUTH_TOKEN` at build time; the host binary bakes it
   too (`CMakeLists.txt -DTCP_AUTH_TOKEN`). Changing `ESP32_TCP_TOKEN` requires `make flash`
   **and** `make native`. Test by hand: `nc <ip> 3333`, first line `AUTH <token>`, expect `OK`;
   `ERROR unauthorized` means mismatch.
4. **Car asleep.** A parked/sleeping Tesla puts little or nothing on the bus. Open a door, press
   the brake, wake it.
5. **TX/RX swapped.** Swap D22/D21 at the transceiver.
6. Both OBD2 grounds (pins 4 and 5) tied to common GND.
7. `make check-esp32 ESP32_HOST=<ip>` — confirms ping, TCP 3333 and TCP 80.
8. Watch `make monitor`: the loop heartbeat prints the monitor flag as `ACTIVE` or `idle`. `idle`
   with a client connected points straight back at items 2 and 3.
9. Only **one** client at a time — a new TCP connection evicts the existing one. Close any
   lingering `nc` or iOS app session before starting the pipeline.

**DBC path problems** — "DBCLoadException" / no signals decoded:
- The DBC is now resolved relative to the **executable**, not the CWD (commit `c699d1d`), so
  running from any directory works. If you hit a load failure, confirm
  `resources/dbc/Model3CAN.dbc` exists next to (or above) the binary, and that you passed
  `--vehicle tesla` (Tesla → `Model3CAN.dbc`; `audi_mlb_evo` → `vw_mlb.dbc`).
- Rows appear but every signal column is blank and `dbc_signal_count=0`: frames are arriving but
  no *known* IDs are present — usually the wrong vehicle profile, or a partly-woken car.
- `make update-dbc` refreshes the DBC files from opendbc.

**Monitor (serial console) not opening / not auto-activating:**
- `make flash` intentionally no longer auto-opens a monitor. It prints the startup log once
  (30 s) and exits. Run `make monitor` for the live console, or `make capture` to log the serial
  stream to a timestamped file.
- `make monitor` shells out to `screen <port> 115200`. Exit with **Ctrl-A, k, y** — a stranded
  `screen` holds the port and the next `make flash` fails to open it. `screen -ls` then
  `screen -X -S <id> quit` to clear it.
- If the port is busy or missing, `make firmware-port` prints what was detected; override with
  `make monitor ESP32_PORT=/dev/cu.usbserial-XXXX`.

**Gearbox-log empty or missing:**
- Confirmed working on `--live-telemetry` in the canonical engine-sim-cli checkout
  (`$ESC_ROOT`; `attachGearboxLogger`, `CLIMain.cpp:115`). An empty file on another checkout means
  you are on an older binary — there was a historic bug where `reconfigureProfile` wiped the
  logger. Rebuild from the canonical repo.
- The file is written continuously; check it **after** the run ends cleanly (Ctrl-C or `--duration`).

**Engine never starts / dies after catching:**
- Use `--threaded`. Sync-pull's audio-clock coupling makes starts a coin-flip.
- Use the Ferrari preset first; engine-start reliability varies by MR script.
- If mph stays 0 the whole run you are replaying the parked lead-in — add `--start-from`.

**Other:**
- Board not enumerated by macOS → USB-A → USB-C adapter (broken USB-C data pins on cheap boards).
- Device unreachable / discovery finds nothing → it may have fallen back to AP mode. Join
  `ESP32-CAN` / `cancan12` and use `tcp:192.168.4.1`, or re-flash over USB with correct creds.
- Status LED patterns are documented by `./build-native/vehicle-sim --led-diag` (solid = client
  connected; the auth-failure blink pattern is distinct and worth learning).

## DELIVERABLES

1. Both new boards flashed, streaming, and verified with a real `AUTH` + frame check.
2. A recorded road test: `captures/RoadTest_<timestamp>.raw.txt` + `.csv`.
3. A gearbox log from the drive.
4. A short written verdict: did it sound like an ICE? Where did the illusion break (gear hunting,
   over-rev, latency, audio dropouts)?
5. If you learned something this brief got wrong, fix it and save the corrected version to
   `$VSIM_ROOT/docs/road-test-instructions.md` (this repo).

Be honest about what worked and what didn't. Gather evidence — don't speculate.
