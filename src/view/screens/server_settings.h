/*
 * Which service groups server mode stops (stored per user, no root needed)
 * and where the root helper scripts live.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

namespace settings {

struct StopGroup {
    const char* id;     // name understood by server-mode.sh
    const char* title;  // shown in the UI
};

const std::vector<StopGroup>& stop_groups();
std::vector<std::string> load_enabled_groups();  // all groups when nothing was saved
bool save_enabled_groups(const std::vector<std::string>& groups);

std::string script_path(const char* name);

} // namespace settings
