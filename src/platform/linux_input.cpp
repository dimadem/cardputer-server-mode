#include "linux_input.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if !USE_DESKTOP
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {
namespace {

bool last_key_pressed = false;
uint32_t pressed_key = 0;
uint32_t press_started_at = 0;
bool long_press_sent = false;
bool pressed_key_consumed = false;
lv_timer_t* long_press_timer = nullptr;
KeyReleaseListener key_release_listener = nullptr;
void* key_release_listener_user_data = nullptr;
ScreenKeyHandler screen_key_handler = nullptr;
void* screen_key_handler_user_data = nullptr;
GlobalKeyListener global_key_listener = nullptr;
void* global_key_listener_user_data = nullptr;

constexpr uint32_t kLongPressMs = 700;

#if !USE_DESKTOP
struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
    bool router_event_pending{false};
    uint32_t router_key{0};
    bool router_pressed{false};
    std::array<uint32_t, KEY_CNT> pressed_keys{}; // mapped key per held evdev code
};
#endif

bool emit_global_key(uint32_t key, bool long_pressed) {
    return global_key_listener != nullptr &&
           global_key_listener(key, describe_key(key), long_pressed, global_key_listener_user_data);
}

void long_press_timer_cb(lv_timer_t*) {
    if (pressed_key == 0 || long_press_sent || lv_tick_elaps(press_started_at) < kLongPressMs) {
        return;
    }

    long_press_sent = true;
    if (emit_global_key(pressed_key, true)) {
        pressed_key_consumed = true;
    }
}

void ensure_long_press_timer() {
    if (!long_press_timer) {
        long_press_timer = lv_timer_create(long_press_timer_cb, 40, nullptr);
    }
    if (long_press_timer) {
        lv_timer_resume(long_press_timer);
        lv_timer_reset(long_press_timer);
    }
}

void route_key_state(uint32_t key, bool pressed) {
    const bool new_hold = pressed && (!last_key_pressed || pressed_key != key);
    if (new_hold) {
        pressed_key = key;
        press_started_at = lv_tick_get();
        long_press_sent = false;
        pressed_key_consumed = emit_global_key(key, false);
        ensure_long_press_timer();
        if (!pressed_key_consumed && screen_key_handler &&
            screen_key_handler(key, screen_key_handler_user_data)) {
            pressed_key_consumed = true;
        }
    }
    else if (!pressed && pressed_key == key) {
        if (key_release_listener) {
            key_release_listener(key, describe_key(key), key_release_listener_user_data);
        }
        pressed_key = 0;
        pressed_key_consumed = false;
        if (long_press_timer) {
            lv_timer_pause(long_press_timer);
        }
    }

    last_key_pressed = pressed;
}

void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

#if !USE_DESKTOP
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (keypad) {
        // LVGL emits LV_EVENT_KEY on every poll, even when evdev had no new event.
        if (!keypad->router_event_pending) {
            return;
        }

        keypad->router_event_pending = false;
        const auto key = keypad->router_key;
        const bool pressed = keypad->router_pressed;

        route_key_state(key, pressed);
        return;
    }
#endif

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    route_key_state(key, pressed);
}

#if !USE_DESKTOP
// each Shift key separately: releasing one while the other is held keeps Shift on
bool left_shift_down = false;
bool right_shift_down = false;

bool shift_down() {
    return left_shift_down || right_shift_down;
}

char shifted(char normal, char with_shift) {
    return shift_down() ? with_shift : normal;
}

uint32_t map_evdev_key(uint16_t code) {
    // letters: evdev codes follow the physical QWERTY rows, not the alphabet
    static constexpr struct {
        uint16_t code;
        char ch;
    } kLetters[] = {
        {KEY_Q, 'q'}, {KEY_W, 'w'}, {KEY_E, 'e'}, {KEY_R, 'r'}, {KEY_T, 't'}, {KEY_Y, 'y'},
        {KEY_U, 'u'}, {KEY_I, 'i'}, {KEY_O, 'o'}, {KEY_P, 'p'}, {KEY_A, 'a'}, {KEY_S, 's'},
        {KEY_D, 'd'}, {KEY_F, 'f'}, {KEY_G, 'g'}, {KEY_H, 'h'}, {KEY_J, 'j'}, {KEY_K, 'k'},
        {KEY_L, 'l'}, {KEY_Z, 'z'}, {KEY_X, 'x'}, {KEY_C, 'c'}, {KEY_V, 'v'}, {KEY_B, 'b'},
        {KEY_N, 'n'}, {KEY_M, 'm'},
    };
    for (const auto& letter : kLetters) {
        if (letter.code == code) {
            return static_cast<uint32_t>(shift_down() ? letter.ch - 'a' + 'A' : letter.ch);
        }
    }

    switch (code) {
        case KEY_1: return shifted('1', '!');
        case KEY_2: return shifted('2', '@');
        case KEY_3: return shifted('3', '#');
        case KEY_4: return shifted('4', '$');
        case KEY_5: return shifted('5', '%');
        case KEY_6: return shifted('6', '^');
        case KEY_7: return shifted('7', '&');
        case KEY_8: return shifted('8', '*');
        case KEY_9: return shifted('9', '(');
        case KEY_0: return shifted('0', ')');
        case KEY_MINUS: return shifted('-', '_');
        case KEY_EQUAL: return shifted('=', '+');
        case KEY_LEFTBRACE: return shifted('[', '{');
        case KEY_RIGHTBRACE: return shifted(']', '}');
        case KEY_BACKSLASH: return shifted('\\', '|');
        case KEY_SEMICOLON: return shifted(';', ':');
        case KEY_APOSTROPHE: return shifted('\'', '"');
        case KEY_GRAVE: return shifted('`', '~');
        case KEY_COMMA: return shifted(',', '<');
        case KEY_DOT: return shifted('.', '>');
        case KEY_SLASH: return shifted('/', '?');
        case KEY_SPACE: return ' ';
        case KEY_ENTER:
        case KEY_KPENTER: return LV_KEY_ENTER;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_TAB: return '\t';
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_DELETE: return LV_KEY_DEL;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_SYSRQ: return kKeyPrintScreen;
        case KEY_HELP: return kKeyHelp;
        default: return 0;
    }
}

