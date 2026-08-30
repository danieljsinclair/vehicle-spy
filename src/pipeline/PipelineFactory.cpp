#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/USBTransport.h"

#include <cctype>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

namespace {

bool startsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size()
        && s.compare(0, prefix.size(), prefix) == 0;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isDemo(std::string_view target) noexcept { return target == "demo"; }
bool isFile(std::string_view target) noexcept { return startsWith(target, "file:"); }
bool isTcp(std::string_view target) noexcept { return startsWith(target, "tcp:"); }
bool isUsb(std::string_view target) noexcept { return startsWith(target, "usb:"); }

} // namespace

bool parseTcpTarget(std::string_view target, std::string& hostOut, int& portOut) noexcept {
    constexpr std::string_view prefix = "tcp:";
    if (!startsWith(target, prefix)) return false;
    std::string_view body = target.substr(prefix.size());
    if (body.empty()) return false;

    if (auto lastColon = body.rfind(':'); lastColon != std::string_view::npos) {
        std::string_view hostPart = body.substr(0, lastColon);
        std::string_view portPart = body.substr(lastColon + 1);
        if (!portPart.empty()
            && std::all_of(portPart.begin(), portPart.end(),
                           [](unsigned char c) { return std::isdigit(c); })) {
            try {
                int port = std::stoi(std::string(portPart));
                if (port < 1 || port > 65535) return false;
                if (hostPart.empty()) return false;
                hostOut = std::string(hostPart);
                portOut = port;
                return true;
            } catch (...) {
                return false;
            }
        }
    }
    hostOut = std::string(body);
    portOut = 3333;
    return true;
}

std::string resolveAdapterProtocol(
    std::string_view connectTarget,
    std::string_view adapterProtocol) noexcept {
    if (const auto lowered = toLower(std::string(adapterProtocol));
        lowered == "raw" || lowered == "elm327") {
        return lowered;
    }
    // Default table (documented in PipelineFactory.h + the --adapter-protocol
    // help): file/demo/tcp/usb carry raw CAN lines → "raw"; everything else
    // (BLE-style targets) speaks the ELM327 dialect.
    if (isDemo(connectTarget) || isFile(connectTarget) || isTcp(connectTarget)
        || isUsb(connectTarget)) {
        return "raw";
    }
    return "elm327";
}

PipelineSource buildPipelineSource(
    std::string_view connectTarget,
    std::string_view adapterProtocol,
    std::shared_ptr<StopToken> stop) {

    if (isDemo(connectTarget)) {
        PipelineSource src;
        src.transport = std::make_unique<DemoTransport>();
        return src;
    }
    if (isTcp(connectTarget)) {
        std::string host;
        int port = 3333;
        if (!parseTcpTarget(connectTarget, host, port)) return {};
        if (!stop) stop = std::make_shared<StopToken>();
        PipelineSource src;
        src.transport = std::make_unique<TCPTransport>(
            TransportEndpoint{std::move(host), port, std::string(adapterProtocol)},
            std::make_shared<StdOut>(), TcpReadTiming{}, stop);
        return src;
    }
    if (isUsb(connectTarget)) {
        if (!stop) stop = std::make_shared<StopToken>();
        PipelineSource src;
        src.transport = std::make_unique<USBTransport>(
            std::string(connectTarget.substr(4)), 115200,
            std::make_shared<StdOut>(), stop);
        return src;
    }
    return {};
}

} // namespace vehicle_sim::pipeline
