#pragma once

#include <stdexcept>
#include <string>

namespace dmfr {

class DmfrError : public std::runtime_error {
public:
    explicit DmfrError(const std::string& message)
        : std::runtime_error(message) {}
};

class IOError : public DmfrError {
public:
    explicit IOError(const std::string& message) : DmfrError(message) {}
};

class ConfigError : public DmfrError {
public:
    explicit ConfigError(const std::string& message) : DmfrError(message) {}
};

} // namespace dmfr
