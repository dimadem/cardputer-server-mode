/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

enum class AppPage {
    Apple = 0,  // main Server Mode page
    Butter = 1, // settings page
};

class BaseModel {
public:
    static const char* app_title();

    bool dark_mode() const;
    void set_dark_mode(bool enabled);

    AppPage current_page() const;
    void set_current_page(AppPage page);

private:
    bool dark_mode_ = true; // Server Mode is dark by default
    AppPage current_page_ = AppPage::Apple;
};

} // namespace model
