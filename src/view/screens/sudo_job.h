/*
 * Runs one root helper script through "sudo -S" without blocking the UI.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace screen {

// How a finished job ended, from the sudo output and the script exit code.
enum class JobOutcome {
    Success,        // exit 0
    WrongPassword,  // sudo rejected the password, nothing ran
    NotAllowed,     // sudo policy: this user may not run the script, nothing ran
    PartialFailure, // exit 1: some units failed, the state file keeps them
    BadArguments,   // exit 2
    Busy,           // exit 3: another switch holds the lock
    NotFound,       // exit 127: sudo or the script is missing
    Killed,         // terminated by a signal
    OtherError,     // any other exit code
};

struct JobResult {
    JobOutcome outcome;
    int exit_code; // raw code for the log and the message; -1 when there is none
};

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
    // Reads pending output; returns the result once the process has exited.
    std::optional<JobResult> poll();
    const std::string& output() const { return output_; }
    uint32_t elapsed_s() const;
    // Blocks until the running process exits (used when the owner goes away).
    void wait();

    static void wipe(std::string& secret);

private:
    static bool wrong_password(const std::string& output);
    static bool not_allowed(const std::string& output);
    JobResult classify(int status) const;
    void drain();

    pid_t pid_{-1};
    int fd_{-1};
    uint32_t started_tick_{0};
    std::string output_;
};

} // namespace screen
