#include "vehicle-sim/cli/ProvisioningRunner.h"
#include "vehicle-sim/cli/LogSanitizer.h"
#include "vehicle-sim/pipeline/ISocket.h"
#include "vehicle-sim/pipeline/PosixSocket.h"

#include <fcntl.h>
#include <functional>
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

// TCP console port: an ISerialPort over a TCP socket to the ESP32's console.
//
// The firmware serves [STATE] heartbeats over TCP (can-bridge.ino writes them
// to the adopted client via TcpServerManager::writeLineToClient), but ONLY to a
// client that has authenticated: TcpServerManager::cycle reads the first line
// of every new connection and rejects anything that isn't "AUTH <token>" with
// "ERROR unauthorized" + a drop. So open() does the same AUTH handshake the
// live TCP transport does (TCPTransport::connectAndAuth) — send
// "AUTH vehicle-sim-2026\r", expect "OK" — before the read loop can see
// heartbeats. After auth the cadence is device-driven (no command needed), so
// read()/selectReadable() just drain whatever the firmware pushes.
//
// Why a distinct class rather than reusing TCPTransport: TCPTransport is a
// full ITransport (reconnect hunting, HELO, ELM327 init, deviceId) that never
// exposes the ISerialPort seam runStatus() needs. The console path wants only
// "connect, auth, read lines until [STATE]" — a thin wrapper that reuses the
// battle-tested ISocket/PosixSocket (the SAME socket seam the transport uses)
// so the connect/auth behavior is identical byte-for-byte.
class TcpConsolePort final : public ISerialPort {
public:
    // host is an IPv4 literal or hostname; port is the console TCP port
    // (firmware default 3333). `socket` is injected so tests can supply a
    // scripted FakeSocket; production passes a PosixSocket.
    TcpConsolePort(std::string host,
                   int port,
                   std::shared_ptr<vehicle_sim::pipeline::ISocket> socket)
        : host_(std::move(host)), port_(port), socket_(std::move(socket)) {}

    // Connect + AUTH handshake. Mirrors TCPTransport::connectAndAuth up to the
    // "OK" — we stop there (no ELM327 init, no HELO) because the console path
    // only wants the heartbeat stream, not the CAN stream contract.
    bool open() override {
        if (socket_->connect(host_, port_, /*stop=*/nullptr) < 0) {
            return false;
        }
        // Bound the auth-stage recv() so a silent peer can't hang open() past
        // the firmware's 5s auth window.
        if (!socket_->setRecvTimeout(AUTH_RECV_TIMEOUT_MS)) {
            socket_->close();
            return false;
        }
        // Disable Nagle so the single AUTH frame is flushed immediately rather
        // than held for the delayed-ACK window. Mirrors the transport's
        // setNoDelay(true) on connect.
        if (!socket_->setNoDelay(true)) {
            socket_->close();
            return false;
        }
        // The auth frame the firmware expects (TcpServerManager::isValidAuthToken):
        // first line = "AUTH " + token. Built at runtime — the token is a
        // build-time macro (TCP_AUTH_TOKEN) that can't be concatenated with
        // adjacent literals in a static constexpr, so we compose the frame
        // once per open() (a one-time connect cost, not per-read).
        const std::string authFrame = "AUTH " + std::string(AUTH_TOKEN) + "\r";
        if (!socket_->sendAll(authFrame)) {
            socket_->close();
            return false;
        }
        std::array<char, 64> resp{};
        const ssize_t n = socket_->recv(resp.data(), resp.size() - 1);
        if (n <= 0) {
            socket_->close();
            return false;
        }
        const std::string view(resp.data(), static_cast<std::size_t>(n));
        if (view.find("OK") == std::string::npos) {
            // Peer answered but did not grant auth ("ERROR unauthorized").
            socket_->close();
            return false;
        }
        authenticated_ = true;
        return true;
    }

    bool writeAll(std::string_view data) override {
        if (!authenticated_) return false;
        return socket_->sendAll(data);
    }

    int selectReadable(int timeoutUs) override {
        if (!authenticated_) return -1;
        return socket_->selectReadable(timeoutUs);
    }

    ssize_t read(char* buf, size_t len) override {
        if (!authenticated_) return -1;
        return socket_->recv(buf, len);
    }

