#include "dmfr/collectors/build_collector.h"

namespace dmfr::collectors {

bool BuildCollector::initialize() {
    return true;
}

void BuildCollector::shutdown() {}

bool BuildCollector::is_available() const {
    return true;
}

std::optional<Event> BuildCollector::on_hook(const std::string& hook_name,
                                             const nlohmann::json& data) {
    std::string type = hook_name.empty() ? "invocation" : hook_name;
    if (type != "invocation") {
        return std::nullopt;
    }
    return make_build_event(data);
}

Event BuildCollector::make_build_event(const nlohmann::json& data) {
    nlohmann::json payload = data;
    if (!payload.contains("command")) {
        payload["command"] = "";
    }
    if (!payload.contains("args")) {
        payload["args"] = nlohmann::json::array();
    }
    if (!payload.contains("cwd")) {
        payload["cwd"] = "";
    }
    if (!payload.contains("start_time")) {
        payload["start_time"] = 0;
    }
    if (!payload.contains("duration_ms")) {
        payload["duration_ms"] = 0;
    }
    if (!payload.contains("exit_code")) {
        payload["exit_code"] = 0;
    }
    if (!payload.contains("stdout")) {
        payload["stdout"] = "";
    }
    if (!payload.contains("stderr")) {
        payload["stderr"] = "";
    }

    std::string project_id = payload.value("cwd", "");
    auto event = Event::create(Source::Build, "invocation", project_id, std::move(payload));
    event.correlation_keys = {"command"};
    return event;
}

} // namespace dmfr::collectors
