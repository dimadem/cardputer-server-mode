/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "server_panel.h"

#include <memory>

namespace screen {

// Main (and only) page of the app: hosts the Server Mode panel.
class AppleScreen : public BaseScreen {
public:
    AppleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;

    std::unique_ptr<ServerPanel> panel_;
};

} // namespace screen
