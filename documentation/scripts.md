# Скрипты режима сервера

Скрипты лежат в `scripts/` и устанавливаются в `/usr/lib/cz-server-mode/`. Этот путь зашит в программу при сборке (`CZ_SCRIPT_DIR`). Все скрипты, кроме `advanced.sh status`, требуют root. Программа запускает их через `sudo`, но их можно вызывать и вручную по ssh.

## common.sh

Общие настройки, подключается остальными скриптами.

### Группы служб

| Группа | Системные юниты | Комментарий |
| --- | --- | --- |
| `desktop` | `lightdm.service`, `wayvnc-control.service` | весь рабочий стол Raspberry Pi и вывод на HDMI |
| `printing` | `cups.service`, `cups-browsed.service`, `cups.socket`, `cups.path` | печать |
| `packagekit` | `packagekit.service` | фоновые обновления; запускается по требованию через D-Bus |
| `portals` | (user-юниты) | `xdg-desktop-portal*`, `xdg-document-portal`, `xdg-permission-store`, `gvfs-*` |
| `accounts` | `accounts-daemon.service` | |
| `nfs` | `rpcbind.service`, `rpcbind.socket`, `nfs-blkmap.service` | |

Список групп продублирован в `src/view/screens/server_settings.cpp` (`stop_groups()`), и оба места нужно менять вместе.

### Функции

- `need_root`: завершает скрипт, если запущен не от root.
- `take_lock`: `flock -n` на `/run/cz-server-mode.lock`. Если переключение уже идёт, скрипт выходит с кодом 3. Блокировку берут программа, служба загрузки и ручные вызовы.
- `target_user`: пользователь, для которого останавливать user-юниты: `SUDO_USER`, а если его нет, владелец процесса APPLaunch.
- `user_systemctl`: `systemctl --user` от имени этого пользователя через `runuser` с нужными `XDG_RUNTIME_DIR` и `DBUS_SESSION_BUS_ADDRESS`.
- `mem_avail_mb`: `MemAvailable` в МБ.

## server-mode.sh

```
server-mode.sh [--boot] [группа...]     # без групп останавливаются все
```

1. Проверяет имена групп (неизвестная группа → код 2).
2. Берёт блокировку. Если файл состояния уже есть, выводит «already in server mode» и выходит с кодом 0.
3. Собирает список **активных** юнитов выбранных групп. С флагом `--boot` (запуск из службы загрузки) `lightdm` записывается, даже если он ещё не запущен, чтобы обратное переключение подняло рабочий стол.
4. **Сначала** атомарно (tmp + mv) записывает список в `/run/cz-server-mode/stopped-units`, потом останавливает юниты. Если что-то упадёт посередине, `normal-mode.sh` всё равно знает, что поднимать.
5. Останавливает user-юниты группы `portals`.
6. Печатает `MemAvailable: до -> после`. Эту строку программа показывает как результат.

## normal-mode.sh

1. Берёт блокировку. Если файла состояния нет, выводит «already in normal mode» и выходит с кодом 0.
2. Запускает юниты из файла состояния в обратном порядке, так что `lightdm` поднимается последним.
3. `packagekit.service` и user-юниты вручную не запускаются: они стартуют сами по D-Bus, когда нужны. Ручной запуск порталов раньше давал зависание на 2 минуты.
4. Если какой-то юнит не запустился, в файле состояния остаются только неудавшиеся (для повтора), и скрипт выходит с кодом 1.

## Коды выхода

| Код | Значение | Что показывает программа |
| --- | --- | --- |
| 0 | успех (или уже в этом режиме) | Done / Already in this mode |
| 1 | часть юнитов не остановилась или не запустилась | Partly failed, code 1 (see log) |
| 2 | неверные аргументы | Partly failed, code 2 |
| 3 | занято: идёт другое переключение | Busy: another switch is running |
| 127 | не найден sudo или скрипт | sudo or script not found |

Неверный пароль определяется по выводу sudo, а не по коду.

## Ручной вызов по ssh

```shell
sudo /usr/lib/cz-server-mode/server-mode.sh              # все группы
sudo /usr/lib/cz-server-mode/server-mode.sh desktop nfs  # только эти
sudo /usr/lib/cz-server-mode/normal-mode.sh
cat /run/cz-server-mode/stopped-units                    # что сейчас остановлено
```
