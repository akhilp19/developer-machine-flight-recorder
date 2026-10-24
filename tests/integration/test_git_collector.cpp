#include "dmfr/collectors/git_collector.h"
#include "dmfr/common/paths.h"
#include "dmfr/common/process.h"
#include "dmfr/store/store.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

static fs::path find_cli_binary() {
    // Tests run from <build>/tests/integration, so the CLI is at <build>/bin/dmfr.
    fs::path test_dir = fs::current_path();
    fs::path candidate = test_dir / ".." / ".." / "bin" / "dmfr";
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
        return fs::canonical(candidate, ec);
    }
    return "dmfr";
}

TEST_CASE("Git commit hook produces an event in the store", "[collectors][git]") {
    // Use an isolated HOME directory so we don't touch the user's real store.
    fs::path tmp_home = fs::temp_directory_path() / "dmfr_test_home";
    fs::path tmp_repo = fs::temp_directory_path() / "dmfr_test_repo";

    std::error_code ec;
    fs::remove_all(tmp_home, ec);
    fs::remove_all(tmp_repo, ec);
    fs::create_directories(tmp_home, ec);
    fs::create_directories(tmp_repo, ec);

    std::string orig_home = std::getenv("HOME") ? std::getenv("HOME") : "";
    ::setenv("HOME", tmp_home.string().c_str(), 1);

    // Initialize a git repository.
    REQUIRE(dmfr::process::foreground({"git", "-C", tmp_repo.string(), "init"}) == 0);
    REQUIRE(dmfr::process::foreground({"git", "-C", tmp_repo.string(), "config", "user.email", "test@example.com"}) == 0);
    REQUIRE(dmfr::process::foreground({"git", "-C", tmp_repo.string(), "config", "user.name", "Test User"}) == 0);

    // Install the DMFR hook.
    REQUIRE(dmfr::collectors::GitCollector::install_hooks(tmp_repo));
    REQUIRE(fs::exists(tmp_repo / ".git" / "hooks" / "post-commit"));

    // Prepare PATH so the hook can find the dmfr binary.
    fs::path cli_bin = find_cli_binary().parent_path();
    std::string orig_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    std::string test_path = cli_bin.string() + ":" + orig_path;
    ::setenv("PATH", test_path.c_str(), 1);

    // Make a commit.
    {
        std::ofstream out(tmp_repo / "file.txt");
        out << "hello\n";
    }
    REQUIRE(dmfr::process::foreground({"git", "-C", tmp_repo.string(), "add", "file.txt"}) == 0);
    REQUIRE(dmfr::process::foreground({"git", "-C", tmp_repo.string(), "-c", "core.hooksPath=.git/hooks", "commit", "-m", "test commit"}) == 0);

    // Give the hook a moment to finish.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Query the store.
    dmfr::store::Store store(dmfr::paths::database_path());
    REQUIRE(store.open());

    dmfr::store::Query query;
    query.source = dmfr::Source::Git;
    query.type = "commit";
    query.limit = 10;

    std::vector<dmfr::Event> events;
    for (int i = 0; i < 30 && events.empty(); ++i) {
        events = store.query(query);
        if (events.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    REQUIRE_FALSE(events.empty());
    REQUIRE(events.front().source == dmfr::Source::Git);
    REQUIRE(events.front().type == "commit");
    REQUIRE_FALSE(events.front().payload.value("sha", "").empty());

    store.close();

    // Cleanup.
    ::setenv("HOME", orig_home.c_str(), 1);
    ::setenv("PATH", orig_path.c_str(), 1);
    fs::remove_all(tmp_home, ec);
    fs::remove_all(tmp_repo, ec);
}
