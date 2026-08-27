#include "vehicle-sim/cli/ProvisioningRunner.h"
#include "vehicle-sim/cli/LogSanitizer.h"

#include <fcntl.h>
#include <glob.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace vehicle_sim::cli {

namespace {

// Production serial port: a real /dev/cu.* fd in raw 8N1 @ 115200, mirroring
// the firmware's USB Serial console. All byte I/O is delegated to this class
// so runProvisioningCommand() has no POSIX dependency and is unit-testable.
class PosixSerialPort final : public ISerialPort {
public:
    explicit PosixSerialPort(std::string port) : port_(std::move(port)) {}

    bool open() override {
        fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            return false;
        }

        termios attrs{};
        if (tcgetattr(fd_, &attrs) != 0) {
            close();
            return false;
        }

        cfmakeraw(&attrs);
#ifdef __APPLE__
        attrs.c_ispeed = 115200;
        attrs.c_ospeed = 115200;
#else
        if (cfsetispeed(&attrs, B115200) != 0 ||
            cfsetospeed(&attrs, B115200) != 0) {
            close();
            return false;
        }
#endif
        attrs.c_cflag |= (CLOCAL | CREAD);
        attrs.c_cflag &= static_cast<tcflag_t>(~CSIZE);
        attrs.c_cflag |= CS8;
        attrs.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
        attrs.c_cflag &= static_cast<tcflag_t>(~PARENB);
        attrs.c_cc[VMIN] = 0;
        attrs.c_cc[VTIME] = 0;

        if (tcsetattr(fd_, TCSANOW, &attrs) != 0) {
            close();
            return false;
        }
        return true;
    }

    bool writeAll(std::string_view data) override {
        if (fd_ < 0) return false;
        const ssize_t written =
            ::write(fd_, data.data(), data.size());
        return written >= 0 &&
               static_cast<std::size_t>(written) == data.size();
    }

    int selectReadable(int timeoutUs) override {
        if (fd_ < 0) return -1;
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd_, &readSet);
        timeval tv{};
        tv.tv_sec = timeoutUs / 1'000'000;
        tv.tv_usec = timeoutUs % 1'000'000;
        return ::select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
    }

    ssize_t read(char* buf, size_t len) override {
        if (fd_ < 0) return -1;
        return ::read(fd_, buf, len);
    }

    void close() noexcept override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    std::string port_;
    int fd_ = -1;
};

// How long a single poll step waits for readable bytes before re-checking the
// overall deadline (200ms — responsive to Ctrl-C, cheap enough to spin on).
constexpr int SERIAL_POLL_INTERVAL_US = 200'000;

// Per-read chunk size. The firmware's replies are short single lines; anything
// larger is drained across successive poll steps.
constexpr std::size_t SERIAL_READ_CHUNK = 256;

// What a single poll step concluded. Naming the three outcomes removes the
// out-parameter flag the previous inline lambda needed, so each caller branch
// is explicit rather than a bool whose meaning depends on a second variable.
enum class PollStep { KeepPolling, Matched, Stop };

// (forLog moved to the shared LogSanitizer — see vehicle-sim/cli/LogSanitizer.h.)

// Drop whole "[STATE] ... <CR/LF>" lines so the matcher ignores periodic
// heartbeat / WiFi-state noise. Declared here; defined after pollOnce.
void stripStateLines(std::string& reply);

