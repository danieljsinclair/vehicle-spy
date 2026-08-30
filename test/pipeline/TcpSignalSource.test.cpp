// TcpSignalSource — the vanilla pipeline class the iOS wrapper (VehicleSimWrapper.mm)
// drives for its TCP path. It used to live INSIDE the .mm, untestable from
// ctest; these tests are the reason it moved. The transport is a scripted
// in-memory ITransport (no network, no mocks of production code beyond the
// transport seam), and the decode runs through the real DBCTranslationService
// with the real Tesla DBC — the same decode the CLI live path performs.

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/TcpSignalSource.h"
#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/domain/DBCTranslationService.h"
#include "vehicle-sim/domain/DefaultVehicleConfigs.h"
#include "vehicle-sim/domain/VehicleConfigResolver.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::domain;

namespace {

// Minimal in-memory transport that yields each scripted line once. Mirrors
// the LiveTwaiSource test's ScriptedTransport.
class ScriptedTransport final : public ITransport {
public:
    explicit ScriptedTransport(std::vector<std::string> lines)
        : lines_(std::move(lines)) {}

    bool open() override { return openResult_; }
    [[nodiscard]] bool isOpen() const noexcept override {
        return idx_ < lines_.size();
    }
    std::optional<std::string> nextLine() override {
        if (idx_ >= lines_.size()) return std::nullopt;
        return lines_[idx_++];
    }

private:
    std::vector<std::string> lines_;
    std::size_t idx_ = 0;
    bool openResult_ = true;
};

// A transport whose open() always fails.
class UnopenableTransport final : public ITransport {
public:
    bool open() override { return false; }
    [[nodiscard]] bool isOpen() const noexcept override { return false; }
    std::optional<std::string> nextLine() override { return std::nullopt; }
};

class TcpSignalSourceTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_ = std::make_unique<DBCTranslationService>();
        DefaultVehicleConfigs::registerAll(service_->registry());
        // Same load the wrapper performs for a CAN vehicle (minus the iOS
        // bundle lookup, which loadVehicleFromPath replaces on-device).
        VehicleConfigResolver resolver(*service_);
        (void)resolver.resolve("tesla");
    }

    std::unique_ptr<DBCTranslationService> service_;

    // Poll latestSignal() until `pred` holds or the deadline passes. The
    // pipeline runs on its own thread, so the decode lands asynchronously.
    template <typename Pred>
    bool waitFor(Pred pred, int timeoutMs = 2000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (pred()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return pred();
    }
};

} // namespace

// The defining contract: a live CAN line streamed by the transport is decoded
// through the real translation service and becomes the latest signal. The
// Frame is DI_systemStatus (CAN 0x118/280) with DI_accelPedalPos raw 0xBC
// at data byte 4, scale 0.4 -> 75.2% (the same known-good frame the DBC
// integration test uses).
TEST_F(TcpSignalSourceTest, StreamsDecodedSignalFromTransport) {
    auto stop = std::make_shared<StopToken>();
    TCPSignalSource source(
        std::make_unique<ScriptedTransport>(
            std::vector<std::string>{"118 00 00 00 00 BC 00 00 00"}),
        *service_, stop);

    EXPECT_FALSE(source.isRunning()) << "not started yet";
    source.start();

    EXPECT_TRUE(waitFor([&] {
        return source.latestSignal().getThrottlePercent().has_value();
    })) << "decoded frame never arrived";
    EXPECT_NEAR(*source.latestSignal().getThrottlePercent(), 75.2, 0.1);

    source.stop();
    EXPECT_FALSE(source.isRunning());
}

// Default latestSignal() (nothing decoded yet) must be a well-formed null
// signal, not a crash — the wrapper polls it every UI tick from connect on.
TEST_F(TcpSignalSourceTest, LatestSignal_BeforeAnyFrame_IsNullSignal) {
    auto stop = std::make_shared<StopToken>();
    TCPSignalSource source(
        std::make_unique<ScriptedTransport>(std::vector<std::string>{}),
        *service_, stop);

    const auto signal = source.latestSignal();
    EXPECT_FALSE(signal.getThrottlePercent().has_value());
    EXPECT_FALSE(signal.getSpeedKmh().has_value());
}

// start() with a transport that cannot open leaves the source not running
// (the wrapper treats this as a failed connect).
TEST_F(TcpSignalSourceTest, Start_WhenTransportOpenFails_IsNotRunning) {
    auto stop = std::make_shared<StopToken>();
    TCPSignalSource source(
        std::make_unique<UnopenableTransport>(), *service_, stop);

    source.start();
    EXPECT_FALSE(source.isRunning());

    // stop() on a never-running source must be a safe no-op.
    source.stop();
    EXPECT_FALSE(source.isRunning());
}

// When the transport exhausts (peer close / network drop), the worker thread
// ends and isRunning() flips false — the silent-drop signal the wrapper's
// isConnectionAlive surfaces to the ViewModel.
TEST_F(TcpSignalSourceTest, TransportExhaustion_MarksSourceNotRunning) {
    auto stop = std::make_shared<StopToken>();
    TCPSignalSource source(
        std::make_unique<ScriptedTransport>(
            std::vector<std::string>{"118 00 00 00 00 BC 00 00 00"}),
        *service_, stop);

    source.start();
    EXPECT_TRUE(waitFor([&] { return !source.isRunning(); }))
        << "exhausted transport must end the pipeline thread";

    source.stop();
}

// start() is idempotent: a second start while running must not spawn a second
// pipeline thread (running_ exchange guard).
TEST_F(TcpSignalSourceTest, Start_Twice_DoesNotRestartPipeline) {
    auto stop = std::make_shared<StopToken>();
    TCPSignalSource source(
        std::make_unique<ScriptedTransport>(
            std::vector<std::string>{"118 00 00 00 00 BC 00 00 00"}),
        *service_, stop);

    source.start();
    source.start();  // no-op while running

    EXPECT_TRUE(waitFor([&] {
        return source.latestSignal().getThrottlePercent().has_value();
    }));
    source.stop();
    EXPECT_FALSE(source.isRunning());
}

// stop() from the polling side must join the worker promptly so dealloc is
// safe (the wrapper's dealloc calls stop).
TEST_F(TcpSignalSourceTest, Stop_JoinsWorker) {
    auto stop = std::make_shared<StopToken>();
    {
        TCPSignalSource source(
            std::make_unique<ScriptedTransport>(
                std::vector<std::string>{"118 00 00 00 00 BC 00 00 00"}),
            *service_, stop);
        source.start();
        EXPECT_TRUE(waitFor([&] {
            return source.latestSignal().getThrottlePercent().has_value();
        }));
        source.stop();
    }  // destructor also calls stop() — must not double-join or hang
    SUCCEED();
}
