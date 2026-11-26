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
#include <iterator>

namespace screen {
namespace {

constexpr uint32_t kJobPeriodMs = 200;
constexpr int kListHeight = 94;
constexpr const char* kHint = "↑↓ select · ←→ change · Enter apply · Tab back";

} // namespace

AdvancedPanel::AdvancedPanel(lv_obj_t* content,
                             viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets)
    : view_model_(view_model) {
    password_.reserve(kMaxPassword + 1);
    row_font_ = assets.load_standard_font(13, app::StandardFontWeight::Regular);
    if (!row_font_) row_font_ = &lv_font_montserrat_12;
    const auto* regular = assets.load_standard_font(14, app::StandardFontWeight::Regular);
    const auto* small = assets.load_standard_font(12, app::StandardFontWeight::Regular);
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
    using settings::BootFlag;
    const auto header = [](std::string_view title) { return Item{Kind::Header, title}; };
    const auto flag = [](std::string_view title, BootFlag boot_flag) {
        Item item{Kind::Flag, title};
        item.flag = boot_flag;
        return item;
    };

    const auto enabled = settings::load_enabled_groups();
    add_item(header("Server mode stops (instant):"));
    for (const auto& info : settings::kStopGroups) {
        Item item{Kind::Group, info.title};
        item.group = info.group;
        item.value = enabled.contains(info.group);
        add_item(item);
    }
    add_item(header("Disable at boot (reboot):"));
    add_item(flag("HDMI", BootFlag::HdmiOff));
    add_item(flag("Camera", BootFlag::CameraOff));
    add_item(flag("cloud-init", BootFlag::CloudInitOff));
    add_item(header("Boot (reboot):"));
    add_item(Item{Kind::Gpu, "GPU memory"});
    add_item(flag("Boot into server mode", BootFlag::BootServer));
    add_item(Item{Kind::Reset, "Reset all to defaults"});

    load_status();
    selected_ = 1; // first selectable row
    render_all();

    update_password_label();
    if (!status_) {
        set_message("Boot status unavailable: reboot options disabled", Tone::Error);
    }
    else {
        set_message(kHint, Tone::Info);
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
    lv_obj_set_flex_align(item.row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    item.label = lv_label_create(item.row);
    lv_obj_set_flex_grow(item.label, 1);
    lv_label_set_long_mode(item.label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(item.label, row_font_, 0);

    item.value_label = lv_label_create(item.row);
    lv_obj_set_style_text_font(item.value_label, row_font_, 0);
    lv_label_set_text(item.value_label, "");
    items_.push_back(item);
}

// ---- state from advanced.sh vs the choices on screen

void AdvancedPanel::load_status() {
    status_ = settings::read_boot_status();
    if (!status_) {
        return;
    }
    for (auto& item : items_) {
        if (item.kind == Kind::Gpu) {
            item.gpu = settings::gpu_from_megabytes(status_->gpu_mb);
        }
        else if (item.kind == Kind::Flag) {
            item.value = status_->flag(item.flag);
        }
    }
}

settings::GroupSet AdvancedPanel::enabled_groups() const {
    settings::GroupSet groups;
    for (const auto& item : items_) {
        if (item.kind == Kind::Group) groups.set(item.group, item.value);
    }
    return groups;
}

// The boot unit stops the groups chosen when it was written: "*" when they differ now.
bool AdvancedPanel::boot_groups_outdated() const {
    return status_ && status_->boot_server && status_->boot_groups != enabled_groups();
}

bool AdvancedPanel::item_changed(const Item& item) const {
    if (!status_) return false; // unknown device state: nothing to compare with
    switch (item.kind) {
        case Kind::Gpu: return item.gpu && settings::megabytes(*item.gpu) != status_->gpu_mb;
        case Kind::Flag:
            if (status_->flag(item.flag) != item.value) return true;
            return item.flag == settings::BootFlag::BootServer && item.value &&
                   boot_groups_outdated();
        case Kind::Header:
        case Kind::Group:
        case Kind::Reset: return false;
    }
    return false;
}

bool AdvancedPanel::has_boot_changes() const {
    return status_ && std::any_of(items_.begin(), items_.end(), [this](const Item& item) {
               return item_changed(item);
           });
}

// Only what differs from the device goes to advanced.sh.
std::vector<std::string> AdvancedPanel::changed_args() const {
    std::vector<std::string> args;
    for (const auto& item : items_) {
        if (!item_changed(item)) continue;
        if (item.kind == Kind::Gpu && item.gpu) {
            args.push_back("gpu=" + std::to_string(settings::megabytes(*item.gpu)));
        }
        else if (item.kind == Kind::Flag) {
            args.push_back(std::string(settings::flag_key(item.flag)) + "=" +
                           (item.value ? "1" : "0"));
            if (item.flag == settings::BootFlag::BootServer && item.value) {
                args.push_back("groups=" + enabled_groups().csv());
            }
        }
    }
    return args;
}

bool AdvancedPanel::boot_server_chosen() const {
    return std::any_of(items_.begin(), items_.end(), [](const Item& item) {
        return item.kind == Kind::Flag && item.flag == settings::BootFlag::BootServer && item.value;
    });
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
        case Kind::Reset: break;
        case Kind::Group:
        case Kind::Flag:
            active = item.value;
            value = item.value ? "yes" : "no";
            break;
        case Kind::Gpu: {
            // nullopt: show the value from config.txt as it is
            const int mb =
                item.gpu ? settings::megabytes(*item.gpu) : (status_ ? status_->gpu_mb : 0);
            active = item.gpu != settings::GpuMemory::Mb128;
            value = mb > 0 ? "< " + std::to_string(mb) + " MB >" : "?";
            break;
        }
    }
    const bool changed = item_changed(item);
    const bool indented =
        item.kind == Kind::Group || item.kind == Kind::Gpu || item.kind == Kind::Flag;
    std::string name = std::string(indented ? "  " : "") + std::string(item.title);
    if (changed) name += " *";
    set_text_if_changed(item.label, name.c_str());
    set_text_if_changed(item.value_label, value.c_str());

    lv_color_t color = colors.text;
    if (item.kind == Kind::Header)
        color = colors.text_disabled;
    else if (item.kind == Kind::Reset)
        color = colors.error;
    else if (changed)
        color = colors.warning;
    lv_obj_set_style_text_color(item.label, color, 0);
    lv_obj_set_style_text_color(item.value_label,
                                active ? colors.success : colors.text_disabled,
                                0);

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

bool AdvancedPanel::save_groups() {
    if (!settings::save_enabled_groups(enabled_groups())) {
        set_message("Failed to save settings", Tone::Error);
        return false;
    }
    set_message(boot_groups_outdated() ? "Saved. For boot: Enter (reboot)"
                                       : "Saved: applies on next server mode switch",
                Tone::Success);
    return true;
}

void AdvancedPanel::change_value(int delta) {
    auto& item = items_[selected_];
    switch (item.kind) {
        case Kind::Group:
            item.value = !item.value;
            if (!save_groups()) item.value = !item.value; // the screen keeps showing the file
            render_all(); // the "boot into server" row may become outdated
            return;
        case Kind::Flag:
        case Kind::Gpu:
            if (!status_) {
                set_message("Boot status unavailable", Tone::Error);
                return;
            }
            break;
        case Kind::Header:
        case Kind::Reset: return;
    }
    switch (item.kind) {
        case Kind::Flag: item.value = !item.value; break;
        case Kind::Gpu: {
            const auto& choices = settings::kGpuChoices;
            const auto it =
                item.gpu ? std::find(choices.begin(), choices.end(), *item.gpu) : choices.end();
            const auto count = static_cast<std::ptrdiff_t>(choices.size());
            const auto index = it == choices.end() ? 0 : std::distance(choices.begin(), it);
            // wraps: 128 -> 64 -> 32 -> 16 -> 128
            item.gpu = choices[static_cast<size_t>((index + delta + count) % count)];
            break;
        }
        case Kind::Header:
        case Kind::Group:
        case Kind::Reset: return;
    }
    render_item(selected_);
    if (item.kind == Kind::Gpu && item.gpu == settings::GpuMemory::Mb16) {
        set_message("16 MB: no camera or hardware video", Tone::Warning);
    }
    else {
        set_message(has_boot_changes() ? "Enter: apply and reboot" : "No changes", Tone::Info);
    }
}

// ---- keys

bool AdvancedPanel::key_cb(uint32_t key, void* user_data) {
    auto* panel = static_cast<AdvancedPanel*>(user_data);
    if (panel == nullptr) {
        return false;
    }
    panel->on_key(key);
    return true;
}

void AdvancedPanel::on_key(uint32_t key) {
    if (job_.running()) {
        return;
    }

    // password prompt: only typing, Enter (confirm) and Tab (cancel)
    if (prompt_ != Prompt::None) {
        if (key == kKeyTab) {
            cancel_password();
        }
        else if (key == LV_KEY_ENTER) {
            start_job();
        }
        else if (edit_password(password_, key)) {
            update_password_label();
        }
        return;
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
        case kKeyTab: view_model_.show_apple_page(); break;
        case LV_KEY_UP: move_selection(-1); break;
        case LV_KEY_DOWN: move_selection(1); break;
        case LV_KEY_LEFT: change_value(-1); break;
        case LV_KEY_RIGHT: change_value(1); break;
        case LV_KEY_ENTER:
            // reset does not need the status: it puts back the backups and the defaults
            if (items_[selected_].kind == Kind::Reset) {
                ask_password(Prompt::Reset);
            }
            else if (!status_) {
                set_message("Boot status unavailable", Tone::Error);
            }
            else if (boot_server_chosen() && has_boot_changes() && enabled_groups().empty()) {
                set_message("Boot into server mode: select a group", Tone::Warning);
            }
            else if (has_boot_changes()) {
                ask_password(Prompt::Apply);
            }
            else {
                set_message("No changes that need a reboot", Tone::Warning);
            }
            break;
        default: break; // swallow every other key: no accidental typing while picking options
    }
}

// ---- password prompt and the root job

void AdvancedPanel::update_password_label() {
    const auto colors = view::palette(view_model_.is_dark_mode());
    // the password row is empty until a password is asked (Enter is in the hint line);
    // it keeps its height, so the hint line stays at the bottom
    set_text_if_changed(password_label_,
                        prompt_ != Prompt::None ? password_text(password_).c_str() : "");
    lv_obj_set_style_text_color(password_label_, colors.text, 0);
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
    set_message(kHint, Tone::Info);
}

void AdvancedPanel::set_busy(bool busy) {
    view_model_.set_busy(busy);
    if (busy) {
        last_elapsed_s_ = UINT32_MAX;
        lv_timer_resume(timer_);
    }
    else {
        lv_timer_pause(timer_);
    }
}

void AdvancedPanel::start_job() {
    if (password_.empty()) {
        set_message("Enter the sudo password first", Tone::Warning);
        return;
    }
    std::vector<std::string> argv{settings::script_path(settings::Script::Advanced)};
    if (prompt_ == Prompt::Reset) {
        argv.emplace_back("reset");
    }
    else {
        argv.emplace_back("apply");
        const auto args = changed_args();
        argv.insert(argv.end(), args.begin(), args.end());
    }
    argv.emplace_back("--reboot");

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
    if (const auto result = panel->job_.poll()) {
        panel->finish_job(*result);
        return;
    }
    const uint32_t elapsed = panel->job_.elapsed_s();
    if (elapsed != panel->last_elapsed_s_) {
        panel->last_elapsed_s_ = elapsed;
        panel->set_message("Applying… " + std::to_string(elapsed) + " s", Tone::Info);
    }
}

void AdvancedPanel::finish_job(const JobResult& result) {
    set_busy(false);
    const auto& output = job_.output();
    LOG_INFO("advanced finished, exit code {}, output:\n{}", result.exit_code, output);
    switch (result.outcome) {
        case JobOutcome::Success: {
            const bool reset = prompt_ == Prompt::Reset;
            prompt_ = Prompt::None;
            update_password_label();
            set_message(reset ? "Reset done. Rebooting…" : "Applied. Rebooting…", Tone::Success);
            return;
        }
        case JobOutcome::WrongPassword:
            // nothing was changed: keep the user's choices and the prompt
            set_message("Wrong password · Tab cancel", Tone::Error);
            return;
        case JobOutcome::NotAllowed:
            prompt_ = Prompt::None;
            update_password_label();
            set_message("sudo: not allowed for this user", Tone::Error);
            return;
        case JobOutcome::Busy:
        case JobOutcome::PartialFailure:
        case JobOutcome::BadArguments:
        case JobOutcome::NotFound:
        case JobOutcome::Killed:
        case JobOutcome::OtherError: break;
    }
    // the script may have applied part of the changes: show the real state
    prompt_ = Prompt::None;
    load_status();
    render_all();
    update_password_label();
    set_message(result.outcome == JobOutcome::Busy
                    ? "Busy: another switch is running"
                    : "Error, code " + std::to_string(result.exit_code) + " (see log)",
                Tone::Error);
}

void AdvancedPanel::set_message(const std::string& text, Tone tone) {
    set_tone_text(message_label_, text, tone, view_model_.is_dark_mode());
}

} // namespace screen
