#pragma once

#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <memory>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

/**
 * POSIX serial transport for USB CAN bridges. It reads raw bytes from a macOS
 * /dev/cu.* serial device, splits them into newline-delimited lines, and passes
 * those lines to the normaliser. It knows nothing about DBC, frames, or decode.
 */
class USBTransport final : public ITransport {
public:
    explicit USBTransport(std::string_view port, int baud = 115200,
                 std::shared_ptr<ITransportOutput> output = std::make_shared<StdOut>(),
                 std::shared_ptr<StopToken> stop = std::make_shared<StopToken>());
    ~USBTransport() override;

    USBTransport(const USBTransport&) = delete;
    USBTransport& operator=(const USBTransport&) = delete;
    USBTransport(USBTransport&&) = delete;
    USBTransport& operator=(USBTransport&&) = delete;

    bool open() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    std::optional<std::string> nextLine() override;

    /**
     * Request that nextLine() return nullopt at the next select() timeout.
     * The shared StopToken (injected at construction, owned by the live
     * run-context) is the cooperative stop signal; the signal handler flips it
     * via SignalStopBroker. requestStop()/reset() are async-signal-safe atomic
     * ops on the token.
     */
    void requestStop() noexcept { stop_->requestStop(); }
    /** Reset the stop token (for tests / repeated runs). */
    void resetStop() noexcept { stop_->reset(); }

private:
    std::string port_;
    int baud_;
    std::shared_ptr<ITransportOutput> output_;
    std::shared_ptr<StopToken> stop_;
    int fd_ = -1;
    bool opened_ = false;
    bool exhausted_ = false;
    std::string pending_;
};

} // namespace vehicle_sim::pipeline
