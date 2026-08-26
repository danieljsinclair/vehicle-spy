#include "vehicle-sim/discovery/UDPDiscovery.h"

#include "vehicle-sim/discovery/PosixDiscoverySocket.h"

#include <iostream>
#include <array>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <memory>
#include <string>

namespace vehicle_sim::discovery {

// The injected StopToken (set by the signal handler via SignalStopBroker,
// polled by poll()) is used instead of EINTR because macOS's SA_RESTART causes
// poll() to auto-restart after signals, never returning EINTR. It is checked
// each 100ms iteration, ensuring Ctrl-C responds within ~100ms.

static uint64_t nowEpoch() {
    return static_cast<uint64_t>(std::time(nullptr));
}

// Format an IPv4 address (host byte order, 32-bit) as dotted-quad. Equivalent
// to inet_ntoa(in_addr{s_addr=htonl(addr)}) for every IPv4 value, with no POSIX
// dependency — used so the IDiscoverySocket seam can report addresses as plain
// integers while UDPDiscovery still produces the same address string it did
// when it called inet_ntoa directly.
static std::string ipv4DottedQuad(uint32_t addrHostOrder) {
    return std::to_string((addrHostOrder >> 24) & 0xFFu) + "." +
           std::to_string((addrHostOrder >> 16) & 0xFFu) + "." +
           std::to_string((addrHostOrder >> 8) & 0xFFu) + "." +
           std::to_string(addrHostOrder & 0xFFu);
}

class UDPDiscovery::Impl {
public:
    explicit Impl(std::unique_ptr<IDiscoverySocket> socket)
        : socket_(std::move(socket)) {
        if (!socket_) {
            socket_ = std::make_unique<PosixDiscoverySocket>();
        }
    }

    // Raw UDP socket (production = PosixDiscoverySocket; tests inject a fake).
    // Impl is a PIMPL internal; encapsulation is at the UDPDiscovery boundary.
    std::unique_ptr<IDiscoverySocket> socket_;
    bool listening = false;
    std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> publicKey{};
    bool hasPublicKey = false;
    uint64_t maxClockSkew = DEFAULT_MAX_CLOCK_SKEW;
    DeviceCallback callback;
    std::vector<DiscoveredDevice> pending;
    // Track already-seen addresses for deduplication
    std::vector<std::string> seenAddresses;
    // Cooperative stop signal (injected; shared with the caller's signal handler
    // via SignalStopBroker). Polled each iteration so Ctrl+C ends poll() promptly.
    std::shared_ptr<pipeline::StopToken> stop_ = std::make_shared<pipeline::StopToken>();

    bool start() {
        if (listening) {
            return true;  // already started
        }

        if (!socket_->bind(DISCOVERY_PORT)) {
            return false;
        }

        if (!socket_->setNonBlocking()) {
            // The original Impl treated a failed fcntl as non-fatal (it only
            // applied the non-blocking flag if F_GETFL succeeded; a failure left
            // the socket blocking but still bound/listening). Preserve that: do
            // not fail start() here.
        }

        listening = true;
        return true;
    }

    void stop() {
        socket_->close();
        listening = false;
    }

    bool isListening() const {
        return listening;
    }

    // Try to receive and process one packet. Returns true if a valid device was found.
    bool tryReceive() {
        std::array<uint8_t, PACKET_LEN * 2> buf;  // allow some extra space
        uint32_t fromAddrHost = 0;

        ssize_t n = socket_->recvFrom(buf.data(), buf.size(), &fromAddrHost);
        if (n < 0) {
            return false;
        }

        // Debug: log packet reception
        std::cerr << "UDPDiscovery: received " << n << " bytes from "
                  << ipv4DottedQuad(fromAddrHost) << "\n";

        // Parse the packet
        DiscoveryPacket packet;
        if (!parse(buf.data(), static_cast<size_t>(n), packet)) {
            std::cerr << "UDPDiscovery: failed to parse packet (wrong size or magic)\n";
            return false;
        }

        // Debug: log timestamp for diagnosis
        uint64_t now = nowEpoch();
        std::cerr << "UDPDiscovery: packet timestamp=" << packet.timestamp
                  << ", now=" << now << ", deviceId:";
        for (auto b : packet.deviceId) std::cerr << std::hex << (int)b;
        std::cerr << std::dec << "\n";

        // Discovery packets are intentionally unsigned — the firmware sends a
        // zeroed signature field. The OTA key is used for firmware *update*
        // authentication, not for discovery. Discovery is the bootstrap that
        // learns the device's IP before any secure channel exists.
        //
        // IMPORTANT: We do NOT check timestamp freshness for unsigned discovery.
        // The timestamp may be uptime-based (seconds since boot) if the device
        // lacks NTP sync, which can be wildly different from host Unix time.
        // Timestamp freshness is only meaningful for signed packets (OTA),
        // where the signature provides the authenticity guarantee.
        // For discovery, we accept any valid packet format regardless of timestamp.

        // Extract the IP address string (equivalent to the former inet_ntoa call).
        std::string addrStr = ipv4DottedQuad(fromAddrHost);

        // Build the discovered device
        DiscoveredDevice device;
        device.deviceId = packet.deviceId;
        device.address = addrStr;
        device.canPort = packet.canPort;
        device.otaPort = packet.otaPort;
        device.timestamp = packet.timestamp;

        // Add to pending (will be collected by poll())
        pending.push_back(device);

        // Fire callback if set
        if (callback) {
            callback(device);
        }

        return true;
    }

