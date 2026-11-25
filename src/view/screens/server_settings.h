/*
 * Domain types of Server Mode: the service groups that server mode stops
 * (stored per user, no root needed), the root helper scripts and the
 * boot-time state reported by "advanced.sh status".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace settings {

// ---- service groups

// Keep in sync with ALL_GROUPS / group_units() in scripts/common.sh.
enum class StopGroup : uint8_t { Desktop, Printing, PackageKit, Portals, Accounts, Nfs };
inline constexpr size_t kStopGroupCount = 6;

struct StopGroupInfo {
    StopGroup group{};
    std::string_view id;    // name understood by server-mode.sh
    std::string_view title; // shown in the UI
};

// All groups, in UI and script order.
inline constexpr std::array<StopGroupInfo, kStopGroupCount> kStopGroups = {{
    {StopGroup::Desktop, "desktop", "Desktop and HDMI"},
    {StopGroup::Printing, "printing", "Printing (CUPS)"},
    {StopGroup::PackageKit, "packagekit", "PackageKit (updates)"},
    {StopGroup::Portals, "portals", "Portals and gvfs"},
    {StopGroup::Accounts, "accounts", "accounts-daemon"},
    {StopGroup::Nfs, "nfs", "NFS (rpcbind)"},
}};

std::optional<StopGroup> parse_group(std::string_view id);

// A set of stop groups. Ids and CSV always come out in kStopGroups order.
class GroupSet {
public:
    static GroupSet all();
    // nullopt when the text has an unknown id; an empty text gives an empty set
    static std::optional<GroupSet> parse_csv(std::string_view csv);

    bool contains(StopGroup group) const { return bits_.test(index(group)); }
    void set(StopGroup group, bool enabled) { bits_.set(index(group), enabled); }
    bool empty() const { return bits_.none(); }
    bool operator==(const GroupSet& other) const { return bits_ == other.bits_; }
    bool operator!=(const GroupSet& other) const { return bits_ != other.bits_; }

    std::vector<std::string> ids() const;
    std::string csv() const;

private:
    static size_t index(StopGroup group) { return static_cast<size_t>(group); }

    std::bitset<kStopGroupCount> bits_;
};

GroupSet load_enabled_groups(); // all groups when nothing was saved
bool save_enabled_groups(const GroupSet& groups);

// ---- root helper scripts (installed in CZ_SCRIPT_DIR)

enum class Script { ServerMode, NormalMode, Advanced };
std::string script_path(Script script);

// ---- boot-time state (advanced.sh)

// gpu_mem values the UI offers; 16 MB is the Raspberry Pi minimum.
enum class GpuMemory : int { Mb128 = 128, Mb64 = 64, Mb32 = 32, Mb16 = 16 };
inline constexpr std::array<GpuMemory, 4> kGpuChoices = {GpuMemory::Mb128,
                                                         GpuMemory::Mb64,
                                                         GpuMemory::Mb32,
                                                         GpuMemory::Mb16};
constexpr int megabytes(GpuMemory gpu) {
    return static_cast<int>(gpu);
}
std::optional<GpuMemory> gpu_from_megabytes(int mb); // nullopt for a value the UI does not offer

// On/off boot options; flag_key() is the advanced.sh key.
enum class BootFlag { BootServer, CameraOff, HdmiOff, CloudInitOff };
std::string_view flag_key(BootFlag flag);

struct BootStatus {
    int gpu_mb{64}; // raw gpu_mem from config.txt: may be a value not in kGpuChoices
    bool boot_server{false};
    bool camera_off{false};
    bool hdmi_off{false};
    bool cloudinit_off{false};
    bool modified{false}; // boot files differ from the saved originals
    GroupSet boot_groups; // what the boot unit stops

    bool flag(BootFlag which) const;
};

// Parses "key=value ..." from "advanced.sh status": nullopt if a key is missing or malformed.
std::optional<BootStatus> parse_boot_status(std::string_view text);
// Runs "advanced.sh status" (no root needed): nullopt when it fails.
std::optional<BootStatus> read_boot_status();

} // namespace settings
