#include "vehicle-sim/cli/CsvReplayPath.h"

#include <fstream>

namespace vehicle_sim::cli {

bool isDecodedTelemetryCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string header;
    if (!std::getline(in, header)) return false;
    return header.find("timestamp_ms") != std::string::npos;
}

} // namespace vehicle_sim::cli
