/*
 * SPDX-License-Identifier: MIT
 */

#include "server_panel.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "linux_input.h"
#include "logger.h"
#include "server_settings.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <optional>
#include <utility>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace screen {
namespace {

constexpr const char* kStateFile = "/run/cz-server-mode/stopped-units";
constexpr uint32_t kIdlePeriodMs = 2000; // status refresh
constexpr uint32_t kJobPeriodMs = 200;   // while a switch runs
constexpr uint32_t kProcRescanMs = 10000;

struct MemInfo {
    long total_mb{0};
    long avail_mb{0};
    long swap_used_mb{0};
};

// "Key:   1234 kB" -> 1234 when the line starts with key
std::optional<long> meminfo_kb(const char* line, std::string_view key) {
    if (std::strncmp(line, key.data(), key.size()) != 0) {
        return std::nullopt;
    }
    const char* start = line + key.size();
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(start, &end, 10);
    if (end == start || errno != 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<MemInfo> read_meminfo() {
    long total = 0;
    long avail = 0;
    long swap_total = 0;
    long swap_free = 0;
    const std::array<std::pair<std::string_view, long*>, 4> fields = {{
        {"MemTotal:", &total},
        {"MemAvailable:", &avail},
        {"SwapTotal:", &swap_total},
        {"SwapFree:", &swap_free},
    }};
    FILE* file = std::fopen("/proc/meminfo", "r");
    if (file == nullptr) {
        return std::nullopt;
    }
    char line[128];
    size_t found = 0;
    while (found < fields.size() && std::fgets(line, sizeof(line), file) != nullptr) {
        for (const auto& [key, target] : fields) {
            if (const auto kb = meminfo_kb(line, key)) {
                *target = *kb;
                ++found;
                break;
            }
        }
    }
    std::fclose(file);
    if (found < fields.size()) {
        return std::nullopt;
    }
    return MemInfo{total / 1024, avail / 1024, (swap_total - swap_free) / 1024};
}

bool process_is(pid_t pid, std::string_view name) {
    char path[32];
    std::snprintf(path, sizeof(path), "/proc/%d/comm", static_cast<int>(pid));
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return false;
    }
    char comm[32] = {};
    const bool ok = std::fgets(comm, sizeof(comm), file) != nullptr;
    std::fclose(file);
    if (!ok) {
        return false;
    }
    std::string_view read_name(comm);
    if (!read_name.empty() && read_name.back() == '\n') {
        read_name.remove_suffix(1);
    }
    return read_name == name;
}

// pid from a /proc entry name, nullopt for entries that are not processes
std::optional<pid_t> parse_pid(const char* text) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno != 0 || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

pid_t find_process(std::string_view name) {
    DIR* proc = opendir("/proc");
    if (proc == nullptr) {
        return -1;
    }
    pid_t found = -1;
    while (const auto* entry = readdir(proc)) {
        const auto pid = parse_pid(entry->d_name);
        if (pid && process_is(*pid, name)) {
            found = *pid;
            break;
        }
    }
    closedir(proc);
    return found;
}

} // namespace

ServerPanel::ServerPanel(lv_obj_t* content,
                         viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets)
    : view_model_(view_model) {
    password_.reserve(kMaxPassword + 1);

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_hor(column, 10, 0);
    lv_obj_set_style_pad_ver(column, 4, 0);
    lv_obj_set_style_pad_row(column, 2, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    const auto* big = assets.load_standard_font(18);
    const auto* regular = assets.load_standard_font(14, app::StandardFontWeight::Regular);
    const auto* small = assets.load_standard_font(12, app::StandardFontWeight::Regular);
    auto* dark = view_model_.dark_mode_subject();

    mode_label_ = make_label(column, big, &lv_font_montserrat_20, dark);
    mem_label_ = make_label(column, regular, &lv_font_montserrat_14, dark);
    detail_label_ = make_label(column, small, &lv_font_montserrat_12, dark);
    password_label_ = make_label(column, regular, &lv_font_montserrat_14, dark);
    message_label_ = make_label(column, small, &lv_font_montserrat_12, dark);
    auto* hint_label = make_label(column, small, &lv_font_montserrat_12, dark);
    lv_label_set_text(hint_label, "Tab: settings · hold ESC: exit");
    lv_obj_set_style_text_color(hint_label,
                                view::palette(view_model_.is_dark_mode()).text_disabled,
                                0);

    platform::set_screen_key_handler(key_cb, this);
    timer_ = lv_timer_create(tick_cb, kIdlePeriodMs, this);

    refresh_status();
    update_password_label();
    set_message("Enter sudo password, press Enter", Tone::Info);
}

ServerPanel::~ServerPanel() {
    platform::clear_screen_key_handler(key_cb, this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
    job_.wait();
    view_model_.set_busy(false); // not set_busy(): the timer is gone
    SudoJob::wipe(password_);
}

void ServerPanel::tick_cb(lv_timer_t* timer) {
    auto* panel = static_cast<ServerPanel*>(lv_timer_get_user_data(timer));
    if (!panel) {
        return;
    }
    if (!panel->job_.running()) {
        panel->refresh_status();
        return;
    }
    if (const auto result = panel->job_.poll()) {
        panel->finish_switch(*result);
        return;
    }
    const uint32_t elapsed = panel->job_.elapsed_s();
    if (elapsed != panel->last_elapsed_s_) { // the text changes once per second
        panel->last_elapsed_s_ = elapsed;
        panel->set_message(std::string(panel->job_target_ == Mode::Server ? "Enabling server mode… "
                                                                          : "Restoring desktop… ") +
                               std::to_string(elapsed) + " s",
                           Tone::Info);
    }
}

bool ServerPanel::key_cb(uint32_t key, void* user_data) {
    auto* panel = static_cast<ServerPanel*>(user_data);
    return panel != nullptr && panel->on_key(key);
}

bool ServerPanel::on_key(uint32_t key) {
    if (job_.running()) {
        return true; // ignore keys while a switch is running
    }
    if (key == kKeyTab) {
        view_model_.show_butter_page(); // settings page
        return true;
    }
    if (key == LV_KEY_ENTER) {
        start_switch();
        return true;
    }
    if (edit_password(password_, key)) {
        update_password_label();
        return true;
    }
    return false;
}

bool ServerPanel::desktop_running() {
    if (desktop_pid_ > 0 && process_is(desktop_pid_, "labwc")) {
        return true;
    }
    desktop_pid_ = -1;
    if (!proc_scanned_ || lv_tick_elaps(last_proc_scan_tick_) >= kProcRescanMs) {
        proc_scanned_ = true;
        last_proc_scan_tick_ = lv_tick_get();
        desktop_pid_ = find_process("labwc");
    }
    return desktop_pid_ > 0;
}

void ServerPanel::refresh_status() {
    mode_ = access(kStateFile, F_OK) == 0 ? Mode::Server : Mode::Normal;
    const bool server = mode_ == Mode::Server;
    const auto mem = read_meminfo();
    const bool desktop = desktop_running();

    char text[96];
    const auto colors = view::palette(view_model_.is_dark_mode());
    set_text_if_changed(mode_label_, server ? "Mode: SERVER" : "Mode: normal");
    lv_obj_set_style_text_color(mode_label_, server ? colors.success : colors.warning, 0);
    if (mem) {
        std::snprintf(text, sizeof(text), "Free %ld of %ld MB", mem->avail_mb, mem->total_mb);
        set_text_if_changed(mem_label_, text);
        std::snprintf(text,
                      sizeof(text),
                      "Swap %ld MB · desktop %s",
                      mem->swap_used_mb,
                      desktop ? "running" : "stopped");
    }
    else {
        set_text_if_changed(mem_label_, "Memory: unavailable");
        std::snprintf(text, sizeof(text), "desktop %s", desktop ? "running" : "stopped");
    }
    set_text_if_changed(detail_label_, text);
}

void ServerPanel::update_password_label() {
    set_text_if_changed(password_label_, password_text(password_).c_str());
}

void ServerPanel::set_message(const std::string& text, Tone tone) {
    set_tone_text(message_label_, text, tone, view_model_.is_dark_mode());
}

void ServerPanel::set_busy(bool busy) {
    view_model_.set_busy(busy);
    lv_timer_set_period(timer_, busy ? kJobPeriodMs : kIdlePeriodMs);
}

void ServerPanel::start_switch() {
    if (password_.empty()) {
        set_message("Enter the sudo password first", Tone::Warning);
        return;
    }

    refresh_status();
    job_target_ = mode_ == Mode::Server ? Mode::Normal : Mode::Server;
    std::vector<std::string> argv;
    if (job_target_ == Mode::Server) {
        const auto groups = settings::load_enabled_groups();
        if (groups.empty()) {
            SudoJob::wipe(password_);
            update_password_label();
            set_message("Nothing selected in settings", Tone::Warning);
            return;
        }
        argv.push_back(settings::script_path(settings::Script::ServerMode));
        const auto ids = groups.ids();
        argv.insert(argv.end(), ids.begin(), ids.end());
    }
    else {
        argv.push_back(settings::script_path(settings::Script::NormalMode));
    }

    std::string error;
    const bool started = job_.start(password_, argv, error);
    update_password_label();
    if (!started) {
        set_message("Failed to start: " + error, Tone::Error);
        return;
    }
    set_busy(true);
    last_elapsed_s_ = UINT32_MAX;
    LOG_INFO("switching to {} mode via {}",
             job_target_ == Mode::Server ? "server" : "normal",
             argv.front());
    set_message(job_target_ == Mode::Server ? "Enabling server mode…" : "Restoring desktop…",
                Tone::Info);
}

void ServerPanel::finish_switch(const JobResult& result) {
    set_busy(false);
    const auto& output = job_.output();
    LOG_INFO("switch finished, exit code {}, output:\n{}", result.exit_code, output);
    proc_scanned_ = false; // the desktop just started or stopped: look again now
    refresh_status();

    switch (result.outcome) {
        case JobOutcome::Success: set_message(success_summary(output), Tone::Success); return;
        case JobOutcome::WrongPassword: set_message("Wrong password", Tone::Error); return;
        case JobOutcome::NotAllowed:
            set_message("sudo: not allowed for this user", Tone::Error);
            return;
        case JobOutcome::Busy:
            set_message("Busy: another switch is running", Tone::Warning);
            return;
        case JobOutcome::NotFound: set_message("sudo or script not found", Tone::Error); return;
        case JobOutcome::Killed: set_message("Switch was killed (see log)", Tone::Error); return;
        case JobOutcome::BadArguments:
        case JobOutcome::OtherError:
            set_message("Error, code " + std::to_string(result.exit_code) + " (see log)",
                        Tone::Error);
            return;
        case JobOutcome::PartialFailure: break;
    }
    set_message("Partly failed, code " + std::to_string(result.exit_code) + " (see log)",
                Tone::Error);
}

// "Done: 130 MB -> 184 MB" from the script's MemAvailable line.
std::string ServerPanel::success_summary(const std::string& output) {
    constexpr std::string_view kMarker = "MemAvailable: ";
    if (const auto pos = output.rfind(kMarker); pos != std::string::npos) {
        const auto start = pos + kMarker.size();
        return "Done: " + output.substr(start, output.find('\n', start) - start);
    }
    if (output.find("already") != std::string::npos) {
        return "Already in this mode";
    }
    return "Done";
}

} // namespace screen
