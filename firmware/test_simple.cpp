#include "vanilla/FirmwareApp.h"
#include "mocks/WiFiMock.h"
#include "mocks/PreferencesMock.h"
#include "mocks/ArduinoMock.h"
#include "vanilla/DiscoveryManager.h"
#include <iostream>

using namespace esp32_firmware;

class MockUdp : public IUdp {
public:
    void begin(uint16_t port) override { std::cout << "UDP::begin" << std::endl; }
    int beginPacket(const std::string& ip, uint16_t port) override { std::cout << "UDP::beginPacket" << std::endl; return 1; }
    size_t write(const uint8_t* data, size_t len) override { std::cout << "UDP::write" << std::endl; return len; }
    int endPacket() override { std::cout << "UDP::endPacket" << std::endl; return 1; }
};

class MockTime : public ITime {
public:
    uint64_t getCurrentTimestamp() const override { std::cout << "Time::getCurrentTimestamp" << std::endl; return 1000000000; }
    uint32_t millis() const override { std::cout << "Time::millis" << std::endl; return 0; }
};

class MockStatusLED : public IStatusLED {
public:
    void setPattern(int pattern) override {}
    void update(uint32_t now) override { std::cout << "LED::update" << std::endl; }
};

int main() {
    std::cout << "Creating mocks..." << std::endl;
    WiFiMock wifiMock;
    PreferencesMock prefsMock;
    MockStatusLED statusLedMock;
    MockUdp udpMock;
    MockTime timeMock;
    std::array<uint8_t, 16> testDeviceId = {};
    
    std::cout << "Setting WiFi mode to AP..." << std::endl;
    wifiMock.setMode(2);
    
    std::cout << "Creating FirmwareApp..." << std::endl;
    FirmwareApp app(wifiMock, prefsMock, statusLedMock, wifiMock, udpMock, timeMock, testDeviceId, "baked-ssid", "baked-pass");
    
    std::cout << "Calling init()..." << std::endl;
    app.init();
    
    std::cout << "Calling update(0)..." << std::endl;
    app.update(0);
    
    std::cout << "Calling update(1000)..." << std::endl;
    app.update(1000);
    
    std::cout << "Done!" << std::endl;
    return 0;
}
