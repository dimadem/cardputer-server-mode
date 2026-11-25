# Известные проблемы и восстановление

## Перезагрузка «ничего не делает»

На самом деле перезагрузка идёт, просто долго: user-служба `mpris-proxy` (Bluetooth) задерживает выключение на 90 секунд. `advanced.sh` ставит drop-in с таймаутом 2 с перед своей перезагрузкой. При ручном `reboot` задержка может остаться.

## Рабочий стол не возвращается после загрузки в режим сервера

В режиме загрузки `multi-user` мастер первой настройки Raspberry Pi OS (`userconfig.service`) может ждать ввода на tty8, и тогда `lightdm` не стартует. Если первичная настройка уже пройдена, мастер можно отключить:

```shell
sudo systemctl disable --now userconfig.service
```

## Не отключайте Bluetooth

Лаунчер APPLaunch постоянно опрашивает bluez. Без Bluetooth он раз в 2 секунды пишет ошибки в журнал, поэтому Server Mode Bluetooth не трогает.

## «Busy: another switch is running»

Другое переключение уже идёт (из программы, службы загрузки или ssh). Если вы уверены, что ничего не запущено, блокировка освобождается сама: это `flock`, и он снимается вместе с процессом.

## Ручное восстановление

Вернуть всё по ssh без программы:

```shell
sudo /usr/lib/cz-server-mode/normal-mode.sh            # поднять остановленные службы
sudo /usr/lib/cz-server-mode/advanced.sh reset --reboot # вернуть загрузочные файлы
```

Если скриптов нет, то же самое вручную:

```shell
sudo cp /boot/firmware/config.txt.cz-orig /boot/firmware/config.txt
sudo cp /boot/firmware/cmdline.txt.cz-orig /boot/firmware/cmdline.txt
sudo systemctl disable cz-server-mode-boot.service
sudo rm -f /etc/systemd/system/cz-server-mode-boot.service /etc/cloud/cloud-init.disabled
sudo systemctl set-default graphical.target
sudo reboot
```

Если плата не загружается после изменения `config.txt`, вставьте карту в компьютер и восстановите файлы из `*.cz-orig` в разделе `bootfs`.

## Журнал программы

Программа пишет сообщения с тегом `server-mode`, вместе с выводом скриптов, в stdout. Если программа запущена из лаунчера, этот вывод попадает в журнал user-службы APPLaunch:

```shell
journalctl --user -u APPLaunch -n 200 | grep server-mode
journalctl -u cz-server-mode-boot.service -b    # служба загрузки
```
