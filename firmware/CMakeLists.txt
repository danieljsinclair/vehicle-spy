cmake_minimum_required(VERSION 3.20)
project(esp32-firmware-host-tests VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform-specific settings
if(APPLE)
    # macOS specific frameworks
elseif(LINUX)
    # Linux specific libraries if needed
endif()

# GoogleTest via FetchContent
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
FetchContent_MakeAvailable(googletest)

enable_testing()

find_package(Threads REQUIRED)

# Mock Arduino/ESP32 APIs for host testing
# This provides stubs for WiFi, Serial, Preferences, etc.
# Platform-agnostic mocks that work on Linux/macOS
set(ARDUINO_MOCK_SOURCES
    mocks/ArduinoMock.cpp
    mocks/WiFiMock.cpp
    mocks/SerialMock.cpp
    mocks/PreferencesMock.cpp
    mocks/TWAIMock.cpp
    mocks/ESPMock.cpp
)

# Vanilla C++ classes extracted from the .ino
set(VANILLA_SOURCES
    vanilla/WiFiManager.cpp
    vanilla/DiscoveryManager.cpp
    vanilla/NtpTimeSync.cpp
    vanilla/CanBridge.cpp
    vanilla/AtCommandDispatcher.cpp
    vanilla/OtaUpdateServer.cpp
)

# StatusLED already exists and is vanilla C++
set(STATUS_LED_SOURCES
    can-bridge/StatusLED.cpp
    can-bridge/HardwareStatusLEDOutput.cpp
)

# Build vanilla library
add_library(esp32-firmware-lib
    ${VANILLA_SOURCES}
    ${STATUS_LED_SOURCES}
)

# Include directories
target_include_directories(esp32-firmware-lib
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vanilla
    ${CMAKE_CURRENT_SOURCE_DIR}/can-bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vanilla
    ${CMAKE_CURRENT_SOURCE_DIR}/can-bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(esp32-firmware-lib PUBLIC Threads::Threads)

# Test executable
add_executable(esp32-firmware-tests
    tests/test_main.cpp
    tests/WiFiManager_test.cpp
    tests/DiscoveryManager_test.cpp
    tests/NtpTimeSync_test.cpp
    tests/CanBridge_test.cpp
    tests/AtCommandDispatcher_test.cpp
    tests/OtaUpdateServer_test.cpp
    tests/StatusLED_test.cpp
)

target_link_libraries(esp32-firmware-tests
    PRIVATE
    GTest::gtest_main
    GTest::gmock
    esp32-firmware-lib
)

target_compile_options(esp32-firmware-tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME esp32-firmware-tests COMMAND esp32-firmware-tests)
set_tests_properties(esp32-firmware-tests PROPERTIES TIMEOUT 30)