#include "vehicle-sim/ble/BLEManagerBase.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <string_view>

namespace vehicle_sim {

// ================================================
// BLEManagerBase Implementation
//
// The ELM327/OBD2 protocol, polling, CAN-monitor, VIN and auto-detection
// behaviour lives in the composed Elm327Session (roles 4+5). This file holds
// the BLE transport core: the constructor wiring the collaborators together,
// the thin queryPID forward to the session, the RSSI signal-quality helper,
// and the ASCII send. Device storage, connection state, callbacks and
// raw-activity bookkeeping are owned by the private collaborators
// (DeviceRegistry, ConnectionState, CallbackHub, RawActivity) and reached
// through the public reference-returning accessors — the base no longer
// forwards to them (cpp:S1448).
// ================================================

BLEManagerBase::BLEManagerBase(util::IClock* clock)
    : session_{*this, clock} {
    // Collaborators are member-initialised (callbacks_, device_registry_,
    // connection_state_, raw_activity_). The session intentionally has its
    // clock set here so test/FakeClock injection and default SystemClock both
    // work; the RawActivity routes raw bytes into this session.
}

// ================================================
// BLE Transport Helpers
// ================================================

void BLEManagerBase::sendASCII(std::string_view command) {
    std::vector<uint8_t> bytes(command.begin(), command.end());
    send(bytes);
}

} // namespace vehicle_sim
