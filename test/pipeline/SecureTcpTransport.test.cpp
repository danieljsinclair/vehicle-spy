// SecureTcpTransport.test.cpp — Tests for the authenticated encrypted TCP transport.
//
// Tests cover: handshake failure with wrong key, successful handshake (with a
// mock server that speaks the protocol), encryption/decryption round-trip,
// tamper detection, and stop-flag behavior.
//
// The mock server is a minimal implementation of the server side of the
// handshake + data protocol, using real libsodium crypto.

#include <gtest/gtest.h>
#include "vehicle-sim/pipeline/SecureTcpTransport.h"
#include "vehicle-sim/pipeline/StopToken.h"
#include "vehicle-sim/discovery/DiscoveryVerifier.h"

#include <sodium.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace { std::shared_ptr<vehicle_sim::pipeline::StopToken> g_testStop = std::make_shared<vehicle_sim::pipeline::StopToken>(); }

using namespace vehicle_sim::pipeline;
using namespace vehicle_sim::discovery;

namespace {

// ── Mock encrypted server ──────────────────────────────────────────────────
// Implements the server side of the SecureTcpTransport handshake + data
// protocol so we can test the client against a real (local) peer.

class MockSecureServer {
public:
    struct Keypair {
        std::array<uint8_t, ED25519_PUBLIC_KEY_LEN> publicKey;
        std::array<uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> secretKey;
    };

    static Keypair generateKeypair() {
        Keypair kp{};
        EXPECT_EQ(crypto_sign_ed25519_keypair(kp.publicKey.data(), kp.secretKey.data()), 0);
        return kp;
    }

    bool start() {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) return false;
        int yes = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        if (listen(listenFd_, 1) != 0) return false;

        socklen_t len = sizeof(addr);
        getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        return port_ > 0;
    }

    ~MockSecureServer() {
        if (clientFd_ >= 0) close(clientFd_);
        if (listenFd_ >= 0) close(listenFd_);
    }

    int port() const { return port_; }

