/*
 * Small helpers shared by the Server Mode panels.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bindings.h"
#include "lvgl.h"
#include "theme.h"

#include <cstring>
#include <string>

namespace screen {

// Mirrors view::widgets::ToastTone, plus Info.
enum class Tone { Normal, Info, Success, Warning, Error };

constexpr uint32_t kKeyTab = '\t';  // KEY_TAB as mapped by platform/linux_input.cpp
constexpr size_t kMaxPassword = 64;

inline lv_color_t tone_color(const view::ThemePalette& colors, Tone tone) {
    switch (tone) {
        case Tone::Info: return colors.info;
        case Tone::Success: return colors.success;
        case Tone::Warning: return colors.warning;
        case Tone::Error: return colors.error;
        case Tone::Normal: break;
    }
    return colors.text;
}

// LVGL redraws a label on every set_text: skip it when nothing changed.
inline void set_text_if_changed(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

inline void set_tone_text(lv_obj_t* label, const std::string& text, Tone tone, bool dark_mode) {
    set_text_if_changed(label, text.c_str());
    lv_obj_set_style_text_color(label, tone_color(view::palette(dark_mode), tone), 0);
}

// One-line themed label used by both panels.
inline lv_obj_t* make_label(lv_obj_t* parent,
                            const lv_font_t* font,
                            const lv_font_t* fallback,
                            lv_subject_t* dark_mode_subject) {
    auto* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font ? font : fallback, 0);
    reactive::bind_theme(label, dark_mode_subject, reactive::ThemeRole::Text);
    lv_label_set_text(label, "");
    return label;
}

// Shared password editing: returns true when the key was a password edit.
// The string is reserved once (kMaxPassword + 1 for the newline SudoJob adds),
// so typing never reallocates and leaves no stale copies behind.
inline bool edit_password(std::string& password, uint32_t key) {
    if (password.capacity() < kMaxPassword + 1) {
        password.reserve(kMaxPassword + 1);
    }
    if (key == LV_KEY_BACKSPACE) {
        if (!password.empty()) {
            password.back() = '\0';
            password.pop_back();
        }
        return true;
    }
    if (key == LV_KEY_DEL) {
        for (auto& ch : password) ch = '\0';
        password.clear();
        return true;
    }
    if (key >= 32 && key <= 126) {
        if (password.size() < kMaxPassword) password.push_back(static_cast<char>(key));
        return true;
    }
    return false;
}

inline std::string password_text(const std::string& password) {
    std::string text = "Password: ";
    text.append(password.size(), '*');
    text += "_";
    return text;
}

} // namespace screen
