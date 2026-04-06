cmake_minimum_required(VERSION 3.20)
project(vehicle-sim VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Threads REQUIRED)

# Platform-specific settings
if(APPLE)
    # macOS specific frameworks if needed
elseif(LINUX)
    # Linux specific libraries if needed
endif()

# iOS build option
option(BUILD_IOS "Build for iOS simulator/device" OFF)
if(BUILD_IOS)
    # Enable Swift only when building iOS with Xcode generator
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        enable_language(Swift)
        set(CMAKE_Swift_VERSION "5.9" CACHE STRING "Swift language version")
    endif()
    set(CMAKE_OSX_SYSROOT "iphonesimulator" CACHE STRING "iOS SDK")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "iOS architectures")
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer" CACHE STRING "Code signing identity")
    set(CMAKE_MACOSX_BUNDLE YES)
endif()

# Add submodules if present
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine-sim-bridge/CMakeLists.txt)
    add_subdirectory(engine-sim-bridge ${CMAKE_BINARY_DIR}/engine-sim-bridge)
endif()

# Define core library sources
set(VEHICLE_SIM_LIB_SOURCES
    src/VehicleSim.cpp
    src/physics/PhysicsEngine.cpp
    src/presentation/TelemetryFormatter.cpp
    src/ble/BLEManager.cpp
    src/ble/platform/BLEManagerMock.cpp
    src/ble/ITransport.cpp
)
    src/ble/TeslaBLETransport.cpp
    src/config/ConfigLoader.cpp
    src/domain/VehicleSignal.cpp
    src/domain/TelemetrySignal.cpp
    src/domain/TeslaSignalTranslator.cpp
)

# Library target with core functionality (for testing and main executable)
add_library(vehicle-sim-lib
    ${VEHICLE_SIM_LIB_SOURCES}
)

target_link_libraries(vehicle-sim-lib
    PUBLIC
    Threads::Threads
)

target_include_directories(vehicle-sim-lib
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(vehicle-sim-lib PRIVATE -g)

# Main executable
add_executable(vehicle-sim
    src/main.cpp
)

target_link_libraries(vehicle-sim
    PRIVATE
    vehicle-sim-lib
)

target_compile_options(vehicle-sim PRIVATE -g)

# iOS app target (vehicle-sim-ios)
if(BUILD_IOS)
    # Build static core library for iOS
    add_library(vehicle-sim-core-ios STATIC
        ${VEHICLE_SIM_LIB_SOURCES}
    )
    target_include_directories(vehicle-sim-core-ios
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_options(vehicle-sim-core-ios PRIVATE -g)
    set_target_properties(vehicle-sim-core-ios PROPERTIES
        OUTPUT_NAME vehicle-sim-core
    )

    # iOS app bundle - Swift as main language
    add_executable(vehicle-sim-ios
        vehicle-sim-ios/VehicleSim/VehicleSimAppApp.swift
        vehicle-sim-ios/VehicleSim/ContentView.swift
        vehicle-sim-ios/VehicleSim/VehicleViewModel.swift
        vehicle-sim-ios/VehicleSim/Models/TelemetryData.swift
        vehicle-sim-ios/VehicleSim/VehicleSimWrapper.mm
    )
    target_include_directories(vehicle-sim-ios
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vehicle-sim-ios
    )
    target_link_libraries(vehicle-sim-ios
        PRIVATE
        vehicle-sim-core-ios
        "-lc++"
    )
    set_target_properties(vehicle-sim-ios PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION app
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/vehicle-sim-ios/Info.plist
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/vehicle-sim-ios/VehicleSim/VehicleSim-Bridging-Header.h"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
    )
endif()

# Set rpath for executable to find dylibs in build directory
if(APPLE)
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

# Testing (optional)
option(BUILD_TESTS "Build vehicle-sim unit tests" ON)
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
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
