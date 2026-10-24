#include "dmfr/ipc/client.h"

#include "dmfr/common/errors.h"

#include <fmt/format.h>

#include <array>
#include <cstdint>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace dmfr::ipc {

namespace {

bool write_all(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd, data + written, len - written);
        if (n <= 0) {
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

bool read_exact(int fd, char* buffer, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::read(fd, buffer + total, len - total);
        if (n <= 0) {
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

Client::Client(std::string endpoint) : endpoint_(std::move(endpoint)) {}

Client::~Client() {
    disconnect();
}

bool Client::connect() {
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (endpoint_.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, endpoint_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }

    fd_ = fd;
    return true;
#else
    throw DmfrError("IPC client not implemented on this platform");
#endif
}

void Client::disconnect() {
    if (fd_ != -1) {
#if defined(__unix__) || defined(__APPLE__)
        ::close(fd_);
#endif
        fd_ = -1;
    }
}

std::optional<nlohmann::json> Client::send(const nlohmann::json& request) {
    if (fd_ == -1) {
        return std::nullopt;
    }

    std::string payload = request.dump();
    if (payload.size() > UINT32_MAX) {
        return std::nullopt;
    }

    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t net_len = __builtin_bswap32(len); // big-endian

    if (!write_all(fd_, reinterpret_cast<const char*>(&net_len), sizeof(net_len))) {
        return std::nullopt;
    }
    if (!write_all(fd_, payload.data(), payload.size())) {
        return std::nullopt;
    }

    uint32_t resp_len_net = 0;
    if (!read_exact(fd_, reinterpret_cast<char*>(&resp_len_net), sizeof(resp_len_net))) {
        return std::nullopt;
    }
    uint32_t resp_len = __builtin_bswap32(resp_len_net);

    std::string resp(resp_len, '\0');
    if (!read_exact(fd_, resp.data(), resp_len)) {
        return std::nullopt;
    }

    try {
        return nlohmann::json::parse(resp);
    } catch (...) {
        return std::nullopt;
    }
}

bool Client::is_connected() const {
    return fd_ != -1;
}

} // namespace dmfr::ipc
