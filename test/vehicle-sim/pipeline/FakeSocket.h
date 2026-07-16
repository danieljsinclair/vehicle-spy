#pragma once

#include "vehicle-sim/pipeline/ISocket.h"
#include "vehicle-sim/pipeline/StopToken.h"

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vehicle_sim::pipeline {
namespace test {

// Scripted recv bytes for ONE connection. After all chunks are consumed, the
// next recv() returns 0 (peer close) — modeling a clean EOF.
//
// This is the ONLY network seam the fast TCPTransport suites use: there is no
// real socket, no real connect/recv/select, and no real loopback server. Every
// handshake byte the production code expects is scripted here.
struct FakeConnectScript {
    bool connectOk = true;
    std::deque<std::string> recvChunks;  // delivered in order; empty => EOF after
};

// A HOST-KEYED scriptable socket. connect(host, ...) pops the next queued script
// for THAT host (FIFO per host); a host with no queued script is treated as
// unreachable (connect returns -1). Host-keying makes the hunt tests
// deterministic regardless of how many old-IP attempts fire before a
// discovery-win switch — the discovered-IP connect always pops the discovered
// host's script, never an old-IP script.
class FakeSocket final : public ISocket {
public:
    // Queue the script for the NEXT connect() to `host`.
    void enqueue(const std::string& host, FakeConnectScript script) {
        scripts_[host].push_back(std::move(script));
    }

    int connect(const std::string& host, int /*port*/, const StopToken* /*stop*/) override {
        auto it = scripts_.find(host);
        if (it == scripts_.end() || it->second.empty()) {
            // No script for this host: unreachable / refused.
            connected_ = false;
            current_.clear();
            peerClosed_ = false;
            fd_ = -1;
            return -1;
        }
        FakeConnectScript s = std::move(it->second.front());
        it->second.pop_front();
        if (!s.connectOk) {
            connected_ = false;
            current_.clear();
            peerClosed_ = false;
            fd_ = -1;
            return -1;
        }
        ++connectCount_;
        connected_ = true;
        current_ = std::move(s.recvChunks);
        // An empty script is an immediate EOF (recvr returns 0 on first call).
        drained_ = current_.empty();
        peerClosed_ = false;
        fd_ = connectCount_;  // arbitrary non-negative fd
        return fd_;
    }

    ssize_t recv(char* buf, size_t len) override {
        if (!connected_) return -1;
        if (current_.empty()) {
            drained_ = true;     // last chunk consumed → peer close (EOF)
            peerClosed_ = true;
            return 0;  // peer close / EOF
        }
        std::string chunk = std::move(current_.front());
        current_.pop_front();
        const size_t n = std::min(chunk.size(), len);
        std::memcpy(buf, chunk.data(), n);
        if (current_.empty()) drained_ = true;  // this was the final chunk
        if (chunk.size() > len) {
            current_.push_front(chunk.substr(n));
            drained_ = false;  // residual bytes remain buffered
        }
        return static_cast<ssize_t>(n);
    }

    // A real socket reports readable when the peer has closed (EOF): the
    // subsequent recv returns 0. Mirror that so nextLine()'s select→recv→EOF
    // path triggers the hunt on peer-close instead of spinning on a silent
    // select(timeout) forever.
    int selectReadable(int /*timeoutUs*/) override {
        if (!connected_) return 0;
        if (!current_.empty() || peerClosed_ || drained_) return 1;
        return 0;
    }

    void close() noexcept override {
        connected_ = false;
        current_.clear();
        peerClosed_ = false;
        fd_ = -1;
    }

    bool setRecvTimeout(int /*ms*/) override { return connected_; }

    bool sendAll(std::string_view data) override {
        if (!connected_) return false;
        sent_.emplace_back(data);
        return true;
    }

    // Number of SUCCESSFUL connects (mirrors the old server acceptedCount()).
    int connectCount() const { return connectCount_; }
    const std::vector<std::string>& sent() const { return sent_; }

    // Catenate everything the transport sent (AUTH / ATI / ATHELO / ELM
    // commands) — mirrors the old LoopbackServer capture so handshake
    // assertions can stay identical in substance.
    std::string sentBlob() const {
        std::string s;
        for (const auto& x : sent_) s += x;
        return s;
    }

private:
    std::map<std::string, std::deque<FakeConnectScript>> scripts_;
    std::deque<std::string> current_;   // active connection's remaining recv chunks
    bool drained_ = false;   // all scripted chunks delivered (EOF pending on next recv)
    bool peerClosed_ = false;
    bool connected_ = false;
    int fd_ = -1;
    int connectCount_ = 0;
    std::vector<std::string> sent_;
};

// Standard AUTH/ATI/ATHELO handshake recv chunks (the server side).
inline std::deque<std::string> heloHandshakeChunks(
    std::string_view deviceIdHex = "0123456789ABCDEF0123456789ABCDEF") {
    const std::string ack = std::string("ACK DEVICE=ESP32-CAN FIRMWARE=0.1 DEVICEID=") +
                             std::string(deviceIdHex) + "\r\r>";
    return {"OK\r", "ESP32 CAN Bridge v0.1\r>", ack};
}

// A successful (handshake-only) connect script for the "raw" protocol.
inline FakeConnectScript handshakeConnect(
    std::string_view deviceIdHex = "0123456789ABCDEF0123456789ABCDEF") {
    return FakeConnectScript{true, heloHandshakeChunks(deviceIdHex)};
}

// A successful connect that also answers the ELM327 AT-init sequence.
inline FakeConnectScript elmHandshakeConnect(
    std::string_view deviceIdHex = "0123456789ABCDEF0123456789ABCDEF") {
    std::deque<std::string> c;
    c.push_back("OK\r");                       // AUTH response
    for (int i = 0; i < 5; ++i) c.push_back("OK\r");  // 5 ELM AT-init responses
    c.push_back("ESP32 CAN Bridge v0.1\r>");  // ATI response
    const std::string ack = std::string("ACK DEVICE=ESP32-CAN FIRMWARE=0.1 DEVICEID=") +
                            std::string(deviceIdHex) + "\r\r>";
    c.push_back(ack);                          // ATHELO ACK
    return FakeConnectScript{true, std::move(c)};
}

// A connect that answers AUTH with a rejection (no "OK").
inline FakeConnectScript authRejectedConnect(
    std::string_view rejection = "ERROR unauthorized\r") {
    return FakeConnectScript{true, {std::string(rejection)}};
}

// A connect that fails at the connect() stage (host unreachable / refused).
inline FakeConnectScript failConnect() { return FakeConnectScript{false, {}}; }

} // namespace test
} // namespace vehicle_sim::pipeline
