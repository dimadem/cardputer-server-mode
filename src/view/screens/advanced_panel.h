/*
 * Settings page: which services server mode stops (instant) and boot-time
 * tweaks that need a reboot (GPU memory, boot into server mode, HDMI, camera, cloud-init).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_common.h"
#include "sudo_job.h"

#include "lvgl.h"

#include <map>
#include <string>
#include <vector>

namespace app {
class AssetManager;
}

namespace viewmodel {
class BaseViewModel;
}

namespace screen {

class AdvancedPanel {
public:
    AdvancedPanel(lv_obj_t* content, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~AdvancedPanel();

    AdvancedPanel(const AdvancedPanel&) = delete;
    AdvancedPanel& operator=(const AdvancedPanel&) = delete;

private:
    enum class Kind { Header, Group, Gpu, Flag, Reset };
    enum class Prompt { None, Apply, Reset };  // what Enter in the password prompt does

    struct Item {
        Kind kind;
        std::string title;
        std::string key;  // group id or advanced.sh key
        bool value{false};
        int gpu{128};
        lv_obj_t* row{nullptr};
        lv_obj_t* label{nullptr};        // name, left
        lv_obj_t* value_label{nullptr};  // state, right
    };

    static void tick_cb(lv_timer_t* timer);
    static bool key_cb(uint32_t key, void* user_data);

    bool on_key(uint32_t key);
    void load_status();
    int status_int(const char* key, int fallback) const;
    std::string enabled_groups_csv() const;
    bool boot_groups_outdated() const;
    bool item_changed(const Item& item) const;
    bool has_boot_changes() const;
    std::vector<std::string> changed_args() const;
    void add_item(Item item);
    void render_item(size_t index);
    void render_all();
    void move_selection(int delta);
    void change_value(int delta);
    void save_groups();
    void ask_password(Prompt prompt);
    void cancel_password();
    void start_job();
    void finish_job(int exit_code);
    void set_message(const std::string& text, Tone tone);
    void update_password_label();
    void set_busy(bool busy);

    viewmodel::BaseViewModel& view_model_;
    const lv_font_t* row_font_{nullptr};
    lv_obj_t* list_{nullptr};
    lv_obj_t* password_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
    lv_timer_t* timer_{nullptr};  // runs only while a job runs

    std::vector<Item> items_;
    size_t selected_{0};
    std::map<std::string, std::string> current_;  // "advanced.sh status": empty if unavailable
    std::string password_;
    SudoJob job_;
    Prompt prompt_{Prompt::None};
    uint32_t last_elapsed_s_{UINT32_MAX};
};

} // namespace screen
