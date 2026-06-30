#include "vehicle-sim/discovery/UDPDiscovery.h"

#include <iostream>
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

// Global stop flag for discovery poll (set by signal handler, polled by poll())
// Using a flag instead of EINTR because macOS's SA_RESTART causes poll() to
// auto-restart after signals, never returning EINTR. The flag is checked each
// 100ms iteration, ensuring Ctrl-C responds within ~100ms.
namespace {
    std::atomic<bool> g_discoveryStopRequested{false};
}  // namespace

// Get current Unix epoch seconds
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

        // Bind to the discovery port
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(DISCOVERY_PORT);

        if (::bind(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
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

        uint8_t buf[PACKET_LEN * 2];  // allow some extra space
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);

        ssize_t n = ::recvfrom(sockfd, buf, sizeof(buf), 0,
                               reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
        if (n < 0) {
            return false;
        }

        // Debug: log packet reception
        std::cerr << "UDPDiscovery: received " << n << " bytes from "
                  << inet_ntoa(fromAddr.sin_addr) << "\n";

        // Parse the packet
        DiscoveryPacket packet;
        if (!parse(buf, static_cast<size_t>(n), packet)) {
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

        int remainingMs = static_cast<int>(timeout.count());
        auto start = std::chrono::steady_clock::now();

        while (remainingMs > 0) {
            // Check the stop flag at each iteration (set by signal handler on Ctrl-C)
            if (g_discoveryStopRequested.load()) {
                break;
            }

            int ret = ::poll(&pfd, 1, std::min(remainingMs, 100));  // poll in 100ms chunks
            if (ret < 0) {
                if (errno == EINTR) break;  // SIGINT/SIGTERM: stop the poll immediately
                continue;                   // other transient error: keep going
            }
            if (ret > 0 && (pfd.revents & POLLIN)) {
                // Drain all available packets
                while (tryReceive()) {
                    // keep draining
                }
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            remainingMs = static_cast<int>(timeout.count()) - static_cast<int>(elapsed.count());
        }

        // Collect pending devices, deduplicating by address
        for (auto& device : pending) {
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

    void setDeviceCallback(DeviceCallback cb) {
        callback = cb;
    }
};

UDPDiscovery::UDPDiscovery() : impl_(std::make_unique<Impl>()) {}

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

void UDPDiscovery::setDeviceCallback(DeviceCallback cb) {
    impl_->setDeviceCallback(cb);
}

void UDPDiscovery::requestStop() noexcept {
    g_discoveryStopRequested.store(true);
}

void UDPDiscovery::resetStop() noexcept {
    g_discoveryStopRequested.store(false);
}

} // namespace vehicle_sim::discovery
