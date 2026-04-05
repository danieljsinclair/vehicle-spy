#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace vehicle_sim {

struct BLEDeviceInfo {
    std::string address;
    std::string name;
    bool isConnected;
};

} // namespace vehicle_sim
