#pragma once

#include <optional>
#include <string>

namespace vehicle_sim::pipeline {

/**
 * How bytes/lines arrive from an adapter. Phase 1 is file replay; later
 * phases add Demo/BLE/TCP/USB transports. A transport is a pull-based source
 * of logical "lines" (a line == one newline-terminated record from the
 * adapter — a CAN-monitor row, an ELM327 response, etc.). It deliberately
 * knows nothing about frames, DBC, or decode (Open/Closed: new transports
 * are added by implementing this interface, not by editing conditionals).
 *
 * The pull model returns std::nullopt once the source is exhausted (EOF for
 * a file, disconnect for a socket). Streaming transports added later can
 * block inside nextLine() until data arrives — that is an implementation
 * detail hidden behind this interface.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    ITransport() = default;
    ITransport(const ITransport&) = delete;
    ITransport& operator=(const ITransport&) = delete;
    ITransport(ITransport&&) = delete;
    ITransport& operator=(ITransport&&) = delete;

    /**
     * Open the underlying source. Returns false if the source could not be
     * opened (missing file, unreachable host, ...). After a false return the
     * transport is not usable.
     */
    virtual bool open() = 0;

    /** True iff open() succeeded and the source has not been exhausted. */
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /**
     * Fetch the next line, without the trailing newline. Returns nullopt at
     * EOF / disconnect. Carriage returns are preserved (the normaliser
     * decides how to tolerate CRLF).
     */
    virtual std::optional<std::string> nextLine() = 0;
};

} // namespace vehicle_sim::pipeline
