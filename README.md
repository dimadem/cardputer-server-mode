# Server Mode for CardputerZero

A launcher app for [M5Stack CardputerZero](https://github.com/CardputerZero) that switches the board between the normal Raspberry Pi desktop and a lean **server mode**. In server mode the HDMI desktop and other services the board doesn't need are stopped, and the CardputerZero screen and the APPLaunch launcher keep running.

Measured on a CardputerZero (CM0, 512 MB):

| State | Total RAM | Available |
| --- | --- | --- |
| Normal desktop | 352 MB | ~130 MB |
| Server mode | 352 MB | ~184 MB |
| Server mode at boot, `gpu_mem=16` | 462 MB | ~297 MB |

Nothing is uninstalled. Server mode only stops services, and a reboot or one more press of Enter brings them back. Boot-time tweaks keep a backup of the original boot files, and **Reset** restores them.

## Features

- **Main page**: shows the current mode, free memory, swap usage and whether the desktop is running. Type the sudo password and press Enter to switch modes.
- **Settings page** (Tab):
  - The services server mode stops. These toggles are saved at once and need no password.
  - Boot options that need a reboot: GPU memory (128/64/32/16 MB), boot straight into server mode, HDMI off, camera off, cloud-init off.
  - Reset to the original boot files.
- The password is asked only when Enter applies changes, and it is passed to `sudo -S` through a pipe, never on the command line.
- Dark theme, English UI, no bottom bar.

## Keys

Main page:

| Key | Action |
| --- | --- |
| type | sudo password |
| Enter | switch mode |
| Backspace / Del | delete a character / clear the password |
| Tab | settings |
| Fn+H | help |
| hold ESC | exit |

Settings page (`; . , /` also work as arrows without Fn):

| Key | Action |
| --- | --- |
| ↑ ↓ | select a row |
| ← → | change the value |
| Enter | apply the reboot options (asks for the password) |
| Tab / ESC | back (Tab also cancels the password prompt) |

## Build on the device

The app is built on the CardputerZero itself (the running system is the sysroot):

```shell
sudo apt install -y build-essential cmake ninja-build libpng-dev libjpeg-dev libfmt-dev libfreetype-dev zlib1g-dev
cmake --preset cp0-device
cmake --build --preset cp0-device-rel -j2
# binary: build/cp0-native/Release/cz_server_mode
```

The root helper scripts must be installed in `/usr/lib/cz-server-mode/`, because the path is compiled in:

```shell
sudo install -d /usr/lib/cz-server-mode
sudo install -m 755 scripts/server-mode.sh scripts/normal-mode.sh scripts/advanced.sh /usr/lib/cz-server-mode/
sudo install -m 644 scripts/common.sh /usr/lib/cz-server-mode/
```

Cross-build and the Debian package (`cp0-cross`, `cpack --preset cp0-cross-deb`) come from the upstream template. The package installs the binary, the scripts and the launcher entry. See [documentation/build-and-install.md](documentation/build-and-install.md).

## Documentation

Detailed documentation (in Russian) is in [documentation/](documentation/README.md):

- [overview.md](documentation/overview.md): what the app does and how to use it
- [architecture.md](documentation/architecture.md): code structure and data flow
- [scripts.md](documentation/scripts.md): helper scripts, service groups, state and exit codes
- [boot-options.md](documentation/boot-options.md): reboot-time tweaks and how they are undone
- [build-and-install.md](documentation/build-and-install.md): building, installing, packaging
- [troubleshooting.md](documentation/troubleshooting.md): known issues and manual recovery

## Based on

[CardputerZero/Template](https://github.com/CardputerZero/Template) (C++17, LVGL 9.5, CMake, MVVM). The template's structure and style are kept.

## License

MIT, see [LICENSE](LICENSE).