    int acceptClient(int timeoutMs = 3000) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(listenFd_, &rs);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int r = select(listenFd_ + 1, &rs, nullptr, nullptr, &tv);
        if (r <= 0) { clientFd_ = -1; return -1; }
        clientFd_ = accept(listenFd_, nullptr, nullptr);
        return clientFd_;
    }

    // Perform the server side of the handshake.
    // Returns true on success.
    bool performHandshake(const Keypair& serverIdentity) {
        if (clientFd_ < 0) return false;

        // 1. Receive client ephemeral public key
        std::array<uint8_t, 32> clientPk{};
        if (!recvExact(clientPk.data(), clientPk.size())) return false;

        // 2. Generate server ephemeral keypair
        std::array<uint8_t, 32> serverPk{};
        std::array<uint8_t, 32> serverSk{};
        if (crypto_kx_keypair(serverPk.data(), serverSk.data()) != 0) return false;

        // 3. Send server ephemeral public key
        if (!sendAll(serverPk.data(), serverPk.size())) return false;

        // 4. Compute session keys (server side: rx=client→tx, tx=server→rx)
        //    crypto_kx_server_session_keys(rx, tx, server_pk, server_sk, client_pk)
        std::array<uint8_t, 32> serverRxKey{};
        std::array<uint8_t, 32> serverTxKey{};
        if (crypto_kx_server_session_keys(
                serverRxKey.data(), serverTxKey.data(),
                serverPk.data(), serverSk.data(), clientPk.data()) != 0) return false;

        sodium_memzero(serverSk.data(), serverSk.size());

        // 5. Sign the transcript (clientPk || serverPk) with Ed25519 identity key
        std::array<uint8_t, 64> transcript{};
        std::copy(clientPk.begin(), clientPk.end(), transcript.begin());
        std::copy(serverPk.begin(), serverPk.end(), transcript.begin() + 32);

        std::array<uint8_t, crypto_sign_BYTES> sig{};
        EXPECT_EQ(crypto_sign_ed25519_detached(
            sig.data(), nullptr,
            transcript.data(), transcript.size(),
            serverIdentity.secretKey.data()), 0);

        // 6. Send signature
        if (!sendAll(sig.data(), sig.size())) return false;

        // Store the tx key for sending encrypted data
        txKey_ = serverTxKey;
        return true;
    }

    // Send an encrypted line to the client
    void sendEncryptedLine(const std::string& line) {
        if (clientFd_ < 0) return;

        // Frame: nonce(24) | length(2, big-endian) | ciphertext+tag
        size_t plaintextLen = line.size();
        size_t ctLen = plaintextLen + crypto_secretbox_xchacha20poly1305_MACBYTES;

        std::vector<uint8_t> frame;
        frame.resize(crypto_secretbox_xchacha20poly1305_NONCEBYTES + 2 + ctLen);

        // Nonce: use the counter
        uint64_t nonceCounter = txNonceCounter_++;
        std::memset(frame.data(), 0, crypto_secretbox_xchacha20poly1305_NONCEBYTES);
        std::memcpy(frame.data(), &nonceCounter, sizeof(nonceCounter));

        // Length (big-endian)
        frame[crypto_secretbox_xchacha20poly1305_NONCEBYTES] =
            static_cast<uint8_t>((ctLen >> 8) & 0xFF);
        frame[crypto_secretbox_xchacha20poly1305_NONCEBYTES + 1] =
            static_cast<uint8_t>(ctLen & 0xFF);

        // Encrypt
        crypto_secretbox_xchacha20poly1305_easy(
            frame.data() + crypto_secretbox_xchacha20poly1305_NONCEBYTES + 2,
            reinterpret_cast<const uint8_t*>(line.data()), plaintextLen,
            frame.data(), txKey_.data());

        sendAll(frame.data(), frame.size());
    }

    void closeClient() {
        if (clientFd_ >= 0) { close(clientFd_); clientFd_ = -1; }
    }

    // Build a valid encrypted frame for `line` exactly as sendEncryptedLine does,
    // but return the raw bytes instead of sending them. Used by tests that need
    // to deliver a frame in pieces or mutate it before sending.
    std::vector<uint8_t> buildEncryptedFrame(const std::string& line) {
        size_t plaintextLen = line.size();
        size_t ctLen = plaintextLen + crypto_secretbox_xchacha20poly1305_MACBYTES;

        std::vector<uint8_t> frame(
            crypto_secretbox_xchacha20poly1305_NONCEBYTES + 2 + ctLen);

        uint64_t nonceCounter = txNonceCounter_++;
        std::memset(frame.data(), 0, crypto_secretbox_xchacha20poly1305_NONCEBYTES);
        std::memcpy(frame.data(), &nonceCounter, sizeof(nonceCounter));

        frame[crypto_secretbox_xchacha20poly1305_NONCEBYTES] =
            static_cast<uint8_t>((ctLen >> 8) & 0xFF);
        frame[crypto_secretbox_xchacha20poly1305_NONCEBYTES + 1] =
            static_cast<uint8_t>(ctLen & 0xFF);

        crypto_secretbox_xchacha20poly1305_easy(
            frame.data() + crypto_secretbox_xchacha20poly1305_NONCEBYTES + 2,
            reinterpret_cast<const uint8_t*>(line.data()), plaintextLen,
            frame.data(), txKey_.data());

        return frame;
    }

    // Send an encrypted line in two TCP writes, with `splitAt` bytes in the
    // first write and the remainder in the second. Forces the client to
    // reassemble a single frame across two recv() calls.
    void sendEncryptedLineSplit(const std::string& line, size_t splitAt) {
        auto frame = buildEncryptedFrame(line);
        if (splitAt > frame.size()) splitAt = frame.size();
        if (splitAt > 0) sendAll(frame.data(), splitAt);
        // Give the client a beat to issue a recv() on the first chunk only.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (splitAt < frame.size()) {
            sendAll(frame.data() + splitAt, frame.size() - splitAt);
        }
    }

    // Send an encrypted line whose ciphertext has been tampered: the byte at
    // `flipOffset` (counted from the start of the ciphertext) is inverted.
    // Decryption must fail -> the client disconnects.
    void sendTamperedEncryptedLine(const std::string& line, size_t flipOffset) {
        auto frame = buildEncryptedFrame(line);
        const size_t ctStart = crypto_secretbox_xchacha20poly1305_NONCEBYTES + 2;
        const size_t ctLen = frame.size() - ctStart;
        if (ctLen > 0) {
            frame[ctStart + (flipOffset % ctLen)] ^= 0xFF;
        }
        sendAll(frame.data(), frame.size());
    }

    // Send arbitrary raw bytes on the client socket (no framing/crypto).
    // Used to flood the client's raw buffer past its overflow cap.
    void sendRawBytes(const uint8_t* data, size_t len) {
        sendAll(data, len);
    }

    // Receive exactly 32 bytes (a public key) from the client. Used by
    // partial-handshake tests that need to consume the client's ephemeral key
    // without completing the protocol.
    bool recvExactPublic(std::array<uint8_t, 32>& dst) {
        return recvExact(dst.data(), dst.size());
    }

    // Send exactly 32 raw bytes (e.g. an arbitrary ephemeral public key) to
    // the client, with no signature or further protocol bytes following.
    bool sendRawPublic(const std::array<uint8_t, 32>& src) {
        return sendAll(src.data(), src.size());
    }

