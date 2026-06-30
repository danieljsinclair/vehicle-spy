#include "vehicle-sim/pipeline/PipelineFactory.h"
#include "vehicle-sim/pipeline/CaptureNormaliser.h"
#include "vehicle-sim/pipeline/DemoTransport.h"
#include "vehicle-sim/pipeline/Elm327Normaliser.h"
#include "vehicle-sim/pipeline/FileTransport.h"
#include "vehicle-sim/pipeline/RawFrameNormaliser.h"
#include "vehicle-sim/pipeline/TCPTransport.h"
#include "vehicle-sim/pipeline/USBTransport.h"

#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

namespace {

bool startsWith(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isFile(std::string_view target) noexcept { return startsWith(target, "file:"); }
bool isDemo(std::string_view target) noexcept { return target == "demo"; }
bool isTcp(std::string_view target) noexcept { return startsWith(target, "tcp:"); }
bool isUsb(std::string_view target) noexcept { return startsWith(target, "usb:"); }

} // namespace

// Parse "tcp:<host>:<port>" or "tcp:<host>" (port defaults to 3333). Returns
// false if the target is not a well-formed tcp: form. This is the single
// canonical TCP-target parser for the engine.
bool parseTcpTarget(std::string_view target, std::string& hostOut, int& portOut) noexcept {
    constexpr std::string_view prefix = "tcp:";
    if (!startsWith(target, prefix)) return false;

    std::string_view body = target.substr(prefix.size());
    if (body.empty()) return false;

    // Split on the LAST ':' so the port (if present) follows it; a bare
    // "tcp:<host>" keeps the default port.
    if (auto lastColon = body.rfind(':'); lastColon != std::string_view::npos) {
        std::string_view hostPart = body.substr(0, lastColon);
        std::string_view portPart = body.substr(lastColon + 1);

        // The port must be all-digits and non-empty to count as a port;
        // otherwise treat the whole body as a host (defensive against odd
        // inputs like "tcp:host.local").
        if (!portPart.empty() &&
            std::all_of(portPart.begin(), portPart.end(),
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

    // No usable port token → whole body is the host, default port.
    hostOut = std::string(body);
    portOut = 3333;
    return true;
}

std::string resolveAdapterProtocol(
    std::string_view connectTarget,
    std::string_view adapterProtocol) noexcept {
    // An explicit, recognised override always wins.
    if (const auto lowered = toLower(std::string(adapterProtocol));
        lowered == "raw" || lowered == "elm327") {
        return lowered;
    }
    // Default table (applied when omitted or "default").
    if (isFile(connectTarget) || isDemo(connectTarget) ||
        isTcp(connectTarget) || isUsb(connectTarget)) {
        return "raw";
    }
    // Anything else (a BLE address) defaults to elm327.
    return "elm327";
}

PipelineSource buildPipelineSource(
    std::string_view connectTarget,
    std::string_view adapterProtocol) {

    if (isFile(connectTarget)) {
        std::string path(connectTarget.substr(5));
        PipelineSource src;
        src.transport = std::make_unique<FileTransport>(std::move(path));
        src.normaliser = std::make_unique<CaptureNormaliser>();
        return src;
    }

    if (isDemo(connectTarget)) {
        PipelineSource src;
        src.transport = std::make_unique<DemoTransport>();
        src.normaliser = std::make_unique<RawFrameNormaliser>();
        return src;
    }

    if (isTcp(connectTarget)) {
        std::string host;
        int port = 3333;
        if (!parseTcpTarget(connectTarget, host, port)) {
            return {};
        }
        // The caller (LiveRunContext) passes the raw --adapter-protocol option,
        // so resolve the effective protocol here before selecting a normaliser.
        // elm327 -> ELM327 monitor dialect; anything else -> raw live frames.
        const auto effective = resolveAdapterProtocol(connectTarget, adapterProtocol);
        PipelineSource src;
        src.transport = std::make_unique<TCPTransport>(std::move(host), port, adapterProtocol);
        if (effective == "elm327") {
            src.normaliser = std::make_unique<Elm327Normaliser>();
        } else {
            src.normaliser = std::make_unique<RawFrameNormaliser>();
        }
        return src;
    }

    if (isUsb(connectTarget)) {
        const auto effective = resolveAdapterProtocol(connectTarget, adapterProtocol);
        PipelineSource src;
        src.transport = std::make_unique<USBTransport>(std::string(connectTarget.substr(4)));
        if (effective == "elm327") {
            src.normaliser = std::make_unique<Elm327Normaliser>();
        } else {
            src.normaliser = std::make_unique<RawFrameNormaliser>();
        }
        return src;
    }

    return {};
}

} // namespace vehicle_sim::pipeline
