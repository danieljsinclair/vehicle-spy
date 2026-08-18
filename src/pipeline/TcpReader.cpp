#include "vehicle-sim/pipeline/TcpReader.h"

#include <array>
#include <cerrno>
#include <cstring>

namespace vehicle_sim::pipeline {

namespace {
constexpr std::size_t MAX_PENDING_LEN = 4096;

// Failure-notification helpers for nextLine(). Each absorbs the
// "if (onFailure_)" guard so nextLine() never nests more than 3 levels and
// its Cognitive Complexity stays within Sonar's threshold. The compiler
// inlines these trivially — zero runtime overhead.
inline void notifySelectError(const ReadFailureCallback& cb) {
    if (cb) cb(ReadFailureKind::SelectError);
}
inline void notifyStopRequested(const ReadFailureCallback& cb) {
    if (cb) cb(ReadFailureKind::StopRequested);
}
// Returns true if the read loop should continue (reconnected).
inline bool notifyRecvFailure(const ReadFailureCallback& cb) {
    return cb && cb(ReadFailureKind::RecvFailure);
}
} // namespace

TcpReader::TcpReader(std::shared_ptr<ISocket> socket,
                     std::shared_ptr<StopToken> stop,
                     int readTimeoutUs,
                     ReadFailureCallback onFailure)
    : socket_(std::move(socket))
    , stop_(std::move(stop))
    , readTimeoutUs_(readTimeoutUs > 0 ? readTimeoutUs : 500000)
    , onFailure_(std::move(onFailure))
{
}

bool TcpReader::shouldStop() const {
    return stop_->stopRequested();
}

std::optional<std::string> TcpReader::takeBufferedLine() {
    const std::size_t end = pending_.find_first_of("\r\n");
    if (end == std::string::npos) {
        return std::nullopt;
    }
    std::string line(pending_, 0, end);
    pending_.erase(0, end + 1);
    return line;
}

int TcpReader::selectReady() const {
    const int pollUs = std::min(readTimeoutUs_, 1000);
    return socket_->selectReadable(pollUs);
}

ssize_t TcpReader::readSocketIntoPending() {
    std::array<char, 256> buffer;
    ssize_t n = socket_->recv(buffer.data(), buffer.size());
    if (n > 0) {
        pending_.append(buffer.data(), static_cast<std::size_t>(n));
        if (pending_.size() > MAX_PENDING_LEN) {
            pending_.clear();
        }
    }
    return n;
}

std::optional<std::string> TcpReader::nextLine() {
    if (auto line = takeBufferedLine()) {
        return *line;
    }

    for (;;) {
        const int ready = selectReady();
        if (ready < 0) {
            if (errno == EINTR) continue;  // signal interrupted select — retry
            notifySelectError(onFailure_); // genuine select error → notify owner
            return std::nullopt;
        }
        if (ready == 0) {
            if (shouldStop()) {
                notifyStopRequested(onFailure_); // stop during silent poll
                return std::nullopt;
            }
            continue;                      // poll again (no data yet)
        }

        if (ssize_t n = readSocketIntoPending(); n <= 0) {
            if (notifyRecvFailure(onFailure_)) continue; // reconnected
            return std::nullopt;
        }

        const std::size_t end = pending_.find_first_of("\r\n");
        if (end != std::string::npos) {
            std::string line(pending_, 0, end);
            pending_.erase(0, end + 1);
            return line;
        }
    }
}

} // namespace vehicle_sim::pipeline