// Run ONE poll step: wait briefly for readable bytes, append whatever arrived
// to `reply`, and report whether the caller should keep polling, stop having
// matched, or stop without a match.
//
// SRP: this owns a single socket wait+read+match step; runProvisioningCommand()
// owns the framing and the deadline. Keeping them apart is what lets the read
// loop stay a three-line single-exit construct.
PollStep pollOnce(ISerialPort& port,
                  std::string_view expect,
                  std::string& reply,
                  std::ostream& log) {
    const int ready = port.selectReadable(SERIAL_POLL_INTERVAL_US);
    if (ready < 0) {
        log << "[provision] Select failed on serial port\n";
        return PollStep::Stop;
    }
    if (ready == 0) {
        // No expected reply (e.g. fire-and-forget) — success once sent.
        return expect.empty() ? PollStep::Matched : PollStep::KeepPolling;
    }

    std::array<char, SERIAL_READ_CHUNK> buf{};
    const ssize_t n = port.read(buf.data(), buf.size());
    if (n < 0) {
        log << "[provision] Read failed on serial port\n";
        return PollStep::Stop;
    }
    if (n == 0) {
        return PollStep::Stop;  // peer closed / no more data
    }

    const auto count = static_cast<std::size_t>(n);
    reply.append(buf.data(), count);
    log << "[provision] <- " << std::string_view(buf.data(), count);

    // The firmware emits periodic [STATE] heartbeat / WiFi-state lines that have
    // nothing to do with the provisioning ack (LoopHeartbeat.cpp, WiFiManager.cpp).
    // Under a state-flood these lines bury the real ack in noise; strip whole
    // "[STATE] ... <CR/LF>" lines from the match buffer so find() sees only the
    // relevant reply. We only drop [STATE] lines — the "ATSETWIFI" command echo and
    // the "OK WiFi credentials stored" ack are preserved verbatim, so the data
    // fields in the AT frame are never touched.
    stripStateLines(reply);

    const bool matched =
        !expect.empty() && reply.find(expect) != std::string::npos;
    return matched ? PollStep::Matched : PollStep::KeepPolling;
}

// Strip "[STATE] ... <terminator>" heartbeat lines from `reply` so the device's
// periodic state broadcasts don't bury the AT-command ack the matcher looks for.
// A line is complete up to its next terminator ('\n', a lone '\r', or a "\r\n"
// pair). We drop ONLY a complete line that begins with "[STATE]"; every other
// line is preserved verbatim — complete non-STATE lines (the "ATSETWIFI" echo,
// the "OK WiFi credentials stored" ack), partial trailing lines, and partial
// [STATE] lines left in place to be completed and dropped on a later poll step.
void stripStateLines(std::string& reply) {
    std::string out;
    out.reserve(reply.size());

    std::size_t pos = 0;
    const std::size_t n = reply.size();
    while (pos < n) {
        const std::size_t term = reply.find_first_of("\r\n", pos);
        if (term == std::string::npos) {
            // Trailing partial line: keep it (incl. a partial [STATE] line).
            out.append(reply, pos, n - pos);
            break;
        }

        const bool crlf = reply[term] == '\r' && term + 1 < n && reply[term + 1] == '\n';
        const std::size_t termLen = crlf ? 2 : 1;
        if (const bool isStateLine = reply.compare(pos, 7, "[STATE]") == 0; !isStateLine) {
            // Keep the whole line including its terminator(s).
            out.append(reply, pos, (term - pos) + termLen);
        }
        // else: complete [STATE] line — drop it entirely.

        pos = term + termLen;
    }

    reply = std::move(out);
}

} // namespace

std::unique_ptr<ISerialPort> createSerialPort(const std::string& port) {
    return std::make_unique<PosixSerialPort>(port);
}

// Auto-detect glob patterns, in priority order. The device enumerates under
// /dev/cu.usbserial-* on macOS, /dev/cu.SLAB_USBtoUART on Silicon Labs CP210x
// boards, and /dev/cu.wchusbserial* on WCH CH340/CH341 boards. The first
// pattern that yields at least one match wins; if no pattern matches, the
// caller falls back to ESP32_DEFAULT_USB_PORT.
constexpr const char* kUsbGlobPatterns[] = {
    "/dev/cu.usbserial*",
    "/dev/cu.SLAB_USBtoUART",
    "/dev/cu.wchusbserial*",
};

// Return the first path that matches any auto-detect glob pattern. The order
// of patterns is the priority order; the first non-empty result wins. Returns
// an empty string if no pattern matched (caller falls back to the default).
std::string autoDetectSerialPort() {
    for (const char* pattern : kUsbGlobPatterns) {
        glob_t globResult{};
        if (::glob(pattern, GLOB_NOSORT, nullptr, &globResult) == 0) {
            if (globResult.gl_pathc > 0) {
                std::string first = globResult.gl_pathv[0];
                ::globfree(&globResult);
                return first;
            }
        }
        ::globfree(&globResult);
    }
    return "";
}

