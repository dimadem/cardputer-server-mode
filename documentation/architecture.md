# Архитектура

Программа построена на шаблоне [CardputerZero/Template](https://github.com/CardputerZero/Template): C++17, LVGL 9.5, CMake, MVVM. Структура и стиль шаблона сохранены, новый код лежит в основном в `src/view/screens/` и `scripts/`.

## Слои

```
main.cpp → app::App (жизненный цикл, главный цикл LVGL, ESC-выход, тосты)
             ├─ model::BaseModel           состояние: текущая страница, тема
             ├─ viewmodel::BaseViewModel   субъекты LVGL, действия, флаг busy
             ├─ app::ScreenManager         пересоздаёт экран при смене страницы
             │    ├─ AppleScreen  → ServerPanel    (главный экран)
             │    └─ ButterScreen → AdvancedPanel  (настройки)
             └─ platform::linux_input      evdev-клавиатура → LVGL keypad
```

Имена страниц `Apple`/`Butter` достались от шаблона и не переименовывались, чтобы не расходиться с ним.

## Файлы приложения

| Файл | Назначение |
| --- | --- |
| `src/view/screens/server_panel.*` | Главный экран: чтение `/proc/meminfo`, проверка, запущен ли рабочий стол (PID labwc кэшируется, полный обход `/proc` не чаще раза в 10 с), ввод пароля, запуск переключения |
| `src/view/screens/advanced_panel.*` | Настройки: список строк (`Kind::Header/Group/Flag/Gpu/Reset`), чтение `advanced.sh status`, вычисление изменённых пунктов, окно пароля (`Prompt::None/Apply/Reset`) |
| `src/view/screens/server_settings.*` | Список групп служб (`stop_groups()`), чтение и запись `~/.config/cz-server-mode/stop-groups`, путь к скриптам |
| `src/view/screens/sudo_job.*` | Запуск `sudo -S -k` в дочернем процессе с неблокирующим чтением вывода |
| `src/view/screens/panel_common.h` | Общие мелочи: цвета статусов (`Tone`), метки, редактирование пароля |
| `src/view/widgets/help_popup.cpp` | Справка Fn+H для обеих страниц |
| `src/platform/linux_input.*` | Раскладка evdev → символы и стрелки, обработчик клавиш текущего экрана |
| `src/app/app.cpp` | Главный цикл, удержание ESC для выхода (блокируется, пока `busy`) |

## Ввод с клавиатуры

`linux_input` читает `/dev/input/event*` и переводит коды evdev в символы, учитывая Shift. Экран регистрирует свой обработчик через `set_screen_key_handler`, и этот обработчик получает клавишу раньше навигации шаблона. Поэтому пароль печатается прямо в панель, а на экране настроек обычные клавиши не попадают никуда, кроме окна пароля.

Отпускание клавиши сопоставляется с той, что была выдана при нажатии (`pressed_keys`), чтобы смена Shift или Fn между нажатием и отпусканием не оставляла «залипших» клавиш.

## Запуск root-скриптов (SudoJob)

```
UI-поток                         дочерний процесс
SudoJob::start(password, argv)
  pipe2(O_CLOEXEC) ×2
  fork ───────────────────────→ LC_ALL=C
                                 execv /usr/bin/sudo -S -k -p "" <script> <args>
  write(password + "\n") → stdin
  wipe(password)
таймер LVGL (200 мс):
  poll() → читает stdout/stderr (неблокирующе)
  waitpid(WNOHANG) → exit code
```

- Пароль передаётся только через stdin, его нет ни в argv, ни в окружении. После записи буфер затирается.
- `-k` заставляет sudo каждый раз спрашивать пароль и не использовать кэш.
- `LC_ALL=C` нужен, чтобы сообщение sudo о неверном пароле было на английском и его можно было распознать (`SudoJob::wrong_password`).
- Деструктор ждёт завершения процесса, поэтому зомби не остаётся.
- Пока задача идёт, `BaseViewModel::busy()` равен true: удержание ESC не закрывает программу, клавиши игнорируются.

## Главный цикл

`lv_timer_handler()` возвращает время до следующего таймера, и цикл спит столько же, но не больше 30 мс. Таймеры панелей работают с периодом 2 с в простое и 200 мс во время задачи, а таймер настроек в простое приостановлен. Так программа почти не тратит CPU, пока просто открыта.

## Где хранятся данные

| Что | Где |
| --- | --- |
| Тема (системная) | `/etc/cz-server-mode.conf` |
| Тема (пользователь) | `~/.config/cz-server-mode/cz-server-mode.conf` |
| Выбранные группы | `~/.config/cz-server-mode/stop-groups` (если файла нет, включены все) |
| Что остановлено сейчас | `/run/cz-server-mode/stopped-units` (tmpfs, исчезает при перезагрузке) |
| Блокировка | `/run/cz-server-mode.lock` |
| Копии загрузочных файлов | `/boot/firmware/config.txt.cz-orig`, `cmdline.txt.cz-orig` |
| Служба загрузки в режим сервера | `/etc/systemd/system/cz-server-mode-boot.service` |
