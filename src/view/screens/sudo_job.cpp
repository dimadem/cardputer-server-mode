/*
 * SPDX-License-Identifier: MIT
 */

#include "sudo_job.h"

#include "lvgl.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ; // NOLINT(readability-redundant-declaration): POSIX, not in every header

namespace screen {
namespace {

constexpr size_t kMaxOutput = size_t{64} * 1024; // the scripts print a few lines; more is cut

// dup2 that also works when the fd is already the target: then O_CLOEXEC is cleared instead.
bool move_fd(int fd, int target) {
    if (fd == target) {
        return fcntl(fd, F_SETFD, 0) == 0;
    }
    return dup2(fd, target) == target;
}

} // namespace

SudoJob::~SudoJob() {
    // Never kill or abandon a running script: it may be half-way through boot
    // files. Wait for it (a few seconds) and keep draining its output.
    wait();
    if (fd_ >= 0) {
        close(fd_);
    }
}

void SudoJob::wait() {
    while (running() && !poll()) {
        usleep(50 * 1000);
    }
}

void SudoJob::wipe(std::string& secret) {
    if (!secret.empty()) {
        explicit_bzero(secret.data(), secret.size());
    }
    secret.clear();
}

bool SudoJob::wrong_password(const std::string& output) {
    return output.find("incorrect password") != std::string::npos ||
           output.find("Sorry, try again") != std::string::npos ||
           output.find("password is required") != std::string::npos;
}

bool SudoJob::not_allowed(const std::string& output) {
    return output.find("is not in the sudoers file") != std::string::npos ||
           output.find("is not allowed to") != std::string::npos;
}

bool SudoJob::start(std::string& password,
                    const std::vector<std::string>& argv,
                    std::string& error) {
    if (running()) {
        wipe(password);
        error = "busy";
        return false;
    }
    std::signal(SIGPIPE, SIG_IGN); // sudo may exit before reading the password

    std::vector<char*> args;
    for (const char* fixed : {"sudo", "-S", "-k", "-p", ""}) {
        args.push_back(const_cast<char*>(fixed));
    }
    for (const auto& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    // The environment is built before fork: the child only calls async-signal-safe functions.
    // LC_ALL=C keeps the sudo messages in English, so wrong_password() can match them.
    std::vector<char*> env;
    for (char** var = environ; var != nullptr && *var != nullptr; ++var) {
        if (std::strncmp(*var, "LC_ALL=", 7) != 0) env.push_back(*var);
    }
    env.push_back(const_cast<char*>("LC_ALL=C"));
    env.push_back(nullptr);

    int in_pipe[2];
    int out_pipe[2];
    if (pipe2(in_pipe, O_CLOEXEC) != 0) {
        error = std::strerror(errno);
        wipe(password);
        return false;
    }
    if (pipe2(out_pipe, O_CLOEXEC) != 0) {
        error = std::strerror(errno);
        close(in_pipe[0]);
        close(in_pipe[1]);
        wipe(password);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        error = std::strerror(errno);
        for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]}) close(fd);
        wipe(password);
        return false;
    }
    if (pid == 0) {
        if (move_fd(in_pipe[0], STDIN_FILENO) && move_fd(out_pipe[1], STDOUT_FILENO) &&
            move_fd(out_pipe[1], STDERR_FILENO)) {
            execve("/usr/bin/sudo", args.data(), env.data());
        }
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    password.push_back('\n'); // in place: no extra copy of the secret
    size_t offset = 0;
    while (offset < password.size()) {
        const ssize_t n = write(in_pipe[1], password.data() + offset, password.size() - offset);
        if (n > 0) {
            offset += static_cast<size_t>(n);
        }
        else if (n < 0 && errno == EINTR) {
            continue;
        }
        else {
            break; // EPIPE: sudo exited early, its exit code tells why
        }
    }
    wipe(password);
    close(in_pipe[1]);

    const int flags = fcntl(out_pipe[0], F_GETFL);
    if (flags < 0 || fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        // a blocking read would freeze the UI: give up on the output, keep the exit code
        LV_LOG_WARN("sudo job: cannot make the output non-blocking");
        close(out_pipe[0]);
        out_pipe[0] = -1;
    }
    fd_ = out_pipe[0];
    pid_ = pid;
    output_.clear();
    started_tick_ = lv_tick_get();
    return true;
}

void SudoJob::drain() {
    char buffer[512];
    ssize_t n = 0;
    while (fd_ >= 0 && (n = read(fd_, buffer, sizeof(buffer))) > 0) {
        if (output_.size() < kMaxOutput) {
            output_.append(buffer, std::min(static_cast<size_t>(n), kMaxOutput - output_.size()));
        }
    }
}

// Exit codes of scripts/*.sh, plus 127 from the child when execv fails.
enum class ScriptExit : int {
    Ok = 0,
    PartialFailure = 1,
    BadArguments = 2,
    Busy = 3,
    NotFound = 127
};

JobResult SudoJob::classify(int status) const {
    if (!WIFEXITED(status)) {
        return {JobOutcome::Killed, -1};
    }
    const int code = WEXITSTATUS(status);
    if (code != 0 && wrong_password(output_)) {
        return {JobOutcome::WrongPassword, code};
    }
    if (code != 0 && not_allowed(output_)) {
        return {JobOutcome::NotAllowed, code};
    }
    switch (static_cast<ScriptExit>(code)) {
        case ScriptExit::Ok: return {JobOutcome::Success, code};
        case ScriptExit::PartialFailure: return {JobOutcome::PartialFailure, code};
        case ScriptExit::BadArguments: return {JobOutcome::BadArguments, code};
        case ScriptExit::Busy: return {JobOutcome::Busy, code};
        case ScriptExit::NotFound: return {JobOutcome::NotFound, code};
    }
    return {JobOutcome::OtherError, code};
}

std::optional<JobResult> SudoJob::poll() {
    if (!running()) {
        return std::nullopt;
    }
    drain();
    int status = 0;
    const pid_t done = waitpid(pid_, &status, WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR)) {
        return std::nullopt;
    }
    drain();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    pid_ = -1;
    if (done < 0) {
        return JobResult{JobOutcome::OtherError, -1}; // the child is gone (ECHILD): no status
    }
    return classify(status);
}

uint32_t SudoJob::elapsed_s() const {
    return lv_tick_elaps(started_tick_) / 1000;
}

} // namespace screen
