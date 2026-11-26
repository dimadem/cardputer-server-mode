/*
 * Settings page: the groups server mode stops and the boot-time tweaks.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_common.h"
#include "server_settings.h"
#include "sudo_job.h"

#include "lvgl.h"

#include <optional>
#include <string>
#include <string_view>
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
    AdvancedPanel(lv_obj_t* content,
                  viewmodel::BaseViewModel& view_model,
                  app::AssetManager& assets);
    ~AdvancedPanel();

    AdvancedPanel(const AdvancedPanel&) = delete;
    AdvancedPanel& operator=(const AdvancedPanel&) = delete;

private:
    enum class Kind { Header, Group, Gpu, Flag, Reset };
    enum class Prompt { None, Apply, Reset }; // what Enter in the password prompt does

    // Group uses group + value, Flag uses flag + value, Gpu uses gpu
    // (nullopt: config.txt has a value not in kGpuChoices, kept until changed).
    struct Item {
        Kind kind{Kind::Header};
        std::string_view title;
        settings::StopGroup group{settings::StopGroup::Desktop};
        settings::BootFlag flag{settings::BootFlag::BootServer};
        bool value{false};
        std::optional<settings::GpuMemory> gpu = std::nullopt;
        lv_obj_t* row{nullptr};
        lv_obj_t* label{nullptr};
        lv_obj_t* value_label{nullptr};
    };

    static void tick_cb(lv_timer_t* timer);
    static bool key_cb(uint32_t key, void* user_data);

    void on_key(uint32_t key); // takes every key: nothing reaches the template navigation
    void add_item(Item item);
    void load_status();
    settings::GroupSet enabled_groups() const;
    bool boot_groups_outdated() const;
    bool item_changed(const Item& item) const;
    bool has_boot_changes() const;
    std::vector<std::string> changed_args() const;
    bool boot_server_chosen() const;
    void render_item(size_t index);
    void render_all();
    void move_selection(int delta);
    bool save_groups();
    void change_value(int delta);
    void ask_password(Prompt prompt);
    void cancel_password();
    void update_password_row();
    void set_message(const std::string& text, Tone tone);
    void set_busy(bool busy);
    void start_job();
    void finish_job(const JobResult& result);

    viewmodel::BaseViewModel& view_model_;
    const lv_font_t* row_font_{nullptr};
    lv_obj_t* list_{nullptr};
    lv_obj_t* password_label_{nullptr};
    lv_obj_t* message_label_{nullptr};
    lv_timer_t* timer_{nullptr}; // runs only while a job runs

    std::vector<Item> items_;
    size_t selected_{0};
    std::optional<settings::BootStatus> status_; // "advanced.sh status": nullopt if unavailable
    std::string password_;
    SudoJob job_;
    Prompt prompt_{Prompt::None};
    uint32_t last_elapsed_s_{UINT32_MAX};
};

} // namespace screen
