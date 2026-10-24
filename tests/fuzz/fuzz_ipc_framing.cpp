#include "dmfr/common/event.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

// Reproduce the framing parser logic used by IPC client/server for fuzzing.
bool parse_framed_message(const std::string& input, std::string& output) {
    if (input.size() < sizeof(uint32_t)) {
        return false;
    }

    uint32_t net_len = 0;
    std::memcpy(&net_len, input.data(), sizeof(net_len));
    uint32_t len = __builtin_bswap32(net_len);

    constexpr uint32_t max_len = 16 * 1024 * 1024;
    if (len > max_len) {
        return false;
    }

    if (input.size() < sizeof(uint32_t) + len) {
        return false;
    }

    output.assign(input.data() + sizeof(uint32_t), len);
    return true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);

    std::string payload;
    if (parse_framed_message(input, payload)) {
        try {
            nlohmann::json j = nlohmann::json::parse(payload, nullptr, false);
            (void)j.is_discarded();
        } catch (...) {
            // Invalid JSON payload is expected; ignore.
        }
    }

    return 0;
}
