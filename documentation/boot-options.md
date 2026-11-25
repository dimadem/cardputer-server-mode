# Boot options (advanced.sh)

```
advanced.sh status                         # current values, root access is not necessary
advanced.sh apply key=value... [--reboot]  # apply the values
advanced.sh reset [--reboot]               # go back to the original files
```

## Keys

| Key | Values | Change |
| --- | --- | --- |
| `gpu` | 16, 32, 64, 128 | The `gpu_mem=` line in `/boot/firmware/config.txt` |
| `camera_off` | 0/1 | The script changes `dtoverlay=imx219` into the comment `#cz-off dtoverlay=imx219` |
| `hdmi_off` | 0/1 | The script adds `video=HDMI-A-1:d` to `/boot/firmware/cmdline.txt` |
| `cloudinit_off` | 0/1 | The script makes the file `/etc/cloud/cloud-init.disabled` |
| `boot_server` | 0/1 | The `cz-server-mode-boot.service` unit and the default boot target |
| `groups` | `a,b,c` | The groups that the boot service stops |

The app sends only the changed keys. When "Boot into server mode" is on, the app also sends `groups=`.

The script examines all arguments before it changes a file. If an argument is not correct, the script changes nothing and stops with exit code 2. These arguments are not correct:

- A value other than 0 or 1 for `camera_off`, `hdmi_off`, `cloudinit_off` and `boot_server`.
- An empty `groups=`, or a group name that is not known.
- `boot_server=1` without `groups=`.

The app does not let you apply "Boot into server mode" when you select no group.

### GPU memory

On a Raspberry Pi, the minimum value of `gpu_mem` is 16. A value of 8 is not possible. With 16 MB, the camera and the hardware video codecs do not operate, and the app shows a message about this. The CardputerZero screen and the console operate correctly.

The system gets the memory after the reboot. The total memory increases from approximately 352 MB to approximately 462 MB.

### Boot into server mode

When `boot_server=1`, the script makes this unit:

```ini
[Unit]
Description=CardputerZero Server Mode at boot
After=network.target cups.service accounts-daemon.service rpcbind.service
# the package was removed without "advanced.sh reset": do nothing
ConditionFileIsExecutable=/usr/lib/cz-server-mode/server-mode.sh

[Service]
Type=oneshot
ExecStart=/usr/lib/cz-server-mode/server-mode.sh --boot <groups>
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

The script also sets the default target to `multi-user.target`. Thus, the desktop does not start at boot.

When `boot_server=0`, the script removes the unit and sets the default target to `graphical.target`. The script does this only if the unit is there. Thus, it does not change a default target that a different person or program set.

## Safe changes

The script has these protections for the boot files:

- At the first `apply`, the script makes the copies `config.txt.cz-orig` and `cmdline.txt.cz-orig`. If a copy is empty (for example, after a power failure), the next `apply` makes the copy again.
- The script changes the work copies `*.cz-new`. It writes a work copy to the boot file only if the content is different (`cmp`).
- Each write goes through a temporary file: copy, `sync`, `mv`, `sync`. The script never writes the boot file directly. The boot partition uses FAT, and a rename on FAT is not atomic. Thus, a power failure at the time of a write can cause damage to the file. This risk is small. Do not remove the power during an `apply`.
- The script writes `gpu_mem=` only before the first section or in the `[all]` section. If the file has no such line, the script adds it in an `[all]` section at the end.
- Before the script adds `gpu_mem=`, it makes sure that the file ends with a newline.

## Reset

The "Reset all to defaults" row operates also when the app cannot read the boot status. The `reset` command copies the two files back from `*.cz-orig`. If a copy is empty, the script does not restore it and stops with exit code 1. The script removes the copies only after it restores the two files. Then it removes the boot service, sets `graphical.target` and enables cloud-init again.

## Reboot

With `--reboot`, the board reboots after 3 seconds (`systemd-run --on-active=3 systemctl reboot`). During these 3 seconds, the app shows the result.

The `mpris-proxy` user service ignores SIGTERM. It can stop the shutdown for 90 seconds. Before the reboot, the script puts a drop-in with `TimeoutStopSec=2` for this service in `/run`. This drop-in has an effect only for this reboot.

## Status output

```
gpu=16 boot_server=1 camera_off=1 hdmi_off=1 cloudinit_off=1 modified=1 boot_groups=desktop,printing,packagekit,portals,accounts,nfs
```
