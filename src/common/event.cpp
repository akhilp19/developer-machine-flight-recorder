#include "dmfr/common/event.h"

#include <random>
#include <sstream>

namespace dmfr {

std::string generate_uuid() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<int> dist(0, 15);

    const char* hex = "0123456789abcdef";
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) {
            ss << '-';
        }
        ss << hex[dist(rng)];
    }
    return ss.str();
}

std::string to_string(Source source) {
    switch (source) {
        case Source::Git: return "git";
        case Source::Build: return "build";
        case Source::System: return "system";
        case Source::Environment: return "environment";
        case Source::Plugin: return "plugin";
        default: return "unknown";
    }
}

std::string to_string(Sensitivity sensitivity) {
    switch (sensitivity) {
        case Sensitivity::Public: return "public";
        case Sensitivity::Private: return "private";
        case Sensitivity::Scrubbed: return "scrubbed";
        default: return "public";
    }
}

Event Event::create(Source source,
                    std::string type,
                    std::string project_id,
                    nlohmann::json payload) {
    Event event;
    event.event_id = generate_uuid();
    event.timestamp = std::chrono::system_clock::now();
    event.source = source;
    event.type = std::move(type);
    event.project_id = std::move(project_id);
    event.payload = std::move(payload);
    return event;
}

} // namespace dmfr