private:
    bool sendAll(const uint8_t* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::send(clientFd_, data + sent, len - sent, 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    bool recvExact(uint8_t* dst, size_t len) {
        size_t got = 0;
        while (got < len) {
            ssize_t n = recv(clientFd_, dst + got, len - got, 0);
            if (n <= 0) return false;
            got += static_cast<size_t>(n);
        }
        return true;
    }

    int listenFd_ = -1;
    int clientFd_ = -1;
    int port_ = 0;
    std::array<uint8_t, 32> txKey_{};
    uint64_t txNonceCounter_ = 0;
};

} // namespace

// ── Test: handshake with correct key succeeds ──────────────────────────────

TEST(SecureTcpTransportTest, HandshakeSuccess_WithCorrectKey) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    // Run the server handshake on a thread
    std::atomic<bool> serverOk{false};
    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        serverOk = server.performHandshake(serverKp);
    });

    EXPECT_TRUE(t.open());
    serverThread.join();
    EXPECT_TRUE(serverOk);

    server.closeClient();
    EXPECT_FALSE(t.nextLine().has_value());
}

// ── Test: handshake with wrong key fails ───────────────────────────────────

TEST(SecureTcpTransportTest, HandshakeFails_WithWrongKey) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    auto clientKp = MockSecureServer::generateKeypair();  // different keypair

    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    // Client has the WRONG public key
    SecureTcpTransport t("127.0.0.1", server.port(), clientKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        // Server signs with its key, but client expects a different key
        (void)server.performHandshake(serverKp);
    });

    // open() must fail because signature verification fails
    EXPECT_FALSE(t.open());
    serverThread.join();

    server.closeClient();
}

// ── Test: encrypted data round-trip ────────────────────────────────────────

TEST(SecureTcpTransportTest, EncryptedDataRoundTrip) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));

        // Send some encrypted CAN-monitor lines
        server.sendEncryptedLine("118 3C 00 18 00 00 00 00 FF");
        server.sendEncryptedLine("108 00 00 00 90 01 00 00 00");
        server.sendEncryptedLine("297 00 00 10 00 00 00 00 00");

        // Close immediately: nextLine() returns nullopt on EOF (recv <= 0)
        // without waiting on the select poll timeout, so the client drains
        // all buffered frames and exits its read loop promptly.
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    std::vector<std::string> lines;
    while (auto line = t.nextLine()) {
        lines.push_back(*line);
    }

    serverThread.join();

    ASSERT_GE(lines.size(), 3u);
    EXPECT_EQ(lines[0], "118 3C 00 18 00 00 00 00 FF");
    EXPECT_EQ(lines[1], "108 00 00 00 90 01 00 00 00");
    EXPECT_EQ(lines[2], "297 00 00 10 00 00 00 00 00");
}

