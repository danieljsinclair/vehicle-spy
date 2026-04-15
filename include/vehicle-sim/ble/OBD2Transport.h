#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <functional>
#include <mutex>
#include <deque>
#include "vehicle-sim/ble/ITransport.h"
#include "vehicle-sim/ble/BLEPlatform.h"

namespace vehicle_sim::ble {

/**
 * Generic ELM327 BLE transport
 *
 * ITransport implementation for standard ELM327-compatible OBD2 adapters.
 * Unlike TeslaBLETransport which uses a custom packet protocol with
 * header bytes and checksums, OBD2Transport simply passes data through
 * after stripping ELM327 line terminators (CR, prompt '>').
 *
 * This is the transport for your Toyota Aygo, VW Golf, Ford Focus, etc.
 * — anything with a standard OBD2 port and a cheap ELM327 BLE adapter.
 */
class OBD2Transport : public ITransport {
public:
    explicit OBD2Transport(std::unique_ptr<BLEPlatform> platform);
    ~OBD2Transport() override;

    // ITransport interface
    bool connect(const std::string& address) override;
    void disconnect() override;
    bool isConnected() const override;
    std::optional<std::vector<std::uint8_t>> readData() override;
    void send(const std::vector<std::uint8_t>& data) override;
    void subscribeToNotifications(
        std::function<void(const std::vector<std::uint8_t>&)> callback
    ) override;
    void unsubscribe() override;

private:
    std::unique_ptr<BLEPlatform> platform_;

    mutable std::mutex state_mutex_;
    bool connected_;

    mutable std::mutex buffer_mutex_;
    std::deque<std::uint8_t> receive_buffer_;

    mutable std::mutex callback_mutex_;
    std::function<void(const std::vector<std::uint8_t>&)> notification_callback_;

    void processIncomingData(const std::vector<std::uint8_t>& data);
    void extractCleanResponses();
    void forwardResponse(const std::vector<std::uint8_t>& response);
};

} // namespace vehicle_sim::ble
