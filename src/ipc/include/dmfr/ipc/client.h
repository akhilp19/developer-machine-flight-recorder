#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace dmfr::ipc {

class Client {
public:
    explicit Client(std::string endpoint);
    ~Client();

    bool connect();
    void disconnect();

    std::optional<nlohmann::json> send(const nlohmann::json& request);

    bool is_connected() const;

private:
    std::string endpoint_;
    int fd_{-1};
};

} // namespace dmfr::ipc
