#include "dmfr/common/process.h"
#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace dmfr::process {
std::string resolve(const std::string& command) {
    namespace fs = std::filesystem;
    if (command.find('/') != std::string::npos) return fs::absolute(command).lexically_normal().string();
    const char* path = std::getenv("PATH");
    if (!path) return "";
    std::istringstream entries(std::string(path) + ":");
    std::string entry;
    while (std::getline(entries, entry, ':')) {
        auto candidate = (entry.empty() ? fs::current_path() : fs::path(entry)) / command;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)
#if defined(__unix__) || defined(__APPLE__)
            && ::access(candidate.c_str(), X_OK) == 0
#endif
        ) return fs::absolute(candidate).lexically_normal().string();
    }
    return "";
}

std::string fingerprint(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return "";
    std::ifstream input(file, std::ios::binary);
    if (!input) return "";
    uint64_t hash = 14695981039346656037ULL;
    std::array<char, 65536> buffer;
    while (input) {
        input.read(buffer.data(), buffer.size());
        for (std::streamsize i = 0; i < input.gcount(); ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            hash *= 1099511628211ULL;
        }
    }
    if (!input.eof()) return "";
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

#if defined(__unix__) || defined(__APPLE__)
namespace {
std::vector<char*> arguments(const std::vector<std::string>& argv) {
    std::vector<char*> result;
    for (const auto& arg : argv) result.push_back(const_cast<char*>(arg.c_str()));
    result.push_back(nullptr);
    return result;
}
int exit_status(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 126;
}
volatile sig_atomic_t foreground_pid = 0;
void relay(int sig) {
    const int saved = errno;
    if (foreground_pid > 0) ::kill(static_cast<pid_t>(foreground_pid), sig);
    errno = saved;
}
struct Signals {
    struct sigaction old[3]{};
    const std::array<int, 3> signals{SIGINT, SIGQUIT, SIGTERM};
    Signals() {
        struct sigaction action{};
        action.sa_handler = relay;
        sigemptyset(&action.sa_mask);
        for (size_t i = 0; i < signals.size(); ++i) ::sigaction(signals[i], &action, &old[i]);
    }
    ~Signals() {
        foreground_pid = 0;
        for (size_t i = 0; i < signals.size(); ++i) ::sigaction(signals[i], &old[i], nullptr);
    }
};
}
#endif

int foreground(const std::vector<std::string>& argv) {
#if defined(__unix__) || defined(__APPLE__)
    if (argv.empty()) return 127;
    const auto executable = resolve(argv.front());
    if (executable.empty()) return 127;
    auto args = arguments(argv);
    sigset_t blocked, original_mask;
    sigemptyset(&blocked);
    for (int sig : {SIGINT, SIGQUIT, SIGTERM}) sigaddset(&blocked, sig);
    ::sigprocmask(SIG_BLOCK, &blocked, &original_mask);
    Signals guard;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t defaults;
    sigemptyset(&defaults);
    for (int sig : guard.signals) sigaddset(&defaults, sig);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setsigmask(&attr, &original_mask);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    pid_t pid;
    const int error = ::posix_spawn(&pid, executable.c_str(), nullptr, &attr, args.data(), environ);
    posix_spawnattr_destroy(&attr);
    if (error) {
        ::sigprocmask(SIG_SETMASK, &original_mask, nullptr);
        return error == ENOENT ? 127 : 126;
    }
    foreground_pid = pid;
    ::sigprocmask(SIG_SETMASK, &original_mask, nullptr);
    int status = 0;
    pid_t waited;
    do { waited = ::waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    foreground_pid = 0;
    return waited < 0 ? 126 : exit_status(status);
#else
    (void)argv;
    throw std::runtime_error("foreground recording requires macOS or Linux");
#endif
}

Result capture(const std::vector<std::string>& argv, std::chrono::milliseconds timeout, size_t max_output) {
    Result result;
#if defined(__unix__) || defined(__APPLE__)
    if (argv.empty()) return result;
    const auto executable = resolve(argv.front());
    if (executable.empty()) return result;
    int pipefd[2];
    if (::pipe(pipefd) != 0) return result;
    ::fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    auto args = arguments(argv);
    pid_t pid;
    const int error = ::posix_spawn(&pid, executable.c_str(), &actions, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(pipefd[1]);
    if (error) { ::close(pipefd[0]); return result; }
    ::fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool eof = false, done = false;
    int status = 0;
    std::array<char, 4096> buffer;
    while (!eof || !done) {
        if (std::chrono::steady_clock::now() >= deadline) { result.timed_out = true; break; }
        const auto n = ::read(pipefd[0], buffer.data(), buffer.size());
        if (n > 0) {
            const auto size = static_cast<size_t>(n);
            const auto available = max_output - result.output.size();
            result.output.append(buffer.data(), std::min(size, available));
            if (size > available) { result.truncated = true; break; }
        } else if (n == 0) eof = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { result.truncated = true; break; }
        if (!done) {
            auto waited = ::waitpid(pid, &status, WNOHANG);
            done = waited == pid || (waited < 0 && errno != EINTR);
        }
        if (n <= 0 && (!done || !eof)) {
            pollfd descriptor{pipefd[0], POLLIN, 0};
            ::poll(&descriptor, 1, 10);
        }
    }
    // Terminate descendants left by otherwise successful probe programs.
    ::kill(-pid, SIGKILL);
    if (!done) {
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    ::close(pipefd[0]);
    result.exit_code = exit_status(status);
    while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r')) result.output.pop_back();
#else
    (void)argv; (void)timeout; (void)max_output;
#endif
    return result;
}
}
