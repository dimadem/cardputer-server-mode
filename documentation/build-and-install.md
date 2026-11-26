# Build and installation

## Build on the board

The easiest procedure is to build the app on the CardputerZero. The `cp0-device` preset uses the system of the board as the sysroot (`CM0_SDK_ROOT=/`).

1. Install the build packages:

   ```shell
   sudo apt install -y build-essential cmake ninja-build libpng-dev libjpeg-dev libfmt-dev libfreetype-dev zlib1g-dev
   ```

2. Configure the build:

   ```shell
   cmake --preset cp0-device
   ```

3. Build the app:

   ```shell
   cmake --build --preset cp0-device-rel -j2
   ```

The first build also builds LVGL and takes a long time. The next builds take 1 to 2 minutes. The binary file is `build/cp0-native/Release/cz_server_mode`.

To copy the source from a computer and build it on the board, use these commands:

```shell
rsync -a --exclude build ./ <user>@<board>.local:cz-server-mode/
ssh <user>@<board>.local 'cd cz-server-mode && cmake --build --preset cp0-device-rel -j2'
```

## Static checks

The `tools/check.sh` script examines the code with these tools:

- The `clang-format` tool examines the code style. The configuration is in `.clang-format`.
- The `clang-tidy` tool finds bugs, unsafe conversions and old C++ constructs. The configuration is in `.clang-tidy`.
- The `cppcheck` tool finds bugs and functions that the code does not use (dead code).
- The `shellcheck` tool finds problems in the shell scripts.

The build also uses strict compiler warnings for the app code (`APP_STRICT_WARNINGS`, on by default). To stop the build on a warning, set `-DAPP_WARNINGS_AS_ERRORS=ON`.

To do the checks on the board, do these steps:

1. Install the tools in a Python virtual environment. This does not change the system packages.

   ```shell
   python3 -m venv ~/.cache/cz-lint-venv
   ~/.cache/cz-lint-venv/bin/pip install clang-format clang-tidy cppcheck shellcheck-py
   ```

2. Build the app. The checks use `build/cp0-native/compile_commands.json`.
3. Start the checks:

   ```shell
   tools/check.sh
   ```

On the board, clang-tidy takes approximately 8 minutes. The script shows only the problems in the files of this app. The template files keep the upstream style.

## Install the scripts

```shell
sudo install -d /usr/lib/cz-server-mode
sudo install -m 755 scripts/server-mode.sh scripts/normal-mode.sh scripts/advanced.sh /usr/lib/cz-server-mode/
sudo install -m 644 scripts/common.sh /usr/lib/cz-server-mode/
```

## Development shortcut in the launcher

You can start the binary file from APPLaunch without a package. To do this, make the file `/usr/share/APPLaunch/applications/zz-server-mode-dev.desktop`:

```ini
[Desktop Entry]
Name=Server Mode (dev)
Exec=/home/<user>/cz-server-mode/build/cp0-native/Release/cz_server_mode
Icon=share/images/cz_server_mode.png
Terminal=false
Type=Application
```

After each build, start the app again from the launcher.

## Debian package

CPack makes the package. The configuration is in `cmake/cm0-package.cmake`.

| Path | Content |
| --- | --- |
| `/usr/bin/cz_server_mode` | The app |
| `/usr/lib/cz-server-mode/` | `server-mode.sh`, `normal-mode.sh`, `advanced.sh`, `common.sh` |
| `/etc/cz-server-mode.conf` | The default configuration (`dark_mode=yes`) |
| `/usr/share/APPLaunch/applications/cz_server_mode.desktop` | The "Server Mode" launcher entry |
| `/usr/share/APPLaunch/share/images/cz_server_mode*.png` | The icon |
| `/usr/share/cz_server_mode/fonts/` | The icon font |
| `/usr/share/doc/servermode/` | `README.md` and the licenses |

The package depends on `sudo`, `util-linux` and `procps`. The package does not install a system service (`APP_INSTALL_SYSTEMD_SERVICE OFF`). The rules of the CardputerZero Store do not let an app package install a root service. The app makes the boot service only when the user selects "Boot into server mode".

To build the package on the board, use these commands:

```shell
cmake --preset cp0-device -DAPP_MAINTAINER="<name> <<email>>"
cmake --build --preset cp0-device-rel -j2
cpack --preset cp0-device-deb   # → dist/ServerMode_<version>_<revision>_arm64.deb
```

To build the package on a computer (cross build from the template), use this command:

```shell
cmake --workflow --preset cp0-cross-package   # → dist/ServerMode_<version>_<revision>_arm64.deb
```

To install the package on the board, use this command:

```shell
sudo apt install --no-install-recommends ./ServerMode_0.1.0_rev1_arm64.deb
```

## Publication in the CardputerZero Store

To publish the app, do these steps:

1. Make two screenshots on the board (Fn+J). Put them in `screenshot/`. The size is 320×170.
2. Write the screenshot paths and `share_code` in `app-builder.json`. The other fields of the `store` section have their values. The icon is `assets/images/cz_server_mode.png`.
3. Send a pull request to the CardputerZero/packages repository.
