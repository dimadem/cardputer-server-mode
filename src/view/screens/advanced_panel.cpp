/*
 * SPDX-License-Identifier: MIT
 */

#include "advanced_panel.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "linux_input.h"
#include "logger.h"
#include "server_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>

namespace screen {
namespace {

constexpr uint32_t kJobPeriodMs = 200;
constexpr std::array<int, 4> kGpuValues = {128, 64, 32, 16};
constexpr int kListHeight = 94;

std::map<std::string, std::string> read_advanced_status() {
    std::map<std::string, std::string> values;
    const std::string command = settings::script_path("advanced.sh") + " status 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return values;
    }
    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) output += buffer;
    if (pclose(pipe) != 0) {
        return {};
    }
    std::istringstream stream(output);
    std::string token;
    while (stream >> token) {
        const auto eq = token.find('=');
        if (eq != std::string::npos) values[token.substr(0, eq)] = token.substr(eq + 1);
    }
    return values;
}

} // namespace

AdvancedPanel::AdvancedPanel(lv_obj_t* content,
                             viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets)
    : view_model_(view_model) {
    password_.reserve(kMaxPassword + 1);
    row_font_ = assets.load_standard_font(13, app::StandardFontWeight::Regular);
    if (!row_font_) row_font_ = &lv_font_montserrat_12;
    auto* regular = assets.load_standard_font(14, app::StandardFontWeight::Regular);
    auto* small = assets.load_standard_font(12, app::StandardFontWeight::Regular);
    auto* dark = view_model_.dark_mode_subject();

    auto* column = lv_obj_create(content);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(column, 6, 0);
    lv_obj_set_style_pad_ver(column, 2, 0);
    lv_obj_set_style_pad_row(column, 2, 0);
    lv_obj_clear_flag(column, LV_OBJ_FLAG_SCROLLABLE);

    list_ = lv_obj_create(column);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), kListHeight);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    password_label_ = make_label(column, regular, &lv_font_montserrat_14, dark);
    message_label_ = make_label(column, small, &lv_font_montserrat_12, dark);
    lv_obj_set_style_pad_hor(password_label_, 4, 0);
    lv_obj_set_style_pad_hor(message_label_, 4, 0);

    // ---- items
    const auto enabled = settings::load_enabled_groups();
    add_item({Kind::Header, "Server mode stops (instant):", ""});
    for (const auto& group : settings::stop_groups()) {
        const bool on = std::find(enabled.begin(), enabled.end(), group.id) != enabled.end();
        add_item({Kind::Group, group.title, group.id, on});
    }
    add_item({Kind::Header, "Disable at boot (reboot):", ""});
    add_item({Kind::Flag, "HDMI", "hdmi_off"});
    add_item({Kind::Flag, "Camera", "camera_off"});
    add_item({Kind::Flag, "cloud-init", "cloudinit_off"});
    add_item({Kind::Header, "Boot (reboot):", ""});
    add_item({Kind::Gpu, "GPU memory", "gpu"});
    add_item({Kind::Flag, "Boot into server mode", "boot_server"});
    add_item({Kind::Reset, "Reset all to defaults", "reset"});

    load_status();
    selected_ = 1;  // first selectable row
    render_all();

    update_password_label();
    if (current_.empty()) {
        set_message("Boot status unavailable: reboot options disabled", Tone::Error);
    } else {
        set_message("↑↓ select · ←→ change · Tab back", Tone::Info);
    }

    platform::set_screen_key_handler(key_cb, this);
    timer_ = lv_timer_create(tick_cb, kJobPeriodMs, this);
    lv_timer_pause(timer_);
}

AdvancedPanel::~AdvancedPanel() {
    platform::clear_screen_key_handler(key_cb, this);
    if (timer_) {
        lv_timer_delete(timer_);
    }
    job_.wait();
    view_model_.set_busy(false);
    SudoJob::wipe(password_);
}

