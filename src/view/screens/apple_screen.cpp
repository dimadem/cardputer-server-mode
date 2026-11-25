/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "apple_screen.h"

namespace screen {

AppleScreen::AppleScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void AppleScreen::build_content(lv_obj_t* content) {
    panel_ = std::make_unique<ServerPanel>(content, view_model(), assets());
}

} // namespace screen
