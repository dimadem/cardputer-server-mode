# Server mode scripts

The scripts are in `scripts/`. The installation puts them in `/usr/lib/cz-server-mode/`. The build writes this path into the app (`CZ_SCRIPT_DIR`).

All scripts must have root access, but `advanced.sh status` does not. The app starts the scripts with `sudo`. You can also start them manually through ssh.

## common.sh

The `common.sh` file contains the shared values and functions. The other scripts read it.

### Service groups

| Group | System units | Comment |
| --- | --- | --- |
| `desktop` | `lightdm.service`, `wayvnc-control.service` | The full Raspberry Pi desktop and the HDMI output |
| `printing` | `cups.service`, `cups-browsed.service`, `cups.socket`, `cups.path` | Printers |
| `packagekit` | `packagekit.service` | Background updates. D-Bus starts it when necessary. |
| `portals` | User units | `xdg-desktop-portal*`, `xdg-document-portal`, `xdg-permission-store`, `gvfs-*` |
| `accounts` | `accounts-daemon.service` | |
| `nfs` | `rpcbind.service`, `rpcbind.socket`, `nfs-blkmap.service` | |

The list of groups is also in `src/view/screens/server_settings.cpp` (`stop_groups()`). When you change the list, change the two files.

### Functions

- `need_root`: Stops the script if the user is not root.
- `take_lock`: Gets a `flock -n` lock on `/run/cz-server-mode.lock`. If a different switch has the lock, the script stops with exit code 3. The app, the boot service and manual commands use the same lock.
- `target_user`: Gives the user for the user units. This is `SUDO_USER`. If `SUDO_USER` is empty, it is the owner of the APPLaunch process.
- `user_systemctl`: Starts `systemctl --user` as this user through `runuser`, with the correct `XDG_RUNTIME_DIR` and `DBUS_SESSION_BUS_ADDRESS`.
- `mem_avail_mb`: Gives `MemAvailable` in MB.

## server-mode.sh

```
server-mode.sh [--boot] [group...]     # with no groups, the script stops all groups
```

The script does these steps:

1. It examines the group names. If a group name is not known, the script stops with exit code 2.
2. It gets the lock. If the state file is there, it writes "already in server mode" and stops with exit code 0.
3. It makes a list of the **active** units of the selected groups. With `--boot`, it adds `lightdm` to the list also when `lightdm` did not start. Thus, `normal-mode.sh` can start the desktop later.
4. It writes the list to `/run/cz-server-mode/stopped-units` before it stops a unit. The write is atomic (a temporary file and `mv`). If the script stops during the switch, `normal-mode.sh` can find the units.
5. It stops the units.
6. It stops the user units of the `portals` group.
7. It writes `MemAvailable: before -> after`. The app shows this line as the result.

## normal-mode.sh

The script does these steps:

1. It gets the lock. If the state file is not there, it writes "already in normal mode" and stops with exit code 0.
2. It starts the units from the state file in the reverse sequence. Thus, `lightdm` starts last.
3. It does not start `packagekit.service` and the user units. D-Bus starts them when necessary. A manual start of the portals caused a delay of 2 minutes.
4. If a unit does not start, the state file keeps only the units that did not start. Then the script stops with exit code 1. You can do the switch again.

## Exit codes

| Code | Meaning | App message |
| --- | --- | --- |
| 0 | Success, or the board is in this mode at this time | `Done`, `Already in this mode` |
| 1 | One or more units did not stop or start | `Partly failed, code 1 (see log)` |
| 2 | An argument is not correct | `Partly failed, code 2` |
| 3 | A different switch has the lock | `Busy: another switch is running` |
| 127 | The system cannot find sudo or the script | `sudo or script not found` |

The app finds an incorrect password from the output of sudo, not from the exit code.

## Manual commands through ssh

```shell
sudo /usr/lib/cz-server-mode/server-mode.sh              # all groups
sudo /usr/lib/cz-server-mode/server-mode.sh desktop nfs  # only these groups
sudo /usr/lib/cz-server-mode/normal-mode.sh
cat /run/cz-server-mode/stopped-units                    # the units that are stopped now
```