void AdvancedPanel::add_item(Item item) {
    item.row = lv_obj_create(list_);
    lv_obj_remove_style_all(item.row);
    lv_obj_set_size(item.row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(item.row, 4, 0);
    lv_obj_set_style_pad_ver(item.row, 1, 0);
    lv_obj_set_style_radius(item.row, 3, 0);
    lv_obj_clear_flag(item.row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(item.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item.row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    item.label = lv_label_create(item.row);
    lv_obj_set_flex_grow(item.label, 1);
    lv_label_set_long_mode(item.label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(item.label, row_font_, 0);

    item.value_label = lv_label_create(item.row);
    lv_obj_set_style_text_font(item.value_label, row_font_, 0);
    lv_label_set_text(item.value_label, "");
    items_.push_back(std::move(item));
}

// ---- state from advanced.sh vs the choices on screen

void AdvancedPanel::load_status() {
    current_ = read_advanced_status();
    for (auto& item : items_) {
        if (item.kind == Kind::Gpu) {
            item.gpu = status_int("gpu", item.gpu);
        } else if (item.kind == Kind::Flag) {
            item.value = status_int(item.key.c_str(), 0) != 0;
        }
    }
}

int AdvancedPanel::status_int(const char* key, int fallback) const {
    const auto it = current_.find(key);
    if (it == current_.end()) return fallback;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

std::string AdvancedPanel::enabled_groups_csv() const {
    std::string csv;
    for (const auto& item : items_) {
        if (item.kind == Kind::Group && item.value) csv += (csv.empty() ? "" : ",") + item.key;
    }
    return csv;
}

// The boot unit stops the groups chosen when it was written: "*" when they differ now.
bool AdvancedPanel::boot_groups_outdated() const {
    if (status_int("boot_server", 0) == 0) return false;
    const auto it = current_.find("boot_groups");
    std::string unit_groups = it == current_.end() ? "" : it->second;
    if (unit_groups.empty()) {  // older unit without groups: it stopped all of them
        for (const auto& group : settings::stop_groups()) {
            unit_groups += (unit_groups.empty() ? "" : ",") + std::string(group.id);
        }
    }
    return unit_groups != enabled_groups_csv();
}

bool AdvancedPanel::item_changed(const Item& item) const {
    if (current_.empty()) return false;  // unknown device state: nothing to compare with
    if (item.kind == Kind::Gpu) return status_int("gpu", 64) != item.gpu;
    if (item.kind != Kind::Flag) return false;
    const bool now = status_int(item.key.c_str(), 0) != 0;
    if (now != item.value) return true;
    return item.key == "boot_server" && item.value && boot_groups_outdated();
}

bool AdvancedPanel::has_boot_changes() const {
    return !current_.empty() &&
           std::any_of(items_.begin(), items_.end(), [this](const Item& item) { return item_changed(item); });
}

// Only what differs from the device goes to advanced.sh.
std::vector<std::string> AdvancedPanel::changed_args() const {
    std::vector<std::string> args;
    for (const auto& item : items_) {
        if (!item_changed(item)) continue;
        if (item.kind == Kind::Gpu) {
            args.push_back("gpu=" + std::to_string(item.gpu));
        } else {
            args.push_back(item.key + "=" + (item.value ? "1" : "0"));
            if (item.key == "boot_server" && item.value) args.push_back("groups=" + enabled_groups_csv());
        }
    }
    return args;
}

// ---- rendering

void AdvancedPanel::render_item(size_t index) {
    auto& item = items_[index];
    const auto colors = view::palette(view_model_.is_dark_mode());
    // left: name (yellow + "*" when changed but not applied yet)
    // right: "yes" (will be switched off / on) in color, "no" dimmed
    std::string value;
    bool active = false;
    switch (item.kind) {
        case Kind::Header:
        case Kind::Reset:
            break;
        case Kind::Group:
        case Kind::Flag:
            active = item.value;
            value = item.value ? "yes" : "no";
            break;
        case Kind::Gpu:
            active = item.gpu != 128;
            value = "< " + std::to_string(item.gpu) + " MB >";
            break;
    }
    const bool changed = item_changed(item);
    std::string name = (item.kind == Kind::Group || item.kind == Kind::Gpu || item.kind == Kind::Flag ? "  " : "") +
                       item.title;
    if (changed) name += " *";
    set_text_if_changed(item.label, name.c_str());
    set_text_if_changed(item.value_label, value.c_str());

    lv_color_t color = colors.text;
    if (item.kind == Kind::Header) color = colors.text_disabled;
    else if (item.kind == Kind::Reset) color = colors.error;
    else if (changed) color = colors.warning;
    lv_obj_set_style_text_color(item.label, color, 0);
    lv_obj_set_style_text_color(item.value_label, active ? colors.success : colors.text_disabled, 0);

    lv_obj_set_style_bg_color(item.row, colors.primary, 0);
    lv_obj_set_style_bg_opa(item.row, index == selected_ ? LV_OPA_30 : LV_OPA_TRANSP, 0);
}

void AdvancedPanel::render_all() {
    for (size_t i = 0; i < items_.size(); ++i) render_item(i);
    if (selected_ < items_.size()) {
        lv_obj_scroll_to_view(items_[selected_].row, LV_ANIM_OFF);
    }
}

void AdvancedPanel::move_selection(int delta) {
    size_t next = selected_;
    while (true) {
        if (delta < 0 && next == 0) return;
        next = delta < 0 ? next - 1 : next + 1;
        if (next >= items_.size()) return;
        if (items_[next].kind != Kind::Header) break;
    }
    const size_t previous = selected_;
    selected_ = next;
    render_item(previous);
    render_item(selected_);
    // keep the section header visible when moving to the first row of a section
    if (items_[selected_ - 1].kind == Kind::Header) {
        lv_obj_scroll_to_view(items_[selected_ - 1].row, LV_ANIM_OFF);
    }
    lv_obj_scroll_to_view(items_[selected_].row, LV_ANIM_OFF);
}

// ---- editing

void AdvancedPanel::save_groups() {
    std::vector<std::string> groups;
    for (const auto& item : items_) {
        if (item.kind == Kind::Group && item.value) groups.push_back(item.key);
    }
    if (settings::save_enabled_groups(groups)) {
        set_message(boot_groups_outdated() ? "Saved. For boot: Enter (reboot)"
                                           : "Saved: applies on next server mode switch",
                    Tone::Success);
    } else {
        set_message("Failed to save settings", Tone::Error);
    }
}

void AdvancedPanel::change_value(int delta) {
    auto& item = items_[selected_];
    switch (item.kind) {
        case Kind::Group:
            item.value = !item.value;
            save_groups();
            render_all();  // the "boot into server" row may become outdated
            return;
        case Kind::Flag:
            item.value = !item.value;
            break;
        case Kind::Gpu: {
            auto it = std::find(kGpuValues.begin(), kGpuValues.end(), item.gpu);
            int index = it == kGpuValues.end() ? 0 : static_cast<int>(it - kGpuValues.begin());
            const int count = static_cast<int>(kGpuValues.size());
            index = (index + delta + count) % count;  // wraps: 128 -> 64 -> 32 -> 16 -> 128
            item.gpu = kGpuValues[static_cast<size_t>(index)];
            break;
        }
        case Kind::Header:
        case Kind::Reset:
            return;
    }
    render_item(selected_);
    if (item.kind == Kind::Gpu && item.gpu == 16) {
        set_message("16 MB: no camera or hardware video", Tone::Warning);
    } else {
        set_message(has_boot_changes() ? "Enter: apply and reboot" : "No changes", Tone::Info);
    }
}

// ---- keys

bool AdvancedPanel::key_cb(uint32_t key, void* user_data) {
    auto* panel = static_cast<AdvancedPanel*>(user_data);
    return panel && panel->on_key(key);
}

bool AdvancedPanel::on_key(uint32_t key) {
    if (job_.running()) {
        return true;
    }

    // password prompt: only typing, Enter (confirm) and Tab (cancel)
    if (prompt_ != Prompt::None) {
        if (key == kKeyTab) {
            cancel_password();
        } else if (key == LV_KEY_ENTER) {
            start_job();
        } else if (edit_password(password_, key)) {
            update_password_label();
        }
        return true;
    }

    // Cardputer arrows sit on ; . , / (Fn layer): accept the plain keys too,
    // so the list works without holding Fn.
    switch (key) {
        case ';': key = LV_KEY_UP; break;
        case '.': key = LV_KEY_DOWN; break;
        case ',': key = LV_KEY_LEFT; break;
        case '/': key = LV_KEY_RIGHT; break;
        default: break;
    }

    switch (key) {
        case kKeyTab:
            view_model_.show_apple_page();
            break;
        case LV_KEY_UP:
            move_selection(-1);
            break;
        case LV_KEY_DOWN:
            move_selection(1);
            break;
        case LV_KEY_LEFT:
            change_value(-1);
            break;
        case LV_KEY_RIGHT:
            change_value(1);
            break;
        case LV_KEY_ENTER:
            if (current_.empty()) {
                set_message("Boot status unavailable", Tone::Error);
            } else if (items_[selected_].kind == Kind::Reset) {
                ask_password(Prompt::Reset);
            } else if (has_boot_changes()) {
                ask_password(Prompt::Apply);
            } else {
                set_message("No changes that need a reboot", Tone::Warning);
            }
            break;
        default:
            break;  // swallow every other key: no accidental typing while picking options
    }
    return true;
}

// ---- password prompt and the root job

void AdvancedPanel::update_password_label() {
    const auto colors = view::palette(view_model_.is_dark_mode());
    if (prompt_ != Prompt::None) {
        set_text_if_changed(password_label_, password_text(password_).c_str());
        lv_obj_set_style_text_color(password_label_, colors.text, 0);
    } else {
        set_text_if_changed(password_label_, "Enter: apply with reboot");
        lv_obj_set_style_text_color(password_label_, colors.text_disabled, 0);
    }
}

void AdvancedPanel::ask_password(Prompt prompt) {
    prompt_ = prompt;
    update_password_label();
    set_message(prompt == Prompt::Reset ? "Reset: sudo password, Enter · Tab cancel"
                                        : "sudo password, Enter · Tab cancel",
                Tone::Warning);
}

void AdvancedPanel::cancel_password() {
    prompt_ = Prompt::None;
    SudoJob::wipe(password_);
    update_password_label();
    set_message("Cancelled", Tone::Info);
}

void AdvancedPanel::set_busy(bool busy) {
    view_model_.set_busy(busy);
    if (busy) {
        last_elapsed_s_ = UINT32_MAX;
        lv_timer_resume(timer_);
    } else {
        lv_timer_pause(timer_);
    }
}

void AdvancedPanel::start_job() {
    if (password_.empty()) {
        set_message("Enter the sudo password first", Tone::Warning);
        return;
    }
    std::vector<std::string> argv{settings::script_path("advanced.sh")};
    if (prompt_ == Prompt::Reset) {
        argv.push_back("reset");
    } else {
        argv.push_back("apply");
        const auto args = changed_args();
        argv.insert(argv.end(), args.begin(), args.end());
    }
    argv.push_back("--reboot");

    std::string error;
    const bool started = job_.start(password_, argv, error);
    update_password_label();
    if (!started) {
        set_message("Failed to start: " + error, Tone::Error);
        return;
    }
    set_busy(true);
    LOG_INFO("advanced: {}", prompt_ == Prompt::Reset ? "reset" : "apply");
    set_message("Applying…", Tone::Info);
}

void AdvancedPanel::tick_cb(lv_timer_t* timer) {
    auto* panel = static_cast<AdvancedPanel*>(lv_timer_get_user_data(timer));
    if (!panel || !panel->job_.running()) {
        return;
    }
    int exit_code = 0;
    if (panel->job_.poll(exit_code)) {
        panel->finish_job(exit_code);
        return;
    }
    const uint32_t elapsed = panel->job_.elapsed_s();
    if (elapsed != panel->last_elapsed_s_) {
        panel->last_elapsed_s_ = elapsed;
        panel->set_message("Applying… " + std::to_string(elapsed) + " s", Tone::Info);
    }
}

void AdvancedPanel::finish_job(int exit_code) {
    set_busy(false);
    const auto& output = job_.output();
    LOG_INFO("advanced finished, exit code {}, output:\n{}", exit_code, output);
    if (exit_code == 0) {
        const bool reset = prompt_ == Prompt::Reset;
        prompt_ = Prompt::None;
        update_password_label();
        set_message(reset ? "Reset done. Rebooting…" : "Applied. Rebooting…", Tone::Success);
        return;
    }
    if (SudoJob::wrong_password(output)) {
        // nothing was changed: keep the user's choices and the prompt
        set_message("Wrong password · Tab cancel", Tone::Error);
        return;
    }
    // the script may have applied part of the changes: show the real state
    prompt_ = Prompt::None;
    load_status();
    render_all();
    update_password_label();
    set_message(exit_code == 3 ? "Busy: another switch is running"
                               : "Error, code " + std::to_string(exit_code) + " (see log)",
                Tone::Error);
}

void AdvancedPanel::set_message(const std::string& text, Tone tone) {
    set_tone_text(message_label_, text, tone, view_model_.is_dark_mode());
}

} // namespace screen
