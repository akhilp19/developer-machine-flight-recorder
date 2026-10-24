#include "dmfr/store/store.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

using namespace std::chrono_literals;

TEST_CASE("Store can open and close", "[store]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_store.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE_FALSE(store.is_open());
    REQUIRE(store.open());
    REQUIRE(store.is_open());
    store.close();
    REQUIRE_FALSE(store.is_open());

    std::filesystem::remove(tmp);
}

TEST_CASE("Store can insert and query events", "[store]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_store_query.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE(store.open());

    auto event = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", nlohmann::json{{"command", "test"}});
    REQUIRE(store.insert(event));

    dmfr::store::Query query;
    query.limit = 10;
    auto results = store.query(query);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].event_id == event.event_id);
    REQUIRE(results[0].source == dmfr::Source::Build);

    store.close();
    std::filesystem::remove(tmp);
}

TEST_CASE("Store get_event_by_id returns the correct event", "[store]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_store_by_id.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE(store.open());

    auto event = dmfr::Event::create(dmfr::Source::System, "os_version", "", nlohmann::json{{"version", "14.0"}});
    REQUIRE(store.insert(event));

    auto found = store.get_event_by_id(event.event_id);
    REQUIRE(found.has_value());
    REQUIRE(found->event_id == event.event_id);

    auto missing = store.get_event_by_id("does-not-exist");
    REQUIRE_FALSE(missing.has_value());

    store.close();
    std::filesystem::remove(tmp);
}

TEST_CASE("Store query supports offset and ordering", "[store]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_store_paging.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE(store.open());

    for (int i = 0; i < 5; ++i) {
        auto event = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", nlohmann::json{{"i", i}});
        // Stagger timestamps to make ordering deterministic.
        event.timestamp += std::chrono::seconds(i);
        REQUIRE(store.insert(event));
    }

    dmfr::store::Query desc_query;
    desc_query.limit = 2;
    desc_query.offset = 1;
    auto desc = store.query(desc_query);
    REQUIRE(desc.size() == 2);
    REQUIRE(desc[0].payload["i"] == 3);

    dmfr::store::Query asc_query;
    asc_query.limit = 2;
    asc_query.offset = 1;
    asc_query.order = dmfr::store::Order::Ascending;
    auto asc = store.query(asc_query);
    REQUIRE(asc.size() == 2);
    REQUIRE(asc[0].payload["i"] == 1);

    store.close();
    std::filesystem::remove(tmp);
}

TEST_CASE("Store get_event_context returns surrounding events", "[store]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_store_context.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE(store.open());

    auto anchor = dmfr::Event::create(dmfr::Source::Build, "invocation", "proj", nlohmann::json{{"i", 0}});
    REQUIRE(store.insert(anchor));

    for (int i = 1; i <= 3; ++i) {
        auto event = dmfr::Event::create(dmfr::Source::System, "snapshot", "proj", nlohmann::json{{"i", i}});
        event.timestamp = anchor.timestamp + std::chrono::minutes(i);
        REQUIRE(store.insert(event));
    }

    // An unrelated event outside the project should be excluded when same_project is true.
    auto unrelated = dmfr::Event::create(dmfr::Source::System, "snapshot", "other", nlohmann::json{{"i", 99}});
    unrelated.timestamp = anchor.timestamp + std::chrono::minutes(1);
    REQUIRE(store.insert(unrelated));

    auto context = store.get_event_context(anchor, 10, 10, true, false);
    REQUIRE(context.size() == 3);

    store.close();
    std::filesystem::remove(tmp);
}

TEST_CASE("Store query latency for 7-day range stays under 2 seconds", "[store][performance]") {
    auto tmp = std::filesystem::temp_directory_path() / "dmfr_test_store_perf.db";
    std::filesystem::remove(tmp);

    dmfr::store::Store store(tmp);
    REQUIRE(store.open());

    std::mt19937 rng{42};
    std::uniform_int_distribution<int> source_dist(1, 4);
    std::uniform_int_distribution<int> type_dist(0, 4);
    const std::array<std::string, 5> types = {"invocation", "os_version", "dotfile_change", "commit", "hook"};

    auto now = std::chrono::system_clock::now();
    for (size_t i = 0; i < 10'000; ++i) {
        auto source = static_cast<dmfr::Source>(source_dist(rng));
        auto event = dmfr::Event::create(source, types[type_dist(rng)], "proj", nlohmann::json{{"i", i}});
        // Spread over the last 14 days so a 7-day query still returns a meaningful subset.
        event.timestamp = now - std::chrono::hours((i % 14) * 24 + (i % 24));
        REQUIRE(store.insert(event));
    }

    dmfr::store::Query query;
    query.from = now - std::chrono::hours(24 * 7);
    query.to = now;
    query.limit = 1'000;

    auto start = std::chrono::steady_clock::now();
    auto results = store.query(query);
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(!results.empty());
    REQUIRE(elapsed < 2s);

    store.close();
    std::filesystem::remove(tmp);
}