std::string resolveSerialPort(const std::string& transport) {
    // Empty or "auto" -> auto-detect, fall back to the build-time default.
    if (transport.empty() || transport == "auto") {
        std::string detected = autoDetectSerialPort();
        if (!detected.empty()) {
            return detected;
        }
        return ESP32_DEFAULT_USB_PORT;
    }
    // "usb:<path>" -> strip the prefix, return the path verbatim. Validation
    // has already rejected anything that isn't empty / auto / usb:; this is
    // defensive — return "" on any other form.
    if (transport.rfind("usb:", 0) == 0) {
        return transport.substr(4);
    }
    return "";
}

int runProvisioningCommand(ISerialPort& port,
                           const std::string& command,
                           const std::string& expect,
                           int timeoutS,
                           std::ostream& log) {
    // The firmware framer terminates lines on '\r' or '\n' — send a REAL CR byte,
    // never a literal backslash-r. ATSETWIFI carries ssid,pass so terminator is
    // required to flush the command into the dispatcher.
    const std::string frame = command + "\r";
    if (!port.writeAll(frame)) {
        log << "[provision] Failed to write to serial port\n";
        return 1;
    }
    // `command` is argv-derived, so it is sanitized before echoing to keep a
    // crafted SSID/password from forging extra log lines (cpp:S5145).
    log << "[provision] -> " << forLog(command) << "  (sent " << frame.size()
        << " bytes)\n";

    // Read raw bytes, accumulating into a buffer, until `expect` appears or the
    // timeout elapses. We do NOT split on newlines — the firmware may emit
    // multi-line startup logs; we only care about the substring.
    //
    // Each step's wait+read+match lives in pollOnce(); this loop owns only the
    // deadline. That split keeps the loop a single-exit construct with exactly
    // one break, and keeps both pieces small enough to read at a glance.
    std::string reply;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeoutS);

    PollStep outcome = PollStep::KeepPolling;
    while (outcome == PollStep::KeepPolling &&
           std::chrono::steady_clock::now() < deadline) {
        outcome = pollOnce(port, expect, reply, log);
    }

    int rc = 1;
    if (outcome == PollStep::Matched) {
        log << "[provision] OK (matched '" << expect << "')\n";
        rc = 0;
    } else {
        log << "[provision] TIMEOUT: expected substring '" << expect
            << "' not seen within " << timeoutS << "s\n";
    }
    return rc;
}

int runStatus(ISerialPort& port,
              int timeoutS,
              std::ostream& out,
              std::ostream& err) {
    // [STATE] lines are device-driven (no AT ack), so we do not send a
    // command. We just wait for the NEXT heartbeat line and print it.
    //
    // The read loop mirrors pollOnce()'s shape (single read step per
    // iteration, single exit point) so the timeout-bounded wait is a
    // single-exit construct — the only branch is "matched / keep polling /
    // stop". When a [STATE] line lands, we extract everything up to the next
    // CR/LF terminator and write that substring to `out` so callers can
    // capture / pipe it cleanly.
    std::string reply;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeoutS);

    while (std::chrono::steady_clock::now() < deadline) {
        const int ready = port.selectReadable(SERIAL_POLL_INTERVAL_US);
        if (ready < 0) {
            err << "[provision] Select failed on serial port\n";
            return 1;
        }
        if (ready == 0) {
            continue;  // poll step elapsed, keep waiting up to the deadline
        }

        std::array<char, SERIAL_READ_CHUNK> buf{};
        const ssize_t n = port.read(buf.data(), buf.size());
        if (n < 0) {
            err << "[provision] Read failed on serial port\n";
            return 1;
        }
        if (n == 0) {
            // Peer closed / no more data. The heartbeat is device-driven, so
            // an EOF without a [STATE] line means the device is gone (most
            // likely mid-reboot) — surface the same TIMEOUT diagnostic the
            // deadline path uses so the operator gets a clear "no snapshot"
            // message regardless of which way the wait ended.
            err << "[provision] TIMEOUT: no [STATE] line received within "
                << timeoutS << "s (peer closed)\n";
            return 1;
        }

        const auto count = static_cast<std::size_t>(n);
        reply.append(buf.data(), count);

        // The [STATE] line is single-line; as soon as we see the marker AND
        // a terminator we can slice the line out and print it. We search the
        // WHOLE buffer (not just the new bytes) so a marker that arrived at
        // the tail of a previous chunk is still caught.
        const std::size_t markerPos = reply.find(PROVISION_OK_STATUS);
        if (markerPos == std::string::npos) {
            continue;
        }
        const std::size_t termPos =
            reply.find_first_of("\r\n", markerPos);
        if (termPos == std::string::npos) {
            // Marker seen but the line is not yet complete; keep reading.
            continue;
        }

        out << reply.substr(markerPos, termPos - markerPos) << "\n";
        return 0;
    }

    err << "[provision] TIMEOUT: no [STATE] line received within "
        << timeoutS << "s\n";
    return 1;
}

