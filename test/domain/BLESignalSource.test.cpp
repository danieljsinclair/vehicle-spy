#include <gtest/gtest.h>

#include "vehicle-sim/domain/BLESignalSource.h"
#include "vehicle-sim/BLEManager.h"
#include "vehicle-sim/ble/BLEManagerBase.h"

#include <vector>

using namespace vehicle_sim;
using namespace vehicle_sim::domain;

// A minimal fake BLE platform (no gmock) that records the data-received
// callback BLEManager registers, so we can push real bytes through the genuine
// BLEManager -> BLESignalSource chain with NO real BLE/network I/O. The SUT is
// the real BLESignalSource; the platform is the only seam we stub, and we stub
// it by recording the callback (not by mocking behaviour).
class FakeBLEManagerBase : public BLEManagerBase {
public:
    BLEManager::DataCallback capturedDataCallback;

    std::vector<BLEDeviceInfo> scanForDevices(int) override { return {}; }
    bool connect(std::string_view) override { return false; }
    void disconnect() override {}
    void send(const std::vector<uint8_t>&) override {}
    bool isConnected() const override { return false; }
    std::string getConnectedDeviceId() const override { return {}; }
    void setDeviceFoundCallback(DeviceCallback) override {}
    void setDataReceivedCallback(DataCallback callback) override {
        capturedDataCallback = std::move(callback);
    }
};

namespace {

// Builds a real BLEManager wired to a FakeBLEManagerBase and exposes the
// captured data-callback to deliver bytes through. Pushing bytes exercises the
// FULL real chain:
//   platform callback -> BLEManager::onDataReceived -> source lambda
//     -> BLESignalSource::onDataReceived (the parse contract under test).
struct WiredHarness {
    BLEManager manager;

    explicit WiredHarness() {
        auto fake = std::make_unique<FakeBLEManagerBase>();
        fake_ = fake.get();
        manager.setPlatform(std::move(fake));
    }

    // Push bytes through the real BLEManager -> BLESignalSource parse path.
    void deliver(const std::vector<std::uint8_t>& bytes) {
        ASSERT_TRUE(fake_->capturedDataCallback);
        fake_->capturedDataCallback(bytes);
    }

private:
    FakeBLEManagerBase* fake_{nullptr};
};

} // namespace

// ---------------------------------------------------------------------------
// Pins: happy-path CAN frame parsing. A BLE frame is [CAN-ID lo, CAN-ID hi,
// payload bytes...]; CAN-ID is little-endian. Confirms 0x1234 decodes from
// {0x34, 0x12} and the trailing payload is stored verbatim. This is the core
// parsing contract every BLE veneer relies on.
TEST(BLESignalSourceTest, ParsesLittleEndianCanIdAndPayload) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    // CAN-ID 0x1234 (lo=0x34, hi=0x12) + payload {0xAA,0xBB,0xCC,0xDD}.
    h.deliver({0x34, 0x12, 0xAA, 0xBB, 0xCC, 0xDD});

    const auto& frames = source.accumulatedFrames();
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames.count(0x1234u), 1u);
    EXPECT_EQ(frames.at(0x1234u), (std::vector<std::uint8_t>{0xAA, 0xBB, 0xCC, 0xDD}));
}

// ---------------------------------------------------------------------------
// Pins: little-endian byte order is honoured for both low and high bytes.
// {0xFF,0x00} -> 0x00FF (NOT 0xFF00); {0x00,0xFF} -> 0xFF00. Guards
// against a big-endian regression in the CAN-ID extraction contract.
TEST(BLESignalSourceTest, HonoursLittleEndianByteOrder) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    h.deliver({0xFF, 0x00, 0x01});
    h.deliver({0x00, 0xFF, 0x02});

    const auto& frames = source.accumulatedFrames();
    ASSERT_EQ(frames.size(), 2u);
    ASSERT_EQ(frames.count(0x00FFu), 1u);
    EXPECT_EQ(frames.at(0x00FFu), (std::vector<std::uint8_t>{0x01}));
    ASSERT_EQ(frames.count(0xFF00u), 1u);
    EXPECT_EQ(frames.at(0xFF00u), (std::vector<std::uint8_t>{0x02}));
}

