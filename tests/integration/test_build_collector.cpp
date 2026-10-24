#include "dmfr/collectors/build_collector.h"
#include "dmfr/build/build.h"
#include "dmfr/common/paths.h"
#include "dmfr/store/store.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

static fs::path find_cli_binary() {
    fs::path test_dir = fs::current_path();
    fs::path candidate = test_dir / ".." / ".." / "bin" / "dmfr";
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
        return fs::canonical(candidate, ec);
    }
    return "dmfr";
}

TEST_CASE("Build telemetry wrapper captures a wrapped command", "[collectors][build]") {
    // Use an isolated HOME directory so wrappers and store stay inside it.
    fs::path tmp_home = fs::temp_directory_path() / "dmfr_build_test_home";
    std::error_code ec;
    fs::remove_all(tmp_home, ec);
    fs::create_directories(tmp_home, ec);

    std::string orig_home = std::getenv("HOME") ? std::getenv("HOME") : "";
    ::setenv("HOME", tmp_home.string().c_str(), 1);

    fs::path cli_bin = find_cli_binary().parent_path();
    std::string orig_path = std::getenv("PATH") ? std::getenv("PATH") : "";

    // Install wrappers using the CLI.
    std::string install_cmd = cli_bin.string() + "/dmfr install-wrappers";
    int rc = std::system(install_cmd.c_str());
    REQUIRE(rc == 0);

    fs::path wrapper_dir = dmfr::paths::bin_dir();
    REQUIRE(fs::exists(wrapper_dir / "make"));

    // Create a fake `make` binary in a separate directory.
    fs::path fake_bin = tmp_home / "fake_bin";
    fs::create_directories(fake_bin, ec);
    {
        std::ofstream out(fake_bin / "make");
        out << "#!/bin/sh\nif [ \"$1\" = --version ]; then echo fake-make-1; exit 0; fi\necho 'fake make'\nexit \"${DMFR_TEST_FAIL:-0}\"\n";
    }
    fs::permissions(fake_bin / "make",
                      fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                      fs::perm_options::add,
                      ec);

    // PATH = wrapper_dir:fake_bin:cli_bin:original
    std::string test_path = wrapper_dir.string() + ":" + fake_bin.string() + ":" + cli_bin.string() + ":" + orig_path;
    ::setenv("PATH", test_path.c_str(), 1);

    // Run the wrapped make.
    rc = std::system("make hello");
    // The fake make ignores arguments, so it should exit 0.
    REQUIRE(rc == 0);

    // Wait for the wrapper to send the event.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Query the store.
    dmfr::store::Store store(dmfr::paths::database_path());
    REQUIRE(store.open());

    dmfr::store::Query query;
    query.source = dmfr::Source::Build;
    query.type = "invocation";
    query.limit = 10;

    std::vector<dmfr::Event> events;
    for (int i = 0; i < 30 && events.empty(); ++i) {
        events = store.query(query);
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    REQUIRE_FALSE(events.empty());
    REQUIRE(events.front().source == dmfr::Source::Build);
    REQUIRE(events.front().type == "invocation");
    REQUIRE(events.front().payload.value("command", "") == "make");
    REQUIRE(events.front().payload.value("exit_code", -1) == 0);

    auto snapshot_id = events.front().payload.value("snapshot_id", "");
    REQUIRE_FALSE(snapshot_id.empty());
    auto snapshot = store.get_snapshot_by_id(snapshot_id);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->notes["capture_context"] == "build_process_before_execution");
    REQUIRE(snapshot->env_vars.at("PATH").find(wrapper_dir.string()) == std::string::npos);
    REQUIRE(events.front().payload["executable"] == "~/fake_bin/make");
    REQUIRE(events.front().payload["args"] == nlohmann::json::array({"hello"}));
    REQUIRE(snapshot->env_vars.at("PATH").find("/.dmfr/bin") == std::string::npos);
    // Direct recording with installed wrappers must not recurse. The same argv
    // must match the earlier successful wrapped invocation.
    ::setenv("DMFR_TEST_FAIL", "7", 1);
    const auto direct_command = cli_bin.string() + "/dmfr record -- make hello";
    rc = std::system(direct_command.c_str());
    ::unsetenv("DMFR_TEST_FAIL");
    REQUIRE(rc == 7 * 256);
    REQUIRE(store.query(query).size() == 2);
    dmfr::scrubber::Scrubber scrubber;
    auto report = dmfr::build::diagnose(store, scrubber,
        fs::weakly_canonical(fs::current_path()).string());
    REQUIRE(report["status"] == "compared");
    REQUIRE(report["diff"]["env_vars"]["DMFR_TEST_FAIL"]["status"] == "added");
    store.close();

    // Cleanup.
    ::setenv("HOME", orig_home.c_str(), 1);
    ::setenv("PATH", orig_path.c_str(), 1);
    fs::remove_all(tmp_home, ec);
}
