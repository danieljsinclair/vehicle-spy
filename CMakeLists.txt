cmake_minimum_required(VERSION 3.20)
project(vehicle-sim VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Threads REQUIRED)

# Platform-specific settings
find_library(COREBLUETOOTH CoreBluetooth)
find_library(FOUNDATION Foundation)

# Platform-specific settings
if(APPLE)
    # macOS specific frameworks
elseif(LINUX)
    # Linux specific libraries if needed
endif()

# CLI11 for command-line parsing
include(FetchContent)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0
)
FetchContent_MakeAvailable(cli11)

# iOS build option — builds C++ library only (iOS app uses standalone Xcode project)
option(BUILD_IOS "Build C++ library for iOS" OFF)
if(BUILD_IOS)
    set(CMAKE_SYSTEM_NAME iOS CACHE STRING "Target OS")
    set(CMAKE_OSX_SYSROOT iphoneos CACHE STRING "iOS SDK")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "Min iOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "iOS architectures")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-" CACHE STRING "Code signing (auto)")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO CACHE BOOL "")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO CACHE BOOL "")
endif()

# NOTE: We are a standalone Tesla→iPhone data flow module
# We do NOT integrate with external physics engines
# Our ONLY job: Read Tesla CAN bus -> Normalize -> Display on iPhone

# Define core library sources
set(VEHICLE_SIM_LIB_SOURCES
    src/VehicleSim.cpp
    src/presentation/TelemetryFormatter.cpp
    src/ble/BLEManager.cpp
    src/ble/BLEManagerBase.cpp
    src/ble/TeslaBLETransport.cpp
    src/ble/platform/BLEManagerMock.cpp
    src/config/ConfigLoader.cpp
    src/domain/VehicleSignal.cpp
    src/domain/TelemetrySignal.cpp
    src/domain/OBD2SignalTranslator.cpp
    src/domain/OBD2SignalTranslatorBase.cpp
    src/domain/CANDecoder.cpp
    src/domain/DBCSignalDefinition.cpp
    src/domain/DBCSignalMapper.cpp
    src/domain/DBCFileParser.cpp
    src/domain/VehicleConfig.cpp
    src/domain/VehicleSignalFactory.cpp
    src/domain/EventDispatcher.cpp
    src/ble/OBD2Transport.cpp
    src/domain/VehicleDetector.cpp
    src/domain/DBCSignalTranslator.cpp
    src/domain/DBCTranslationService.cpp
    src/domain/DefaultVehicleConfigs.cpp
    src/domain/DemoSignalProvider.cpp
    src/domain/DemoSignalSource.cpp
    src/domain/BLESignalSource.cpp
    src/domain/SimulationSignalSource.cpp
    src/domain/IDBCContentProvider.cpp
    src/domain/VehicleConfigResolver.cpp
    src/domain/CaptureLog.cpp
    src/domain/SignalSourceFactory.cpp
    src/pipeline/FileTransport.cpp
    src/pipeline/RawFrameNormaliser.cpp
    src/pipeline/Elm327Normaliser.cpp
    src/pipeline/CaptureNormaliser.cpp
    src/pipeline/DemoTransport.cpp
    src/pipeline/TCPTransport.cpp
    src/pipeline/USBTransport.cpp
    src/pipeline/OTAHttpTransport.cpp
    src/pipeline/PipelineFactory.cpp
    src/pipeline/RawLogSink.cpp
    src/pipeline/DecodedCsvSink.cpp
    src/pipeline/ConsoleProgressReporter.cpp
    src/pipeline/PipelineReplay.cpp
    src/cli/CliOptions.cpp
    src/cli/Orchestration.cpp
    src/cli/BLEConnectionManager.cpp
    src/cli/TelemetryRunner.cpp
    src/cli/BLERunContext.cpp
    src/cli/ReplayRunContext.cpp
    src/cli/LiveRunContext.cpp
    src/presentation/VehicleSignalFormatter.cpp
    src/boundary/ELM327Transport.cpp
    src/boundary/OBD2Protocol.cpp
    src/telemetry/TraceLogger.cpp
    src/telemetry/RawTraceLogger.cpp
    src/util/SystemClock.cpp
    src/discovery/DiscoveryPacket.cpp
    src/discovery/DiscoveryVerifier.cpp
    src/discovery/UDPDiscovery.cpp
    src/pipeline/SecureTcpTransport.cpp
    src/pipeline/ITransportOutput.cpp
)

