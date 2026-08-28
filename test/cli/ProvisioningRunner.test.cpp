#include <gtest/gtest.h>

#include "vehicle-sim/cli/CliOptions.h"
#include "vehicle-sim/cli/ProvisioningRunner.h"

#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace vehicle_sim::cli;

namespace {

// Scripted serial port for the provisioning unit suites. There is no real
// device, no real open/select/read — every byte the firmware emits is scripted,
// and every byte the runner writes is captured so the AT frame can be asserted
// exactly. This is the sole serial seam the fast provisioning tests use.
class FakeSerialPort final : public ISerialPort {
public:
    // Queue the device's reply chunks (delivered in order). After all chunks
    // are consumed, selectReadable returns 1 + a zero-byte read models EOF
    // UNLESS setSilentAfterQueue(true) is set — that mode models a device
    // that simply stops sending (the deadline path of runStatus).
    void enqueueReply(std::deque<std::string> chunks) {
        reply_ = std::move(chunks);
    }

    // Make open() fail, to drive the open-failure diagnostic path.
    void setOpenFails(bool fails) { openFails_ = fails; }

    // When true: after the queued reply is drained, selectReadable() returns
    // 0 (no bytes) and read() is never called. Models a device that is
    // silent-but-still-connected, so the deadline elapses naturally rather
    // than the connection dropping.
    void setSilentAfterQueue(bool silent) { silentAfterQueue_ = silent; }

    int openCallCount() const { return openCount_; }
    int closeCallCount() const { return closeCount_; }

    bool open() override {
        ++openCount_;
        if (openFails_) return false;
        opened_ = true;
        return true;
    }
    bool writeAll(std::string_view data) override {
        if (!opened_) return false;
        written_.emplace_back(data);
        return true;
    }
    int selectReadable(int /*timeoutUs*/) override {
        // Readable as long as there are scripted bytes. After the queue
        // drains, either: (a) signal EOF via one more readable+zero-byte
        // read (the runProvisioningCommand case), or (b) become silent so
        // the deadline elapses (the runStatus deadline case).
        if (!reply_.empty()) return 1;
        if (silentAfterQueue_) return 0;
        if (!eofSignaled_) return 1;
        return 0;
    }
    ssize_t read(char* buf, size_t len) override {
        if (!reply_.empty()) {
            std::string chunk = std::move(reply_.front());
            reply_.pop_front();
            const size_t n = std::min(chunk.size(), len);
            std::memcpy(buf, chunk.data(), n);
            if (chunk.size() > len) {
                reply_.push_front(chunk.substr(n));
            }
            return static_cast<ssize_t>(n);
        }
        eofSignaled_ = true;
        return 0;  // EOF
    }
    void close() noexcept override { ++closeCount_; opened_ = false; }

    // Catenate everything the runner wrote to the port (the AT frames).
    std::string writtenBlob() const {
        std::string s;
        for (const auto& x : written_) s += x;
        return s;
    }

private:
    bool opened_ = false;
    bool openFails_ = false;
    bool eofSignaled_ = false;
    bool silentAfterQueue_ = false;
    int openCount_ = 0;
    int closeCount_ = 0;
    std::deque<std::string> reply_;
    std::vector<std::string> written_;
};

// Build a WifiProvisioningOptions with a single provisioning flag set.
WifiProvisioningOptions provisioningOpts(bool clear, bool reboot, const std::string& ssid,
                             const std::string& pass) {
    WifiProvisioningOptions o;
    o.clear_wifi_creds = clear;
    o.reboot_esp32 = reboot;
    o.set_wifi_ssid = ssid;
    o.set_wifi_pass = pass;
    o.transport = "usb:/dev/cu.usbserial-TEST";
    return o;
}

} // namespace

// --- runProvisioningCommand: exact AT frame bytes & reply matching ----------

