/*
 * Runs one root helper script through "sudo -S" without blocking the UI.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace screen {

class SudoJob {
public:
    SudoJob() = default;
    ~SudoJob();

    SudoJob(const SudoJob&) = delete;
    SudoJob& operator=(const SudoJob&) = delete;

    // Starts "sudo -S -k -p '' <argv...>". The password is wiped in any case.
    // The password string should have one spare byte of capacity (see edit_password).
    bool start(std::string& password, const std::vector<std::string>& argv, std::string& error);
    bool running() const { return pid_ > 0; }
    // Reads pending output; returns true once the process has exited (exit_code -1 if killed).
    bool poll(int& exit_code);
    const std::string& output() const { return output_; }
    uint32_t elapsed_s() const;
    // Blocks until the running process exits (used when the owner goes away).
    void wait();

    static bool wrong_password(const std::string& output);
    static void wipe(std::string& secret);

private:
    void drain();

    pid_t pid_{-1};
    int fd_{-1};
    uint32_t started_tick_{0};
    std::string output_;
};

} // namespace screen