    void close() noexcept override {
        socket_->close();
        authenticated_ = false;
    }

private:
    // The auth token the firmware expects (TcpServerManager::isValidAuthToken):
    // first line = "AUTH " + token. TCP_AUTH_TOKEN is defined by the build
    // (CMakeLists / Makefile) and matches the firmware's TCP_AUTH_TOKEN; the
    // literal here is the fallback when built without the define.
    static constexpr const char* AUTH_TOKEN =
#ifndef TCP_AUTH_TOKEN
        "vehicle-sim-2026";
#else
        TCP_AUTH_TOKEN;
#endif

    // Auth-stage recv() bound (ms). The firmware's TcpServerManager sets the
    // auth read timeout to 5000ms; staying under it keeps open() from racing
    // the server's drop.
    static constexpr int AUTH_RECV_TIMEOUT_MS = 4000;

    std::string host_;
    int port_;
    std::shared_ptr<vehicle_sim::pipeline::ISocket> socket_;
    bool authenticated_ = false;
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

// Why pollOnce returned Stop. The runStatus() caller surfaces this in its
// diagnostic — a peer-closed-without-data means the device rebooted mid-wait
// (a separate operator-meaningful failure from "deadline elapsed with the
// device silent"). runProvisioningCommand() ignores it (both are TIMEOUT).
enum class StopReason { None, SelectError, ReadError, PeerClosed };

// (forLog moved to the shared LogSanitizer — see vehicle-sim/cli/LogSanitizer.h.)

// Match predicate over the accumulated reply buffer. Returns Matched when
// the caller's success condition is satisfied, KeepPolling otherwise.
// Defined per use site:
//   * runProvisioningCommand: substring match against the AT ack (after
//     stripping [STATE] heartbeat noise)
//   * runStatus: a [STATE] marker followed by a CR/LF terminator
// SRP: pollOnce() owns the wait+read step; the matcher's job is to ask "is
// this reply good enough to stop?", nothing else. The reference is
// const because both matchers are pure functions over the buffer — the
// substring filter lives in stripStateLines() which returns a new string
// (functional, no in-place mutation).
using MatchFn = std::function<PollStep(const std::string& reply)>;

// Drop whole "[STATE] ... <CR/LF>" lines so the substring matcher ignores
// periodic heartbeat / WiFi-state noise. Declared here, defined after the
// matcher factories (which call it from inside their lambdas at runtime —
// forward declaration is the only ordering requirement).
std::string stripStateLines(const std::string& reply);

// Run ONE poll step: wait briefly for readable bytes, append whatever arrived
// to `reply`, and report whether the caller should keep polling, stop having
// matched, or stop without a match.
//
// SRP: this owns a single socket wait+read+match step; the surrounding loop
// (runProvisioningCommand, runStatus) owns the framing and the deadline.
// Keeping them apart is what lets the read loop stay a three-line single-exit
// construct shared by BOTH callers.
//
// `stopReason` is an out-parameter that names WHY the step returned Stop
// (select error, read error, peer-closed). Set on every Stop; callers that
// don't care (runProvisioningCommand) can ignore it. runStatus() uses it to
// distinguish "deadline elapsed" from "device disappeared mid-wait" so the
// operator gets the right diagnostic.
//
// `Match` is a template so each caller can pass its concrete callable
// (function pointer, lambda, etc.) without the type-erasure overhead of
// std::function (cpp:S5213). The constraint matches MatchFn's signature:
// `PollStep(const std::string&)`.
template <typename Match>
PollStep pollOnce(ISerialPort& port,
                  const Match& match,
                  std::string& reply,
                  std::ostream& log,
                  StopReason& stopReason);

// Build a substring match predicate for an AT ack (e.g. "stored", "cleared").
// An empty `expect` is "fire-and-forget": Matched immediately on the first
// successful read step (mirrors the original pollOnce() semantics for the
// "no expected reply" path).
//
// The matcher takes a CONST reference (no in-place mutation) and asks
// stripStateLines() to RETURN a filtered copy of the buffer for the
// substring search. Under a state-flood the firmware's periodic heartbeat
// noise otherwise buries the real ack; the strip is local to the substring
// path because runStatus() wants to see the [STATE] lines, not lose them.
MatchFn makeSubstringMatcher(std::string_view expect) {
    return [expect](const std::string& reply) {
        if (expect.empty()) {
            return PollStep::Matched;
        }
        const std::string filtered = stripStateLines(reply);
        return filtered.find(expect) != std::string::npos
                   ? PollStep::Matched
                   : PollStep::KeepPolling;
    };
}

// Build a predicate that matches "[STATE] ... <CR/LF>" — the LoopHeartbeat
// heartbeat the firmware emits on a 5s cadence. We require BOTH the marker
// AND a terminator so a [STATE] line that arrived at the tail of a previous
// chunk and is not yet terminated keeps us reading.
MatchFn makeStateLineMatcher() {
    return [](std::string_view reply) {
        const std::size_t markerPos = reply.find(PROVISION_OK_STATUS);
        if (markerPos == std::string::npos) {
            return PollStep::KeepPolling;
        }
        return reply.find_first_of("\r\n", markerPos) != std::string::npos
                   ? PollStep::Matched
                   : PollStep::KeepPolling;
    };
}

// One poll step: wait briefly for readable bytes, append whatever arrived
// to `reply`, then ask the matcher to decide. See the forward declaration
// above for the SRP / stopReason contract.
//
// `log` receives I/O-failure diagnostics (select/read errors) and the
// per-byte echo of the bytes just read. runStatus() uses a discarding
// stream for this argument so the operator sees only the captured
// [STATE] line, not every raw heartbeat byte that flew by.
template <typename Match>
PollStep pollOnce(ISerialPort& port,
                  const Match& match,
                  std::string& reply,
                  std::ostream& log,
                  StopReason& stopReason) {
    stopReason = StopReason::None;
    const int ready = port.selectReadable(SERIAL_POLL_INTERVAL_US);
    if (ready < 0) {
        log << "[provision] Select failed on serial port\n";
        stopReason = StopReason::SelectError;
        return PollStep::Stop;
    }
    if (ready == 0) {
        // Poll step elapsed with no bytes. Consult the matcher on the
        // current buffer anyway so fire-and-forget matchers (empty expect)
        // can short-circuit to Matched without waiting for a read.
        return match(reply);
    }

    std::array<char, SERIAL_READ_CHUNK> buf{};
    const ssize_t n = port.read(buf.data(), buf.size());
    if (n < 0) {
        log << "[provision] Read failed on serial port\n";
        stopReason = StopReason::ReadError;
        return PollStep::Stop;
    }
    if (n == 0) {
        // Peer closed / no more data. The matcher is not consulted: an
        // empty buffer cannot satisfy a substring or [STATE] match, and
        // returning Matched here would lie to the caller. Caller
        // distinguishes "peer closed" from "deadline" via stopReason.
        stopReason = StopReason::PeerClosed;
        return PollStep::Stop;
    }

    const auto count = static_cast<std::size_t>(n);
    reply.append(buf.data(), count);
    log << "[provision] <- " << std::string_view(buf.data(), count);

    // Stripping [STATE] heartbeat lines is the substring matcher's job
    // (it drops them so the AT-ack substring is not buried by noise).
    // The state-line matcher does NOT strip — the [STATE] line IS the
    // signal it is looking for.
    return match(reply);
}

// Discarding stream sink. Used by runStatus() so the per-byte echo
// (which is meaningful for AT-ack debugging in runProvisioningCommand)
// does not flood the operator's stderr while a [STATE] heartbeat
// streams by.
class NullStream final : public std::ostream {
public:
    NullStream() : std::ostream(&buf_) {}
private:
    class NullBuffer final : public std::streambuf {
    public:
        int overflow(int c) override { return c; }
    };
    NullBuffer buf_;
};

// Strip "[STATE] ... <terminator>" heartbeat lines from `reply` so the device's
// periodic state broadcasts don't bury the AT-command ack the matcher looks for.
// A line is complete up to its next terminator ('\n', a lone '\r', or a "\r\n"
// pair). We drop ONLY a complete line that begins with "[STATE]"; every other
// line is preserved verbatim — complete non-STATE lines (the "ATSETWIFI" echo,
// the "OK WiFi credentials stored" ack), partial trailing lines, and partial
// [STATE] lines left in place to be completed and dropped on a later poll step.
// Returns a new string rather than mutating `reply` in place — the caller is
// usually a const matcher that owns the buffer but must not rewrite it.
std::string stripStateLines(const std::string& reply) {
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

    return out;
}

} // namespace

