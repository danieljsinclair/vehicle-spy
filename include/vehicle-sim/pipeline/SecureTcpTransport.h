// SecureTcpTransport.h — Authenticated encrypted TCP transport for CAN frames.
//
// Wraps a plain TCP connection with a Noise_XX-like handshake using libsodium:
//
//   1. HANDSHAKE (performed in open()):
//      a. Generate ephemeral X25519 keypair.
//      b. Send client ephemeral public key (32 bytes).
//      c. Receive server ephemeral public key (32 bytes).
//      d. Compute shared secret via crypto_kx (X25519 key exchange).
//      e. Server signs the handshake transcript with its Ed25519 identity key;
//         client verifies with the known OTA public key.
//      f. Derive session keys (tx/rx) from the shared secret via crypto_kdf.
//
//   2. DATA PHASE (nextLine()):
//      a. Read a ciphertext frame: [nonce(24) | ciphertext(N) | tag(16)].
//      b. Decrypt with crypto_secretbox_xchacha20poly1305_open.
//      c. Return the plaintext line.
//
//   3. INTEGRITY: every frame is authenticated (AEAD). Tampered frames are
//      rejected (decryption fails → transport returns nullopt / EOF).
//
// If the handshake fails (bad signature, timeout, protocol error), open()
// returns false and the transport is not usable. The peer is untrusted until
// the Ed25519 signature on the handshake verifies against the OTA public key.
//
// This transport is ONLY used when the CLI connects with --connect tcp:... and
// the adapter protocol is "secure". The existing TCPTransport (plaintext) is
// preserved for "raw" and "elm327" protocols. USB, BLE, demo, and file paths
// are unaffected.

#ifndef VEHICLE_SIM_PIPELINE_SECURE_TCP_TRANSPORT_H
#define VEHICLE_SIM_PIPELINE_SECURE_TCP_TRANSPORT_H

#include "vehicle-sim/pipeline/ITransport.h"
#include "vehicle-sim/pipeline/ITransportOutput.h"
#include "vehicle-sim/discovery/DiscoveryVerifier.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace vehicle_sim::pipeline {

class SecureTcpTransport final : public ITransport {
public:
    /**
     * @param host        IPv4/hostname of the CAN-bridge.
     * @param port        TCP port.
     * @param publicKey   Ed25519 public key for handshake verification
     *                    (the same OTA/discovery key).
     * @param output      Sink for status/error messages.
     * @param recvTimeoutUs  Max wait (microseconds) for a socket read in the
     *                       nextLine() poll loop before re-checking the stop
     *                       flag. Defaults to RECV_TIMEOUT_US (500ms) — the
     *                       production value. Injectable so tests can pass a
     *                       tiny value instead of sleeping for the poll floor.
     */
    SecureTcpTransport(
        std::string host,
        int port,
        const std::array<uint8_t, discovery::ED25519_PUBLIC_KEY_LEN>& publicKey,
        std::shared_ptr<ITransportOutput> output = std::make_shared<StdOut>(),
        int recvTimeoutUs = static_cast<int>(RECV_TIMEOUT_US)
    );

    ~SecureTcpTransport() override;

    SecureTcpTransport(const SecureTcpTransport&) = delete;
    SecureTcpTransport& operator=(const SecureTcpTransport&) = delete;
    SecureTcpTransport(SecureTcpTransport&&) = delete;
    SecureTcpTransport& operator=(SecureTcpTransport&&) = delete;

    bool open() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    std::optional<std::string> nextLine() override;

    //! Request that nextLine() return nullopt at the next opportunity.
    static void requestStop() noexcept;
    //! Reset the stop flag (for tests / repeated runs).
    static void resetStop() noexcept;

    // Wire format constants (public for utility function access)
    static constexpr size_t X25519_PUBLIC_KEY_LEN = 32;
    static constexpr size_t X25519_SECRET_KEY_LEN = 32;
    static constexpr size_t SESSION_KEY_LEN = 32;  // crypto_kx_SESSIONKEYBYTES
    static constexpr size_t NONCE_LEN = 24;         // crypto_secretbox_xchacha20poly1305_NONCEBYTES
    static constexpr size_t TAG_LEN = 16;           // crypto_secretbox_xchacha20poly1305_MACBYTES

    // Connection/reconnect constants (public for utility function access)
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;      // TCP connect timeout
    static constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 10000;   // Handshake (crypto) timeout
    static constexpr uint32_t RECV_TIMEOUT_US = 500000;       // Data phase recv timeout (0.5s)
    static constexpr int MAX_RECONNECT_ATTEMPTS = 10;        // Bounded reconnection attempts
    static constexpr int BASE_RECONNECT_DELAY_MS = 1000;      // Initial reconnect delay
    static constexpr int MAX_RECONNECT_DELAY_MS = 10000;      // Max exponential backoff
    static constexpr size_t MAX_LINE_LEN = 4096;

private:
    // ── Wire format ──────────────────────────────────────────────────────
    // Handshake: client_pubkey(32) | server_pubkey(32) | server_sig(64)
    // Data frame: nonce(24) | ciphertext(len) | tag(16)
    //   Total overhead per frame: 24 + 16 = 40 bytes.

    // Low-level helpers
    bool performHandshake();
    std::optional<std::string> readEncryptedLine();

    // If rawBuffer_ holds a complete encrypted frame, decrypt it, consume the
    // frame bytes, and return the plaintext line. Returns nullopt when no full
    // frame is buffered yet (caller should recv more), or nullopt with
    // exhausted_ set when the buffered frame fails authentication (tamper).
    std::optional<std::string> tryDecryptBufferedFrame();

    // Poll the socket for one recv() and append any bytes to rawBuffer_. Returns
    // true to indicate the caller should retry frame extraction (more bytes
    // arrived, a transient EINTR/timeout, or the stop flag is still clear);
    // returns false when the stream is terminal (disconnect, recv error, stop
    // requested, or raw buffer overflow) with exhausted_ set.
    bool pollRecvOrExhaust();

    std::string host_;
    int port_;
    std::array<uint8_t, discovery::ED25519_PUBLIC_KEY_LEN> publicKey_;
    std::shared_ptr<ITransportOutput> output_;
    int recvTimeoutUs_ = static_cast<int>(RECV_TIMEOUT_US);

    int fd_ = -1;
    bool opened_ = false;
    bool exhausted_ = false;
    // Reconnect state
    int reconnectCount_ = 0;

    // Session keys (derived after handshake)
    std::array<uint8_t, SESSION_KEY_LEN> txKey_{};  // client → server (unused for now; CAN is read-only)
    std::array<uint8_t, SESSION_KEY_LEN> rxKey_{};  // server → client

    // Raw encrypted bytes read from the socket (parsed by readEncryptedLine)
    std::string rawBuffer_;
    // Decrypted plaintext lines with '\n' delimiters (consumed by nextLine)
    std::string plaintextBuf_;
};

} // namespace vehicle_sim::pipeline

#endif // VEHICLE_SIM_PIPELINE_SECURE_TCP_TRANSPORT_H