TEST(ProvisioningRunnerTest, SetWifiSendsAtSetWifiFrame) {
    FakeSerialPort port;
    // Firmware replies with the "stored" substring once it has the creds.
    port.enqueueReply({"OK WiFi credentials stored. Rebooting to connect...\r"});
    ASSERT_TRUE(port.open());

    std::ostringstream out, err;
    const int rc = runProvisioningCommand(
        port, "ATSETWIFIMyNet,secret123", PROVISION_OK_STORED,
        PROVISION_TIMEOUT_S, out);

    EXPECT_EQ(rc, 0);
    // The exact frame: command + a REAL '\r' terminator (not "\r" text).
    EXPECT_EQ(port.writtenBlob(), "ATSETWIFIMyNet,secret123\r");
    EXPECT_NE(out.str().find("ATSETWIFIMyNet,secret123"), std::string::npos);
}

TEST(ProvisioningRunnerTest, ClearWifiSendsAtClearWifiFrame) {
    FakeSerialPort port;
    port.enqueueReply({"OK WiFi credentials cleared. Rebooting...\r"});
    ASSERT_TRUE(port.open());

    std::ostringstream out, err;
    const int rc = runProvisioningCommand(
        port, "ATCLEARWIFI", PROVISION_OK_CLEARED,
        PROVISION_TIMEOUT_S, out);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(port.writtenBlob(), "ATCLEARWIFI\r");
}

TEST(ProvisioningRunnerTest, RebootSendsAtRebootFrame) {
    FakeSerialPort port;
    port.enqueueReply({"REBOOT\r"});
    ASSERT_TRUE(port.open());

    std::ostringstream out, err;
    const int rc = runProvisioningCommand(
        port, "ATREBOOT", PROVISION_OK_REBOOT,
        PROVISION_TIMEOUT_S, out);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(port.writtenBlob(), "ATREBOOT\r");
}

TEST(ProvisioningRunnerTest, TimeoutWhenExpectNotSeenReturnsFailure) {
    FakeSerialPort port;
    // Device says nothing matching "stored".
    port.enqueueReply({"some unrelated log line\r"});
    ASSERT_TRUE(port.open());

    std::ostringstream out, err;
    const int rc = runProvisioningCommand(
        port, "ATSETWIFIx,y", PROVISION_OK_STORED, 1 /*short timeout*/,
        out);

    EXPECT_EQ(rc, 1);
    // The frame was still sent — the failure is at the reply-matching stage.
    EXPECT_EQ(port.writtenBlob(), "ATSETWIFIx,y\r");
}

// --- runProvisioning (dispatch): flag -> command selection ------------------

TEST(ProvisioningRunnerTest, DispatchSetWifiViaOpts) {
    FakeSerialPort port;
    port.enqueueReply({"OK WiFi credentials stored. Rebooting to connect...\r"});

    WifiProvisioningOptions opts =
        provisioningOpts(false, false, "HomeNet", "p@ss word!");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 0);
    // The dispatcher must compose ATSETWIFI<ssid>,<pass>\r — prove the two
    // CLI fields are concatenated into the exact frame (space in pass too).
    EXPECT_EQ(port.writtenBlob(), "ATSETWIFIHomeNet,p@ss word!\r");
}

TEST(ProvisioningRunnerTest, DispatchClearWifi) {
    FakeSerialPort port;
    port.enqueueReply({"OK WiFi credentials cleared. Rebooting...\r"});

    WifiProvisioningOptions opts = provisioningOpts(true, false, "", "");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(port.writtenBlob(), "ATCLEARWIFI\r");
}

TEST(ProvisioningRunnerTest, DispatchReboot) {
    FakeSerialPort port;
    port.enqueueReply({"REBOOT\r"});

    WifiProvisioningOptions opts = provisioningOpts(false, true, "", "");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(port.writtenBlob(), "ATREBOOT\r");
}