int runProvisioning(const WifiProvisioningOptions& opts,
                    ISerialPort& port,
                    std::ostream& out,
                    std::ostream& err) {
    if (!port.open()) {
        // Name the port that failed. The caller resolves the transport
        // (auto-detect or usb:<path>) before reaching this overload, so the
        // port we tried is whatever the injected ISerialPort was opened on.
        // The injected test port has no name; in production, the name comes
        // from createSerialPort's argument. We extract it via ISerialPort's
        // name accessor — but the seam doesn't expose that today. Log the
        // transport string instead (sanitized) and the runtime default.
        const std::string resolved = resolveSerialPort(opts.transport);
        err << "[provision] Failed to open serial port " << forLog(resolved)
            << "\n";
        return 1;
    }

    int rc = 1;
    if (opts.status_requested) {
        out << "[provision] Reading [STATE] snapshot from device\n";
        rc = runStatus(port, PROVISION_STATUS_TIMEOUT_S, out, err);
    } else if (opts.clear_wifi_creds) {
        out << "[provision] Clearing WiFi credentials over USB serial\n";
        rc = runProvisioningCommand(port, "ATCLEARWIFI", PROVISION_OK_CLEARED,
                                    PROVISION_TIMEOUT_S, out);
    } else if (opts.reboot_esp32) {
        out << "[provision] Rebooting ESP32 over USB serial\n";
        rc = runProvisioningCommand(port, "ATREBOOT", PROVISION_OK_REBOOT,
                                    PROVISION_TIMEOUT_S, out);
    } else if (!opts.set_wifi_ssid.empty()) {
        out << "[provision] Setting WiFi credentials (SSID="
            << forLog(opts.set_wifi_ssid) << ") over USB serial\n";
        const std::string command =
            "ATSETWIFI" + opts.set_wifi_ssid + "," + opts.set_wifi_pass;
        rc = runProvisioningCommand(port, command, PROVISION_OK_STORED,
                                    PROVISION_TIMEOUT_S, out);
    } else {
        err << "[provision] No provisioning operation requested.\n";
    }

    port.close();
    return rc;
}

int runProvisioning(const WifiProvisioningOptions& opts,
                    std::ostream& out,
                    std::ostream& err) {
    // Open/close is owned SOLELY by the injected-port overload below. This used
    // to open() here as well and then delegate to an overload that open()s
    // again; PosixSerialPort::open() overwrites fd_ without closing the previous
    // descriptor, so every provisioning run leaked one fd (and the trailing
    // close() only reclaimed the second). One owner, one open, one close.
    //
    // The universal --connect transport (auto / usb:<path>) is resolved into
    // a concrete /dev/cu.* path here, BEFORE we open the port. The dispatcher
    // takes the resolved port's ISerialPort, never the transport string.
    const std::string resolved = resolveSerialPort(opts.transport);
    if (resolved.empty()) {
        err << "[provision] Could not resolve provisioning transport '"
            << forLog(opts.transport) << "'\n";
        return 1;
    }
    auto port = createSerialPort(resolved);
    return runProvisioning(opts, *port, out, err);
}

} // namespace vehicle_sim::cli
