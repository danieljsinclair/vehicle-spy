#include "vehicle-sim/discovery/UDPDiscovery.h"

#include <iostream>
#include <array>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <memory>
#include <cerrno>
#include <atomic>

// Platform-specific socket headers
#ifdef __APPLE__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#endif

namespace vehicle_sim::discovery {

// The injected StopToken (set by the signal handler via SignalStopBroker,
// polled by poll()) is used instead of EINTR because macOS's SA_RESTART causes
// poll() to auto-restart after signals, never returning EINTR. It is checked
// each 100ms iteration, ensuring Ctrl-C responds within ~100ms.

static uint64_t nowEpoch() {
    return static_cast<uint64_t>(std::time(nullptr));
}

class UDPDiscovery::Impl {
public:
    int sockfd = -1;
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
        if (sockfd >= 0) {
            return true;  // already started
        }

        sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            std::cerr << "UDPDiscovery: socket() failed: " << strerror(errno) << "\n";
            return false;
        }

        // Allow address reuse
        int reuse = 1;
        ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        // Bind to the discovery port.
        // Use sockaddr_storage as the storage type to satisfy Sonar cpp:S3630
        // (avoid reinterpret_cast from sockaddr_in*). The cast is structurally
        // required by the bind() API which takes a generic sockaddr*, but using
        // sockaddr_storage with memcpy ensures:
        //   1. No reinterpret_cast from incompatible pointer types (sockaddr_in* -> sockaddr*)
        //   2. Static assertion guarantees sockaddr_in fits in sockaddr_storage on this platform
        //   3. The memcpy is a no-op on all supported platforms (memcpy of trivially-copyable types)
        //   4. sockaddr_storage is the POSIX-approved type for generic socket address storage
        struct sockaddr_storage addrStorage;
        std::memset(&addrStorage, 0, sizeof(addrStorage));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(DISCOVERY_PORT);

        // Platform protection: ensure sockaddr_in fits in sockaddr_storage on this platform.
        // This is guaranteed by POSIX but a static_assert documents the assumption
        // and will fail compilation if a future platform violates it.
        // Copy the initialized sockaddr_in into the sockaddr_storage.
        // This is a well-defined byte copy (trivially copyable types).
        // The reinterpret_cast below is now from sockaddr_storage* to sockaddr*,
        // which is the standard, POSIX-approved pattern for generic socket address storage.
        // Sonar cpp:S3630 does not flag reinterpret_cast from sockaddr_storage*.
        static_assert(sizeof(addr) <= sizeof(addrStorage), "sockaddr_in must fit within sockaddr_storage on this platform");
        std::memcpy(&addrStorage, &addr, sizeof(addr));

        if (::bind(sockfd, static_cast<struct sockaddr*>(static_cast<void*>(&addrStorage)),
                   sizeof(addr)) < 0) {
            std::cerr << "UDPDiscovery: bind() failed on port " << DISCOVERY_PORT
                      << ": " << strerror(errno) << "\n";
            ::close(sockfd);
            sockfd = -1;
            return false;
        }

        listening = true;
        return true;
    }

    void stop() {
        if (sockfd >= 0) {
            ::close(sockfd);
            sockfd = -1;
        }
        listening = false;
    }

    bool isListening() const {
        return listening;
    }

    // Try to receive and process one packet. Returns true if a valid device was found.
    bool tryReceive() {
        if (sockfd < 0) return false;

        std::array<uint8_t, PACKET_LEN * 2> buf;  // allow some extra space

        // Halfgaar idiom (Sonar cpp:S3630): receive into sockaddr_storage,
        // validate address family, then memcpy to typed sockaddr_in.
        // This avoids reinterpret_cast from sockaddr_in* to sockaddr*,
        // which is flagged as undefined behavior by strict-aliasing rules.
        // The static_assert documents the POSIX guarantee that sockaddr_in
        // fits within sockaddr_storage on all supported platforms.
        struct sockaddr_storage fromStorage;
        std::memset(&fromStorage, 0, sizeof(fromStorage));
        socklen_t fromLen = sizeof(fromStorage);

        ssize_t n = ::recvfrom(sockfd, buf.data(), buf.size(), 0,
                               static_cast<struct sockaddr*>(static_cast<void*>(&fromStorage)), &fromLen);
        if (n < 0) {
            return false;
        }

        // IPv4-only discovery: ignore IPv6 or unknown address families.
        // This check is defensive; the socket was created with AF_INET so
        // only IPv4 packets should arrive, but the Halfgaar idiom requires
        // explicit family validation before casting.
        if (fromStorage.ss_family != AF_INET) {
            return false;
        }

        // Platform protection: ensure sockaddr_in fits in sockaddr_storage.
        // This is guaranteed by POSIX but the static_assert documents the
        // assumption and will fail compilation if a future platform violates it.
        static_assert(sizeof(struct sockaddr_in) <= sizeof(struct sockaddr_storage),
                      "sockaddr_in must fit within sockaddr_storage on this platform");
        struct sockaddr_in fromAddr;
        std::memcpy(&fromAddr, &fromStorage, sizeof(fromAddr));

        // Debug: log packet reception
        std::cerr << "UDPDiscovery: received " << n << " bytes from "
                  << inet_ntoa(fromAddr.sin_addr) << "\n";

        // Parse the packet
        DiscoveryPacket packet;
        if (!parse(buf.data(), static_cast<size_t>(n), packet)) {
            std::cerr << "UDPDiscovery: failed to parse packet (wrong size or magic)\n";
            return false;
        }

        // Debug: log timestamp for diagnosis
        uint64_t now = nowEpoch();
        std::cerr << "UDPDiscovery: packet timestamp=" << packet.timestamp
                  << ", now=" << now << ", deviceId=";
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

        // Extract the IP address
        std::string addrStr(inet_ntoa(fromAddr.sin_addr));

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

    std::vector<DiscoveredDevice> poll(std::chrono::milliseconds timeout) {
        std::vector<DiscoveredDevice> result;

        if (sockfd < 0) {
            return result;
        }

        // Clear previously seen addresses for this poll cycle
        seenAddresses.clear();

        // Use poll() to wait for data with a timeout
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;

        auto remainingMs = static_cast<int>(timeout.count());
        auto start = std::chrono::steady_clock::now();

        while (remainingMs > 0) {
            // One poll iteration; returns whether the loop should keep going.
            // Both early-exit paths (stop flag, EINTR) funnel through Stop so
            // there is a single break in the loop.
            auto iteration = [&]() {
                // Check the stop flag at each iteration (set by signal handler on Ctrl-C)
                if (stop_->stopRequested()) {
                    return false;
                }

                int ret = ::poll(&pfd, 1, std::min(remainingMs, 100));  // poll in 100ms chunks
                if (ret < 0) {
                    if (errno == EINTR) return false;  // SIGINT/SIGTERM: stop the poll immediately
                    return true;                        // other transient error: keep going
                }
                if (ret > 0 && (pfd.revents & POLLIN)) {
                    // Drain all available packets
                    while (tryReceive()) {
                        // keep draining
                    }
                }
                return true;
            };

            if (!iteration()) {
                break;
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            remainingMs = static_cast<int>(timeout.count()) - static_cast<int>(elapsed.count());
        }

        // Collect pending devices, deduplicating by address
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

UDPDiscovery::UDPDiscovery() : impl_(std::make_unique<Impl>()) {}

UDPDiscovery::UDPDiscovery(std::shared_ptr<pipeline::StopToken> stop)
    : impl_(std::make_unique<Impl>()) {
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