TEST(ProvisioningRunnerTest, DispatchNoOperationReturnsFailure) {
    FakeSerialPort port;
    WifiProvisioningOptions opts = provisioningOpts(false, false, "", "");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 1);
    // Nothing should be sent when no provisioning flag is set.
    EXPECT_TRUE(port.writtenBlob().empty());
    EXPECT_NE(err.str().find("No provisioning operation"), std::string::npos);
}

TEST(ProvisioningRunnerTest, DispatchClearTakesPrecedenceOverSet) {
    FakeSerialPort port;
    port.enqueueReply({"OK WiFi credentials cleared. Rebooting...\r"});

    // Both flags requested: clear_wifi_creds is checked first in the dispatcher.
    WifiProvisioningOptions opts = provisioningOpts(true, false, "ignored", "ignored");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(port.writtenBlob(), "ATCLEARWIFI\r");
}

// --- Log-injection defence (cpp:S5145) & port lifecycle --------------------

// A crafted SSID carrying CR/LF must not be able to forge extra "[provision]"
// log lines. The control bytes are neutralised in the echo, while the frame
// actually written to the device still carries the operator's exact bytes.
TEST(ProvisioningRunnerTest, CraftedSsidControlCharsAreNeutralisedInLog) {
    FakeSerialPort port;
    port.enqueueReply({"OK WiFi credentials stored. Rebooting to connect...\r"});

    WifiProvisioningOptions opts =
        provisioningOpts(false, false, "Net\r\n[provision] OK forged", "pw");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 0);
    // The echoed log must not contain a raw CR/LF from the SSID, which is what
    // would let an attacker inject a whole fake log line.
    const std::string logged = out.str();
    EXPECT_EQ(logged.find("\r\n[provision] OK forged"), std::string::npos)
        << "control bytes from argv must not be echoed verbatim into the log";
    // The wire frame is unchanged — sanitisation is a LOGGING concern only, it
    // must never corrupt the credentials actually sent to the device.
    EXPECT_NE(port.writtenBlob().find("Net\r\n[provision] OK forged"),
              std::string::npos)
        << "the frame sent to the device must carry the operator's exact bytes";
}

// Sanitisation must not mangle legitimate credentials: spaces and punctuation
// are valid WiFi password characters and must still echo verbatim.
TEST(ProvisioningRunnerTest, LegitimatePasswordPunctuationEchoesVerbatim) {
    FakeSerialPort port;
    port.enqueueReply({"OK WiFi credentials stored. Rebooting to connect...\r"});
    ASSERT_TRUE(port.open());

    std::ostringstream out;
    const int rc = runProvisioningCommand(
        port, "ATSETWIFIMy Net,p@ss w0rd!#$", PROVISION_OK_STORED,
        PROVISION_TIMEOUT_S, out);

    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("ATSETWIFIMy Net,p@ss w0rd!#$"), std::string::npos)
        << "printable credential characters must survive sanitisation";
}

// The port must be opened exactly ONCE per provisioning run. The production
// overload used to open() and then delegate to this overload, which open()ed
// again — PosixSerialPort::open() overwrites fd_ without closing, leaking an fd.
TEST(ProvisioningRunnerTest, DispatchOpensAndClosesPortExactlyOnce) {
    FakeSerialPort port;
    port.enqueueReply({"REBOOT\r"});

    WifiProvisioningOptions opts = provisioningOpts(false, true, "", "");
    std::ostringstream out, err;

    ASSERT_EQ(runProvisioning(opts, port, out, err), 0);

    EXPECT_EQ(port.openCallCount(), 1) << "a double open() leaks a descriptor";
    EXPECT_EQ(port.closeCallCount(), 1);
}

// When the port cannot be opened, the failure names the offending port so the
// operator can see WHICH device path failed.
TEST(ProvisioningRunnerTest, OpenFailureNamesThePort) {
    FakeSerialPort port;
    port.setOpenFails(true);

    WifiProvisioningOptions opts = provisioningOpts(false, true, "", "");
    std::ostringstream out, err;

    EXPECT_EQ(runProvisioning(opts, port, out, err), 1);
    EXPECT_NE(err.str().find("/dev/cu.usbserial-TEST"), std::string::npos)
        << "the open-failure diagnostic must name the port that failed";
    EXPECT_TRUE(port.writtenBlob().empty()) << "nothing is sent if open failed";
}