# Add platform-specific BLE implementations for macOS builds
if(APPLE AND NOT BUILD_IOS)
    list(APPEND VEHICLE_SIM_LIB_SOURCES
        src/ble/platform/BLEManagerMacOS.mm
    )
endif()

# Add platform-specific BLE implementations for iOS builds
if(BUILD_IOS)
    list(APPEND VEHICLE_SIM_LIB_SOURCES
        src/ble/platform/BLEManageriOS.mm
    )
endif()

# Library target with core functionality (for testing and main executable)
add_library(vehicle-sim-lib
    ${VEHICLE_SIM_LIB_SOURCES}
)

target_link_libraries(vehicle-sim-lib
    PUBLIC
    Threads::Threads
    CLI11::CLI11
)

# Link libsodium for discovery signature verification
find_path(SODIUM_INCLUDE_DIR sodium.h PATHS /opt/homebrew/include)
find_library(SODIUM_LIBRARY sodium PATHS /opt/homebrew/lib)
if(SODIUM_LIBRARY AND SODIUM_INCLUDE_DIR)
    target_include_directories(vehicle-sim-lib PRIVATE ${SODIUM_INCLUDE_DIR})
    target_link_libraries(vehicle-sim-lib PRIVATE ${SODIUM_LIBRARY})
endif()

# Link CoreBluetooth and Foundation on Apple platforms
if(APPLE)
    target_link_libraries(vehicle-sim-lib
        PRIVATE
        ${COREBLUETOOTH}
        ${FOUNDATION}
    )
endif()

target_include_directories(vehicle-sim-lib
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(vehicle-sim-lib PRIVATE -g -Wall -Wextra -Werror)

# TCP auth token — single credential for TCP commands and OTA
# Can be overridden at build time: -DTCP_AUTH_TOKEN=\"my-token\"
target_compile_definitions(vehicle-sim-lib PRIVATE TCP_AUTH_TOKEN=\"vehicle-sim-2026\")

# Propagate BUILD_IOS as preprocessor define for conditional compilation
# This allows #ifndef BUILD_IOS guards in C++ code to work correctly
if(BUILD_IOS)
    target_compile_definitions(vehicle-sim-lib PRIVATE BUILD_IOS=1)
endif()

# Main executable (CLI — not built for iOS)
if(NOT BUILD_IOS)
add_executable(vehicle-sim
    src/main.cpp
)

target_link_libraries(vehicle-sim
    PRIVATE
    vehicle-sim-lib
    CLI11::CLI11
)

target_compile_options(vehicle-sim PRIVATE -g -Wall -Wextra -Werror)
endif() # NOT BUILD_IOS (closes the executable block)

# Set rpath for executable to find dylibs in build directory
if(APPLE AND NOT BUILD_IOS)
    set_target_properties(vehicle-sim PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
    )
endif()

# Installation (skip for iOS builds)
if(NOT BUILD_IOS)
    install(TARGETS vehicle-sim
        RUNTIME DESTINATION bin
    )
endif()

# Testing (macOS only — iOS app tested via Xcode)
option(BUILD_TESTS "Build vehicle-sim unit tests" ON)
if(BUILD_TESTS AND NOT BUILD_IOS)
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

    # Add tests subdirectory if it exists
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt)
        add_subdirectory(test)
    endif()

    # Custom run_tests target
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running all tests"
    )
endif()
