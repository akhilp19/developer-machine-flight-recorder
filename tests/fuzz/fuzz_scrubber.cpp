#include "dmfr/common/event.h"
#include "dmfr/scrubber/scrubber.h"

#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);

    // Test 1: plain string payload.
    {
        nlohmann::json payload;
        payload["value"] = input;

        auto event = dmfr::Event::create(
            dmfr::Source::Build,
            "output",
            "/project",
            std::move(payload));

        dmfr::scrubber::Scrubber scrubber;
        scrubber.scrub(event);
    }

    // Test 2: env-var-like object payload.
    {
        nlohmann::json payload;
        payload["PUBLIC_VAR"] = input;
        payload["GITHUB_TOKEN"] = input;
        payload["PASSWORD"] = input;
        payload["API_KEY"] = input;
        payload["PATH"] = input;
        payload["HOME"] = input;

        auto event = dmfr::Event::create(
            dmfr::Source::Environment,
            "env_snapshot",
            "/project",
            std::move(payload));

        dmfr::scrubber::Scrubber scrubber;
        scrubber.scrub(event);
    }

    // Test 3: nested payload with path-like keys.
    {
        nlohmann::json payload;
        payload["file"] = input;
        payload["path"] = input;
        payload["cwd"] = input;
        payload["nested"]["sha"] = input;
        payload["nested"]["hash"] = input;
        payload["array"] = {input, input + "_suffix"};

        auto event = dmfr::Event::create(
            dmfr::Source::Git,
            "commit",
            "/project",
            std::move(payload));

        dmfr::scrubber::Scrubber scrubber;
        scrubber.scrub(event);
    }

    // Test 4: try to parse input as JSON and scrub it if valid.
    {
        try {
            nlohmann::json payload = nlohmann::json::parse(input);
            auto event = dmfr::Event::create(
                dmfr::Source::Build,
                "output",
                "/project",
                std::move(payload));

            dmfr::scrubber::Scrubber scrubber;
            scrubber.scrub(event);
        } catch (...) {
            // Invalid JSON is expected; ignore.
        }
    }

    return 0;
}