    // Run ONE wait-and-drain step and report whether polling should continue.
    //
    // Returns false when the caller must stop: a cooperative stop was requested,
    // a signal interrupted the wait, or at least one device is now pending.
    // Extracted from poll() so each function has a single responsibility — this
    // owns "what happened in one 100ms window", poll() owns the time budget and
    // the dedup of the result set.
    bool pollStepShouldContinue(int remainingMs) {
        // Check the stop flag at each iteration (set by signal handler on Ctrl-C)
        if (stop_->stopRequested()) {
            return false;
        }

        const int ret = socket_->pollReadable(std::min(remainingMs, 100));  // 100ms chunks
        if (ret < 0) {
            // EINTR means SIGINT/SIGTERM arrived: stop. Any other transient
            // error is retried on the next iteration.
            return errno != EINTR;
        }
        if (ret > 0) {
            // Drain everything currently readable, so a burst of packets from
            // several devices in one window all surface together.
            while (tryReceive()) {
                // keep draining
            }
        }

        // Adopt on the FIRST valid packet: discovery is "find a device and
        // connect", not "enumerate everything on the LAN". Once at least one
        // device is pending, return immediately instead of blocking the rest
        // of the timeout window. The hunting path (TCPTransport) calls
        // poll(500ms) in a loop and benefits from the same early return.
        return pending.empty();
    }

    // Collect the pending devices, deduplicating by address, and clear the queue.
    std::vector<DiscoveredDevice> drainPendingDeduplicated() {
        std::vector<DiscoveredDevice> result;
        for (const auto& device : pending) {
            if (std::find(seenAddresses.begin(), seenAddresses.end(), device.address)
                == seenAddresses.end()) {
                seenAddresses.push_back(device.address);
                result.push_back(device);
            }
        }
        pending.clear();
        return result;
    }

    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds timeout) {
        // Clear previously seen addresses for this poll cycle
        seenAddresses.clear();

        const auto budgetMs = static_cast<int>(timeout.count());
        const auto start = std::chrono::steady_clock::now();

        auto remainingMs = budgetMs;
        bool keepGoing = true;
        while (keepGoing && remainingMs > 0) {
            keepGoing = pollStepShouldContinue(remainingMs);

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            remainingMs = budgetMs - static_cast<int>(elapsed.count());
        }

        return drainPendingDeduplicated();
    }

    void setPublicKey(const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& key) {
        publicKey = key;
        hasPublicKey = true;
    }

    void setMaxClockSkew(uint64_t seconds) {
        maxClockSkew = seconds;
    }

    void setDeviceCallback(const DeviceCallback& cb) {
        callback = cb;
    }
};

UDPDiscovery::UDPDiscovery() : impl_(std::make_unique<Impl>(nullptr)) {}

UDPDiscovery::UDPDiscovery(std::shared_ptr<pipeline::StopToken> stop)
    : impl_(std::make_unique<Impl>(nullptr)) {
    if (stop) {
        impl_->stop_ = std::move(stop);
    }
}

UDPDiscovery::UDPDiscovery(std::unique_ptr<IDiscoverySocket> socket,
                           std::shared_ptr<pipeline::StopToken> stop)
    : impl_(std::make_unique<Impl>(std::move(socket))) {
    if (stop) {
        impl_->stop_ = std::move(stop);
    }
}

UDPDiscovery::~UDPDiscovery() = default;

bool UDPDiscovery::start() {
    return impl_->start();
}

void UDPDiscovery::stop() {
    impl_->stop();
}

bool UDPDiscovery::isListening() const {
    return impl_->isListening();
}

std::vector<DiscoveredDevice> UDPDiscovery::poll(std::chrono::milliseconds timeout) {
    return impl_->poll(timeout);
}

void UDPDiscovery::setPublicKey(const std::array<uint8_t, ED25519_PUBLIC_KEY_LEN>& key) {
    impl_->setPublicKey(key);
}

void UDPDiscovery::setMaxClockSkew(uint64_t seconds) {
    impl_->setMaxClockSkew(seconds);
}

void UDPDiscovery::setDeviceCallback(const DeviceCallback& cb) {
    impl_->setDeviceCallback(cb);
}

} // namespace vehicle_sim::discovery
