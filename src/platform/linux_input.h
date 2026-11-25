#pragma once

#include "lvgl.h"

#include <cstdint>

namespace platform {

constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U;
constexpr uint32_t kKeyHelp = kSpecialKeyBase | 19U;

using KeyReleaseListener = void (*)(uint32_t key, const char* key_name, void* user_data);
// Receives keys not consumed by the global listener (text input of the active screen).
using ScreenKeyHandler = bool (*)(uint32_t key, void* user_data);

using GlobalKeyListener = bool (*)(uint32_t key,
                                   const char* key_name,
                                   bool long_pressed,
                                   void* user_data);

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);
void set_key_release_listener(KeyReleaseListener listener, void* user_data);
void clear_key_release_listener(KeyReleaseListener listener, const void* user_data);
void set_global_key_listener(GlobalKeyListener listener, void* user_data);
void clear_global_key_listener(GlobalKeyListener listener, const void* user_data);
void set_screen_key_handler(ScreenKeyHandler handler, void* user_data);
void clear_screen_key_handler(ScreenKeyHandler handler, const void* user_data);
const char* describe_key(uint32_t key);

} // namespace platform
