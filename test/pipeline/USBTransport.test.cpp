#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/USBTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <fcntl.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/select.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace vehicle_sim::pipeline;

static std::shared_ptr<StopToken> g_testStop = std::make_shared<StopToken>();

namespace {

class PtyPair {
public:
    PtyPair() {
        if (::openpty(&masterFd_, &slaveFd_, slaveName_, nullptr, nullptr) != 0) {
            slaveFd_ = -1;
            masterFd_ = -1;
        }
    }

    ~PtyPair() {
        if (slaveFd_ >= 0) close(slaveFd_);
        if (masterFd_ >= 0) close(masterFd_);
    }

    bool valid() const { return masterFd_ >= 0 && slaveFd_ >= 0 && slaveName_[0] != '\0'; }
    const char* masterPath() const { return slaveName_; }
    int masterFd() const { return masterFd_; }
    int slaveFd() const { return slaveFd_; }

private:
    int masterFd_ = -1;
    int slaveFd_ = -1;
    char slaveName_[128]{};
};

void writeAll(int fd, const std::string& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        ASSERT_GT(n, 0);
        written += static_cast<std::size_t>(n);
    }
}

} // namespace

TEST(USBTransportTest, ReadsNewlineDelimitedLinesFromPTY) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    g_testStop->reset();
    USBTransport transport(pty.masterPath(), 115200, std::make_shared<StdOut>(), g_testStop);
    ASSERT_TRUE(transport.open());

    writeAll(pty.masterFd(), "1D5 29 00 00 00 00 00 A0 9F\r");

    auto line = transport.nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "1D5 29 00 00 00 00 00 A0 9F");

    g_testStop->reset();
}

TEST(USBTransportTest, RequestStop_TerminatesQuietPTY) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    g_testStop->reset();
    USBTransport transport(pty.masterPath(), 115200, std::make_shared<StdOut>(), g_testStop);
    ASSERT_TRUE(transport.open());

    g_testStop->requestStop();
    const auto start = std::chrono::steady_clock::now();
    auto line = transport.nextLine();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_FALSE(line.has_value());
    EXPECT_LT(elapsed.count(), 1500) << "stop should be prompt";
    EXPECT_FALSE(transport.isOpen());

    g_testStop->reset();
}

TEST(USBTransportTest, OpenFailsOnBogusPath) {
    g_testStop->reset();
    USBTransport transport("/dev/does-not-exist-for-vehicle-sim", 115200, std::make_shared<StdOut>(), g_testStop);

    EXPECT_FALSE(transport.open());
    EXPECT_FALSE(transport.isOpen());
}