TEST(ProvisioningRunnerTest, SetWifiFailurePropagatesReturnCode) {
    FakeSerialPort port;
    port.enqueueReply({"no match here\r"});

    WifiProvisioningOptions opts = provisioningOpts(false, false, "ssid", "pass");
    std::ostringstream out, err;

    const int rc = runProvisioning(opts, port, out, err);

    EXPECT_EQ(rc, 1);
    EXPECT_EQ(port.writtenBlob(), "ATSETWIFIssid,pass\r");
}

// --- runStatus(): [STATE] line capture + edge cases ------------------------

// A complete "[STATE] ... \r\n" line is captured and printed verbatim to `out`.
// runStatus does NOT send an AT command (device-driven heartbeat), so the
// FakeSerialPort's written_ vector MUST stay empty.
TEST(ProvisioningRunnerTest, RunStatusCapturesStateLine) {
    FakeSerialPort port;
    port.enqueueReply({"[STATE] uptime=1234 wifi=STA ssid=HomeNet ip=10.0.0.5\r\n"});

    std::ostringstream out, err;
    const int rc = runStatus(port, 1, out, err);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.str(), "[STATE] uptime=1234 wifi=STA ssid=HomeNet ip=10.0.0.5\n");
    EXPECT_TRUE(err.str().empty())
        << "happy path must not produce a TIMEOUT diagnostic";
    EXPECT_TRUE(port.writtenBlob().empty())
        << "runStatus is read-only; no bytes must hit the wire";
}

// The matcher requires BOTH the [STATE] marker AND a terminator before
// returning Matched. A chunk that carries the marker but no newline (yet)
// must NOT short-circuit the loop — the next poll step completes the line.
// We script two chunks: first the marker-without-terminator, then the
// rest of the line. The captured line must be the whole, complete, joined
// string (not just the first chunk).
TEST(ProvisioningRunnerTest, RunStatusKeepsReadingUntilLineComplete) {
    FakeSerialPort port;
    port.enqueueReply({
        "[STATE] uptime=99",                                // marker, no terminator yet
        " wifi=AP ssid=Setup ip=0.0.0.0\r\n"               // rest of the line
    });

    std::ostringstream out, err;
    const int rc = runStatus(port, 1, out, err);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.str(),
              "[STATE] uptime=99 wifi=AP ssid=Setup ip=0.0.0.0\n")
        << "a partial [STATE] chunk must keep reading until the terminator lands";
}

// When the device never emits a [STATE] line and never closes, the deadline
// elapses and we surface the generic TIMEOUT diagnostic. The bare message
// (without the "(peer closed)" suffix) is what operators see when the device
// is silent-but-alive.
TEST(ProvisioningRunnerTest, RunStatusDeadlineElapsedReportsTimeout) {
    FakeSerialPort port;
    // Queue a NON-[STATE] reply (heartbeat without the state marker) and
    // then go silent — the deadline elapses with the matcher still asking
    // for more. Without setSilentAfterQueue(true), the fake's post-queue
    // EOF would be indistinguishable from a real peer-closed mid-wait, so
    // we need the explicit "silent device" mode to test this branch.
    port.enqueueReply({"some unrelated log line\r\n"});
    port.setSilentAfterQueue(true);

    std::ostringstream out, err;
    const int rc = runStatus(port, 1, out, err);

    EXPECT_EQ(rc, 1);
    EXPECT_TRUE(out.str().empty())
        << "no [STATE] line was captured; nothing must reach the success sink";
    EXPECT_NE(err.str().find("TIMEOUT"), std::string::npos);
    EXPECT_NE(err.str().find("no [STATE] line received"), std::string::npos);
    // Deadline-elapsed path: NO peer-closed suffix.
    EXPECT_EQ(err.str().find("peer closed"), std::string::npos);
}