std::unique_ptr<ISerialPort> createSerialPort(const std::string& port) {
    return std::make_unique<PosixSerialPort>(port);
}

// Build the production TCP console port for host:port. The caller supplies the
// resolved host/port (from parseTcpTarget); the port defaults to the firmware
// console port (3333) when the target carried no explicit port. Tests inject a
// FakeSocket via the unit-test overload below.
std::unique_ptr<ISerialPort> createTcpConsolePort(const std::string& host, int port) {
    return std::make_unique<TcpConsolePort>(
        host, port, std::make_shared<vehicle_sim::pipeline::PosixSocket>());
}

std::unique_ptr<ISerialPort> createTcpConsolePort(
    const std::string& host,
    int port,
    std::shared_ptr<vehicle_sim::pipeline::ISocket> socket) {
    return std::make_unique<TcpConsolePort>(host, port, std::move(socket));
}

// Auto-detect glob patterns, in priority order. The device enumerates under
// /dev/cu.usbserial-* on macOS, /dev/cu.SLAB_USBtoUART on Silicon Labs CP210x
// boards, and /dev/cu.wchusbserial* on WCH CH340/CH341 boards. The first
// pattern that yields at least one match wins; if no pattern matches, the
// caller falls back to ESP32_DEFAULT_USB_PORT.
constexpr std::array<const char*, 3> kUsbGlobPatterns = {
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
        if (::glob(pattern, GLOB_NOSORT, nullptr, &globResult) == 0 &&
            globResult.gl_pathc > 0) {
            std::string first = globResult.gl_pathv[0];
            ::globfree(&globResult);
            return first;
        }
        ::globfree(&globResult);
    }
    return "";
}

