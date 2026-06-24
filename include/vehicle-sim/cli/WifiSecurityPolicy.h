// WifiSecurityPolicy.h — Trust policy for ESP32 WiFi connections.
//
// Policy: discovery is IDENTITY-BEARING (each packet carries a deviceId) but
// UNTRUSTED until the Ed25519 signature verifies. The CLI MUST NOT open a CAN
// TCP stream to an unverified device.
//
// Modes:
//   default-deny  (default)  — connect ONLY to devices whose discovery packet
//                              signature verified against the loaded OTA public
//                              key. Requires --discover or --connect auto to
//                              have run first so the device is known.
//   allow-all     (--insecure) — accept any TCP target, even without prior
//                                discovery. Still prefers verified devices.
//
// Usage:
//   WifiSecurityPolicy policy;
//   policy.setMode(WifiSecurityPolicy::Mode::DefaultDeny);
//   policy.addVerifiedDevice(discoveredDeviceId);
//   if (!policy.isAllowed(targetDeviceId)) { refuse; }

#ifndef VEHICLE_SIM_CLI_WIFI_SECURITY_POLICY_H
#define VEHICLE_SIM_CLI_WIFI_SECURITY_POLICY_H

#include "vehicle-sim/discovery/DiscoveryPacket.h"

#include <array>
#include <string>
#include <vector>

namespace vehicle_sim {
namespace cli {

class WifiSecurityPolicy {
public:
    enum class Mode {
        DefaultDeny,   // only verified devices allowed (default)
        AllowAll       // --insecure: skip verification (development only)
    };

    WifiSecurityPolicy() = default;

    //! Set the policy mode.
    void setMode(Mode m) { mode_ = m; }

    //! Get the current mode.
    Mode mode() const { return mode_; }

    //! Register a verified device (discovered with valid signature).
    void addVerifiedDevice(const std::array<uint8_t, DEVICE_ID_LEN>& deviceId) {
        verifiedDevices_.push_back(deviceId);
    }

    //! Clear the verified-device list (e.g. before a re-discover).
    void clearVerifiedDevices() { verifiedDevices_.clear(); }

    //! Check whether a device is allowed.
    //! In AllowAll mode, always returns true.
    //! In DefaultDeny mode, returns true only if the device is in the
    //! verified set.
    bool isAllowed(const std::array<uint8_t, DEVICE_ID_LEN>& deviceId) const {
        if (mode_ == Mode::AllowAll) return true;
        return isVerified(deviceId);
    }

    //! Check if a device is in the verified set.
    bool isVerified(const std::array<uint8_t, DEVICE_ID_LEN>& deviceId) const {
        for (auto& v : verifiedDevices_) {
            if (v == deviceId) return true;
        }
        return false;
    }

    //! Number of verified devices.
    size_t verifiedCount() const { return verifiedDevices_.size(); }

private:
    Mode mode_ = Mode::DefaultDeny;
    std::vector<std::array<uint8_t, DEVICE_ID_LEN>> verifiedDevices_;
};

} // namespace cli
} // namespace vehicle_sim

#endif // VEHICLE_SIM_CLI_WIFI_SECURITY_POLICY_H
