/*
 * Server Mode main panel: memory state and the normal/server mode switch.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_common.h"
#include "sudo_job.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <sys/types.h>

namespace app {
class AssetManager;
}

namespace viewmodel {
class BaseViewModel;
}

namespace screen {

class ServerPanel {
public:
    ServerPanel(lv_obj_t* content, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~ServerPanel();

    ServerPanel(const ServerPanel&) = delete;
    ServerPanel& operator=(const ServerPanel&) = delete;

private:
    enum class Mode { Normal, Server };

    static void tick_cb(lv_timer_t* timer);
    static bool key_cb(uint32_t key, void* user_data);

    bool on_key(uint32_t key);
    bool desktop_running();
    void refresh_status();
    void update_password_row();
    void set_message(const std::string& text, Tone tone);
    void set_busy(bool busy);
    void start_switch();
    void finish_switch(const JobResult& result);
    static std::string success_summary(const std::string& output);

    viewmodel::BaseViewModel& view_model_;
    lv_obj_t* mode_label_{nullptr};
    lv_obj_t* mem_label_{nullptr};
    lv_obj_t* detail_label_{nullptr};
    lv_obj_t* password_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
    lv_timer_t* timer_{nullptr};

    Mode mode_{Mode::Normal};
    Mode job_target_{Mode::Server};
    uint32_t last_elapsed_s_{UINT32_MAX};
    std::string password_;
    SudoJob job_;

    // labwc's pid is cached: /proc is rescanned only every kProcRescanMs
    pid_t desktop_pid_{-1};
    uint32_t last_proc_scan_tick_{0};
    bool proc_scanned_{false};
};

} // namespace screen
