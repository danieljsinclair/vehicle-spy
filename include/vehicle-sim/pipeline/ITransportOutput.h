#pragma once
#include <string>

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

}  // namespace vehicle_sim::pipeline
