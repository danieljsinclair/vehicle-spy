#pragma once

#include "vehicle-sim/cli/CliOptions.h"

#include <memory>
#include <string>
#include <string_view>

namespace vehicle_sim::cli {

// Serial transport seam for USB provisioning.
//
// THE sole serial I/O seam for provisioning. Production uses PosixSerialPort
// (a verbatim wrapper over open/termios/write/select/read on a /dev/cu.* fd);
// tests inject a FakeSerialPort that scripts the bytes — no real device, no
// real open/select/read, no real ESP32. There is exactly one serial interface;
// production uses PosixSerialPort, tests use FakeSerialPort.
//
// Why this exists (and not just inline POSIX in runProvisioningCommand):
//   The provisioning path drove a REAL serial fd, so the unit suite could only
//   assert on behavior with a physical ESP32 attached. Mocking the port is the
//   only way to assert the exact AT frame bytes deterministically.
class ISerialPort {
public:
    virtual ~ISerialPort() = default;

    /** Open the underlying serial source. Returns false on failure. */
    virtual bool open() = 0;

    /** Write every byte of `data`. Returns true iff all bytes were sent. */
    virtual bool writeAll(std::string_view data) = 0;

    /**
     * Block until the port is readable or `timeoutUs` elapses. Returns >0 when
     * readable, 0 on timeout, <0 on error. Mirrors select(fd+1, read, ...).
     */
    virtual int selectReadable(int timeoutUs) = 0;

    /** Read up to `len` bytes into `buf`. Returns >0 bytes read, 0 on EOF,
     *  <0 on error (errno holds the reason). Mirrors POSIX read(). */
    virtual ssize_t read(char* buf, size_t len) = 0;

    /** Close the port (no-op if not open). Idempotent. */
    virtual void close() noexcept = 0;
};

/** Build the production serial port for `port` (a /dev/cu.* path). */
std::unique_ptr<ISerialPort> createSerialPort(const std::string& port);

// Provisioning reply substrings the firmware emits over USB serial.
//   ATSETWIFI<ssid>,<pass>\r -> "OK WiFi credentials stored. Rebooting to connect..."
//   ATCLEARWIFI\r            -> "OK WiFi credentials cleared. Rebooting..."
//   ATREBOOT\r              -> "REBOOT"
// These are matched as substrings so transient log lines don't cause false negatives.
constexpr const char* PROVISION_OK_STORED = "stored";
constexpr const char* PROVISION_OK_CLEARED = "cleared";
constexpr const char* PROVISION_OK_REBOOT = "REBOOT";

// Substring that marks a LoopHeartbeat [STATE] line. The device emits these
// on a 5-second cadence (firmware/can-bridge/can-bridge.ino HEARTBEAT_INTERVAL_MS);
// --status waits for the NEXT one and prints it verbatim.
constexpr const char* PROVISION_OK_STATUS = "[STATE]";

// Default provisioning timeout. The device floods periodic [STATE] heartbeat /
// WiFi state lines over USB serial while it stores creds and reboots; under that
// load the "OK WiFi credentials stored. Rebooting..." ack can arrive well after
// the previous 8s window closed, producing a false-negative (the tool reports
// TIMEOUT even though provisioning succeeded). 120s gives the store+reboot
// sequence comfortable headroom so the matcher can capture the real ack.
constexpr int PROVISION_TIMEOUT_S = 120;

// --status wait budget. The heartbeat is on a 5-second cadence, so waiting up
// to 8s gives one full cycle plus jitter headroom. A timeout is reported as
// "no [STATE] line received in Ns" — the device is still alive, but no
// heartbeat has been emitted yet.
constexpr int PROVISION_STATUS_TIMEOUT_S = 8;

/**
 * Send a single AT command over the ESP32 USB serial console and wait for the
 * expected reply substring (or timeout).
 *
 * @param port     Opened serial port (the caller owns open/close).
 * @param command  Full AT command WITHOUT line terminator (a real '\r' is appended).
 * @param expect   Substring expected in the device's reply (PROVISION_OK_*).
 * @param timeoutS Seconds to wait for the expected reply before giving up.
 * @param log      Sink for echoed device output / status (std::cout in production).
 * @return 0 on expected reply, 1 on timeout / write failure / unexpected error.
 *
 * Local, pre-association: USB serial provisioning needs no AUTH, and the caller
 * supplies the credentials, so there is no secret to protect here — output goes
 * to the log verbatim (including the SSID line echoed back by the firmware).
 */
[[nodiscard]] int runProvisioningCommand(ISerialPort& port,
                                         const std::string& command,
                                         const std::string& expect,
                                         int timeoutS,
                                         std::ostream& log);

/**
 * Dispatch the provisioning CLI flags to the right AT command and run it.
 * Returns 0 on success, 1 on failure. Writes human-readable status to `out`
 * (std::cout) and errors to `err` (std::cerr).
 *
 * SRP: this function only decides WHICH command to send; the byte-level serial
 * I/O lives in runProvisioningCommand() / runStatus().
 *
 * Test seam: the overload taking an injected ISerialPort lets unit tests run
 * the full dispatch (flag -> AT command selection -> frame bytes) without a
 * real device. The (opts, out, err) form resolves opts.transport via the
 * universal connect selector (auto-detect or usb:<path>) and opens a real port.
 */
[[nodiscard]] int runProvisioning(const WifiProvisioningOptions& opts,
                                  ISerialPort& port,
                                  std::ostream& out,
                                  std::ostream& err);

/**
 * Read from the serial port until the next "[STATE] ..." heartbeat line appears
 * (or the deadline elapses), then write the captured line to `out`. The
 * heartbeat is on a 5-second cadence, so a fresh [STATE] line should arrive
 * within PROVISION_STATUS_TIMEOUT_S; the timeout is reported via err.
 *
 * No AT command is sent — [STATE] lines are device-driven, not acks.
 *
 * @return 0 on success (a [STATE] line was captured), 1 on timeout.
 *
 * SRP: this function only owns the wait-for-state-line loop; the byte-level
 * serial I/O is delegated to the ISerialPort seam. The test suite injects a
 * FakeSerialPort that scripts the [STATE] line directly.
 */
[[nodiscard]] int runStatus(ISerialPort& port,
                            int timeoutS,
                            std::ostream& out,
                            std::ostream& err);

[[nodiscard]] int runProvisioning(const WifiProvisioningOptions& opts,
                                  std::ostream& out,
                                  std::ostream& err);

} // namespace vehicle_sim::cli
