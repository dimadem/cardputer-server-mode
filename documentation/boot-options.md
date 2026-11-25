# Опции с перезагрузкой (advanced.sh)

```
advanced.sh status                         # текущие значения, root не нужен
advanced.sh apply key=value... [--reboot]  # применить
advanced.sh reset [--reboot]               # вернуть исходное
```

## Ключи

| Ключ | Значения | Что меняется |
| --- | --- | --- |
| `gpu` | 16, 32, 64, 128 | строка `gpu_mem=` в `/boot/firmware/config.txt` |
| `camera_off` | 0/1 | `dtoverlay=imx219` комментируется как `#cz-off dtoverlay=imx219` |
| `hdmi_off` | 0/1 | в `/boot/firmware/cmdline.txt` добавляется `video=HDMI-A-1:d` |
| `cloudinit_off` | 0/1 | создаётся `/etc/cloud/cloud-init.disabled` |
| `boot_server` | 0/1 | служба `cz-server-mode-boot.service` и цель загрузки по умолчанию |
| `groups` | `a,b,c` | какие группы останавливает служба загрузки |

Программа передаёт только изменённые ключи. Если включена загрузка в режим сервера, она передаёт ещё и `groups=`.

### Видеопамять

`gpu_mem=16` — это минимум для Raspberry Pi (8 МБ невозможно). При 16 МБ не работают камера и аппаратное видео (кодеки), программа об этом предупреждает. Экран CardputerZero и консоль работают. Освобождённая память видна системе после перезагрузки: всего становится ~462 МБ вместо ~352.

### Загрузка в режим сервера

При `boot_server=1` создаётся служба:

```ini
[Unit]
Description=CardputerZero Server Mode at boot
After=network.target cups.service accounts-daemon.service rpcbind.service

[Service]
Type=oneshot
ExecStart=/usr/lib/cz-server-mode/server-mode.sh --boot <группы>
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Кроме того, цель по умолчанию переключается на `multi-user.target`, так что рабочий стол вообще не стартует. При `boot_server=0` служба удаляется и цель возвращается на `graphical.target`. Это делается только если служба была, чтобы не трогать чужую настройку.

## Безопасность изменений

- При первом `apply` сохраняются копии `config.txt.cz-orig` и `cmdline.txt.cz-orig`.
- Изменения делаются на рабочих копиях `*.cz-new` и записываются, только если файл действительно изменился (`cmp`).
- Запись атомарная: копия во временный файл → `sync` → `mv` → `sync`. При отключении питания файл будет либо старым, либо новым целиком, но не обрезанным.
- Перед дописыванием `gpu_mem=` проверяется, что файл заканчивается переводом строки.

## Сброс

`reset` восстанавливает оба файла из `*.cz-orig` и удаляет копии только после того, как оба восстановлены. Затем удаляет службу загрузки, возвращает `graphical.target` и снова включает cloud-init.

## Перезагрузка

`--reboot` перезагружает плату через 3 секунды (`systemd-run --on-active=3 systemctl reboot`), чтобы программа успела показать результат. Перед этим в `/run` (только на эту перезагрузку) ставится drop-in `TimeoutStopSec=2` для user-службы `mpris-proxy`: она игнорирует SIGTERM и иначе задерживает выключение на 90 секунд.

## Вывод status

```
gpu=16 boot_server=1 camera_off=1 hdmi_off=1 cloudinit_off=1 modified=1 boot_groups=desktop,printing,packagekit,portals,accounts,nfs
```
