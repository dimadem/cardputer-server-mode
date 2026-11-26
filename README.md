# Server Mode for CardputerZero

Server Mode is a launcher app for the [M5Stack CardputerZero](https://github.com/CardputerZero). The app changes the board from the normal Raspberry Pi desktop to a server mode with more free memory.

In server mode, the app stops the HDMI desktop and other services that a server does not need. The CardputerZero screen and the APPLaunch launcher continue to operate.

These values are from a CardputerZero (CM0, 512 MB):

| State | Total memory | Available memory |
| --- | --- | --- |
| Normal desktop | 352 MB | Approximately 130 MB |
| Server mode | 352 MB | Approximately 184 MB |
| Server mode at boot, `gpu_mem=16` | 462 MB | Approximately 297 MB |

The app does not remove software. Server mode only stops services. A reboot or one more push of Enter starts them again. Before the app changes the boot files, it makes a copy of them. The "Reset all to defaults" row restores these copies.

## Functions

The app has these functions:

- The main page shows the current mode, the free memory, the swap memory and the desktop status. To change the mode, type the sudo password and push Enter.
- The Settings page (Tab) selects the services that server mode stops. The app saves these changes immediately, without a password.
- The Settings page also has boot options: GPU memory (128, 64, 32 or 16 MB), boot into server mode, HDMI off, camera off and cloud-init off. These options have an effect after a reboot.
- The "Reset all to defaults" row restores the original boot files.
- The app reads the password only when you apply a change with Enter. The app sends the password to `sudo -S` through a pipe, not on the command line.
- The app has a dark theme, an English user interface and no bottom bar.

## Keys

Main page:

| Key | Action |
| --- | --- |
| Letters and digits | Type the sudo password |
| Enter | Change the mode |
| Backspace / Del | Delete one character / clear the password |
| Tab | Open the Settings page |
| Fn+H | Show the help |
| Hold ESC | Close the app |

Settings page (the `; . , /` keys also operate as arrows without Fn):

| Key | Action |
| --- | --- |
| ↑ ↓ | Select a row |
| ← → | Change the value |
| Enter | Apply the boot options. The app shows a password prompt. |
| Tab / ESC | Go back. In the password prompt, Tab cancels. |

## Build on the board

The `cp0-device` preset builds the app on the CardputerZero. The system of the board is the sysroot.

```shell
sudo apt install -y build-essential cmake ninja-build libpng-dev libjpeg-dev libfmt-dev libfreetype-dev zlib1g-dev
cmake --preset cp0-device
cmake --build --preset cp0-device-rel -j2
# binary file: build/cp0-native/Release/cz_server_mode
```

The build writes the path of the root scripts into the app. Thus, install the scripts in `/usr/lib/cz-server-mode/`:

```shell
sudo install -d /usr/lib/cz-server-mode
sudo install -m 755 scripts/server-mode.sh scripts/normal-mode.sh scripts/advanced.sh /usr/lib/cz-server-mode/
sudo install -m 644 scripts/common.sh /usr/lib/cz-server-mode/
```

The cross build (`cp0-cross`) and the Debian package (`cpack --preset cp0-device-deb` on the board, `cp0-cross-deb` on a computer) come from the template. The package installs the binary file, the scripts and the launcher entry. Refer to [`documentation/build-and-install.md`](documentation/build-and-install.md).

## Documentation

The [`documentation/`](documentation/README.md) directory contains these files:

- [`overview.md`](documentation/overview.md): The function of the app and the procedures for the user.
- [`architecture.md`](documentation/architecture.md): The code structure and the data flow.
- [`scripts.md`](documentation/scripts.md): The root scripts, the service groups, the state and the exit codes.
- [`boot-options.md`](documentation/boot-options.md): The boot options and the reset.
- [`build-and-install.md`](documentation/build-and-install.md): The build, the installation and the package.
- [`troubleshooting.md`](documentation/troubleshooting.md): Known problems and manual recovery.

## Origin

The app uses the [CardputerZero/Template](https://github.com/CardputerZero/Template) project (C++17, LVGL 9.5, CMake, MVVM). The app keeps the structure and the style of the template.

## License

MIT. Refer to [LICENSE](LICENSE).
