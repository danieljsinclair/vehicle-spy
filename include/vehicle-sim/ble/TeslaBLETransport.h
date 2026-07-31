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
 * Tesla Model Y BLE transport implementation
 *
 * Concrete ITransport implementation for Tesla Model Y OBD2 BLE scanner.
 * Handles Tesla-specific packet protocol with header validation and checksums.
 *
 * Thread-safe: All public methods are protected by mutex.
 * Tesla implementation detail - completely hidden behind ITransport interface.
 */
class TeslaBLETransport : public ITransport {
public:
    /**
     * Constructor - inject platform-specific BLE implementation
     * @param platform BLEPlatform implementation (mock, iOS CoreBluetooth, etc.)
     */
    explicit TeslaBLETransport(std::unique_ptr<BLEPlatform> platform);

    ~TeslaBLETransport() override;

    // ITransport interface implementation
    bool connect(const std::string& address) override;
    void disconnect() override;
    bool isConnected() const override;
    std::optional<std::vector<std::uint8_t>> readData() override;
    void send(const std::vector<std::uint8_t>& data) override;
    void subscribeToNotifications(std::function<void(const std::vector<std::uint8_t>&)> callback) override;
    void unsubscribe() override;

private:
    // Tesla packet protocol constants
    static constexpr std::uint8_t HEADER_BYTE_1 = 0xAA;
    static constexpr std::uint8_t HEADER_BYTE_2 = 0x55;

    // Platform-specific BLE implementation
    std::unique_ptr<BLEPlatform> platform_;

    // Connection state
    mutable std::mutex state_mutex_;
    bool connected_{false};

    // Packet buffer for reassembling fragmented packets
    mutable std::mutex buffer_mutex_;
    std::deque<std::uint8_t> receive_buffer_;

    // Notification callback
    mutable std::mutex callback_mutex_;
    std::function<void(const std::vector<std::uint8_t>&)> notification_callback_{nullptr};

    // Private methods for packet processing
    void processIncomingData(const std::vector<std::uint8_t>& data);
    bool validatePacket(const std::vector<std::uint8_t>& packet) const;
    std::uint8_t calculateChecksum(const std::vector<std::uint8_t>& data) const;
    void extractCompletePackets();
    void forwardPacket(const std::vector<std::uint8_t>& packet);
};

} // namespace vehicle_sim::ble
