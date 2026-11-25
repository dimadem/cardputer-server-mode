/*
 * SPDX-License-Identifier: MIT
 */

#include "server_settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifndef CZ_SCRIPT_DIR
#define CZ_SCRIPT_DIR "/usr/lib/cz-server-mode"
#endif

namespace settings {
namespace {

std::filesystem::path groups_file() {
    std::filesystem::path root;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] == '/') {
        root = xdg;
    }
    else if (const char* home = std::getenv("HOME"); home && home[0] == '/') {
        root = std::filesystem::path(home) / ".config";
    }
    else {
        root = "/tmp";
    }
    return root / "cz-server-mode" / "stop-groups";
}

std::optional<bool> parse_bool(std::string_view text) {
    if (text == "1") return true;
    if (text == "0") return false;
    return std::nullopt;
}

std::optional<int> parse_int(std::string_view text) {
    if (text.empty() || text.size() > 6) return std::nullopt;
    int value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return std::nullopt;
        value = value * 10 + (ch - '0');
    }
    return value;
}

std::string_view script_name(Script script) {
    switch (script) {
        case Script::ServerMode: return "server-mode.sh";
        case Script::NormalMode: return "normal-mode.sh";
        case Script::Advanced: return "advanced.sh";
    }
    return "";
}

} // namespace

// ---- service groups

std::optional<StopGroup> parse_group(std::string_view id) {
    for (const auto& info : kStopGroups) {
        if (info.id == id) return info.group;
    }
    return std::nullopt;
}

GroupSet GroupSet::all() {
    GroupSet set;
    set.bits_.set();
    return set;
}

std::optional<GroupSet> GroupSet::parse_csv(std::string_view csv) {
    GroupSet set;
    while (!csv.empty()) {
        const size_t comma = csv.find(',');
        const std::string_view id = csv.substr(0, comma);
        const auto group = parse_group(id);
        if (!group) return std::nullopt;
        set.set(*group, true);
        csv = comma == std::string_view::npos ? std::string_view{} : csv.substr(comma + 1);
    }
    return set;
}

std::vector<std::string> GroupSet::ids() const {
    std::vector<std::string> result;
    for (const auto& info : kStopGroups) {
        if (contains(info.group)) result.emplace_back(info.id);
    }
    return result;
}

std::string GroupSet::csv() const {
    std::string result;
    for (const auto& id : ids()) {
        if (!result.empty()) result += ',';
        result += id;
    }
    return result;
}

GroupSet load_enabled_groups() {
    std::ifstream file(groups_file());
    if (!file) {
        return GroupSet::all();
    }
    GroupSet result;
    std::string line;
    while (std::getline(file, line)) {
        if (const auto group = parse_group(line))
            result.set(*group, true); // unknown lines are ignored
    }
    return result;
}

bool save_enabled_groups(const GroupSet& groups) {
    const auto path = groups_file();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // temp file + rename: a failed write leaves the old file in place
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        for (const auto& id : groups.ids()) file << id << "\n";
        file.close();
        if (!file) {
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    return !ec;
}

// ---- scripts

// Fixed at build time: the scripts run as root, so no environment override.
std::string script_path(Script script) {
    return std::string(CZ_SCRIPT_DIR) + "/" + std::string(script_name(script));
}

// ---- boot status

std::optional<GpuMemory> gpu_from_megabytes(int mb) {
    for (const GpuMemory gpu : kGpuChoices) {
        if (megabytes(gpu) == mb) return gpu;
    }
    return std::nullopt;
}

std::string_view flag_key(BootFlag flag) {
    switch (flag) {
        case BootFlag::BootServer: return "boot_server";
        case BootFlag::CameraOff: return "camera_off";
        case BootFlag::HdmiOff: return "hdmi_off";
        case BootFlag::CloudInitOff: return "cloudinit_off";
    }
    return "";
}

bool BootStatus::flag(BootFlag which) const {
    switch (which) {
        case BootFlag::BootServer: return boot_server;
        case BootFlag::CameraOff: return camera_off;
        case BootFlag::HdmiOff: return hdmi_off;
        case BootFlag::CloudInitOff: return cloudinit_off;
    }
    return false;
}

std::optional<BootStatus> parse_boot_status(std::string_view text) {
    std::map<std::string, std::string, std::less<>> values;
    std::istringstream stream{std::string(text)};
    std::string token;
    while (stream >> token) {
        const auto eq = token.find('=');
        if (eq != std::string::npos) values[token.substr(0, eq)] = token.substr(eq + 1);
    }
    const auto value = [&values](std::string_view key) -> std::optional<std::string_view> {
        const auto it = values.find(key);
        if (it == values.end()) return std::nullopt;
        return std::string_view(it->second);
    };
    const auto boolean = [&value](std::string_view key) -> std::optional<bool> {
        const auto raw = value(key);
        return raw ? parse_bool(*raw) : std::nullopt;
    };

    BootStatus status;
    const auto gpu = value("gpu");
    const auto gpu_mb = gpu ? parse_int(*gpu) : std::nullopt;
    const auto boot_server = boolean("boot_server");
    const auto camera_off = boolean(flag_key(BootFlag::CameraOff));
    const auto hdmi_off = boolean(flag_key(BootFlag::HdmiOff));
    const auto cloudinit_off = boolean(flag_key(BootFlag::CloudInitOff));
    const auto modified = boolean("modified");
    const auto groups_text = value("boot_groups");
    if (!gpu_mb || !boot_server || !camera_off || !hdmi_off || !cloudinit_off || !modified ||
        !groups_text) {
        return std::nullopt;
    }
    // a unit without a group list (or none at all) stops every group
    const auto groups = groups_text->empty() ? GroupSet::all() : GroupSet::parse_csv(*groups_text);
    if (!groups) return std::nullopt;

    status.gpu_mb = *gpu_mb;
    status.boot_server = *boot_server;
    status.camera_off = *camera_off;
    status.hdmi_off = *hdmi_off;
    status.cloudinit_off = *cloudinit_off;
    status.modified = *modified;
    status.boot_groups = *groups;
    return status;
}

std::optional<BootStatus> read_boot_status() {
    const std::string command = script_path(Script::Advanced) + " status 2>/dev/null";
    // NOLINTNEXTLINE(bugprone-command-processor): fixed path set at build time, no user input
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }
    char buffer[256];
    std::string output;
    while (std::fgets(buffer, sizeof(buffer), pipe)) output += buffer;
    if (pclose(pipe) != 0) {
        return std::nullopt;
    }
    return parse_boot_status(output);
}

} // namespace settings
