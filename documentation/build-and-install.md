# Сборка и установка

## Сборка на плате

Удобнее всего собирать прямо на CardputerZero: пресет `cp0-device` использует работающую систему как sysroot (`CM0_SDK_ROOT=/`).

```shell
sudo apt install -y build-essential cmake ninja-build libpng-dev libjpeg-dev libfmt-dev libfreetype-dev zlib1g-dev
cmake --preset cp0-device
cmake --build --preset cp0-device-rel -j2
```

Первая сборка (вместе с LVGL) идёт долго, повторная занимает 1–2 минуты. Бинарник: `build/cp0-native/Release/cz_server_mode`.

Синхронизация с компьютера:

```shell
rsync -a --exclude build ./ <user>@<board>.local:cz-server-mode/
ssh <user>@<board>.local 'cd cz-server-mode && cmake --build --preset cp0-device-rel -j2'
```

## Установка скриптов

```shell
sudo install -d /usr/lib/cz-server-mode
sudo install -m 755 scripts/server-mode.sh scripts/normal-mode.sh scripts/advanced.sh /usr/lib/cz-server-mode/
sudo install -m 644 scripts/common.sh /usr/lib/cz-server-mode/
```

## Dev-ярлык в лаунчере

Чтобы запускать собранный бинарник из APPLaunch без пакета, создайте `/usr/share/APPLaunch/applications/zz-server-mode-dev.desktop`:

```ini
[Desktop Entry]
Name=Server Mode (dev)
Exec=/home/<user>/cz-server-mode/build/cp0-native/Release/cz_server_mode
Icon=share/images/files.png
Terminal=false
Type=Application
```

После пересборки достаточно снова запустить приложение из лаунчера.

## Debian-пакет

Пакет собирается с помощью CPack (настройки в `cmake/cm0-package.cmake`):

| Путь | Содержимое |
| --- | --- |
| `/usr/bin/cz_server_mode` | программа |
| `/usr/lib/cz-server-mode/` | `server-mode.sh`, `normal-mode.sh`, `advanced.sh`, `common.sh` |
| `/etc/cz-server-mode.conf` | настройки по умолчанию (`dark_mode=yes`) |
| `/usr/share/APPLaunch/applications/cz_server_mode.desktop` | ярлык лаунчера «Server Mode» |
| `/usr/share/APPLaunch/share/images/cz_server_mode*.png` | иконка |

Зависимости: `sudo, util-linux, procps`. Системная служба не ставится (`APP_INSTALL_SYSTEMD_SERVICE OFF`): правила магазина CardputerZero запрещают пакетам приложений ставить root-службы. Служба загрузки в режим сервера создаётся только по явному выбору пользователя в настройках.

Кросс-сборка с компьютера (из шаблона):

```shell
cmake --workflow --preset cp0-cross-package   # → dist/ServerMode_<версия>_<ревизия>_arm64.deb
```

Установка на плате:

```shell
sudo apt install --no-install-recommends ./ServerMode_0.1.0_rev1_arm64.deb
```

## Публикация в CardputerZero Store

Нужно заполнить раздел `store` в `app-builder.json` (сейчас там значения шаблона), сделать свою иконку и скриншоты и отправить PR в репозиторий CardputerZero/packages.
