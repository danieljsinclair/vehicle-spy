# Test CMake configuration
# NOTE: We are standalone Tesla→iPhone data flow - no physics simulation

add_executable(vehicle-sim-tests
    test_main.cpp
    test_example.cpp
    VehicleSimulator.test.cpp
    domain/VehicleSignal.test.cpp
    domain/TelemetrySignal.test.cpp
    domain/ISignalTranslator.test.cpp
    domain/TeslaSignalParser.test.cpp
    domain/OBD2SignalTranslator.test.cpp
    domain/AudiSignalTranslator.test.cpp
    domain/SignalTranslatorFactory.test.cpp
    domain/CANDecoder.test.cpp
    domain/AudiMLBTranslator.test.cpp
    domain/TeslaCANTranslator.test.cpp
    domain/VehicleSignalEV.test.cpp
    domain/CANSignalDecoderShared.test.cpp
    domain/CANSignalDecoderBase.test.cpp
    domain/DBCParser.test.cpp
    domain/DBCFileParser.test.cpp
    domain/EventDispatcher.test.cpp
    ble/BLEManager.test.cpp
    presentation/TelemetryFormatter.test.cpp
    boundary/TeslaBLETransport.test.cpp
    boundary/OBD2Transport.test.cpp
    integration/EventDispatcherIntegration.test.cpp
)

target_link_libraries(vehicle-sim-tests
    PRIVATE
    GTest::gtest_main
    GTest::gmock
    vehicle-sim-lib
)

# Link Objective-C runtime for macOS (needed for Objective-C++ BLE code)
if(APPLE)
    find_library(FOUNDATION Foundation)
    target_link_libraries(vehicle-sim-tests PRIVATE "-lobjc")
    target_link_libraries(vehicle-sim-tests PRIVATE ${COREBLUETOOTH})
    target_link_libraries(vehicle-sim-tests PRIVATE ${FOUNDATION})
endif()

target_include_directories(vehicle-sim-tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_test(NAME vehicle-sim-tests COMMAND vehicle-sim-tests)
