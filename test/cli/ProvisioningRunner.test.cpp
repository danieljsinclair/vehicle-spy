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
    // are consumed, selectReadable returns 1 + a zero-byte read models EOF.
    void enqueueReply(std::deque<std::string> chunks) {
        reply_ = std::move(chunks);
    }

    // Make open() fail, to drive the open-failure diagnostic path.
    void setOpenFails(bool fails) { openFails_ = fails; }

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
        // Readable as long as there are scripted bytes, then readable-once on
        // the final EOF so runProvisioningCommand's read() sees 0 and stops.
        if (!reply_.empty() || !eofSignaled_) return 1;
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
