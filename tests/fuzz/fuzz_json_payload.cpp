#include "dmfr/common/event.h"

#include <cstdint>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);

    // Fuzz JSON parsing of payloads, which is a common untrusted input path.
    try {
        nlohmann::json payload = nlohmann::json::parse(input, nullptr, false);
        if (!payload.is_discarded()) {
            auto event = dmfr::Event::create(
                dmfr::Source::Build,
                "output",
                "/project",
                std::move(payload));

            // Serializing back validates round-trip safety.
            (void)event.payload.dump();
        }
    } catch (...) {
        // Invalid JSON is expected; ignore.
    }

    return 0;
}
