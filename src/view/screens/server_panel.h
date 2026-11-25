/*
 * Server Mode main panel: memory state and the switch between the normal
 * desktop and the lean server mode.
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
    static void tick_cb(lv_timer_t* timer);
    static bool key_cb(uint32_t key, void* user_data);

    bool on_key(uint32_t key);
    void refresh_status();
    bool desktop_running();
    void start_switch();
    void finish_switch(int exit_code);
    void set_message(const std::string& text, Tone tone);
    void update_password_label();
    void set_busy(bool busy);

    viewmodel::BaseViewModel& view_model_;
    lv_obj_t* mode_label_{nullptr};
    lv_obj_t* mem_label_{nullptr};
    lv_obj_t* detail_label_{nullptr};
    lv_obj_t* password_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
    lv_timer_t* timer_{nullptr};

    bool server_mode_{false};
    bool job_to_server_{false};
    uint32_t last_elapsed_s_{UINT32_MAX};
    std::string password_;
    SudoJob job_;

    // desktop detection: remember labwc's pid, rescan /proc only now and then
    pid_t desktop_pid_{-1};
    uint32_t last_proc_scan_tick_{0};
    bool proc_scanned_{false};
};

} // namespace screen
