# Overview

## Purpose

The CardputerZero has 512 MB of memory. The Raspberry Pi OS desktop (lightdm and labwc for HDMI) and background services use a large part of this memory. A server (ssh, network and your programs) does not need them.

Server Mode stops the services that a server does not need. The app keeps these services:

- The CardputerZero screen and the APPLaunch launcher.
- Wi-Fi (NetworkManager and wpa_supplicant).
- The ssh server.
- The avahi service, for the `.local` host names.
- Bluetooth. If Bluetooth stops, the launcher writes errors to the log every 2 seconds.
- Audio (pipewire) and udisks2.

You can select the services that the app stops:

- The desktop and HDMI.
- The printer service (CUPS).
- PackageKit.
- The portals and gvfs.
- The accounts-daemon service.
- NFS.

The launcher writes directly to the framebuffer of the CardputerZero screen (an SPI display, 320×170, `/dev/fb0`). Thus, the screen does not need the desktop.

The app does not remove or disable a service permanently. Server mode stays on until a reboot or until you go back to normal mode. Only the boot options give a permanent change, and you can reset them. Refer to [boot-options.md](boot-options.md).

## Main page

```
Mode: SERVER
Free 297 of 462 MB
Swap 0 MB · desktop stopped
Password: ****
Done: 130 MB -> 184 MB
Tab: settings · hold ESC: exit
```

The status on the main page changes every 2 seconds. During a switch, it changes every 200 ms.

To change the mode, type the sudo password and push Enter. In normal mode, the app starts server mode. In server mode, the app starts the desktop again.

During a switch, the app shows "Enabling server mode… N s". During this time, ESC does not close the app.

The result can be one of these messages:

- "Done: 130 MB -> 184 MB". The two numbers show the available memory (MemAvailable) before and after the switch.
- "Wrong password".
- "Busy: another switch is running". A different switch did not stop at that time.
- An error code.

## Settings page (Tab)

The Settings page has three sections:

1. **Server mode stops (instant)**. These rows select the service groups that server mode stops. The app saves each change immediately in `~/.config/cz-server-mode/stop-groups`. You do not type a password. The change has an effect at the next start of server mode.
2. **Disable at boot (reboot)**. These rows control HDMI, the camera and cloud-init.
3. **Boot (reboot)**. These rows are "GPU memory" (128, 64, 32 or 16 MB), "Boot into server mode" and "Reset all to defaults".

The right side of each row shows the value: `yes`, `no` or `< 64 MB >`. If you change a row and do not apply the change, the row is yellow and has a `*`.

To apply the boot options, do these steps:

1. Push Enter.
2. Type the sudo password and push Enter.
3. Wait 3 seconds. The board reboots.

The app sends only the changed items to `advanced.sh apply`. If the password is not correct, the app keeps your values. Type the password again, or push Tab to cancel.

"Boot into server mode" uses a boot service with a list of groups. If you change the groups after you enable this row, the row shows that the boot service is not current. When you push Enter, the app writes the new groups to the boot service.

## Keys

| Page | Key | Action |
| --- | --- | --- |
| Main | Letters and digits | Type the password |
| Main | Enter | Change the mode |
| Main | Backspace / Del | Delete one character / clear the password |
| Main | Tab | Open the Settings page |
| Settings | ↑ ↓ (or `;` `.`) | Select a row |
| Settings | ← → (or `,` `/`) | Change the value |
| Settings | Enter | Apply the boot options (with the password) |
| Settings | Tab / ESC | Go back. In the password field, Tab cancels. |
| All | Fn+H | Show the help |
| Main | Hold ESC | Close the app |

On the CardputerZero keyboard, the arrow keys are on the Fn layer (`Fn + ; . , /`). On the Settings page, these keys also operate without Fn.

## Procedure: make the board a remote server

1. Push Tab to open the Settings page.
2. Set "Boot into server mode" to `yes`.
3. Optional: set "GPU memory" to 16 MB, and set HDMI and the camera to `yes`.
4. Push Enter, then type the sudo password and push Enter.
5. Wait for the reboot. The board starts in `multi-user.target` and the desktop does not start.

To start the desktop for a short time, type the password on the main page and push Enter.

To go back to the default configuration, select "Reset all to defaults" on the Settings page. Then push Enter and type the password.
