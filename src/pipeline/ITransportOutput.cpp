#include "vehicle-sim/pipeline/ITransportOutput.h"
#include <iostream>

namespace vehicle_sim::pipeline {

void StdOut::out(const std::string& msg) {
    std::cout << msg << std::endl;
}

void StdOut::err(const std::string& msg) {
    std::cerr << msg << std::endl;
}

}  // namespace vehicle_sim::pipeline
