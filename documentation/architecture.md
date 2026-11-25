# Architecture

The app uses the [CardputerZero/Template](https://github.com/CardputerZero/Template) project: C++17, LVGL 9.5, CMake and the MVVM pattern. The app keeps the structure and the style of the template. Most of the new code is in `src/view/screens/` and `scripts/`.

## Layers

```
main.cpp → app::App (life cycle, LVGL main loop, ESC exit, toasts)
             ├─ model::BaseModel           state: current page, theme
             ├─ viewmodel::BaseViewModel   LVGL subjects, actions, busy flag
             ├─ app::ScreenManager         makes a new screen when the page changes
             │    ├─ AppleScreen  → ServerPanel    (main page)
             │    └─ ButterScreen → AdvancedPanel  (Settings page)
             └─ platform::linux_input      evdev keyboard → LVGL keypad
```

The page names `Apple` and `Butter` come from the template. The app keeps these names, so that the code stays near to the template.

## Files of the app

| File | Function |
| --- | --- |
| `src/view/screens/server_panel.*` | The main page. It reads `/proc/meminfo`, finds the desktop process, reads the password and starts the switch. The page keeps the PID of labwc and reads all of `/proc` a maximum of one time in 10 seconds. |
| `src/view/screens/advanced_panel.*` | The Settings page. It has the list of rows (`Kind::Header/Group/Flag/Gpu/Reset`), reads `advanced.sh status`, finds the changed items and shows the password prompt (`Prompt::None/Apply/Reset`). |
| `src/view/screens/server_settings.*` | The list of service groups (`kStopGroups`), the file `~/.config/cz-server-mode/stop-groups` and the path of the scripts. |
| `src/view/screens/sudo_job.*` | Starts `sudo -S -k` in a child process and reads its output without a block. |
| `src/view/screens/panel_common.h` | Shared helpers: status colors (`Tone`), labels and password edit. |
| `src/view/widgets/help_popup.cpp` | The Fn+H help for the two pages. |
| `src/platform/linux_input.*` | The evdev key map for characters and arrows, and the key handler of the current screen. |
| `src/app/app.cpp` | The main loop and the "hold ESC to exit" function. When `busy` is set, ESC has no effect. |

## Types

The code uses `enum class` types and structures for all states and values. It does not use strings or numbers for these values.

| Type | Values | File |
| --- | --- | --- |
| `settings::StopGroup` | `Desktop`, `Printing`, `PackageKit`, `Portals`, `Accounts`, `Nfs` | `server_settings.h` |
| `settings::GroupSet` | A set of `StopGroup` values | `server_settings.h` |
| `settings::Script` | `ServerMode`, `NormalMode`, `Advanced` | `server_settings.h` |
| `settings::GpuMemory` | `Mb128`, `Mb64`, `Mb32`, `Mb16` | `server_settings.h` |
| `settings::BootFlag` | `BootServer`, `CameraOff`, `HdmiOff`, `CloudInitOff` | `server_settings.h` |
| `settings::BootStatus` | The parsed output of `advanced.sh status` | `server_settings.h` |
| `screen::JobOutcome` | `Success`, `WrongPassword`, `NotAllowed`, `PartialFailure`, `BadArguments`, `Busy`, `NotFound`, `Killed`, `OtherError` | `sudo_job.h` |
| `ServerPanel::Mode` | `Normal`, `Server` | `server_panel.h` |
| `AdvancedPanel::Kind` | `Header`, `Group`, `Gpu`, `Flag`, `Reset` | `advanced_panel.h` |
| `AdvancedPanel::Prompt` | `None`, `Apply`, `Reset` | `advanced_panel.h` |
| `screen::Tone` | `Normal`, `Info`, `Success`, `Warning`, `Error` | `panel_common.h` |

`server_settings.cpp` converts the values to the script arguments and parses the script output. `SudoJob::classify` converts the exit code of a script and the sudo output to a `JobOutcome`. The panels convert the values to the text on the screen.

Each `switch` statement on these types has a case for each value. If a case is not there, the `-Wswitch-enum` warning shows it.

The LVGL callbacks use `void* user_data`, because LVGL is a C library. Each callback changes this pointer back to its class in one location only.

## Keyboard input

The `linux_input` module reads `/dev/input/event*`. It changes evdev codes into characters and arrows, and it uses the Shift state.

Each screen sets a key handler with `set_screen_key_handler`. This handler gets each key before the global keys of the template (for example, Fn+H). Thus, the password goes directly to the panel. On the Settings page, normal keys go only to the password prompt.

For each key release, the module sends the key that it sent for the key press (`pressed_keys`). Thus, a change of Shift or Fn between the press and the release does not leave a key in the pressed state.

## Root scripts (SudoJob)

```
UI thread                        child process
SudoJob::start(password, argv)
  pipe2(O_CLOEXEC) ×2
  fork ───────────────────────→ LC_ALL=C
                                 execv /usr/bin/sudo -S -k -p "" <script> <args>
  write(password + "\n") → stdin
  wipe(password)
LVGL timer (200 ms):
  poll() → reads stdout/stderr (no block)
  waitpid(WNOHANG) → exit code
```

The SudoJob class has these properties:

- The password goes only to stdin. It is not in argv or in the environment. After the write, the app erases the buffer.
- With `-k`, sudo does not use its cache and reads the password each time.
- With `LC_ALL=C`, sudo writes its "wrong password" message in English. Thus, `SudoJob::wrong_password` can find it.
- The destructor waits for the child process. Thus, no zombie process stays.
- During a job, `BaseViewModel::busy()` gives `true`. ESC does not close the app, and the app ignores the keys.

## Main loop

The `lv_timer_handler()` function gives the time until the next timer. The loop sleeps for this time, for a maximum of 30 ms.

The panel timers have a period of 2 seconds when the app is idle, and 200 ms during a job. The timer of the Settings page stops when the app is idle. Thus, an open app uses almost no CPU time.

## Data locations

| Data | Location |
| --- | --- |
| Theme (system) | `/etc/cz-server-mode.conf` |
| Theme (user) | `~/.config/cz-server-mode/cz-server-mode.conf` |
| Selected groups | `~/.config/cz-server-mode/stop-groups`. If this file is not there, the app selects all groups. |
| Units that server mode stopped | `/run/cz-server-mode/stopped-units` (tmpfs, a reboot erases it) |
| Lock | `/run/cz-server-mode.lock` |
| Copies of the boot files | `/boot/firmware/config.txt.cz-orig`, `cmdline.txt.cz-orig` |
| Boot service for server mode | `/etc/systemd/system/cz-server-mode-boot.service` |
