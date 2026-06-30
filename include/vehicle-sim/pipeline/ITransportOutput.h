#pragma once
#include <string>
#include <string_view>

namespace vehicle_sim::pipeline {

/**
 * Output interface for transport logging.
 * Production uses StdOut (writes to std::cout/std::cerr).
 * Tests use SilentOutput (discards everything).
 */
class ITransportOutput {
public:
    virtual ~ITransportOutput() = default;
    virtual void info(const std::string& msg) { out(msg); }
    virtual void error(const std::string& msg) { err(msg); }
    virtual void out(const std::string& msg) = 0;
    virtual void err(const std::string& msg) = 0;
};

/** Default production implementation — writes to stdout/stderr. */
class StdOut final : public ITransportOutput {
public:
    void out(const std::string& msg) override;
    void err(const std::string& msg) override;
};

/** Test implementation — discards all output. */
class SilentOutput final : public ITransportOutput {
public:
    void out(const std::string& /*msg*/) override {}
    void err(const std::string& /*msg*/) override {}
};

/**
 * Tagged output wrapper for device-specific console messages.
 * Formats output as:
 *   [CLIENT → deviceId] <message>  (for host/client commands)
 *   [ESP32 deviceId] <message>     (for device responses, in blue)
 */
class TaggedOutput final : public ITransportOutput {
public:
    explicit TaggedOutput(std::shared_ptr<ITransportOutput> base,
                         const std::string& deviceId = "");

    void setDeviceId(std::string_view deviceId);
    void out(const std::string& msg) override;
    void err(const std::string& msg) override;

    // Explicitly tag as client command or device response
    void outClient(const std::string& msg);
    void outDevice(const std::string& msg);

private:
    std::shared_ptr<ITransportOutput> base_;
    std::string deviceId_;
};

}  // namespace vehicle_sim::pipeline
