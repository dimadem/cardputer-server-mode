/*
 * SPDX-License-Identifier: MIT
 */

#include "sudo_job.h"

#include "lvgl.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace screen {

SudoJob::~SudoJob() {
    // Never kill or abandon a running script: it may be half-way through boot
    // files. Wait for it (a few seconds) and keep draining its output.
    wait();
    if (fd_ >= 0) {
        close(fd_);
    }
}

void SudoJob::wait() {
    int exit_code = 0;
    while (running() && !poll(exit_code)) {
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

bool SudoJob::start(std::string& password, const std::vector<std::string>& argv, std::string& error) {
    if (running()) {
        wipe(password);
        error = "busy";
        return false;
    }
    std::signal(SIGPIPE, SIG_IGN);  // sudo may exit before reading the password

    std::vector<char*> args;
    for (const char* fixed : {"sudo", "-S", "-k", "-p", ""}) {
        args.push_back(const_cast<char*>(fixed));
    }
    for (const auto& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

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
        setenv("LC_ALL", "C", 1);  // sudo messages in English: wrong_password() matches them
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        execv("/usr/bin/sudo", args.data());
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    password.push_back('\n');  // in place: no extra copy of the secret
    size_t offset = 0;
    while (offset < password.size()) {
        const ssize_t n = write(in_pipe[1], password.data() + offset, password.size() - offset);
        if (n > 0) {
            offset += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;  // EPIPE: sudo exited early, its exit code tells why
        }
    }
    wipe(password);
    close(in_pipe[1]);

    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL) | O_NONBLOCK);
    fd_ = out_pipe[0];
    pid_ = pid;
    output_.clear();
    started_tick_ = lv_tick_get();
    return true;
}

void SudoJob::drain() {
    char buffer[512];
    ssize_t n;
    while (fd_ >= 0 && (n = read(fd_, buffer, sizeof(buffer))) > 0) {
        output_.append(buffer, static_cast<size_t>(n));
    }
}

bool SudoJob::poll(int& exit_code) {
    if (!running()) {
        return false;
    }
    drain();
    int status = 0;
    const pid_t done = waitpid(pid_, &status, WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR)) {
        return false;
    }
    drain();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    pid_ = -1;
    exit_code = done > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}

uint32_t SudoJob::elapsed_s() const {
    return lv_tick_elaps(started_tick_) / 1000;
}

} // namespace screen