// When the device closes the port before any [STATE] line arrives (most
// commonly a mid-reboot), pollOnce reports Stop with a PeerClosed reason.
// runStatus must surface the distinct "(peer closed)" diagnostic so the
// operator sees the difference between "device is silent" and "device is
// gone".
TEST(ProvisioningRunnerTest, RunStatusPeerClosedReportsDistinctDiagnostic) {
    FakeSerialPort port;
    // The fake's read() returns 0 (EOF) when the queue is empty, which is
    // exactly what a real /dev/cu.* does when the device disconnects.
    // No chunks queued → immediate EOF on the first selectReadable.
    std::ostringstream out, err;
    const int rc = runStatus(port, 1, out, err);

    EXPECT_EQ(rc, 1);
    EXPECT_TRUE(out.str().empty());
    EXPECT_NE(err.str().find("peer closed"), std::string::npos)
        << "peer-closed-during-wait must surface a distinct diagnostic "
           "from the deadline-elapsed case";
}

// Trailing noise AFTER the [STATE] line must not leak into the captured
// output — only the marker-to-terminator substring is reported.
TEST(ProvisioningRunnerTest, RunStatusStopsAtFirstTerminator) {
    FakeSerialPort port;
    port.enqueueReply({
        "[STATE] uptime=1\r\n",                            // the heartbeat we want
        "next heartbeat would be ignored...\r\n"          // but the loop already stopped
    });

    std::ostringstream out, err;
    const int rc = runStatus(port, 1, out, err);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(out.str(), "[STATE] uptime=1\n")
        << "the captured line must stop at the first terminator; "
           "subsequent heartbeat noise must not leak in";
}

// --- resolveSerialPort() ---------------------------------------------------

// "auto" and empty are equivalent: the resolver should run auto-detect and
// fall back to the build-time default when no glob matches. We don't have
// a real /dev/cu.usbserial* on the build host, so the auto-detect leg
// returns "" and the resolver returns the build-time default.
TEST(ProvisioningRunnerTest, ResolveSerialPortAutoFallsBackToDefault) {
    EXPECT_EQ(resolveSerialPort("auto"), ESP32_DEFAULT_USB_PORT);
    EXPECT_EQ(resolveSerialPort(""), ESP32_DEFAULT_USB_PORT);
}

// "usb:<path>" must be the literal value the user supplied, with the
// "usb:" prefix stripped. Any /dev/cu.* path survives verbatim.
TEST(ProvisioningRunnerTest, ResolveSerialPortUsbPrefixIsStripped) {
    EXPECT_EQ(resolveSerialPort("usb:/dev/cu.usbserial-X1"),
              "/dev/cu.usbserial-X1");
    EXPECT_EQ(resolveSerialPort("usb:/dev/cu.wchusbserial1420"),
              "/dev/cu.wchusbserial1420");
}

// Defensive: validation has already rejected anything that isn't empty /
// "auto" / "usb:...", but the resolver must still return "" (NOT crash,
// NOT echo the unknown form back) so the caller's "could not resolve"
// error fires. The tcp/ble/file/demo prefixes that the universal --connect
// accepts for telemetry must be rejected here.
TEST(ProvisioningRunnerTest, ResolveSerialPortRejectsUnknownForms) {
    EXPECT_EQ(resolveSerialPort("tcp:192.168.4.1:3333"), "");
    EXPECT_EQ(resolveSerialPort("ble"), "");
    EXPECT_EQ(resolveSerialPort("file:/tmp/cap.csv"), "");
    EXPECT_EQ(resolveSerialPort("demo"), "");
    EXPECT_EQ(resolveSerialPort("garbage"), "");
    EXPECT_EQ(resolveSerialPort("USB:uppercase-not-matched"), "")
        << "the prefix match is case-sensitive: 'USB:' is not 'usb:'";
}
