#include <gtest/gtest.h>
#include <iostream>
#include <streambuf>

// Null buffer to silence std::cout and std::cerr during tests
class NullBuffer : public std::streambuf {
protected:
    int_type overflow(int_type c) override { return c; }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Redirect both std::cout and std::cerr to null buffer to suppress
    // production code debug output during tests:
    //   - std::cout: VehicleSim.cpp #ifndef VEHICLE_SIM_TEST_SILENTLY blocks
    //   - std::cerr: transport code via ITransportOutput::err() -> StdOut -> std::cerr
    NullBuffer null_buf;
    auto* old_cout = std::cout.rdbuf(&null_buf);
    auto* old_cerr = std::cerr.rdbuf(&null_buf);

    int result = RUN_ALL_TESTS();

    // Restore
    std::cout.rdbuf(old_cout);
    std::cerr.rdbuf(old_cerr);
    return result;
}
