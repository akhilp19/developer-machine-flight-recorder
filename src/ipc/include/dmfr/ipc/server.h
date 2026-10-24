#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace dmfr::ipc {

using RequestHandler = std::function<nlohmann::json(const nlohmann::json&)>;

class Server {
public:
    explicit Server(std::string endpoint);
    ~Server();

    bool start();
    void stop();
    bool is_running() const;

    void set_handler(RequestHandler handler);

private:
    std::string endpoint_;
    RequestHandler handler_;
    int fd_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    void run();
    void handle_client(int client_fd);
};

} // namespace dmfr::ipc
