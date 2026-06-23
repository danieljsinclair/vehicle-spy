# Test CMake configuration
# NOTE: We are standalone Tesla→iPhone data flow - no physics simulation

add_executable(vehicle-sim-tests
    test_main.cpp
    test_example.cpp
    VehicleSimulator.test.cpp
    domain/VehicleSignal.test.cpp
    domain/TelemetrySignal.test.cpp
    domain/ISignalTranslator.test.cpp
    domain/OBD2SignalTranslator.test.cpp
    domain/CANDecoder.test.cpp
    domain/VehicleSignalEV.test.cpp
    domain/CANSignalDecoderShared.test.cpp
    domain/DBCParser.test.cpp
    domain/DBCFileParser.test.cpp
    domain/VehicleSignalFactory.test.cpp
    domain/EventDispatcher.test.cpp
    domain/VehicleDetector.test.cpp
    domain/DBCSignalTranslator.test.cpp
    domain/DBCTranslationService.test.cpp
    domain/CaptureLog.test.cpp
    domain/DefaultVehicleConfigs.test.cpp
    domain/DemoSignalProvider.test.cpp
    domain/SignalSourceFactory.test.cpp
    domain/VehicleConfigResolver.test.cpp
    domain/MockSignalSource.h
    ble/BLEManager.test.cpp
    presentation/TelemetryFormatter.test.cpp
    presentation/VehicleSignalFormatter.test.cpp
    cli/CliOptions.test.cpp
    cli/TelemetryRunner.test.cpp
    cli/Orchestration.test.cpp
    boundary/TeslaBLETransport.test.cpp
    boundary/OBD2Transport.test.cpp
    boundary/ELM327Protocol.test.cpp
    boundary/ELM327Transport.test.cpp
    boundary/OBD2EndToEnd.test.cpp
    boundary/VehicleDetectorIntegration.test.cpp
    integration/DBCPipelineIntegration.test.cpp
    integration/DBCLiveDecoding.test.cpp
    integration/TeslaCANLiveDecoding.test.cpp
    integration/AudiOBD2LiveDecoding.test.cpp
    telemetry/TraceLogger.test.cpp
    pipeline/RawFrameNormaliser.test.cpp
    pipeline/Elm327Normaliser.test.cpp
    pipeline/CaptureNormaliser.test.cpp
    pipeline/DemoTransport.test.cpp
    pipeline/TCPTransport.test.cpp
    pipeline/USBTransport.test.cpp
    pipeline/OTAHttpTransport.test.cpp
    pipeline/PipelineFactory.test.cpp
    pipeline/FileTransport.test.cpp
    pipeline/Sinks.test.cpp
    pipeline/PipelineReplay.test.cpp
    pipeline/ConsoleProgressReporter.test.cpp
    discovery/DiscoveryPacket.test.cpp
    discovery/DiscoveryVerifier.test.cpp
    pipeline/SecureTcpTransport.test.cpp
)

target_link_libraries(vehicle-sim-tests
    PRIVATE
    GTest::gtest_main
    GTest::gmock
    vehicle-sim-lib
    ${SODIUM_LIBRARY}
)

target_compile_options(vehicle-sim-tests PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -DVEHICLE_SIM_TEST_SILENTLY)

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
    ${SODIUM_INCLUDE_DIR}
)

add_test(NAME vehicle-sim-tests COMMAND vehicle-sim-tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