std::string resolveSerialPort(const std::string& transport) {
    // Empty or "auto" -> auto-detect, fall back to the build-time default.
    if (transport.empty() || transport == "auto") {
        if (const std::string detected = autoDetectSerialPort(); !detected.empty()) {
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

    const MatchFn match = makeSubstringMatcher(expect);
    PollStep outcome = PollStep::KeepPolling;
    while (outcome == PollStep::KeepPolling &&
           std::chrono::steady_clock::now() < deadline) {
        // runProvisioningCommand does not distinguish WHY pollOnce stopped;
        // any Stop maps to the same TIMEOUT diagnostic. The reason out-param
        // is still required by the shared loop's contract.
        StopReason reason;
        outcome = pollOnce(port, match, reply, log, reason);
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
    // The wait+read step is shared with runProvisioningCommand() via
    // pollOnce(); only the matcher differs (substring vs. "[STATE] ...
    // <CR/LF>"). When pollOnce reports Stop, the underlying cause is
    // surfaced as the same TIMEOUT diagnostic the deadline path uses so the
    // operator gets a clear "no snapshot" message regardless of which way
    // the wait ended.
    std::string reply;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeoutS);

    const MatchFn match = makeStateLineMatcher();
    PollStep outcome = PollStep::KeepPolling;
    StopReason stopReason = StopReason::None;
    NullStream sink;  // discard the per-byte echo — the [STATE] line is what we want
    while (outcome == PollStep::KeepPolling &&
           std::chrono::steady_clock::now() < deadline) {
        outcome = pollOnce(port, match, reply, sink, stopReason);
    }

    if (outcome == PollStep::Matched) {
        // The matcher guaranteed both the [STATE] marker and a terminator
        // are present, so this find is guaranteed to succeed.
        const std::size_t markerPos = reply.find(PROVISION_OK_STATUS);
        const std::size_t termPos = reply.find_first_of("\r\n", markerPos);
        out << reply.substr(markerPos, termPos - markerPos) << "\n";
        return 0;
    }

    // Distinguish "deadline elapsed" from "device disappeared mid-wait" so
    // the operator gets the right diagnostic — a peer-closed-without-data
    // means the device is most likely mid-reboot, not silent.
    if (stopReason == StopReason::PeerClosed) {
        err << "[provision] TIMEOUT: no [STATE] line received within "
            << timeoutS << "s (peer closed)\n";
    } else {
        err << "[provision] TIMEOUT: no [STATE] line received within "
            << timeoutS << "s\n";
    }
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
