#include "dmfr/ipc/server.h"

#include <fmt/format.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace dmfr::ipc {

namespace {
#if defined(__unix__) || defined(__APPLE__)

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

#endif
} // namespace

Server::Server(std::string endpoint) : endpoint_(std::move(endpoint)) {}

Server::~Server() {
    stop();
}

bool Server::start() {
#if defined(__unix__) || defined(__APPLE__)
    if (running_) {
        return true;
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    // Remove stale socket file.
    std::error_code ec;
    std::filesystem::remove(endpoint_, ec);

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (endpoint_.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, endpoint_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }

    if (::listen(fd, 8) < 0) {
        ::close(fd);
        std::error_code ec2;
        std::filesystem::remove(endpoint_, ec2);
        return false;
    }

    fd_ = fd;
    running_ = true;
    accept_thread_ = std::thread(&Server::run, this);
    return true;
#else
    return false;
#endif
}

void Server::stop() {
    running_ = false;

    if (fd_ != -1) {
#if defined(__unix__) || defined(__APPLE__)
        // Connect to ourselves to unblock accept().
        int wakeup_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (wakeup_fd >= 0) {
            struct sockaddr_un addr {};
            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, endpoint_.c_str(), sizeof(addr.sun_path) - 1);
            ::connect(wakeup_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
            ::close(wakeup_fd);
        }
        ::close(fd_);
#endif
        fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::error_code ec;
    std::filesystem::remove(endpoint_, ec);
}

bool Server::is_running() const {
    return running_;
}

void Server::set_handler(RequestHandler handler) {
    handler_ = std::move(handler);
}

void Server::run() {
#if defined(__unix__) || defined(__APPLE__)
    while (running_) {
        int client_fd = ::accept(fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        handle_client(client_fd);
        ::close(client_fd);
    }
#endif
}

void Server::handle_client(int client_fd) {
#if defined(__unix__) || defined(__APPLE__)
    uint32_t len_net = 0;
    if (!read_exact(client_fd, reinterpret_cast<char*>(&len_net), sizeof(len_net))) {
        return;
    }
    uint32_t len = __builtin_bswap32(len_net);

    constexpr size_t max_request = 16 * 1024 * 1024; // 16 MB
    if (len > max_request) {
        return;
    }

    std::string request_str(len, '\0');
    if (!read_exact(client_fd, request_str.data(), len)) {
        return;
    }

    nlohmann::json request;
    try {
        request = nlohmann::json::parse(request_str);
    } catch (...) {
        return;
    }

    nlohmann::json response = handler_ ? handler_(request) : nlohmann::json{{"error", "no handler"}};
    std::string resp_str = response.dump();

    uint32_t resp_len = static_cast<uint32_t>(resp_str.size());
    uint32_t resp_len_net = __builtin_bswap32(resp_len);

    if (!write_all(client_fd, reinterpret_cast<const char*>(&resp_len_net), sizeof(resp_len_net))) {
        return;
    }
    write_all(client_fd, resp_str.data(), resp_str.size());
#else
    (void)client_fd;
#endif
}

} // namespace dmfr::ipc
