/*
 * SPDX-License-Identifier: MIT
 */

#include "server_settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifndef CZ_SCRIPT_DIR
#define CZ_SCRIPT_DIR "/usr/lib/cz-server-mode"
#endif

namespace settings {
namespace {

std::filesystem::path groups_file() {
    std::filesystem::path root;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] == '/') {
        root = xdg;
    } else if (const char* home = std::getenv("HOME"); home && home[0] == '/') {
        root = std::filesystem::path(home) / ".config";
    } else {
        root = "/tmp";
    }
    return root / "cz-server-mode" / "stop-groups";
}

} // namespace

// Keep in sync with ALL_GROUPS / group_units() in scripts/common.sh.
const std::vector<StopGroup>& stop_groups() {
    static const std::vector<StopGroup> groups = {
        {"desktop", "Desktop and HDMI"},
        {"printing", "Printing (CUPS)"},
        {"packagekit", "PackageKit (updates)"},
        {"portals", "Portals and gvfs"},
        {"accounts", "accounts-daemon"},
        {"nfs", "NFS (rpcbind)"},
    };
    return groups;
}

std::vector<std::string> load_enabled_groups() {
    std::ifstream file(groups_file());
    std::vector<std::string> result;
    if (!file) {
        for (const auto& group : stop_groups()) result.emplace_back(group.id);
        return result;
    }
    std::string line;
    while (std::getline(file, line)) {
        for (const auto& group : stop_groups()) {
            if (line == group.id) result.push_back(line);
        }
    }
    return result;
}

bool save_enabled_groups(const std::vector<std::string>& groups) {
    const auto path = groups_file();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::trunc);
    for (const auto& group : groups) file << group << "\n";
    return static_cast<bool>(file);
}

// Fixed at build time: the scripts run as root, so no environment override.
std::string script_path(const char* name) {
    return std::string(CZ_SCRIPT_DIR) + "/" + name;
}

} // namespace settings
