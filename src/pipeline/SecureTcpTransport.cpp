// SecureTcpTransport.cpp — Authenticated encrypted TCP transport.
//
// See SecureTcpTransport.h for protocol description.

#include "vehicle-sim/pipeline/SecureTcpTransport.h"

#include <sodium.h>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <thread>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace vehicle_sim {
namespace pipeline {

namespace {

std::atomic<bool> g_stopRequested{false};

// Readable aliases for the constants we need
constexpr size_t SECRETBOX_NONCEBYTES = 24;
constexpr size_t SECRETBOX_MACBYTES   = 16;

// Exponential backoff calculation for reconnection
constexpr int calculateReconnectDelayMs(int retryCount) {
    // Exponential backoff: 1s, 2s, 4s, 8s, capped at MAX_RECONNECT_DELAY_MS
    int delay = SecureTcpTransport::BASE_RECONNECT_DELAY_MS * (1 << std::min(retryCount, 12));
    return std::min(delay, SecureTcpTransport::MAX_RECONNECT_DELAY_MS);
}

// Resolve host:port into a connected TCP socket, or -1 on failure.
// Re-resolves DNS on each call for hotspot DHCP roam support.
// Returns -2 on ECONNREFUSED (connection refused - don't retry).
int connectToHost(const std::string& host, int port, uint32_t timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (result) freeaddrinfo(result);
        return -1;
    }

    int fd = -1;
    bool connRefused = false;
    for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

#ifdef SO_NOSIGPIPE
        int nosig = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof(nosig));
#endif
        struct timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;

        // Check if connection was refused
        if (errno == ECONNREFUSED) {
            connRefused = true;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);

    if (connRefused) {
        return -2;  // Connection refused - don't retry
    }
    return fd;
}

// Read exactly `len` bytes from `fd` within `timeoutMs`. Returns false on
// timeout, disconnect, or error.
bool doRecvExact(int fd, uint8_t* dst, size_t len, uint32_t timeoutMs) {
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    while (got < len) {
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return false;
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd, &rs);
        timeval tv{};
        tv.tv_sec = static_cast<time_t>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<suseconds_t>(remaining.count() % 1000000);
        int r = select(fd + 1, &rs, nullptr, nullptr, &tv);
        if (r > 0) {
            ssize_t n = recv(fd, dst + got, len - got, 0);
            if (n <= 0) return false;
            got += static_cast<size_t>(n);
        } else if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        } else {
            return false;  // timeout
        }
    }
    return true;
}

