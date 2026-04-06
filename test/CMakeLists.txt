# Test CMake configuration
# NOTE: Physics engine tests removed - we are standalone Tesla→iPhone data flow module
add_executable(vehicle-sim-tests
    test_main.cpp
    test_example.cpp
    domain/VehicleSignal.test.cpp
    domain/TelemetrySignal.test.cpp
    domain/ISignalTranslator.test.cpp
    domain/TeslaSignalTranslator.test.cpp
    ble/BLEManager.test.cpp
    presentation/TelemetryFormatter.test.cpp
)

target_link_libraries(vehicle-sim-tests
    PRIVATE
    GTest::gtest_main
    GTest::gmock
    vehicle-sim-lib
)

target_include_directories(vehicle-sim-tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_test(NAME vehicle-sim-tests COMMAND vehicle-sim-tests)
