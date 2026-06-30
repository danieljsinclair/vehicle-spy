#include "vehicle-sim/pipeline/USBTransport.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <iostream>

namespace vehicle_sim::pipeline {

namespace {

constexpr int READ_TIMEOUT_US = 500000;
constexpr std::size_t MAX_PENDING_LEN = 4096;

bool setNonBlocking(int fd) noexcept {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

USBTransport::USBTransport(std::string_view port, int baud, std::shared_ptr<ITransportOutput> output,
                           std::shared_ptr<StopToken> stop)
    : port_(port)
    , baud_(baud)
    , output_(std::move(output))
    , stop_(std::move(stop)) {
}

USBTransport::~USBTransport() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool USBTransport::open() {
    if (opened_) {
        return fd_ >= 0 && !exhausted_;
    }
    opened_ = true;

    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        output_->err("[usb] Failed to open " + port_ + ": " + std::strerror(errno));
        return false;
    }

    termios attrs{};
    if (tcgetattr(fd_, &attrs) != 0) {
        output_->err("[usb] Failed to read termios for " + port_ + ": " + std::strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }

    cfmakeraw(&attrs);

#ifdef __APPLE__
    attrs.c_ispeed = baud_;
    attrs.c_ospeed = baud_;
#else
    if (cfsetispeed(&attrs, static_cast<speed_t>(baud_)) != 0 ||
        cfsetospeed(&attrs, static_cast<speed_t>(baud_)) != 0) {
        output_->err("[usb] Failed to set baud rate for " + port_ + ": " + std::strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
#endif

    attrs.c_cflag |= (CLOCAL | CREAD);
    attrs.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    attrs.c_cflag |= CS8;
    attrs.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    attrs.c_cflag &= static_cast<tcflag_t>(~PARENB);
#ifdef CRTSCTS
    attrs.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
    attrs.c_cc[VMIN] = 0;
    attrs.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &attrs) != 0) {
        output_->err("[usb] Failed to configure " + port_ + ": " + std::strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (!setNonBlocking(fd_)) {
        output_->err("[usb] Failed to set non-blocking mode for " + port_ + ": " + std::strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }

    pending_.reserve(256);
    output_->out("[usb] Monitoring " + port_ + " at " + std::to_string(baud_) + " 8N1");
    return true;
}

bool USBTransport::isOpen() const noexcept {
    return opened_ && fd_ >= 0 && !exhausted_;
}

std::optional<std::string> USBTransport::nextLine() {
    if (!opened_ || fd_ < 0 || exhausted_) {
        return std::nullopt;
    }

    while (true) {
        if (stop_->stopRequested()) {
            exhausted_ = true;
            return std::nullopt;
        }

        if (const std::size_t end = pending_.find_first_of("\r\n"); end != std::string::npos) {
            std::string line(pending_, 0, end);
            pending_.erase(0, end + 1);
            return line;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(fd_, &readSet);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = READ_TIMEOUT_US;

        const int ready = select(fd_ + 1, &readSet, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            output_->err("[usb] Select failed on " + port_ + ": " + std::strerror(errno));
            exhausted_ = true;
            return std::nullopt;
        }

        if (stop_->stopRequested()) {
            exhausted_ = true;
            return std::nullopt;
        }

        if (ready == 0) {
            continue;
        }

        std::array<char, 256> buffer;
        const ssize_t n = ::read(fd_, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            output_->err("[usb] Read failed on " + port_ + ": " + std::strerror(errno));
            exhausted_ = true;
            return std::nullopt;
        }
        if (n == 0) {
            exhausted_ = true;
            return std::nullopt;
        }

        pending_.append(buffer.data(), static_cast<std::size_t>(n));
        if (pending_.size() > MAX_PENDING_LEN) {
            pending_.erase(0, pending_.size() - MAX_PENDING_LEN);
        }
    }
}

} // namespace vehicle_sim::pipeline
