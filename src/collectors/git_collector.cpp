#include "dmfr/collectors/git_collector.h"

#include <fmt/format.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace dmfr::collectors {

GitCollector::GitCollector(std::filesystem::path repo_path)
    : repo_path_(std::move(repo_path)) {}

bool GitCollector::initialize() {
    std::error_code ec;
    if (std::filesystem::exists(repo_path_ / ".git", ec)) {
        available_ = true;
    }
    return available_;
}

void GitCollector::shutdown() {
    available_ = false;
}

bool GitCollector::is_available() const {
    return available_;
}

std::optional<Event> GitCollector::on_hook(const std::string& hook_name,
                                           const nlohmann::json& data) {
    if (hook_name == "post-commit") {
        return make_commit_event(data);
    }
    if (hook_name == "post-checkout" || hook_name == "post-rewrite") {
        return make_status_event();
    }
    return std::nullopt;
}

bool GitCollector::install_hooks(const fs::path& repo_path) {
    std::error_code ec;
    if (!fs::exists(repo_path / ".git", ec)) {
        return false;
    }

    fs::path hooks_dir = repo_path / ".git" / "hooks";
    if (!fs::exists(hooks_dir, ec)) {
        return false;
    }

    fs::path post_commit = hooks_dir / "post-commit";

    const std::string repo_str = repo_path.string();
    std::string script = fmt::format(R"sh(#!/bin/sh
# DMFR Git hook - installed by dmfr init
REPO="{}"
SHA=$(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo "")
BRANCH=$(git -C "$REPO" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")
MESSAGE=$(git -C "$REPO" log -1 --pretty=%B 2>/dev/null || echo "")

# Try to locate dmfr next to this script's git binary, then PATH
DMFR=""
THIS_GIT=$(command -v git)
if [ -n "$THIS_GIT" ]; then
    BINDIR=$(dirname "$THIS_GIT")
    if [ -x "$BINDIR/dmfr" ]; then
        DMFR="$BINDIR/dmfr"
    fi
fi
if [ -z "$DMFR" ]; then
    DMFR=$(command -v dmfr)
fi

if [ -n "$DMFR" ] && [ -x "$DMFR" ]; then
    "$DMFR" hook git --repo "$REPO" --type post-commit --sha "$SHA" --branch "$BRANCH" --message "$MESSAGE" >/dev/null 2>&1 &
fi
)sh", repo_str);

    {
        std::ofstream out(post_commit, std::ios::out | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << script;
        if (!out.good()) {
            return false;
        }
    }

    fs::permissions(post_commit,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add,
                    ec);
    if (ec) {
        return false;
    }

    return true;
}

Event GitCollector::make_commit_event(const nlohmann::json& data) {
    nlohmann::json payload;
    payload["sha"] = data.value("sha", "");
    payload["branch"] = data.value("branch", "");
    payload["message"] = data.value("message", "");

    auto event = Event::create(Source::Git, "commit", repo_path_.string(), std::move(payload));
    event.correlation_keys = {"branch", "sha"};
    return event;
}

Event GitCollector::make_status_event() {
    nlohmann::json payload;
    payload["status"] = "dirty";

    return Event::create(Source::Git, "status", repo_path_.string(), std::move(payload));
}

} // namespace dmfr::collectors
