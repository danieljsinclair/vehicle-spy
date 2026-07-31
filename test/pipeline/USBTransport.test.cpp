#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/USBTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <fcntl.h>
#include <cerrno>
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
    // Release ownership of the master fd so a test can close it itself to
    // simulate peer-EOF, without ~PtyPair double-closing (which would risk
    // closing a recycled fd).
    int releaseMaster() {
        int fd = masterFd_;
        masterFd_ = -1;
        return fd;
    }

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

// ============================================================
// USBTransport — lifecycle + read-path edge cases (PTY-backed)
//
// Real production code driven against a pseudo-terminal pair. The PTY gives a
// real fd with real termios/select/read semantics, so these exercise the same
// arms the live /dev/cu.* path hits (EOF, exhaustion, pending-buffer trim,
// not-opened guard, double-open short-circuit) without needing hardware.
// ============================================================

// Contract: nextLine() before open() returns nullopt and does not block — the
// !opened_ guard short-circuits immediately.
TEST(USBTransportTest, NextLine_BeforeOpen_ReturnsNulloptImmediately) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    g_testStop->reset();
    USBTransport transport(pty.masterPath(), 115200, std::make_shared<StdOut>(), g_testStop);

    const auto start = std::chrono::steady_clock::now();
    auto line = transport.nextLine();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_FALSE(line.has_value());
    EXPECT_LT(elapsed.count(), 100) << "not-opened guard must not enter the select loop";
    EXPECT_FALSE(transport.isOpen());
}

// Contract: calling open() twice is idempotent — the second call short-circuits
// on the opened_ flag and returns the current open state without re-opening.
TEST(USBTransportTest, Open_Twice_SecondCallShortCircuitsOnOpenedFlag) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    g_testStop->reset();
    USBTransport transport(pty.masterPath(), 115200, std::make_shared<StdOut>(), g_testStop);

    ASSERT_TRUE(transport.open());
    ASSERT_TRUE(transport.isOpen());

    // Second open() must not re-open the device; it returns the live state.
    EXPECT_TRUE(transport.open());
    EXPECT_TRUE(transport.isOpen());

    // And the transport still functions: write + read a line after the double-open.
    writeAll(pty.masterFd(), "abc 01 02\r");
    auto line = transport.nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "abc 01 02");
}

// Contract: when the peer closes its end (read returns 0 == EOF), nextLine()
// returns nullopt and the transport transitions to exhausted (isOpen() false).
// Subsequent nextLine() calls stay nullopt without blocking.
TEST(USBTransportTest, NextLine_OnPeerEof_ReturnsNulloptAndExhausts) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    g_testStop->reset();
    USBTransport transport(pty.masterPath(), 115200, std::make_shared<StdOut>(), g_testStop);
    ASSERT_TRUE(transport.open());

    // Close the master side → the slave read() yields 0 (EOF) once buffered
    // data is drained. With nothing buffered, nextLine() hits EOF promptly.
    ::close(pty.releaseMaster());

    auto line = transport.nextLine();
    EXPECT_FALSE(line.has_value());
    EXPECT_FALSE(transport.isOpen()) << "EOF must mark the transport exhausted";

    // A subsequent call must also return nullopt without blocking (exhausted_ guard).
    line = transport.nextLine();
    EXPECT_FALSE(line.has_value());
}

// Contract: pending_ is bounded — bytes received without a newline beyond
// MAX_PENDING_LEN are trimmed from the front so the buffer cannot grow
// without limit. The eventual line still surfaces, proving the trim path
// (erase the overflowed front) does not lose the post-burst terminated frame
// or crash.
TEST(USBTransportTest, NextLine_TrimsPendingBufferBeyondMaxLen) {
    PtyPair pty;
    ASSERT_TRUE(pty.valid());

    g_testStop->reset();
    USBTransport transport(pty.masterPath(), 115200, std::make_shared<StdOut>(), g_testStop);
    ASSERT_TRUE(transport.open());

    // Feed a burst well beyond MAX_PENDING_LEN (4096) with NO newline, then a
    // newline-terminated line. The burst is fed from a background thread because
    // the PTY's internal buffer is ~1KB: a foreground writeAll(5000) would block
    // until the slave side is drained, which only happens inside nextLine().
    // The writer thread fills while the reader (nextLine) drains, so neither
    // deadlocks.
    const std::string burst(5000, 'X');
    int masterFd = pty.masterFd();
    std::thread writer([masterFd, &burst] {
        std::size_t written = 0;
        while (written < burst.size()) {
            const ssize_t n = ::write(masterFd, burst.data() + written,
                                      burst.size() - written);
            if (n <= 0) {
                if (errno == EINTR) continue;
                break;  // peer closed or error — stop feeding
            }
            written += static_cast<std::size_t>(n);
        }
        // Then the terminated line.
        const std::string end = "END 01 02 03\n";
        written = 0;
        while (written < end.size()) {
            const ssize_t n = ::write(masterFd, end.data() + written,
                                      end.size() - written);
            if (n <= 0) {
                if (errno == EINTR) continue;
                break;
            }
            written += static_cast<std::size_t>(n);
        }
    });

    auto line = transport.nextLine();
    writer.join();

    ASSERT_TRUE(line.has_value()) << "must surface the terminated line after trim";
    // The burst had no newline, so the whole burst accumulates into pending_.
    // The trim keeps the TAIL (up to MAX_PENDING_LEN=4096) — it bounds the
    // buffer, it does NOT discard all un-terminated data. So the returned
    // line is: <retained tail of X's><terminated frame>. Pin that contract:
    // the line is bounded (no bigger than the cap + frame), ends with the
    // terminated frame, and carries the retained X's.
    constexpr std::size_t kMaxPending = 4096;
    const std::string frame = "END 01 02 03";
    EXPECT_LE(line->size(), kMaxPending + frame.size())
        << "pending must be trimmed to at most MAX_PENDING_LEN before the frame";
    EXPECT_TRUE(line->size() >= frame.size() &&
                line->compare(line->size() - frame.size(), frame.size(), frame) == 0)
        << "line must end with the terminated frame";
    EXPECT_NE(line->find('X'), std::string::npos)
        << "retained tail of the burst should still be present (trim keeps the tail)";

    g_testStop->reset();
}