// Send all bytes, or return false.
bool doSendAll(int fd, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

void SecureTcpTransport::requestStop() noexcept {
    g_stopRequested.store(true);
}

void SecureTcpTransport::resetStop() noexcept {
    g_stopRequested.store(false);
}

SecureTcpTransport::SecureTcpTransport(
    std::string host,
    int port,
    std::array<uint8_t, discovery::ED25519_PUBLIC_KEY_LEN> publicKey,
    std::shared_ptr<ITransportOutput> output,
    int recvTimeoutUs)
    : host_(std::move(host))
    , port_(port)
    , publicKey_(publicKey)
    , output_(std::move(output))
    , recvTimeoutUs_(recvTimeoutUs)
{
    // Ensure libsodium is initialised (idempotent).
    if (sodium_init() < 0) {
        output_->err("[secure-tcp] FATAL: sodium_init failed");
    }
}

SecureTcpTransport::~SecureTcpTransport() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool SecureTcpTransport::performHandshake() {
    // -- Step 1: Generate ephemeral X25519 keypair --
    std::array<uint8_t, X25519_PUBLIC_KEY_LEN> clientPk;
    std::array<uint8_t, X25519_SECRET_KEY_LEN> clientSk;
    if (crypto_kx_keypair(clientPk.data(), clientSk.data()) != 0) {
        output_->err("[secure-tcp] crypto_kx_keypair failed");
        return false;
    }

    // -- Step 2: Send client ephemeral public key --
    if (!doSendAll(fd_, clientPk.data(), clientPk.size())) {
        output_->err("[secure-tcp] Failed to send client public key");
        return false;
    }

    // -- Step 3: Receive server ephemeral public key --
    std::array<uint8_t, X25519_PUBLIC_KEY_LEN> serverPk;
    if (!doRecvExact(fd_, serverPk.data(), serverPk.size(), HANDSHAKE_TIMEOUT_MS)) {
        output_->err("[secure-tcp] Failed to receive server public key");
        return false;
    }

    // -- Step 4: Compute session keys --
    if (crypto_kx_client_session_keys(
            rxKey_.data(), txKey_.data(),
            clientPk.data(), clientSk.data(), serverPk.data()) != 0) {
        output_->err("[secure-tcp] crypto_kx_client_session_keys failed");
        return false;
    }

    // Wipe ephemeral secret key from memory
    sodium_memzero(clientSk.data(), clientSk.size());

    // -- Step 5: Receive server's Ed25519 signature on the transcript --
    std::array<uint8_t, crypto_sign_BYTES> serverSig;
    if (!doRecvExact(fd_, serverSig.data(), serverSig.size(), HANDSHAKE_TIMEOUT_MS)) {
        output_->err("[secure-tcp] Failed to receive server signature");
        return false;
    }

    // Verify the detached signature over clientPk || serverPk
    std::array<uint8_t, X25519_PUBLIC_KEY_LEN * 2> transcript;
    std::copy(clientPk.begin(), clientPk.end(), transcript.begin());
    std::copy(serverPk.begin(), serverPk.end(), transcript.begin() + X25519_PUBLIC_KEY_LEN);

    if (crypto_sign_ed25519_verify_detached(
            serverSig.data(),
            transcript.data(), transcript.size(),
            publicKey_.data()) != 0) {
        output_->err("[secure-tcp] Handshake signature verification FAILED - peer is untrusted");
        return false;
    }

    output_->out("[secure-tcp] Handshake complete - peer authenticated");
    return true;
}

bool SecureTcpTransport::open() {
    if (opened_) {
        return fd_ >= 0 && !exhausted_;
    }
    opened_ = true;
    reconnectCount_ = 0;

    while (reconnectCount_ < MAX_RECONNECT_ATTEMPTS) {
        if (g_stopRequested.load()) {
            output_->err("[secure-tcp] connection cancelled (stop requested)");
            return false;
        }

        int connectResult = connectToHost(host_, port_, CONNECT_TIMEOUT_MS);
        if (connectResult == -2) {
            // Connection refused - don't retry, nothing is listening
            output_->err("[secure-tcp] connection refused to " + host_ + ":" + std::to_string(port_));
            return false;
        }

        if (connectResult < 0) {
            // Connection failed - apply backoff and retry (network issues)
            int delayMs = calculateReconnectDelayMs(reconnectCount_);
            reconnectCount_++;
            if (reconnectCount_ <= MAX_RECONNECT_ATTEMPTS) {
                output_->err("[secure-tcp] connection attempt " + std::to_string(reconnectCount_) +
                           "/" + std::to_string(MAX_RECONNECT_ATTEMPTS) +
                           " failed, retrying in " + std::to_string(delayMs) + "ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
            continue;
        }

        fd_ = connectResult;

        // Set recv timeout backstop
        struct timeval rtv{};
        rtv.tv_sec = 1;
        rtv.tv_usec = 0;
        (void)setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

        if (!performHandshake()) {
            // Handshake failed - AUTHENTICATION failure, not transient
            // Don't retry: wrong key, protocol error, or peer is untrusted
            close(fd_);
            fd_ = -1;
            return false;  // Immediate failure on auth/handshake errors
        }

        // Success - reset reconnect counter
        reconnectCount_ = 0;
        rawBuffer_.reserve(256);
        plaintextBuf_.reserve(256);
        return true;
    }

    output_->err("[secure-tcp] failed to establish TCP connection after " +
               std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts");
    return false;
}

bool SecureTcpTransport::isOpen() const noexcept {
    return opened_ && fd_ >= 0 && !exhausted_;
}

std::optional<std::string> SecureTcpTransport::readEncryptedLine() {
    // Data frame: [nonce(24) | length(2, big-endian) | ciphertext+tag(length)]
    // Uses rawBuffer_ for encrypted socket data only.

    while (true) {
        if (rawBuffer_.size() >= SECRETBOX_NONCEBYTES + 2) {
            uint16_t frameLen = (static_cast<uint16_t>(
                static_cast<unsigned char>(rawBuffer_[SECRETBOX_NONCEBYTES])) << 8)
                | static_cast<uint16_t>(static_cast<unsigned char>(rawBuffer_[SECRETBOX_NONCEBYTES + 1]));

            size_t totalFrame = SECRETBOX_NONCEBYTES + 2 + frameLen;
            if (rawBuffer_.size() >= totalFrame) {
                // Extract nonce
                std::array<uint8_t, SECRETBOX_NONCEBYTES> nonce;
                std::copy(rawBuffer_.begin(), rawBuffer_.begin() + SECRETBOX_NONCEBYTES, nonce.begin());

                // Extract ciphertext+tag
                const unsigned char* ct = reinterpret_cast<const unsigned char*>(
                    rawBuffer_.data() + SECRETBOX_NONCEBYTES + 2);

                // Decrypt
                std::vector<unsigned char> plaintext(frameLen);
                if (crypto_secretbox_xchacha20poly1305_open_easy(
                        plaintext.data(), ct, frameLen, nonce.data(), rxKey_.data()) != 0) {
                    output_->err("[secure-tcp] Frame decryption failed (tampered?)");
                    exhausted_ = true;
                    return std::nullopt;
                }

                rawBuffer_.erase(0, totalFrame);

                return std::string(reinterpret_cast<char*>(plaintext.data()),
                                   frameLen - SECRETBOX_MACBYTES);
            }
        }

        // Need more bytes from the socket. Poll for at most recvTimeoutUs_ so
        // the stop flag is re-checked promptly. Mirrors OTAHttpTransport: the
        // injected value is honoured literally (a tiny test value yields a
        // tiny poll), and we cap at a 100ms floor so a large injected value
        // still re-checks the stop flag at a reasonable cadence.
        int pollUs = recvTimeoutUs_;
        if (pollUs > 100000) pollUs = 100000;  // floor: never block > 100ms
        if (pollUs < 0) pollUs = static_cast<int>(RECV_TIMEOUT_US);

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd_, &readSet);
        struct timeval tv{};
        tv.tv_sec = pollUs / 1000000;
        tv.tv_usec = pollUs % 1000000;

        int ready = select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            exhausted_ = true;
            return std::nullopt;
        }
        if (ready == 0) {
            if (g_stopRequested.load()) {
                exhausted_ = true;
                return std::nullopt;
            }
            continue;
        }

        char buffer[1024];
        ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            exhausted_ = true;
            return std::nullopt;
        }

        rawBuffer_.append(buffer, static_cast<size_t>(n));

        if (rawBuffer_.size() > MAX_LINE_LEN + SECRETBOX_NONCEBYTES + 2 + SECRETBOX_MACBYTES) {
            output_->err("[secure-tcp] Raw buffer overflow - disconnecting");
            exhausted_ = true;
            return std::nullopt;
        }
    }
}

std::optional<std::string> SecureTcpTransport::nextLine() {
    if (!opened_ || fd_ < 0 || exhausted_) {
        return std::nullopt;
    }

    while (true) {
        // First, try to extract a complete line from buffered plaintext
        size_t end = plaintextBuf_.find('\n');
        if (end != std::string::npos) {
            std::string line(plaintextBuf_, 0, end);
            plaintextBuf_.erase(0, end + 1);
            return line;
        }

        // Read next encrypted frame and decrypt into plaintext buffer
        auto frame = readEncryptedLine();
        if (!frame.has_value()) {
            if (!plaintextBuf_.empty()) {
                std::string lastLine = plaintextBuf_;
                plaintextBuf_.clear();
                return lastLine;
            }
            return std::nullopt;
        }

        plaintextBuf_ += *frame;
        plaintextBuf_ += '\n';
    }
}

} // namespace pipeline
} // namespace vehicle_sim