bool has_nav_keys(int fd) {
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    auto has_key = [&](int code) {
        const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
    };

    return has_key(KEY_ESC) || has_key(KEY_LEFT) || has_key(KEY_RIGHT) || has_key(KEY_Z) ||
           has_key(KEY_C) || has_key(KEY_4) || has_key(KEY_5) || has_key(KEY_6) || has_key(KEY_7) ||
           has_key(KEY_8) || has_key(KEY_SYSRQ) || has_key(KEY_HELP);
}

void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type == EV_SYN && input.code == SYN_DROPPED) {
            // the kernel dropped events: a Shift release may be lost
            left_shift_down = false;
            right_shift_down = false;
            continue;
        }
        if (input.type != EV_KEY) {
            continue;
        }

        if (input.code == KEY_LEFTSHIFT) {
            left_shift_down = input.value != 0;
            continue;
        }
        if (input.code == KEY_RIGHTSHIFT) {
            right_shift_down = input.value != 0;
            continue;
        }

        if (input.value == 2 || input.code >= keypad->pressed_keys.size()) {
            continue;
        }
        // A release reports the key its press produced: Shift may have changed in between.
        auto& held = keypad->pressed_keys[input.code];
        const bool key_down = input.value != 0; // 1 press, 2 autorepeat, 0 release
        const uint32_t key = key_down ? map_evdev_key(input.code) : held;
        held = key_down ? key : 0;
        if (key == 0) {
            continue;
        }

        keypad->key = key;
        keypad->state = key_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        keypad->router_event_pending = true;
        keypad->router_key = key;
        keypad->router_pressed = keypad->state == LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        return;
    }

    if (keypad->fd >= 0) {
        close(keypad->fd);
    }
    delete keypad;
}

lv_indev_t* create_keypad_from_fd(int fd) {
    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;

    auto* indev = lv_indev_create();
    if (!indev) {
        delete keypad;
        close(fd);
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    attach_key_router(indev);
    return indev;
}

lv_indev_t* try_create_keypad(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LV_LOG_WARN("failed to open input device %s: %s", path, strerror(errno));
        return nullptr;
    }

    if (!has_nav_keys(fd)) {
        close(fd);
        return nullptr;
    }

    LV_LOG_INFO("using evdev key input %s", path);
    return create_keypad_from_fd(fd);
}

void discover_keypads(lv_display_t* display) {
    // APP_KEY_INPUT_DEVICE is a configure-time option: "" (the default) means auto-discovery
    if constexpr (sizeof(APP_KEY_INPUT_DEVICE) > 1) {
        auto* indev = try_create_keypad(APP_KEY_INPUT_DEVICE);
        if (indev) {
            lv_indev_set_display(indev, display);
        }
        return;
    }

    auto* dir = opendir("/dev/input");
    if (!dir) {
        LV_LOG_WARN("failed to open /dev/input: %s", strerror(errno));
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        std::string path = "/dev/input/";
        path += entry->d_name;
        auto* indev = try_create_keypad(path.c_str());
        if (indev) {
            lv_indev_set_display(indev, display);
        }
    }

    closedir(dir);
}
#endif

} // namespace

void init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    discover_keypads(display);
#else
    LV_UNUSED(display);
#endif
}

void attach_key_router(lv_indev_t* indev) {
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
        return;
    }

    lv_indev_add_event_cb(indev, key_event_cb, LV_EVENT_KEY, nullptr);
}

void set_key_release_listener(KeyReleaseListener listener, void* user_data) {
    key_release_listener = listener;
    key_release_listener_user_data = user_data;
}

void clear_key_release_listener(KeyReleaseListener listener, const void* user_data) {
    if (key_release_listener == listener && key_release_listener_user_data == user_data) {
        key_release_listener = nullptr;
        key_release_listener_user_data = nullptr;
    }
}

void set_global_key_listener(GlobalKeyListener listener, void* user_data) {
    global_key_listener = listener;
    global_key_listener_user_data = user_data;
}

void clear_global_key_listener(GlobalKeyListener listener, const void* user_data) {
    if (global_key_listener == listener && global_key_listener_user_data == user_data) {
        global_key_listener = nullptr;
        global_key_listener_user_data = nullptr;
    }
}

void set_screen_key_handler(ScreenKeyHandler handler, void* user_data) {
    screen_key_handler = handler;
    screen_key_handler_user_data = user_data;
}

void clear_screen_key_handler(ScreenKeyHandler handler, const void* user_data) {
    if (screen_key_handler == handler && screen_key_handler_user_data == user_data) {
        screen_key_handler = nullptr;
        screen_key_handler_user_data = nullptr;
    }
}

const char* describe_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ESC: return "Esc";
        case LV_KEY_LEFT: return "Left";
        case LV_KEY_RIGHT: return "Right";
        case kKeyPrintScreen: return "PrintScreen";
        case kKeyHelp: return "Help";
        default: break;
    }

    static char buffer[24];
    if (key >= 32 && key <= 126) {
        std::snprintf(buffer, sizeof(buffer), "%c", static_cast<char>(key));
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "Key_%u", key);
    }
    return buffer;
}

} // namespace platform