// ---------------------------------------------------------------------------
// Pins: short-frame guard. A frame shorter than 2 bytes carries no CAN-ID, so
// the parse path must reject it (map remains empty). Confirms the length guard
// is real behaviour, not dead code.
TEST(BLESignalSourceTest, RejectsShortFrameWithNoCanId) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    h.deliver({0x34});        // 1 byte — below the 2-byte CAN-ID minimum
    h.deliver({});             // 0 bytes

    EXPECT_TRUE(source.accumulatedFrames().empty());
}

// ---------------------------------------------------------------------------
// Pins: accumulation across distinct CAN-IDs. Each unique little-endian CAN-ID
// becomes a separate keyed entry — the accumulation contract the DBC translator
// (future) consumes. Two different CAN-IDs both persist simultaneously.
TEST(BLESignalSourceTest, AccumulatesDistinctCanIds) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    h.deliver({0x01, 0x01, 0x10});  // CAN-ID 0x0101
    h.deliver({0x02, 0x02, 0x20});  // CAN-ID 0x0202

    const auto& frames = source.accumulatedFrames();
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames.at(0x0101u), (std::vector<std::uint8_t>{0x10}));
    EXPECT_EQ(frames.at(0x0202u), (std::vector<std::uint8_t>{0x20}));
}

// ---------------------------------------------------------------------------
// Pins: re-keyed map semantics — a repeat of the same CAN-ID overwrites its
// prior payload (keyed, not append). Confirms the contract does not silently
// grow/stale entries when a frame is re-transmitted.
TEST(BLESignalSourceTest, RepeatingCanIdOverwritesPriorPayload) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    h.deliver({0x01, 0x01, 0x10, 0x11});
    h.deliver({0x01, 0x01, 0x20, 0x21, 0x22});

    const auto& frames = source.accumulatedFrames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames.at(0x0101u), (std::vector<std::uint8_t>{0x20, 0x21, 0x22}));
}

// ---------------------------------------------------------------------------
// Pins: start() registers the parse callback and accumulates frames; stop()
// releases the callback (further delivers are ignored) AND clears accumulated
// state. The before/after contrast proves start() wired the callback and stop()
// both released it and reset accumulation — not a "doesn't crash" tautology.
TEST(BLESignalSourceTest, StartWiresCallbackStopReleasesAndClears) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    h.deliver({0x01, 0x01, 0x10});
    ASSERT_EQ(source.accumulatedFrames().size(), 1u);

    source.stop();
    EXPECT_EQ(source.accumulatedFrames().size(), 0u);  // stop() clears accumulation
    h.deliver({0x02, 0x02, 0x20});                   // must be ignored after stop()
    EXPECT_EQ(source.accumulatedFrames().size(), 0u);
    EXPECT_EQ(source.accumulatedFrames().count(0x0202u), 0u);
}

// ---------------------------------------------------------------------------
// Pins: latestSignal() returns the default-constructed snapshot unchanged by the
// parse path — the documented contract (translation is "added later"; onData-
// Received only populates accumulatedFrames_). Locks the current, intentional
// behaviour so a future change that wires translation is observable.
TEST(BLESignalSourceTest, LatestSignalIsDefaultBeforeTranslationWired) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();

    h.deliver({0x01, 0x01, 0x10});  // populate frames, but NOT latestSignal_
    const VehicleSignal s = source.latestSignal();
    EXPECT_EQ(s.getTimestampUtcMs(), 0u);
}

// ---------------------------------------------------------------------------
// Pins: double start() is idempotent (connected_ guard) — a second start()
// must not re-register / double-count. The SUT guards with `if (connected_)
// return;`. Confirms the guard contract: re-delivering after a second start
// produces exactly the same accumulation as a single start.
TEST(BLESignalSourceTest, DoubleStartIsIdempotent) {
    WiredHarness h;
    BLESignalSource source(&h.manager);
    source.start();
    source.start();  // guarded no-op

    h.deliver({0x01, 0x01, 0x10});
    EXPECT_EQ(source.accumulatedFrames().size(), 1u);
}
