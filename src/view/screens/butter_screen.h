/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "advanced_panel.h"
#include "base_screen.h"

#include <memory>

namespace screen {

// Second page of the app: settings (stop groups and reboot-time tweaks).
class ButterScreen : public BaseScreen {
public:
    ButterScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;

    std::unique_ptr<AdvancedPanel> panel_;
};

} // namespace screen