// ── Test: connection refused ────────────────────────────────────────────────

TEST(SecureTcpTransportTest, ConnectionRefused_OpenReturnsFalse) {
    ASSERT_GE(sodium_init(), 0);

    // Bind then close a port so connect() is refused
    int refuseFd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(refuseFd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int yes = 1;
    setsockopt(refuseFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ASSERT_EQ(bind(refuseFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(refuseFd, 1), 0);
    socklen_t len = sizeof(addr);
    getsockname(refuseFd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    close(refuseFd);

    auto kp = MockSecureServer::generateKeypair();
    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", port, kp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);
    EXPECT_FALSE(t.open());
    EXPECT_FALSE(t.isOpen());
}

// ── Test: clean disconnect returns nullopt ──────────────────────────────────

TEST(SecureTcpTransportTest, CleanDisconnect_ReturnsNullopt) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));
        // Close immediately without sending data
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    // Should get nullopt (EOF) without hanging
    EXPECT_FALSE(t.nextLine().has_value());
    EXPECT_FALSE(t.isOpen());

    serverThread.join();
}

// ── Test: requestStop terminates nextLine ───────────────────────────────────

TEST(SecureTcpTransportTest, RequestStop_TerminatesNextLine) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    // Tiny recv poll (1ms) so the stop flag is re-checked near-instantly,
    // instead of waiting the production 500ms poll floor.
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey,
                         std::make_shared<StdOut>(), /*recvTimeoutUs=*/1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));
        // Keep connection open but send no data. The server destructor closes
        // the listen/client sockets; we do not need to hold the thread open
        // with a sleep — requestStop() makes nextLine() return nullopt
        // regardless of server state.
    });

    ASSERT_TRUE(t.open());

    // Request stop from "signal handler"
    g_testStop->requestStop();

    // CONTRACT: requestStop() must make nextLine() return nullopt (how Ctrl+C
    // stops a live stream). Assert the contract, not wall-clock promptness — a
    // timing bound (EXPECT_LT(elapsed, N)) is a flake vector under full-suite
    // CPU contention (races scheduler jitter, not the stop logic). The
    // recvTimeoutUs=1000 injected above makes nextLine()'s poll re-check the
    // stop flag every ~1ms, so the return is prompt in practice; we just don't
    // ASSERT a specific wall-clock ceiling.
    auto r = t.nextLine();
    EXPECT_FALSE(r.has_value());

    serverThread.join();
    g_testStop->reset();
}

// ── Test: multiple lines in sequence ────────────────────────────────────────

TEST(SecureTcpTransportTest, MultipleLines_InSequence) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));

        // No inter-send pacing needed: frames are length-prefixed
        // ([nonce(24)|len(2)|ciphertext+tag]), so the client peels coalesced
        // frames correctly regardless of TCP segment merging.
        for (int i = 0; i < 10; ++i) {
            server.sendEncryptedLine("118 " + std::to_string(i) + " 00 00 00 00 00 00 00");
        }

        // Close immediately: the client's read loop drains all buffered frames
        // and returns nullopt on EOF (recv <= 0) without waiting on the select
        // poll timeout.
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    int count = 0;
    while (auto line = t.nextLine()) {
        EXPECT_EQ(line->substr(0, 4), "118 ");
        count++;
    }

    EXPECT_EQ(count, 10);
    serverThread.join();
}

// ============================================================
// S3776 refactor contracts — frame reassembly, tamper detection, overflow.
// Each locks an externally observable behaviour of readEncryptedLine/nextLine
// (input socket bytes -> returned plaintext / nullopt / open state), so the
// S3776 (cc28) + S134 decomposition cannot drift it. Reuses the loopback +
// libsodium mock infra above.
// ============================================================

