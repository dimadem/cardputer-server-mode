# Known problems and recovery

## The reboot does not start

The reboot starts, but it is slow. The `mpris-proxy` user service (Bluetooth) can stop the shutdown for 90 seconds. Before its reboot, `advanced.sh` puts a drop-in with a 2-second timeout for this service. After a manual `reboot` command, the delay can occur.

## The desktop does not start after a boot into server mode

In the `multi-user` boot target, the Raspberry Pi OS first-boot wizard (`userconfig.service`) can wait for input on tty8. Then `lightdm` does not start.

If you did the first-boot configuration before, you can disable the wizard:

```shell
sudo systemctl disable --now userconfig.service
```

## Do not stop Bluetooth

The APPLaunch launcher reads data from bluez all the time. If Bluetooth stops, the launcher writes errors to the log every 2 seconds. Thus, Server Mode does not stop Bluetooth.

## "Busy: another switch is running"

A different switch is in progress. The app, the boot service or an ssh command can start a switch. The lock is a `flock` lock. Thus, the lock goes away when its process stops.

## Manual recovery

To go back to the default configuration through ssh without the app, use these commands:

```shell
sudo /usr/lib/cz-server-mode/normal-mode.sh             # start the stopped services
sudo /usr/lib/cz-server-mode/advanced.sh reset --reboot # restore the boot files
```

If the scripts are not on the board, do these steps manually:

```shell
sudo cp /boot/firmware/config.txt.cz-orig /boot/firmware/config.txt
sudo cp /boot/firmware/cmdline.txt.cz-orig /boot/firmware/cmdline.txt
sudo systemctl disable cz-server-mode-boot.service
sudo rm -f /etc/systemd/system/cz-server-mode-boot.service /etc/cloud/cloud-init.disabled
sudo systemctl set-default graphical.target
sudo reboot
```

If the board does not start after a change to `config.txt`, put the SD card in a computer. Then copy the `*.cz-orig` files back on the `bootfs` partition.

## App log

The app writes its messages and the output of the scripts to stdout, with the `server-mode` tag. When you start the app from the launcher, this output goes to the journal of the APPLaunch user service:

```shell
journalctl --user -u APPLaunch -n 200 | grep server-mode
journalctl -u cz-server-mode-boot.service -b    # the boot service
```
