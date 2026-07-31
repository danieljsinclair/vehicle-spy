// test_main.cpp - GoogleTest entry point for ESP32 firmware host tests

#include <gtest/gtest.h>
#include <iostream>

// Reset all mocks before each test
extern void resetAllMocks();

class MockResetFixture : public ::testing::Test {
protected:
    void SetUp() override {
        resetAllMocks();
    }

    void TearDown() override {
        resetAllMocks();
    }
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}