TEST(SecureTcpTransportTest, SingleFrameSplitAcrossTwoSegments_Reassembled) {
    // One encrypted frame delivered in two TCP writes: nextLine() must
    // reassemble the partial frame across two recv() calls and return the
    // correct plaintext line. Distinct from coalesced-multiframe (already
    // locked by MultipleLines_InSequence), which only exercises whole frames.
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));
        // Split a single frame: first 12 bytes, then the rest.
        server.sendEncryptedLineSplit("118 3C 00 18 00 00 00 00 FF", 12);
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    auto line = t.nextLine();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "118 3C 00 18 00 00 00 00 FF");

    serverThread.join();
}

TEST(SecureTcpTransportTest, TamperedCiphertext_AfterHandshake_ReturnsNulloptAndCloses) {
    // A frame whose ciphertext/tag is flipped after the handshake: decryption
    // fails, nextLine() returns nullopt, and the transport becomes not-open
    // (exhausted). Locks the tamper-detection + disconnect path.
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));
        // Valid framing, but flip the first ciphertext byte.
        server.sendTamperedEncryptedLine("118 3C 00 18 00 00 00 00 FF", 0);
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    EXPECT_FALSE(t.nextLine().has_value());
    EXPECT_FALSE(t.isOpen());

    serverThread.join();
}

TEST(SecureTcpTransportTest, RawBufferOverflow_ReturnsNulloptAndCloses) {
    // A peer that floods raw bytes beyond the overflow cap (MAX_LINE_LEN +
    // nonce + len + mac) must cause nextLine() to return nullopt and the
    // transport to become not-open, rather than growing the buffer unbounded.
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    // Short poll so the client re-checks data promptly during the flood.
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey,
                         std::make_shared<StdOut>(), /*recvTimeoutUs=*/1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));

        // Send well over the overflow cap (MAX_LINE_LEN 4096 + 24 + 2 + 16).
        constexpr size_t kFloodLen = 8192;
        std::vector<uint8_t> garbage(kFloodLen, 0xAA);
        server.sendRawBytes(garbage.data(), garbage.size());
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    EXPECT_FALSE(t.nextLine().has_value());
    EXPECT_FALSE(t.isOpen());

    serverThread.join();
    g_testStop->reset();
}

// ============================================================
// Uncovered-path coverage (P2) — handshake early-failure branches,
// open() cancellation, and nextLine() final-line flush.
//
// Each test drives a REAL loopback connection to a MockSecureServer variant
// that aborts the protocol at a specific step, so the failure is exercised
// against production code paths (performHandshake / open / nextLine) rather
// than mocked. The CapturingOutput sink records err() calls so assertions
// target intent (e.g. "signature verification") without fragile exact strings.
// ============================================================

namespace {

// ITransportOutput that records every err() message, so tests can assert which
// failure path was taken by its distinguishing content (intent, not wording).
class CapturingOutput final : public ITransportOutput {
public:
    void out(const std::string& /*msg*/) override {}
    void err(const std::string& msg) override { messages_.push_back(msg); }

    bool errContains(std::string_view needle) const {
        for (const auto& m : messages_) {
            if (m.find(needle) != std::string::npos) return true;
        }
        return false;
    }
    const std::vector<std::string>& errors() const { return messages_; }

private:
    std::vector<std::string> messages_;
};

} // namespace

// Handshake: server accepts the TCP connection but never sends its ephemeral
// public key (closes early). The client's doRecvExact for the server pubkey
// times out → performHandshake returns false → open() fails. Covers the
// "Failed to receive server public key" branch and the handshake-failure
// (non-retry) path in open().
TEST(SecureTcpTransportTest, HandshakeFails_ServerNeverSendsPublicKey) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    auto out = std::make_shared<CapturingOutput>();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, out, 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        // Read the client's ephemeral public key (so the client's send
        // succeeds), then close without sending anything back.
        std::array<uint8_t, 32> sink{};
        std::ignore = server.recvExactPublic(sink);
        server.closeClient();
    });

    // open() must fail: handshake timed out waiting for the server pubkey.
    EXPECT_FALSE(t.open());
    // Intent: the failure was a handshake/key-exchange problem, not a refusal.
    EXPECT_TRUE(out->errContains("public key"));

    serverThread.join();
}

