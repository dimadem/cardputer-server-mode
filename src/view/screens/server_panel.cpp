/*
 * SPDX-License-Identifier: MIT
 */

#include "server_panel.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "linux_input.h"
#include "logger.h"
#include "server_settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <vector>

namespace screen {
namespace {

constexpr const char* kStateFile = "/run/cz-server-mode/stopped-units";
constexpr uint32_t kIdlePeriodMs = 2000;  // status refresh
constexpr uint32_t kJobPeriodMs = 200;    // while a switch runs
constexpr uint32_t kProcRescanMs = 10000;

struct MemInfo {
    long total_mb{0};
    long avail_mb{0};
    long swap_used_mb{0};
};

MemInfo read_meminfo() {
    long total = 0, avail = 0, swap_total = 0, swap_free = 0;
    if (FILE* file = std::fopen("/proc/meminfo", "r")) {
        char line[128];
        int found = 0;
        while (found < 4 && std::fgets(line, sizeof(line), file)) {
            found += std::sscanf(line, "MemTotal: %ld", &total) +
                     std::sscanf(line, "MemAvailable: %ld", &avail) +
                     std::sscanf(line, "SwapTotal: %ld", &swap_total) +
                     std::sscanf(line, "SwapFree: %ld", &swap_free);
        }
        std::fclose(file);
    }
    return {total / 1024, avail / 1024, (swap_total - swap_free) / 1024};
}

bool process_is(pid_t pid, const char* name) {
    char path[32];
    std::snprintf(path, sizeof(path), "/proc/%d/comm", static_cast<int>(pid));
    FILE* file = std::fopen(path, "r");
    if (!file) {
        return false;
    }
    char comm[32] = {};
    const bool ok = std::fgets(comm, sizeof(comm), file) != nullptr;
    std::fclose(file);
    if (!ok) {
        return false;
    }
    comm[std::strcspn(comm, "\n")] = '\0';
    return std::strcmp(comm, name) == 0;
}

pid_t find_process(const char* name) {
    DIR* proc = opendir("/proc");
    if (!proc) {
        return -1;
    }
    pid_t found = -1;
    while (auto* entry = readdir(proc)) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
            continue;
        }
        const pid_t pid = static_cast<pid_t>(std::atoi(entry->d_name));
        if (process_is(pid, name)) {
            found = pid;
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

    auto* big = assets.load_standard_font(18);
    auto* regular = assets.load_standard_font(14, app::StandardFontWeight::Regular);
    auto* small = assets.load_standard_font(12, app::StandardFontWeight::Regular);
    auto* dark = view_model_.dark_mode_subject();

    mode_label_ = make_label(column, big, &lv_font_montserrat_20, dark);
    mem_label_ = make_label(column, regular, &lv_font_montserrat_14, dark);
    detail_label_ = make_label(column, small, &lv_font_montserrat_12, dark);
    password_label_ = make_label(column, regular, &lv_font_montserrat_14, dark);
    message_label_ = make_label(column, small, &lv_font_montserrat_12, dark);
    auto* hint_label = make_label(column, small, &lv_font_montserrat_12, dark);
    lv_label_set_text(hint_label, "Tab: settings · hold ESC: exit");
    lv_obj_set_style_text_color(hint_label, view::palette(view_model_.is_dark_mode()).text_disabled, 0);

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
    view_model_.set_busy(false);  // not set_busy(): the timer is gone
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
    int exit_code = 0;
    if (panel->job_.poll(exit_code)) {
        panel->finish_switch(exit_code);
        return;
    }
    const uint32_t elapsed = panel->job_.elapsed_s();
    if (elapsed != panel->last_elapsed_s_) {  // the text changes once per second
        panel->last_elapsed_s_ = elapsed;
        panel->set_message(std::string(panel->job_to_server_ ? "Enabling server mode… "
                                                              : "Restoring desktop… ") +
                               std::to_string(elapsed) + " s",
                           Tone::Info);
    }
}

bool ServerPanel::key_cb(uint32_t key, void* user_data) {
    auto* panel = static_cast<ServerPanel*>(user_data);
    return panel && panel->on_key(key);
}

bool ServerPanel::on_key(uint32_t key) {
    if (job_.running()) {
        return true;  // ignore keys while a switch is running
    }
    if (key == kKeyTab) {
        view_model_.show_butter_page();  // settings page
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
    server_mode_ = access(kStateFile, F_OK) == 0;
    const auto mem = read_meminfo();
    const bool desktop = desktop_running();

    char text[96];
    const auto colors = view::palette(view_model_.is_dark_mode());
    set_text_if_changed(mode_label_, server_mode_ ? "Mode: SERVER" : "Mode: normal");
    lv_obj_set_style_text_color(mode_label_, server_mode_ ? colors.success : colors.warning, 0);
    std::snprintf(text, sizeof(text), "Free %ld of %ld MB", mem.avail_mb, mem.total_mb);
    set_text_if_changed(mem_label_, text);
    std::snprintf(text, sizeof(text), "Swap %ld MB · desktop %s", mem.swap_used_mb,
                  desktop ? "running" : "stopped");
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
    job_to_server_ = !server_mode_;
    std::vector<std::string> argv;
    if (job_to_server_) {
        const auto groups = settings::load_enabled_groups();
        if (groups.empty()) {
            SudoJob::wipe(password_);
            update_password_label();
            set_message("Nothing selected in settings", Tone::Warning);
            return;
        }
        argv.push_back(settings::script_path("server-mode.sh"));
        argv.insert(argv.end(), groups.begin(), groups.end());
    } else {
        argv.push_back(settings::script_path("normal-mode.sh"));
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
    LOG_INFO("switching to {} mode via {}", job_to_server_ ? "server" : "normal", argv.front());
    set_message(job_to_server_ ? "Enabling server mode…" : "Restoring desktop…", Tone::Info);
}

void ServerPanel::finish_switch(int exit_code) {
    set_busy(false);
    const auto& output = job_.output();
    LOG_INFO("switch finished, exit code {}, output:\n{}", exit_code, output);
    proc_scanned_ = false;  // the desktop just started or stopped: look again now
    refresh_status();

    if (exit_code == 0) {
        std::string summary = "Done";
        constexpr const char* kMarker = "MemAvailable: ";
        if (const auto pos = output.rfind(kMarker); pos != std::string::npos) {
            const auto start = pos + std::strlen(kMarker);
            summary += ": " + output.substr(start, output.find('\n', start) - start);
        } else if (output.find("already") != std::string::npos) {
            summary = "Already in this mode";
        }
        set_message(summary, Tone::Success);
    } else if (SudoJob::wrong_password(output)) {
        set_message("Wrong password", Tone::Error);
    } else if (exit_code == 3) {
        set_message("Busy: another switch is running", Tone::Warning);
    } else if (exit_code == 127) {
        set_message("sudo or script not found", Tone::Error);
    } else {
        set_message("Partly failed, code " + std::to_string(exit_code) + " (see log)", Tone::Error);
    }
}

} // namespace screen