// Handshake: server sends its ephemeral public key but never sends the Ed25519
// signature. The client's doRecvExact for the signature times out →
// performHandshake returns false. Covers the "Failed to receive server
// signature" branch.
TEST(SecureTcpTransportTest, HandshakeFails_ServerNeverSendsSignature) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    auto out = std::make_shared<CapturingOutput>();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey, out, 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        // Exchange ephemeral keys with a VALID server keypair (so the client's
        // crypto_kx_client_session_keys succeeds), then close WITHOUT sending
        // the Ed25519 signature. The client's doRecvExact for the 64-byte
        // signature hits EOF → "Failed to receive server signature".
        std::array<uint8_t, 32> clientPk{};
        std::ignore = server.recvExactPublic(clientPk);
        std::array<uint8_t, 32> serverPk{};
        std::array<uint8_t, 32> serverSk{};
        ASSERT_EQ(crypto_kx_keypair(serverPk.data(), serverSk.data()), 0);
        ASSERT_TRUE(server.sendRawPublic(serverPk));
        server.closeClient();
    });

    EXPECT_FALSE(t.open());
    EXPECT_TRUE(out->errContains("signature"));

    serverThread.join();
}

// open() cancellation: if stop is requested before a connection is attempted,
// open() returns false and reports cancellation. Covers the stop-requested
// early-return in the connect loop. Uses a port with no listener so the
// stop signal is what terminates, not a successful connect.
TEST(SecureTcpTransportTest, OpenCancelled_WhenStopRequestedBeforeConnect) {
    ASSERT_GE(sodium_init(), 0);

    auto kp = MockSecureServer::generateKeypair();
    auto out = std::make_shared<CapturingOutput>();
    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", /*port=*/9, kp.publicKey, out, 1000, g_testStop);

    // Request stop before open() — the connect loop checks stop first.
    g_testStop->requestStop();

    EXPECT_FALSE(t.open());
    EXPECT_FALSE(t.isOpen());
    EXPECT_TRUE(out->errContains("cancel"));

    g_testStop->reset();
}

// Characterization: after the peer sends exactly one frame and then closes,
// nextLine() returns that frame and then nullopt (the recv() EOF sets
// exhausted_). Note this does NOT exercise the "plaintext remaining without a
// '\n'" flush branch in nextLine() — production unconditionally appends '\n'
// after every decrypted frame, so a single complete frame is returned via the
// normal newline-found path. (Whether that flush branch is reachable at all is
// a separate tech-arch question — it is out of scope for these tests.)
TEST(SecureTcpTransportTest, NextLine_ReturnsFrameThenNulloptOnPeerClose) {
    ASSERT_GE(sodium_init(), 0);

    auto serverKp = MockSecureServer::generateKeypair();
    MockSecureServer server;
    ASSERT_TRUE(server.start());

    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", server.port(), serverKp.publicKey,
                         std::make_shared<StdOut>(), 1000, g_testStop);

    std::thread serverThread([&] {
        ASSERT_GE(server.acceptClient(), 0);
        ASSERT_TRUE(server.performHandshake(serverKp));
        // One complete frame, then the peer disconnects.
        server.sendEncryptedLine("first");
        server.closeClient();
    });

    ASSERT_TRUE(t.open());

    auto first = t.nextLine();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, "first");
    // After the peer closed, the next read returns nullopt (EOF / exhausted).
    EXPECT_FALSE(t.nextLine().has_value());

    serverThread.join();
}

// nextLine() before open(): returns nullopt immediately. Covers the not-open
// early-return at the top of nextLine().
TEST(SecureTcpTransportTest, NextLine_ReturnsNulloptBeforeOpen) {
    ASSERT_GE(sodium_init(), 0);

    auto kp = MockSecureServer::generateKeypair();
    g_testStop->reset();
    SecureTcpTransport t("127.0.0.1", 9, kp.publicKey, std::make_shared<StdOut>(), 1000, g_testStop);

    // Never opened — nextLine() must short-circuit to nullopt.
    EXPECT_FALSE(t.nextLine().has_value());
    EXPECT_FALSE(t.isOpen());
}
